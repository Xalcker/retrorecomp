/*
 * Zip (los juegos y las BIOS al estilo MAME): el directorio central, archivos guardados tal cual o con
 * deflate, y el CRC32 de cada uno. Sin dependencias.
 *
 * El inflate es el de png.c de la plataforma, con una tabla de 10 bits por código para no decodificar
 * bit por bit: un juego trae decenas de MB comprimidos.
 */
#include "neo.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CRC32                                                               */
/* ------------------------------------------------------------------ */

static u32 crc_table[256];

u32 crc32_buf(u32 crc, const u8 *p, size_t n)
{
    if (!crc_table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* Inflate (RFC 1951)                                                  */
/* ------------------------------------------------------------------ */

#define FASTBITS 10

typedef struct {
    const u8 *in;
    size_t len, pos;
    u64 bitbuf;
    int bitcnt;
    size_t over;                    /* bits leídos más allá del final (relleno con ceros) */
    u8 *out;
    size_t outlen, outcap;
    int err;
} Inflate;

typedef struct {
    u16 fast[1 << FASTBITS];        /* por los próximos 10 bits: largo << 9 | símbolo, o 0 si el código es más largo */
    u16 count[16];
    u16 symbol[288];
} Huff;

static void refill(Inflate *s)
{
    while (s->bitcnt <= 56) {
        if (s->pos < s->len) s->bitbuf |= (u64)s->in[s->pos++] << s->bitcnt;
        else s->over += 8;
        s->bitcnt += 8;
    }
}

static unsigned getbits(Inflate *s, int n)
{
    if (s->bitcnt < n) refill(s);
    unsigned v = (unsigned)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static void build(Huff *h, const u8 *lengths, int n)
{
    u16 offs[16], next[16];
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (u16)i;
    /* Códigos canónicos: el primero de cada largo (RFC 1951, 3.2.2) */
    unsigned code = 0;
    for (int len = 1; len < 16; len++) { code = (code + h->count[len - 1]) << 1; next[len] = (u16)code; }
    for (int i = 0; i < n; i++) {
        int len = lengths[i];
        if (!len || len > FASTBITS) { if (len) next[len]++; continue; }
        unsigned c = next[len]++, rev = 0;
        for (int k = 0; k < len; k++) rev |= ((c >> k) & 1) << (len - 1 - k);
        for (unsigned k = rev; k < (1u << FASTBITS); k += 1u << len) h->fast[k] = (u16)(len << 9 | i);
    }
}

static int decode(Inflate *s, const Huff *h)
{
    if (s->bitcnt < 16) refill(s);
    u16 e = h->fast[s->bitbuf & ((1u << FASTBITS) - 1)];
    if (e) { s->bitbuf >>= e >> 9; s->bitcnt -= e >> 9; return e & 511; }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)getbits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;
    return 0;
}

static const u16 lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 lextra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 dextra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void codes(Inflate *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->err) return;
        if (sym < 256) {
            if (s->outlen == s->outcap) { s->err = 1; return; }
            s->out[s->outlen++] = (u8)sym;
            continue;
        }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) { s->err = 1; return; }
        unsigned len = lbase[sym] + getbits(s, lextra[sym]);
        int ds = decode(s, dist);
        if (ds >= 30) { s->err = 1; return; }
        size_t d = dbase[ds] + getbits(s, dextra[ds]);
        if (d > s->outlen || s->outlen + len > s->outcap) { s->err = 1; return; }
        u8 *o = s->out + s->outlen;
        for (unsigned i = 0; i < len; i++) o[i] = o[(ptrdiff_t)i - (ptrdiff_t)d];
        s->outlen += len;
    }
}

static void inflate_stream(Inflate *s)
{
    static Huff fixed_lit, fixed_dist;
    static int fixed_ready;
    int last;
    do {
        last = (int)getbits(s, 1);
        int type = (int)getbits(s, 2);
        if (type == 0) {
            getbits(s, s->bitcnt & 7);                  /* al límite del byte */
            unsigned n = getbits(s, 16), nn = getbits(s, 16);
            if ((n ^ 0xFFFF) != nn || s->outlen + n > s->outcap) { s->err = 1; return; }
            while (n && s->bitcnt >= 8) { s->out[s->outlen++] = (u8)getbits(s, 8); n--; }
            if (s->pos + n > s->len) { s->err = 1; return; }
            memcpy(s->out + s->outlen, s->in + s->pos, n);
            s->outlen += n;
            s->pos += n;
        } else if (type == 1) {
            if (!fixed_ready) {
                u8 l[288];
                int i = 0;
                for (; i < 144; i++) l[i] = 8;
                for (; i < 256; i++) l[i] = 9;
                for (; i < 280; i++) l[i] = 7;
                for (; i < 288; i++) l[i] = 8;
                build(&fixed_lit, l, 288);
                for (i = 0; i < 30; i++) l[i] = 5;
                build(&fixed_dist, l, 30);
                fixed_ready = 1;
            }
            codes(s, &fixed_lit, &fixed_dist);
        } else if (type == 2) {
            static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            u8 lengths[320];
            static Huff lencode, lit, dist;             /* estáticos: cada uno mide ~3 KB */
            int nlen = (int)getbits(s, 5) + 257, ndist = (int)getbits(s, 5) + 1, ncode = (int)getbits(s, 4) + 4;
            memset(lengths, 0, sizeof lengths);
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (u8)getbits(s, 3);
            build(&lencode, lengths, 19);
            int idx = 0;
            while (idx < nlen + ndist && !s->err) {
                int sym = decode(s, &lencode);
                if (sym < 16) { lengths[idx++] = (u8)sym; continue; }
                u8 v = 0;
                int rep;
                if (sym == 16) { if (!idx) { s->err = 1; return; } v = lengths[idx - 1]; rep = 3 + (int)getbits(s, 2); }
                else if (sym == 17) rep = 3 + (int)getbits(s, 3);
                else rep = 11 + (int)getbits(s, 7);
                if (idx + rep > nlen + ndist) { s->err = 1; return; }
                while (rep--) lengths[idx++] = v;
            }
            build(&lit, lengths, nlen);
            build(&dist, lengths + nlen, ndist);
            codes(s, &lit, &dist);
        } else {
            s->err = 1;
        }
        /* Lo que quedó en el búfer sin leer no cuenta como pasado del final */
        if (s->over > (size_t)s->bitcnt) s->err = 1;
    } while (!last && !s->err);
}

int inflate_raw(const u8 *in, size_t in_len, u8 *out, size_t out_len)
{
    Inflate s;
    memset(&s, 0, sizeof s);
    s.in = in; s.len = in_len;
    s.out = out; s.outcap = out_len;
    inflate_stream(&s);
    return s.err || s.outlen != out_len ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Zip                                                                 */
/* ------------------------------------------------------------------ */

static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

void parts_free(RomPart *parts, int n)
{
    if (!parts) return;
    for (int i = 0; i < n; i++) free(parts[i].data);
    free(parts);
}

int zip_read_all(const u8 *zip, size_t len, RomPart **out, const char **err)
{
    *out = NULL;
    /* Fin del directorio central: está en los últimos 22 bytes más el comentario (hasta 64 KB) */
    long eocd = -1;
    for (long i = (long)len - 22; i >= 0 && i >= (long)len - 22 - 65535; i--)
        if (le32(zip + i) == 0x06054B50u) { eocd = i; break; }
    if (eocd < 0) { *err = "no es un zip (no tiene directorio central)"; return -1; }
    u32 count = le16(zip + eocd + 10), cd = le32(zip + eocd + 16);
    if (cd >= len || count == 0xFFFF || cd == 0xFFFFFFFFu) { *err = "zip dañado o zip64 (no soportado)"; return -1; }
    RomPart *parts = calloc(count ? count : 1, sizeof *parts);
    if (!parts) { *err = "sin memoria"; return -1; }
    int n = 0;
    size_t p = cd;
    for (u32 k = 0; k < count; k++) {
        if (p + 46 > len || le32(zip + p) != 0x02014B50u) { *err = "zip dañado (directorio central)"; goto fail; }
        u32 method = le16(zip + p + 10), crc = le32(zip + p + 16), csize = le32(zip + p + 20), usize = le32(zip + p + 24);
        u32 nlen = le16(zip + p + 28), elen = le16(zip + p + 30), clen = le16(zip + p + 32), local = le32(zip + p + 42);
        const char *name = (const char *)zip + p + 46;
        size_t next = p + 46 + nlen + elen + clen;
        if (next > len) { *err = "zip dañado (nombre)"; goto fail; }
        p = next;
        if (nlen == 0 || name[nlen - 1] == '/') continue;                     /* carpeta */
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu) { *err = "zip64 (no soportado)"; goto fail; }
        if ((size_t)local + 30 > len || le32(zip + local) != 0x04034B50u) { *err = "zip dañado (cabecera local)"; goto fail; }
        size_t data = (size_t)local + 30 + le16(zip + local + 26) + le16(zip + local + 28);
        if (data + csize > len) { *err = "zip dañado (datos)"; goto fail; }
        RomPart *r = &parts[n];
        /* Sin la carpeta */
        u32 start = 0;
        for (u32 i = 0; i < nlen; i++) if (name[i] == '/' || name[i] == '\\') start = i + 1;
        u32 l = nlen - start < sizeof r->name - 1 ? nlen - start : (u32)sizeof r->name - 1;
        memcpy(r->name, name + start, l);
        r->name[l] = 0;
        r->size = usize;
        r->data = malloc(usize ? usize : 1);
        if (!r->data) { *err = "sin memoria"; goto fail; }
        n++;
        if (method == 0) {
            if (csize != usize) { *err = "zip dañado (tamaño)"; goto fail; }
            memcpy(r->data, zip + data, usize);
        } else if (method == 8) {
            if (inflate_raw(zip + data, csize, r->data, usize)) { *err = "zip dañado (deflate)"; goto fail; }
        } else {
            *err = "zip con un método de compresión no soportado (solo guardado o deflate)";
            goto fail;
        }
        r->crc = crc32_buf(0, r->data, usize);
        if (r->crc != crc) { *err = "zip dañado (el CRC no coincide)"; goto fail; }
    }
    *out = parts;
    return n;
fail:
    parts_free(parts, n);
    return -1;
}
