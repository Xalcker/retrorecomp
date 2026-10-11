/*
 * Cartucho: formatos de archivo, cabecera de $100 y tabla de vectores.
 *
 * El 68000 ve la ROM como palabras big-endian a partir de $000000. Los archivos .bin, .md y
 * .gen ya vienen así. Los .smd (Super Magic Drive) traen 512 bytes de cabecera del copiador y
 * cada bloque de 16 KB partido en dos mitades: primero los bytes impares y luego los pares. Se
 * normaliza todo a la imagen del 68000, y el SHA1 se calcula sobre esa imagen: la misma ROM en
 * .bin o en .smd tiene el mismo SHA1.
 */
#include "gen.h"
#include "sha1.h"

#include <stdlib.h>
#include <string.h>

static int has_sega(const u8 *rom, size_t len)
{
    if (len < 0x110) return 0;
    return !memcmp(rom + 0x100, "SEGA", 4) || !memcmp(rom + 0x101, "SEGA", 4);
}

static void smd_deinterleave(const u8 *src, size_t blocks, u8 *dst)
{
    for (size_t b = 0; b < blocks; b++) {
        const u8 *in = src + b * 0x4000;
        u8 *out = dst + b * 0x4000;
        for (size_t i = 0; i < 0x2000; i++) {
            out[i * 2 + 1] = in[i];             /* primera mitad: bytes impares */
            out[i * 2]     = in[0x2000 + i];    /* segunda mitad: bytes pares */
        }
    }
}

u8 *cart_normalize(const u8 *data, size_t len, size_t *out_len, int *format, const char **err)
{
    if (len < 0x200) { *err = "el archivo es demasiado chico para ser una ROM de Genesis"; return NULL; }
    *format = ROMFMT_BIN;
    /* Cabecera de copiador de 512 bytes: .smd (entrelazado) o, rara vez, un .bin con cabecera */
    if (len % 0x4000 == 0x200 && len > 0x200) {
        const u8 *body = data + 0x200;
        size_t blen = len - 0x200;
        int smd_magic = data[8] == 0xAA && data[9] == 0xBB;
        u8 *de = malloc(blen);
        if (!de) { *err = "sin memoria"; return NULL; }
        smd_deinterleave(body, blen / 0x4000, de);
        if (smd_magic || (has_sega(de, blen) && !has_sega(body, blen))) {
            *format = ROMFMT_SMD; *out_len = blen; return de;
        }
        if (has_sega(body, blen)) {                 /* .bin con cabecera de copiador */
            memcpy(de, body, blen); *out_len = blen; return de;
        }
        free(de);
    }
    if (len > ROM_LIMIT) { *err = "la ROM pasa de 16 MB"; return NULL; }
    size_t n = len + (len & 1);                     /* tamaño par: el 68000 lee palabras */
    u8 *rom = malloc(n);
    if (!rom) { *err = "sin memoria"; return NULL; }
    memcpy(rom, data, len);
    if (n > len) rom[len] = 0xFF;
    *out_len = n;
    return rom;
}

u16 cart_checksum(const u8 *rom, size_t len)
{
    u16 sum = 0;
    for (size_t i = 0x200; i + 1 < len; i += 2) sum = (u16)(sum + rd16(rom + i));
    return sum;
}

/* Copia un texto de la cabecera sin los espacios ni ceros del final */
static void field(char *dst, const u8 *src, size_t n)
{
    memcpy(dst, src, n);
    dst[n] = 0;
    for (size_t i = 0; i < n; i++) if ((u8)dst[i] < 0x20 || (u8)dst[i] > 0x7E) dst[i] = ' ';
    while (n && dst[n - 1] == ' ') dst[--n] = 0;
}

void cart_parse_header(Cart *c)
{
    GenHeader *h = &c->hdr;
    const u8 *r = c->rom;
    memset(h, 0, sizeof *h);
    if (c->rom_size < 0x200) return;
    h->sega = has_sega(r, c->rom_size);
    field(h->system, r + 0x100, 16);
    field(h->copyright, r + 0x110, 16);
    field(h->name_dom, r + 0x120, 48);
    field(h->name_int, r + 0x150, 48);
    field(h->serial, r + 0x180, 14);
    h->checksum = rd16(r + 0x18E);
    h->rom_start = rd32(r + 0x1A0); h->rom_end = rd32(r + 0x1A4);
    /* Hasta el fin de ROM que declara la cabecera, como la rutina de los juegos (si es razonable) */
    size_t sum_end = h->rom_end >= 0x200 && h->rom_end < c->rom_size ? (size_t)h->rom_end + 1 : c->rom_size;
    h->checksum_calc = cart_checksum(r, sum_end);
    field(h->io, r + 0x190, 16);
    h->ram_start = rd32(r + 0x1A8); h->ram_end = rd32(r + 0x1AC);
    field(h->region, r + 0x1F0, 3);
    c->sram = r[0x1B0] == 'R' && r[0x1B1] == 'A';
    if (c->sram) {
        c->sram_type = r[0x1B2];
        c->sram_start = rd32(r + 0x1B4);
        c->sram_end = rd32(r + 0x1B8);
        if (c->sram_end < c->sram_start) c->sram = 0;
    }
}

u32 cart_vector(const Cart *c, int n)
{
    if (n < 0 || n >= VEC_COUNT || (size_t)n * 4 + 4 > c->rom_size) return 0;
    return rd32(c->rom + n * 4);
}

const char *cart_vector_name(int n)
{
    static const char *const names[] = {
        [VEC_SSP] = "SSP", [VEC_RESET] = "reset", [VEC_BUS] = "error de bus",
        [VEC_ADDRESS] = "error de dirección", [VEC_ILLEGAL] = "instrucción ilegal",
        [VEC_ZERODIV] = "división entre cero", [VEC_CHK] = "CHK", [VEC_TRAPV] = "TRAPV",
        [VEC_PRIV] = "privilegio", [VEC_TRACE] = "traza", [VEC_LINEA] = "línea A", [VEC_LINEF] = "línea F",
        [VEC_SPURIOUS] = "espuria", [VEC_AUTO1 + 1] = "EXT (nivel 2)",
        [VEC_AUTO1 + 3] = "HINT (nivel 4)", [VEC_AUTO1 + 5] = "VINT (nivel 6)",
    };
    if (n < 0 || n >= (int)(sizeof names / sizeof names[0])) return NULL;
    return names[n];
}

/* Mapper, espejos y SRAM según el tamaño de la ROM y la cabecera */
void cart_setup(Cart *c)
{
    u32 m = 1;
    while (m < c->rom_size) m <<= 1;
    c->rom_mask = m - 1;
    c->mapper = (c->rom_size > ROM_LINEAR || !strncmp(c->hdr.system, "SEGA SSF", 8)) ? MAPPER_SSF2 : MAPPER_NONE;
    free(c->sram_data); free(c->sram_saved);
    c->sram_data = c->sram_saved = NULL;
    c->sram_size = 0;
    c->has_eeprom = eeprom_detect(c);
    if (c->has_eeprom) {                /* la cabecera puede declarar "RA" para la EEPROM: no es SRAM */
        c->sram = 0;
        c->sram_size = (size_t)c->eeprom.size_mask + 1;
        c->sram_data = malloc(c->sram_size);
        c->sram_saved = malloc(c->sram_size);
        memset(c->sram_data, 0xFF, c->sram_size);
        memcpy(c->sram_saved, c->sram_data, c->sram_size);
    } else if (c->sram) {
        u32 base = c->sram_start & ~1u, end = c->sram_end | 1u;
        /* Cabeceras rotas: fuera del espacio del cartucho o de más de 1 MB */
        if (base >= ROM_LINEAR || end >= ROM_LINEAR || end - base >= 0x100000) c->sram = 0;
        else {
            c->sram_size = end - base + 1;
            c->sram_data = malloc(c->sram_size);
            c->sram_saved = malloc(c->sram_size);
            memset(c->sram_data, 0xFF, c->sram_size);       /* sin partida: memoria sin programar */
            memcpy(c->sram_saved, c->sram_data, c->sram_size);
        }
    }
    cart_map_reset(c);
}

void cart_map_reset(Cart *c)
{
    for (u32 i = 0; i < 8; i++) c->bank[i] = i * 0x80000;
    c->a130f1 = 0;
    c->map_ver++;
}

u32 cart_rom_offset(const Cart *c, u32 addr)
{
    return c->bank[addr >> 19 & 7] + (addr & 0x7FFFF);
}
