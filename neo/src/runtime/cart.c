/*
 * Cartucho: formatos de archivo, imagen normalizada, cabecera de $100 y tabla de vectores.
 *
 * Un juego de Neo Geo son varios chips: P (programa del 68000), S (capa fija), M1 (Z80), V
 * (muestras) y C (sprites, por pares). Se aceptan tres formatos:
 *   .neo      un archivo con una cabecera de 4096 bytes (tamaños de cada región) y las regiones
 *   .zip      al estilo MAME: un archivo por chip; la base de datos (neodb.c) dice qué es cada uno por
 *             su CRC32, y si el juego no está, se deduce de los sufijos (.p1, .s1, .m1, .v1, .c1...)
 *   carpeta   los mismos archivos sueltos
 * Todo se normaliza a la misma imagen (ver neo.h) y el SHA1 se calcula sobre ella.
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include "neo.h"
#include "neodb.h"
#include "sha1.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *const neo_region_name[REG_COUNT] = { "P", "S", "M1", "V-A", "V-B", "C" };
const char *const neo_entry_name[ENTRY_COUNT] = { "USER", "PLAYER_START", "DEMO_END", "COIN_SOUND" };

static unsigned char *default_read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n >= 0 ? malloc(n ? (size_t)n : 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

unsigned char *(*neo_read_file)(const char *path, size_t *size) = default_read_file;

void cart_free(Cart *c)
{
    free(c->data);
    memset(c, 0, sizeof *c);
}

/* Byte de la dirección a del 68000 en un programa con las palabras invertidas */
static u8 p_byte(const u8 *p, u32 a) { return p[a ^ 1]; }
static int has_neogeo(const u8 *p, size_t len)
{
    static const char tag[] = "NEO-GEO";
    if (len < 0x110) return 0;
    for (u32 i = 0; i < 7; i++) if (p_byte(p, 0x100 + i) != (u8)tag[i]) return 0;
    return 1;
}

/* SHA1, cabecera, vectores y bancos, una vez armada la imagen */
static void finish(Cart *c)
{
    sha1_hex(c->data, c->size, c->sha1);
    for (int r = 0; r < REG_COUNT; r++) sha1_hex(c->data + c->off[r], c->len[r], c->reg_sha1[r]);
    const u8 *p = c->data + c->off[REG_P];
    NeoHeader *h = &c->hdr;
    memset(h, 0, sizeof *h);
    h->neo = has_neogeo(p, c->len[REG_P]);
    if (c->len[REG_P] >= 0x200) {
        h->sysver = p_byte(p, 0x107);
        h->ngh = cart_p16(c, 0x108);
        h->p_size = cart_p32(c, 0x10A);
        h->backup_ptr = cart_p32(c, 0x10E);
        h->backup_size = cart_p16(c, 0x112);
        h->eyecatch = p_byte(p, 0x114);
        h->eyecatch_bank = p_byte(p, 0x115);
        for (int i = 0; i < 3; i++) h->dip[i] = cart_p32(c, 0x116 + 4 * (u32)i);
        for (int i = 0; i < ENTRY_COUNT; i++) {
            u32 a = 0x122 + 6 * (u32)i;
            h->entry[i] = cart_p16(c, a) == 0x4EF9 ? cart_p32(c, a + 2) : 0;     /* JMP abs.l */
        }
        h->security = cart_p32(c, 0x182);
        for (int n = 0; n < VEC_COUNT; n++) c->vec[n] = cart_p32(c, 4 * (u32)n);
    }
    c->p_banks = c->len[REG_P] > P_BANK_SIZE ? (int)((c->len[REG_P] - 1) / P_BANK_SIZE) : 0;
    if (!c->db) c->db = neodb_by_sha1(c->sha1);
    if (c->db) {
        if (!c->title[0]) snprintf(c->title, sizeof c->title, "%s", c->db->title);
        if (!c->maker[0]) snprintf(c->maker, sizeof c->maker, "%s", c->db->maker);
        if (!c->set[0]) snprintf(c->set, sizeof c->set, "%s", c->db->set);
        if (!c->year) c->year = c->db->year;
        if (!c->genre) c->genre = c->db->genre;
    }
}

/* ------------------------------------------------------------------ */
/* .neo                                                                */
/* ------------------------------------------------------------------ */

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put_le32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

#define NEO_HEADER 4096

/* Texto de la cabecera del .neo (relleno con ceros o espacios) sin lo que sobra */
static void neo_text(char *out, size_t size, const u8 *p, size_t n)
{
    size_t l = 0;
    while (l < n && p[l]) l++;
    while (l > 0 && p[l - 1] == ' ') l--;
    if (l >= size) l = size - 1;
    memcpy(out, p, l);
    out[l] = 0;
}

int cart_load_neo(Cart *c, const u8 *data, size_t len, const char **err)
{
    memset(c, 0, sizeof *c);
    if (len < NEO_HEADER || memcmp(data, "NEO", 3) || data[3] != 1) { *err = "no es un archivo .neo (falta \"NEO\\1\")"; return -1; }
    /* P, S, M, V1, V2, C: V1 es el bus A (o los dos) y V2 el bus B */
    static const int order[6] = { REG_P, REG_S, REG_M, REG_VA, REG_VB, REG_C };
    size_t total = 0;
    for (int i = 0; i < 6; i++) {
        size_t n = le32(data + 4 + 4 * i);
        c->off[order[i]] = total;
        c->len[order[i]] = n;
        total += n;
        if (n > len || total > len - NEO_HEADER) { *err = "el .neo está cortado (los tamaños de la cabecera no caben)"; return -1; }
    }
    if (c->len[REG_P] < 0x200 || (c->len[REG_P] & 1)) { *err = "el .neo no tiene un programa (P) válido"; return -1; }
    c->data = malloc(total ? total : 1);
    if (!c->data) { *err = "sin memoria"; return -1; }
    memcpy(c->data, data + NEO_HEADER, total);
    c->size = total;
    c->format = ROMFMT_NEO;
    c->year = le32(data + 0x1C);
    c->genre = le32(data + 0x20);
    neo_text(c->title, sizeof c->title, data + 0x2C, 33);
    neo_text(c->maker, sizeof c->maker, data + 0x4D, 17);
    finish(c);
    return 0;
}

int cart_write_neo(const Cart *c, const char *path)
{
    u8 *h = calloc(1, NEO_HEADER);
    if (!h) return 0;
    memcpy(h, "NEO\1", 4);
    static const int order[6] = { REG_P, REG_S, REG_M, REG_VA, REG_VB, REG_C };
    for (int i = 0; i < 6; i++) put_le32(h + 4 + 4 * i, (u32)c->len[order[i]]);
    put_le32(h + 0x1C, c->year);
    put_le32(h + 0x20, c->genre);
    put_le32(h + 0x28, c->hdr.ngh);
    const char *name = c->title[0] ? c->title : c->set[0] ? c->set : "NEORecomp";
    memcpy(h + 0x2C, name, strlen(name) < 32 ? strlen(name) : 32);
    memcpy(h + 0x4D, c->maker, strlen(c->maker) < 16 ? strlen(c->maker) : 16);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(h, 1, NEO_HEADER, f) == NEO_HEADER && fwrite(c->data, 1, c->size, f) == c->size;
    if (f && fclose(f)) ok = 0;
    free(h);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Chips sueltos (zip o carpeta)                                       */
/* ------------------------------------------------------------------ */

/* Papel de un chip por su nombre: "201-p1.p1", "063-v21.v21", "058-sp2.sp2", "aof_c1.rom"... Mira
 * primero la extensión y luego lo que sigue al último '-' o '_' antes de ella */
static int classify_token(const char *t, size_t n, u8 *role, u8 *idx)
{
    char s[16];
    if (n == 0 || n >= sizeof s) return 0;
    for (size_t i = 0; i < n; i++) s[i] = (char)tolower((unsigned char)t[i]);
    s[n] = 0;
    const char *d = s;
    u8 r;
    if (s[0] == 's' && s[1] == 'p') { r = PART_P; d = s + 2; }     /* sp2: el P2 */
    else if (s[0] == 'p') { r = PART_P; d = s + 1; }
    else if (s[0] == 's') { r = PART_S; d = s + 1; }
    else if (s[0] == 'm') { r = PART_M; d = s + 1; }
    else if (s[0] == 'v') { r = PART_VA; d = s + 1; }
    else if (s[0] == 'c') { r = PART_C; d = s + 1; }
    else return 0;
    if (!*d) return 0;
    for (const char *q = d; *q; q++) if (!isdigit((unsigned char)*q)) return 0;
    int v = atoi(d);
    if (r == PART_VA && strlen(d) == 2) {           /* v11-v19: bus A; v21-v29: bus B */
        if (v / 10 == 2) r = PART_VB;
        else if (v / 10 != 1) return 0;
        v %= 10;
    }
    if (v < 1 || v > 32) return 0;
    *role = r;
    *idx = (u8)v;
    return 1;
}

static int classify(const char *name, u8 *role, u8 *idx)
{
    const char *dot = strrchr(name, '.');
    if (dot && classify_token(dot + 1, strlen(dot + 1), role, idx)) return 1;
    const char *end = dot ? dot : name + strlen(name);
    const char *start = name;
    for (const char *q = name; q < end; q++) if (*q == '-' || *q == '_') start = q + 1;
    return classify_token(start, (size_t)(end - start), role, idx);
}

static void note_file(char *buf, size_t size, const char *name)
{
    size_t l = strlen(buf);
    if (l + 2 >= size) return;
    snprintf(buf + l, size - l, "%s%s", l ? " " : "", name);
}

int cart_load_parts(Cart *c, RomPart *parts, int n, int format, const char **err)
{
    memset(c, 0, sizeof *c);
    c->format = format;
    if (n <= 0) { *err = "no hay archivos"; return -1; }
    u8 *role = calloc((size_t)n, 1), *idx = calloc((size_t)n, 1);
    if (!role || !idx) { free(role); free(idx); *err = "sin memoria"; return -1; }
    c->db = neodb_match(parts, n);
    for (int i = 0; i < n; i++) {
        if (c->db) {
            for (const NeoDbChip *d = c->db->chips; d->size; d++)
                if (d->crc == parts[i].crc && d->size == parts[i].size) { role[i] = d->role; idx[i] = d->idx; }
        } else if (!classify(parts[i].name, &role[i], &idx[i])) role[i] = PART_NONE;
    }
    /* Cada (papel, número) una sola vez */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (role[i] != PART_NONE && role[i] == role[j] && idx[i] == idx[j]) {
                free(role); free(idx); *err = "dos archivos con el mismo papel (por ejemplo dos .p1)"; return -1;
            }
    /* Índice de cada papel por número (0 = falta) */
    int at[7][33];
    memset(at, -1, sizeof at);
    int maxidx[7] = { 0 };
    for (int i = 0; i < n; i++)
        if (role[i] != PART_NONE) { at[role[i]][idx[i]] = i; if (idx[i] > maxidx[role[i]]) maxidx[role[i]] = idx[i]; }
    free(role); free(idx);
    #define PART(r, k) (at[r][k] >= 0 ? &parts[at[r][k]] : NULL)
    if (!PART(PART_P, 1)) { *err = "falta el programa (P1: un archivo .p1)"; return -1; }
    if (!PART(PART_M, 1)) { *err = "falta el programa del Z80 (M1: un archivo .m1)"; return -1; }
    for (int r = PART_P; r <= PART_C; r++)
        for (int k = 1; k <= maxidx[r]; k++)
            if (!PART(r, k) && !(r == PART_S && maxidx[r] == 0)) { *err = "falta un archivo en medio de una serie (por ejemplo .c2 o .v2)"; return -1; }
    if (maxidx[PART_C] & 1) { *err = "las C van por pares (C1/C2, C3/C4...): falta una"; return -1; }
    for (int k = 1; k <= maxidx[PART_C]; k += 2)
        if (PART(PART_C, k)->size != PART(PART_C, k + 1)->size) { *err = "los dos chips de un par de C miden distinto"; return -1; }

    /* Tamaños */
    const RomPart *p1 = PART(PART_P, 1);
    size_t p1_len = p1->size;
    if (maxidx[PART_P] > 1 && p1_len < P_BANK_SIZE) p1_len = P_BANK_SIZE;   /* P2 empieza en $200000 */
    size_t len[REG_COUNT] = { 0 };
    len[REG_P] = p1_len;
    for (int k = 2; k <= maxidx[PART_P]; k++) len[REG_P] += PART(PART_P, k)->size;
    static const int part_of[REG_COUNT] = { PART_P, PART_S, PART_M, PART_VA, PART_VB, PART_C };
    for (int r = REG_S; r < REG_COUNT; r++)
        for (int k = 1; k <= maxidx[part_of[r]]; k++) len[r] += PART(part_of[r], k)->size;
    if ((len[REG_P] & 1) || len[REG_P] < 0x200) { *err = "el programa (P) es demasiado chico o de tamaño impar"; return -1; }
    size_t total = 0;
    for (int r = 0; r < REG_COUNT; r++) { c->off[r] = total; c->len[r] = len[r]; total += len[r]; }
    c->data = malloc(total);
    if (!c->data) { *err = "sin memoria"; return -1; }
    c->size = total;

    /* P: el P1 de 2 MB trae las mitades cambiadas (el segundo MB es $000000). Se acomoda solo si
     * así aparece "NEO-GEO" en $100 (salvo que la base de datos diga otra cosa) */
    u8 *p = c->data + c->off[REG_P];
    memcpy(p, p1->data, p1->size);
    if (p1->size == 2 * P_BANK_SIZE && !(c->db && (c->db->flags & DB_P1_LINEAR))) {
        u8 *tmp = malloc(P_BANK_SIZE);
        if (!tmp) { cart_free(c); *err = "sin memoria"; return -1; }
        memcpy(tmp, p1->data + P_BANK_SIZE, P_BANK_SIZE);
        if (c->db || (has_neogeo(tmp, P_BANK_SIZE) && !has_neogeo(p1->data, P_BANK_SIZE))) {
            memcpy(p, tmp, P_BANK_SIZE);
            memcpy(p + P_BANK_SIZE, p1->data, P_BANK_SIZE);
            c->p1_swapped = 1;
        }
        free(tmp);
    }
    for (size_t i = p1->size; i < p1_len; i++) p[i] = p[i % p1->size];     /* espejo hasta 1 MB */
    note_file(c->files[REG_P], sizeof c->files[REG_P], p1->name);
    size_t o = p1_len;
    for (int k = 2; k <= maxidx[PART_P]; k++) {
        const RomPart *q = PART(PART_P, k);
        memcpy(p + o, q->data, q->size);
        o += q->size;
        note_file(c->files[REG_P], sizeof c->files[REG_P], q->name);
    }
    /* S, M1, V: seguidos en orden */
    for (int r = REG_S; r <= REG_VB; r++) {
        o = c->off[r];
        for (int k = 1; k <= maxidx[part_of[r]]; k++) {
            const RomPart *q = PART(part_of[r], k);
            memcpy(c->data + o, q->data, q->size);
            o += q->size;
            note_file(c->files[r], sizeof c->files[r], q->name);
        }
    }
    /* C: cada par intercalado byte a byte (C1 en los bytes pares, C2 en los impares) */
    o = c->off[REG_C];
    for (int k = 1; k <= maxidx[PART_C]; k += 2) {
        const RomPart *a = PART(PART_C, k), *b = PART(PART_C, k + 1);
        u8 *dst = c->data + o;
        for (size_t i = 0; i < a->size; i++) { dst[2 * i] = a->data[i]; dst[2 * i + 1] = b->data[i]; }
        o += 2 * a->size;
        char pair[300];
        snprintf(pair, sizeof pair, "%s+%s", a->name, b->name);
        note_file(c->files[REG_C], sizeof c->files[REG_C], pair);
    }
    #undef PART
    finish(c);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Desde una ruta                                                      */
/* ------------------------------------------------------------------ */

static int load_dir(Cart *c, const char *path, const char **err)
{
    DIR *d = opendir(path);
    if (!d) { *err = "no se pudo abrir la carpeta"; return -1; }
    RomPart *parts = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(full, &st) || !S_ISREG(st.st_mode)) continue;
        u8 role, idx;
        if (!classify(e->d_name, &role, &idx) && st.st_size > 64 * 1024 * 1024) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            RomPart *np = realloc(parts, (size_t)cap * sizeof *parts);
            if (!np) { closedir(d); parts_free(parts, n); *err = "sin memoria"; return -1; }
            parts = np;
        }
        RomPart *r = &parts[n];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", e->d_name);
        r->data = neo_read_file(full, &r->size);
        if (!r->data) continue;
        r->crc = crc32_buf(0, r->data, r->size);
        n++;
    }
    closedir(d);
    int res = cart_load_parts(c, parts, n, ROMFMT_DIR, err);
    parts_free(parts, n);
    return res;
}

int cart_load_path(Cart *c, const char *path, const char **err)
{
    memset(c, 0, sizeof *c);
    struct stat st;
    if (!stat(path, &st) && S_ISDIR(st.st_mode)) return load_dir(c, path, err);
    size_t len;
    u8 *data = neo_read_file(path, &len);
    if (!data) { *err = "no se pudo leer el archivo"; return -1; }
    int res;
    if (len >= 4 && !memcmp(data, "NEO\1", 4)) res = cart_load_neo(c, data, len, err);
    else if (len >= 4 && !memcmp(data, "PK\3\4", 4)) {
        RomPart *parts;
        int n = zip_read_all(data, len, &parts, err);
        res = n < 0 ? -1 : cart_load_parts(c, parts, n, ROMFMT_ZIP, err);
        if (n >= 0) parts_free(parts, n);
    } else {
        *err = "no es un .neo, un .zip ni una carpeta con los chips del juego";
        res = -1;
    }
    free(data);
    return res;
}

/* ------------------------------------------------------------------ */
/* Lo que todavía no se puede correr                                   */
/* ------------------------------------------------------------------ */

const char *cart_unsupported(const Cart *c, int *fatal)
{
    static char msg[256];
    u16 g = c->hdr.ngh;
    *fatal = 1;
    if (!c->hdr.neo) return "el programa no tiene \"NEO-GEO\" en $100: no es un juego de Neo Geo, o está cifrado";
    /* Desde 1999 (NGH $251, KOF '99) todos los juegos van cifrados (NEO-CMC, SMA, PCM2 o PVC), y
     * algunos de antes */
    if ((g >= 0x251 && g <= 0x275) || g == 0x008 || g == 0x070 || g == 0x151) {
        snprintf(msg, sizeof msg, "NGH $%04X: juego cifrado (NEO-CMC, SMA, PCM2 o PVC); llega en una fase posterior", g);
        return msg;
    }
    /* KOF '98: los sets de MVS (kof98, kof98a, kof98k...) traen un P1 de 2 MB cifrado y protegido; el del
     * AES (kof98h, 242-pn1.p1) es de 1 MB y va sin cifrar. Con 4 MB de P2, 6 MB de programa = MVS. */
    if (g == 0x242 && c->len[REG_P] > 0x500000) {
        snprintf(msg, sizeof msg, "NGH $0242: es el set de MVS de KOF '98 (kof98), con el P1 cifrado; usa el del AES (kof98h, 242-pn1.p1 de 1 MB)");
        return msg;
    }
    if (g == 0x047 || g == 0x052 || g == 0x250) {
        snprintf(msg, sizeof msg, "NGH $%04X: el programa tiene protección (Fatal Fury 2, Super Sidekicks, Metal Slug X); llega en una fase posterior", g);
        return msg;
    }
    *fatal = 0;
    if (g == 0x006 || g == 0x019 || g == 0x038)
        return "juego con cable de enlace entre consolas (Riding Hero, League Bowling, Thrash Rally): el enlace no se emula";
    if (g == 0x004 || g == 0x027 || g == 0x036 || g == 0x048) return "juego de mahjong: necesita el mando de mahjong, que no se emula";
    if (g == 0x236) return "The Irritating Maze necesita un trackball, que no se emula";
    if (c->db && (c->db->flags & DB_MVS_ONLY)) return "salió solo en MVS: puede fallar con la BIOS del AES";
    return NULL;
}

const char *cart_vector_name(int n)
{
    static const char *const names[] = {
        [VEC_SSP] = "SSP", [VEC_RESET] = "reset", [VEC_BUS] = "error de bus",
        [VEC_ADDRESS] = "error de dirección", [VEC_ILLEGAL] = "instrucción ilegal",
        [VEC_ZERODIV] = "división entre cero", [VEC_CHK] = "CHK", [VEC_TRAPV] = "TRAPV",
        [VEC_PRIV] = "privilegio", [VEC_TRACE] = "traza", [VEC_LINEA] = "línea A", [VEC_LINEF] = "línea F",
        [VEC_SPURIOUS] = "espuria", [VEC_AUTO1] = "VBLANK (nivel 1)",
        [VEC_AUTO1 + 1] = "temporizador (nivel 2)", [VEC_AUTO1 + 2] = "arranque (nivel 3)",
    };
    if (n < 0 || n >= (int)(sizeof names / sizeof names[0])) return NULL;
    return names[n];
}

/* ------------------------------------------------------------------ */
/* Identidad del código                                                */
/* ------------------------------------------------------------------ */

long neo_code_offset(const Neo *g, u32 pc)
{
    const Cart *c = &g->cart;
    pc &= 0xFFFFFF;
    size_t plen = c->len[REG_P];
    if (pc < 0x100000) {
        size_t p1 = plen < P_BANK_SIZE ? plen : P_BANK_SIZE;
        return p1 ? (long)(pc % p1) : -1;
    }
    if (pc >= 0x200000 && pc < 0x300000) {
        if (!c->p_banks) return -1;
        size_t base = P_BANK_SIZE * (1 + (size_t)(g->p2_bank % c->p_banks));
        size_t n = plen - base < P_BANK_SIZE ? plen - base : P_BANK_SIZE;       /* un P2 chico se repite */
        return (long)(base + (pc & 0xFFFFF) % n);
    }
    if (pc >= 0xC00000 && pc < 0xD00000) return (long)(BIOS_SPACE + (pc & (BIOS_SIZE - 1)));
    return -1;
}
