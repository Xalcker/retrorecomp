/*
 * PNG mínimo para las pruebas: lee PNG de 8 bits (gris, RGB, RGBA, paleta)
 * sin entrelazado, y escribe PNG en gris sin comprimir. Sin dependencias.
 */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Inflate (RFC 1951)                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    const unsigned char *in;
    size_t len, pos;
    unsigned bitbuf, bitcnt;
    unsigned char *out;
    size_t outlen, outcap;
    int err;
} Inflate;

typedef struct {
    unsigned short count[16];
    unsigned short symbol[288];
} Huff;

static unsigned getbit(Inflate *s)
{
    if (s->bitcnt == 0) {
        if (s->pos >= s->len) { s->err = 1; return 0; }
        s->bitbuf = s->in[s->pos++];
        s->bitcnt = 8;
    }
    unsigned b = s->bitbuf & 1;
    s->bitbuf >>= 1;
    s->bitcnt--;
    return b;
}

static unsigned getbits(Inflate *s, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= getbit(s) << i;
    return v;
}

static void put(Inflate *s, unsigned char c)
{
    if (s->outlen == s->outcap) {
        s->outcap = s->outcap ? s->outcap * 2 : 65536;
        s->out = realloc(s->out, s->outcap);
        if (!s->out) { s->err = 1; return; }
    }
    s->out[s->outlen++] = c;
}

static void build(Huff *h, const unsigned char *lengths, int n)
{
    unsigned short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (unsigned short)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (unsigned short)i;
}

static int decode(Inflate *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)getbit(s);
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

static const unsigned short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char lextra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char dextra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void codes(Inflate *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->err) return;
        if (sym < 256) { put(s, (unsigned char)sym); continue; }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) { s->err = 1; return; }
        unsigned len = lbase[sym] + getbits(s, lextra[sym]);
        int ds = decode(s, dist);
        if (ds >= 30) { s->err = 1; return; }
        size_t d = dbase[ds] + getbits(s, dextra[ds]);
        if (d > s->outlen) { s->err = 1; return; }
        while (len--) put(s, s->out[s->outlen - d]);
    }
}

static void inflate_stream(Inflate *s)
{
    int last;
    do {
        last = (int)getbit(s);
        int type = (int)getbits(s, 2);
        if (type == 0) {
            s->bitcnt = 0;
            if (s->pos + 4 > s->len) { s->err = 1; return; }
            unsigned n = s->in[s->pos] | s->in[s->pos + 1] << 8;
            s->pos += 4;
            if (s->pos + n > s->len) { s->err = 1; return; }
            while (n--) put(s, s->in[s->pos++]);
        } else if (type == 1) {
            static Huff lit, dist;
            static int ready;
            if (!ready) {
                unsigned char l[288];
                int i = 0;
                for (; i < 144; i++) l[i] = 8;
                for (; i < 256; i++) l[i] = 9;
                for (; i < 280; i++) l[i] = 7;
                for (; i < 288; i++) l[i] = 8;
                build(&lit, l, 288);
                for (i = 0; i < 30; i++) l[i] = 5;
                build(&dist, l, 30);
                ready = 1;
            }
            codes(s, &lit, &dist);
        } else if (type == 2) {
            static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            unsigned char lengths[320];
            Huff lencode, lit, dist;
            int nlen = (int)getbits(s, 5) + 257, ndist = (int)getbits(s, 5) + 1, ncode = (int)getbits(s, 4) + 4;
            memset(lengths, 0, sizeof lengths);
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (unsigned char)getbits(s, 3);
            build(&lencode, lengths, 19);
            int idx = 0;
            while (idx < nlen + ndist && !s->err) {
                int sym = decode(s, &lencode);
                if (sym < 16) { lengths[idx++] = (unsigned char)sym; continue; }
                unsigned char v = 0;
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
    } while (!last && !s->err);
}

/* ------------------------------------------------------------------ */
/* Lectura                                                             */
/* ------------------------------------------------------------------ */

static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

unsigned char *png_read_rgb(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *file = malloc((size_t)size);
    if (!file || fread(file, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(file); return NULL; }
    fclose(f);

    unsigned char *idat = NULL, pal[768] = { 0 };
    size_t idat_len = 0;
    int width = 0, height = 0, depth = 0, ctype = 0, interlace = 0;
    size_t pos = 8;
    while (pos + 8 <= (size_t)size) {
        unsigned len = be32(file + pos);
        const unsigned char *type = file + pos + 4, *data = file + pos + 8;
        if (pos + 12 + len > (size_t)size) break;
        if (!memcmp(type, "IHDR", 4)) {
            width = (int)be32(data); height = (int)be32(data + 4);
            depth = data[8]; ctype = data[9]; interlace = data[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            memcpy(pal, data, len > 768 ? 768 : len);
        } else if (!memcmp(type, "IDAT", 4)) {
            idat = realloc(idat, idat_len + len);
            memcpy(idat + idat_len, data, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    free(file);
    if (!idat || interlace || idat_len < 2 || (depth != 8 && !(depth < 8 && (ctype == 0 || ctype == 3)))) { free(idat); return NULL; }

    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: free(idat); return NULL;
    }

    Inflate s = { 0 };
    s.in = idat + 2;    /* cabecera zlib */
    s.len = idat_len - 2;
    inflate_stream(&s);
    free(idat);
    size_t stride = ((size_t)width * channels * depth + 7) / 8;
    size_t bpp = depth < 8 ? 1 : (size_t)channels;  /* bytes por pixel para los filtros */
    if (s.err || s.outlen < (stride + 1) * height) { free(s.out); return NULL; }

    unsigned char *raw = malloc(stride * height);
    unsigned char *rgb = malloc((size_t)width * height * 3);
    for (int y = 0; y < height; y++) {
        const unsigned char *src = s.out + y * (stride + 1);
        unsigned char *row = raw + y * stride, *prev = y ? row - stride : NULL;
        int filter = src[0];
        for (size_t x = 0; x < stride; x++) {
            int a = x >= bpp ? row[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            int v = src[1 + x];
            switch (filter) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: break;
            }
            row[x] = (unsigned char)v;
        }
    }
    free(s.out);

    for (int i = 0; i < width * height; i++) {
        unsigned char *o = rgb + (size_t)i * 3;
        if (depth < 8) {
            /* Pixeles empacados: el primero en los bits altos */
            int x = i % width, y = i / width;
            size_t bit = (size_t)x * depth;
            int v = (raw[y * stride + bit / 8] >> (8 - depth - (int)(bit % 8))) & ((1 << depth) - 1);
            if (ctype == 3) memcpy(o, pal + v * 3, 3);
            else o[0] = o[1] = o[2] = (unsigned char)(v * 255 / ((1 << depth) - 1));
            continue;
        }
        const unsigned char *p = raw + (size_t)i * channels;
        if (ctype == 3) memcpy(o, pal + p[0] * 3, 3);
        else if (channels <= 2) o[0] = o[1] = o[2] = p[0];
        else memcpy(o, p, 3);
    }
    free(raw);
    *w = width;
    *h = height;
    return rgb;
}

/* ------------------------------------------------------------------ */
/* Escritura (gris de 8 bits, deflate sin compresión)                  */
/* ------------------------------------------------------------------ */

static unsigned crc_table[256];

static unsigned crc32_update(unsigned crc, const unsigned char *p, size_t n)
{
    if (!crc_table[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
    unsigned char hdr[8];
    put32(hdr, (unsigned)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    unsigned crc = crc32_update(0, hdr + 4, 4);
    crc = crc32_update(crc, data, len);
    unsigned char c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

int png_write_gray(const char *path, const unsigned char *pixels, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13] = { 0 };
    put32(ihdr, (unsigned)w);
    put32(ihdr + 4, (unsigned)h);
    ihdr[8] = 8;
    chunk(f, "IHDR", ihdr, 13);

    size_t raw_len = (size_t)(w + 1) * h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    unsigned char *z = malloc(z_len), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    unsigned a = 1, b = 0;
    size_t done = 0;
    while (done < raw_len) {
        size_t n = raw_len - done > 65535 ? 65535 : raw_len - done;
        *p++ = done + n == raw_len ? 1 : 0;
        *p++ = (unsigned char)n; *p++ = (unsigned char)(n >> 8);
        *p++ = (unsigned char)~n; *p++ = (unsigned char)(~n >> 8);
        for (size_t i = 0; i < n; i++) {
            size_t k = done + i;
            size_t y = k / (size_t)(w + 1), x = k % (size_t)(w + 1);
            unsigned char v = x == 0 ? 0 : pixels[y * w + x - 1];
            *p++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        done += n;
    }
    put32(p, b << 16 | a);
    chunk(f, "IDAT", z, z_len);
    free(z);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    return 1;
}
