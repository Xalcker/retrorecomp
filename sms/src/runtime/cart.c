/*
 * Cartucho: formato del archivo, cabeceras y mapper.
 *
 * El Z80 ve la ROM tal cual, así que el archivo es la imagen; solo se quita, si la hay, la cabecera de
 * 512 bytes de los copiadores (un tamaño de 16 KB × n + 512). El SHA1 se calcula sobre la imagen sin ella.
 *
 * La consola no exige ninguna marca: la cabecera "TMR SEGA" la piden las BIOS de exportación del SMS y
 * las del Game Gear (que no se emulan: se arranca con el estado que deja la BIOS), los juegos japoneses y
 * los de Codemasters a veces no la traen bien, y el homebrew suele llevar además la SDSC. Nada de esto
 * decide si la ROM corre; sirve para elegir el mapper y el modelo, y para --info.
 *
 * Fuentes: SMS Power! (Development/ROMHeader, BIOSes, CodemastersHeader, SDSCHeader, Mappers).
 */
#include "sms.h"
#include "sha1.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_ROM 32      /* la consola no pide una marca; Fade Test del Game Gear mide 50 bytes */

u8 *cart_normalize(const u8 *data, size_t len, size_t *out_len, int *copier, const char **err)
{
    *copier = 0;
    if (len % PAGE_SIZE == 512 && len > 512) {          /* cabecera de copiador */
        data += 512;
        len -= 512;
        *copier = 1;
    }
    if (len < MIN_ROM) { *err = "el archivo es demasiado chico para ser una ROM de Master System o Game Gear"; return NULL; }
    if (len > ROM_LIMIT) { *err = "la ROM pasa de 4 MB"; return NULL; }
    u8 *rom = malloc(len);
    if (!rom) { *err = "sin memoria"; return NULL; }
    memcpy(rom, data, len);
    *out_len = len;
    return rom;
}

u32 cart_crc32(const u8 *p, size_t len)
{
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* Bytes que cubre cada código de tamaño de la cabecera (BIOSes de SMS Power!); 0 si la BIOS no lo admite */
u32 size_code_bytes(int code)
{
    static const u32 kb[16] = { 256, 512, 1024, 0, 0, 0, 0, 0, 0, 0, 8, 16, 32, 48, 64, 128 };
    return kb[code & 15] * 1024u;
}

const char *region_name(int region)
{
    switch (region) {
    case 3: return "SMS de Japón";
    case 4: return "SMS de exportación";
    case 5: return "Game Gear de Japón";
    case 6: return "Game Gear de exportación";
    case 7: return "Game Gear internacional";
    default: return "desconocida";
    }
}

/* La suma de la BIOS del SMS de exportación: bytes sumados en 16 bits. El rango bajo cubre lo que hay
 * antes de la cabecera ($0000-$xFEF; el código $D, que la BIOS suma mal, llega a $BFEF) y, si la ROM pasa
 * de 32 KB, el alto va de $8000 al final del tamaño declarado */
static u16 bios_checksum(const Cart *c, int code, int *ok)
{
    u32 size = size_code_bytes(code), lo_end, hi_end = 0;
    *ok = 0;
    if (!size) return 0;
    if (code == 0xD) lo_end = 0xBFF0;
    else if (size <= 0x8000) lo_end = size - 0x10;
    else { lo_end = 0x7FF0; hi_end = size; }
    if ((hi_end ? hi_end : lo_end) > c->rom_size) return 0;
    u16 sum = 0;
    for (u32 a = 0; a < lo_end; a++) sum = (u16)(sum + c->rom[a]);
    for (u32 a = 0x8000; a < hi_end; a++) sum = (u16)(sum + c->rom[a]);
    *ok = 1;
    return sum;
}

/* Un texto zero-terminated de la ROM (los punteros de SDSC son direcciones del Z80 en las primeras páginas) */
static void rom_text(const Cart *c, u16 ptr, char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    if (ptr == 0xFFFF || ptr == 0x0000) return;
    for (u32 a = ptr; a < c->rom_size && c->rom[a] && n + 1 < size; a++)
        out[n++] = (char)(c->rom[a] < 0x20 ? ' ' : c->rom[a]);
    out[n] = 0;
}

void cart_parse_headers(Cart *c)
{
    static const u32 where[3] = { 0x7FF0, 0x3FF0, 0x1FF0 };    /* el orden en que las busca la BIOS */
    memset(&c->hdr, 0, sizeof c->hdr);
    memset(&c->cm, 0, sizeof c->cm);
    memset(&c->sdsc, 0, sizeof c->sdsc);
    for (int i = 0; i < 3; i++) {
        u32 at = where[i];
        if (at + 16 > c->rom_size || memcmp(c->rom + at, "TMR SEGA", 8)) continue;
        const u8 *h = c->rom + at;
        SmsHeader *s = &c->hdr;
        s->found = 1;
        s->at = at;
        s->checksum = rd16le(h + 0xA);
        /* Código de producto: dos bytes BCD con las últimas cuatro cifras y, en el nibble alto de +$E,
         * lo que va delante (en hexadecimal: $A es 10). Las cifras van tal cual: hay homebrew con bytes
         * que no son BCD */
        if (h[0xE] >> 4) snprintf(s->product, sizeof s->product, "%u%02X%02X", h[0xE] >> 4, h[0xD], h[0xC]);
        else snprintf(s->product, sizeof s->product, "%02X%02X", h[0xD], h[0xC]);
        s->version = h[0xE] & 15;
        s->region = h[0xF] >> 4;
        s->size_code = h[0xF] & 15;
        s->checksum_calc = bios_checksum(c, s->size_code, &s->checksum_ok);
        break;
    }
    /* La SDSC y la de Codemasters van justo antes, en $xFE0 (la de Codemasters, siempre en $7FE0) */
    u32 pre = (c->hdr.found ? c->hdr.at : 0x7FF0) - 0x10;
    if (pre + 16 <= c->rom_size && !memcmp(c->rom + pre, "SDSC", 4)) {
        const u8 *h = c->rom + pre;
        SdscHeader *d = &c->sdsc;
        d->found = 1;
        d->major = h[4]; d->minor = h[5];
        d->day = h[6]; d->month = h[7];
        d->year = rd16le(h + 8);
        rom_text(c, rd16le(h + 0xA), d->author, sizeof d->author);
        rom_text(c, rd16le(h + 0xC), d->name, sizeof d->name);
    } else if (c->rom_size >= 0x8000) {
        const u8 *h = c->rom + 0x7FE0;
        CmHeader *m = &c->cm;
        m->checksum = rd16le(h + 6);
        m->inverse = rd16le(h + 8);
        if (h[0] && (u32)m->checksum + m->inverse == 0x10000) {
            m->found = 1;
            m->banks = h[0];
            m->day = h[1]; m->month = h[2]; m->year = h[3]; m->hour = h[4]; m->minute = h[5];
            /* Palabras little-endian de las páginas que dice, sin la cabecera de Sega */
            u32 end = (u32)m->banks * PAGE_SIZE;
            if (end > c->rom_size) end = (u32)c->rom_size & ~1u;
            u16 sum = 0;
            for (u32 a = 0; a + 1 < end; a += 2)
                if (a < 0x7FF0 || a >= 0x8000) sum = (u16)(sum + rd16le(c->rom + a));
            m->checksum_calc = sum;
        }
    }
}

int cart_detect_mapper(const Cart *c)
{
    if (c->db && c->db->mapper >= 0) return c->db->mapper;
    if (c->cm.found) return MAPPER_CODEMASTERS;
    if (c->rom_size <= 3 * PAGE_SIZE) return MAPPER_NONE;      /* hasta 48 KB: las tres ranuras fijas */
    return MAPPER_SEGA;
}

const char *mapper_name(int mapper)
{
    switch (mapper) {
    case MAPPER_NONE: return "sin mapper";
    case MAPPER_SEGA: return "Sega (315-5235)";
    case MAPPER_CODEMASTERS: return "Codemasters";
    default: return "?";
    }
}

const char *model_name(int model)
{
    return model == MODEL_GG ? "gg" : "sms";
}

int model_from_name(const char *name)
{
    if (!strcmp(name, "sms")) return MODEL_SMS;
    if (!strcmp(name, "gg")) return MODEL_GG;
    if (!strcmp(name, "auto")) return MODEL_AUTO;
    return -2;
}

/* ¿El nombre termina en ext (sin distinguir mayúsculas)? */
static int has_ext(const char *name, const char *ext)
{
    size_t n = name ? strlen(name) : 0, e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != ext[i]) return 0;
    return 1;
}

int sms_load(Sms *s, const u8 *data, size_t len, const char *name, int model, const char **err)
{
    memset(s, 0, sizeof *s);
    Cart *c = &s->cart;
    c->rom = cart_normalize(data, len, &c->rom_size, &c->copier, err);
    if (!c->rom) return 1;
    sha1_hex(c->rom, c->rom_size, c->sha1);
    c->crc32 = cart_crc32(c->rom, c->rom_size);
    c->pages = (u32)((c->rom_size + PAGE_SIZE - 1) / PAGE_SIZE);
    cart_parse_headers(c);
    c->db = smsdb_find(c->sha1);
    c->mapper = cart_detect_mapper(c);
    c->ext_gg = has_ext(name, ".gg");
    /* El modelo: el pedido; el que dice la base de datos; la extensión (.gg o .sms); si no, la región de
     * la cabecera (5, 6 y 7 son del Game Gear) */
    if (model == MODEL_AUTO && c->db) model = c->db->model;
    if (model == MODEL_AUTO) {
        if (c->ext_gg) model = MODEL_GG;
        else if (has_ext(name, ".sms")) model = MODEL_SMS;
        else model = c->hdr.found && c->hdr.region >= 5 ? MODEL_GG : MODEL_SMS;
    }
    s->model = model;
    /* El modo: en el SMS, siempre SMS. En el Game Gear, un cartucho de SMS (región 3 o 4, o sin cabecera
     * y sin la extensión .gg) corre en su modo SMS */
    if (model == MODEL_SMS) s->sms_mode = 1;
    else if (c->db) s->sms_mode = c->db->sms_mode;
    else if (c->hdr.found) s->sms_mode = c->hdr.region <= 4;
    else s->sms_mode = !c->ext_gg;
    return 0;
}

void sms_free(Sms *s)
{
    free(s->cart.rom);
    s->cart.rom = NULL;
}
