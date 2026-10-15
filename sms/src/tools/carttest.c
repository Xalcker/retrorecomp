/*
 * Prueba sintética del cartucho (make test): arma ROMs en memoria con sus cabeceras y comprueba lo que
 * lee cart.c.
 *   - "TMR SEGA" en $7FF0, $3FF0 y $1FF0, con el código de producto en BCD, la versión, la región, el
 *     tamaño y la suma de la BIOS (calculada aquí por separado, con los rangos de cada código de tamaño)
 *   - la misma ROM con la cabecera de copiador de 512 bytes da la misma imagen, SHA1 y cabecera
 *   - el mapper: sin mapper hasta 48 KB, Sega desde 64 KB, Codemasters por su cabecera (con su suma de
 *     palabras); una SDSC en $7FE0 no se confunde con la de Codemasters
 *   - el modelo y el modo: por la extensión, por la región de la cabecera y con --model
 *   - los archivos que no pueden ser una ROM, y el CRC32
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static void wr16le(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static u8 *random_rom(size_t size, unsigned seed)
{
    u8 *r = malloc(size);
    unsigned x = seed;
    for (size_t i = 0; i < size; i++) { x = x * 1103515245u + 12345u; r[i] = (u8)(x >> 16); }
    return r;
}

/* La suma de la BIOS, escrita aparte de cart.c: bytes de [0, lo) y de [$8000, hi) en 16 bits */
static u16 sum_bytes(const u8 *r, u32 lo, u32 hi)
{
    u16 s = 0;
    for (u32 a = 0; a < lo; a++) s = (u16)(s + r[a]);
    for (u32 a = 0x8000; a < hi; a++) s = (u16)(s + r[a]);
    return s;
}

/* "TMR SEGA" en at, con producto 27026 (26 70 2), versión 1, región y código de tamaño; la suma, si se pide */
static void tmr_sega(u8 *r, u32 at, int region, int size_code, int with_sum, u32 lo, u32 hi)
{
    memcpy(r + at, "TMR SEGA", 8);
    r[at + 8] = r[at + 9] = 0xFF;
    r[at + 0xC] = 0x26; r[at + 0xD] = 0x70; r[at + 0xE] = 0x21;
    r[at + 0xF] = (u8)(region << 4 | size_code);
    wr16le(r + at + 0xA, with_sum ? sum_bytes(r, lo, hi) : 0x1234);
}

static int load(Sms *s, const u8 *data, size_t len, const char *name, int model)
{
    const char *err = NULL;
    if (sms_load(s, data, len, name, model, &err)) { printf("FALLA: %s no carga (%s)\n", name, err); fallas++; return 0; }
    return 1;
}

static void headers(void)
{
    /* 256 KB, cabecera en $7FF0, región 4, tamaño $0: la suma cubre $0000-$7FEF y $8000-$3FFFF */
    size_t size = 0x40000;
    u8 *r = random_rom(size, 1);
    memset(r + 0x7FE0, 0, 16);                              /* sin SDSC ni Codemasters */
    tmr_sega(r, 0x7FF0, 4, 0x0, 1, 0x7FF0, 0x40000);
    static Sms a, b;
    if (load(&a, r, size, "juego.sms", MODEL_AUTO)) {
        const SmsHeader *h = &a.cart.hdr;
        CHECK(h->found && h->at == 0x7FF0, "no encontró TMR SEGA en $7FF0");
        CHECK(!strcmp(h->product, "27026") && h->version == 1, "producto %s versión %u (esperaba 27026 y 1)", h->product, h->version);
        CHECK(h->region == 4 && h->size_code == 0, "región %u tamaño $%X", h->region, h->size_code);
        CHECK(h->checksum_ok && h->checksum == h->checksum_calc, "suma $%04X calculada $%04X", h->checksum, h->checksum_calc);
        CHECK(a.cart.mapper == MAPPER_SEGA && a.cart.pages == 16, "mapper %d páginas %u", a.cart.mapper, a.cart.pages);
        CHECK(a.model == MODEL_SMS && a.sms_mode, "modelo %d modo SMS %d", a.model, a.sms_mode);
        CHECK(!a.cart.copier && !a.cart.cm.found && !a.cart.sdsc.found, "detectó copiador, Codemasters o SDSC de más");
    }
    /* La misma ROM con 512 bytes de cabecera de copiador: la misma imagen y el mismo SHA1 */
    u8 *cp = calloc(1, size + 512);
    memcpy(cp + 512, r, size);
    if (load(&b, cp, size + 512, "juego.sms", MODEL_AUTO)) {
        CHECK(b.cart.copier && b.cart.rom_size == size && !memcmp(b.cart.rom, r, size), "la cabecera de copiador no se quitó bien");
        CHECK(!strcmp(a.cart.sha1, b.cart.sha1) && a.cart.crc32 == b.cart.crc32, "SHA1/CRC32 distintos con y sin copiador");
        CHECK(b.cart.hdr.found && b.cart.hdr.checksum == a.cart.hdr.checksum, "la cabecera cambió con el copiador");
        sms_free(&b);
    }
    sms_free(&a);
    /* Suma equivocada */
    wr16le(r + 0x7FFA, (u16)(rd16le(r + 0x7FFA) + 1));
    if (load(&a, r, size, "juego.sms", MODEL_AUTO)) {
        CHECK(a.cart.hdr.checksum_ok && a.cart.hdr.checksum != a.cart.hdr.checksum_calc, "no notó la suma equivocada");
        sms_free(&a);
    }
    /* Tamaño declarado más grande que la ROM: no se puede comprobar */
    r[0x7FFF] = 0x41;                                       /* 512 KB en una de 256 */
    if (load(&a, r, size, "juego.sms", MODEL_AUTO)) {
        CHECK(!a.cart.hdr.checksum_ok, "comprobó una suma con un tamaño mayor que la ROM");
        sms_free(&a);
    }
    free(r); free(cp);

    /* $3FF0 en 16 KB (código $B: $0000-$3FEF) y $1FF0 en 8 KB (código $A: $0000-$1FEF), sin mapper */
    static const struct { size_t size; u32 at; int code; } small[] = { { 0x4000, 0x3FF0, 0xB }, { 0x2000, 0x1FF0, 0xA } };
    for (int i = 0; i < 2; i++) {
        r = random_rom(small[i].size, 2 + (unsigned)i);
        tmr_sega(r, small[i].at, 4, small[i].code, 1, small[i].at, 0);
        if (load(&a, r, small[i].size, "chica.sms", MODEL_AUTO)) {
            CHECK(a.cart.hdr.found && a.cart.hdr.at == small[i].at, "no encontró TMR SEGA en $%04X", small[i].at);
            CHECK(a.cart.hdr.checksum_ok && a.cart.hdr.checksum == a.cart.hdr.checksum_calc, "suma en $%04X", small[i].at);
            CHECK(a.cart.mapper == MAPPER_NONE, "una ROM de %zu KB con mapper", small[i].size / 1024);
            sms_free(&a);
        }
        free(r);
    }
    /* 48 KB (código $D: la BIOS suma $0000-$BFEF, cabecera incluida): sin mapper; 64 KB sin cabecera: Sega */
    r = random_rom(0xC000, 4);
    memset(r + 0x7FE0, 0, 16);
    tmr_sega(r, 0x7FF0, 4, 0xD, 0, 0, 0);                  /* el rango incluye la cabecera: no hay suma que cuadre */
    if (load(&a, r, 0xC000, "rara.sms", MODEL_AUTO)) {
        u16 expect = sum_bytes(a.cart.rom, 0xBFF0, 0);
        CHECK(a.cart.hdr.checksum_ok && a.cart.hdr.checksum_calc == expect, "código $D: calculada $%04X esperaba $%04X",
              a.cart.hdr.checksum_calc, expect);
        CHECK(a.cart.mapper == MAPPER_NONE, "48 KB con mapper");
        sms_free(&a);
    }
    free(r);
    r = random_rom(0x10000, 5);
    memset(r + 0x7FE0, 0, 32);
    if (load(&a, r, 0x10000, "sin_cabecera.sms", MODEL_AUTO)) {
        CHECK(!a.cart.hdr.found && a.cart.mapper == MAPPER_SEGA && a.model == MODEL_SMS, "64 KB sin cabecera");
        sms_free(&a);
    }
    free(r);
}

static void codemasters(void)
{
    /* 128 KB con la cabecera de Codemasters (8 páginas, su suma de palabras sin $7FF0-$7FFF) y un TMR SEGA
     * con la suma de la BIOS equivocada, como en los juegos reales */
    size_t size = 0x20000;
    u8 *r = random_rom(size, 6);
    u8 *h = r + 0x7FE0;
    memset(h, 0, 16);
    h[0] = 8; h[1] = 0x14; h[2] = 0x10; h[3] = 0x93; h[4] = 0x12; h[5] = 0x30;
    tmr_sega(r, 0x7FF0, 4, 0xF, 0, 0, 0);
    u16 sum = 0;
    for (u32 a = 0; a < size; a += 2) if (a < 0x7FF0 || a >= 0x8000) sum = (u16)(sum + rd16le(r + a));
    /* La suma y su inversa suman $10000, así que escribirlas no cambia lo que suma la rutina */
    wr16le(h + 6, sum); wr16le(h + 8, (u16)(0x10000 - sum));
    static Sms s;
    if (load(&s, r, size, "cm.sms", MODEL_AUTO)) {
        const CmHeader *m = &s.cart.cm;
        CHECK(m->found && m->banks == 8 && m->year == 0x93, "no leyó la cabecera de Codemasters");
        CHECK(m->checksum_calc == m->checksum, "suma de Codemasters $%04X calculada $%04X", m->checksum, m->checksum_calc);
        CHECK(s.cart.mapper == MAPPER_CODEMASTERS, "mapper %d (esperaba Codemasters)", s.cart.mapper);
        sms_free(&s);
    }
    free(r);
}

static void sdsc(void)
{
    /* SDSC en $7FE0 con nombre y autor: se lee y no se toma por la de Codemasters aunque la fecha sume $10000 */
    size_t size = 0x8000;
    u8 *r = random_rom(size, 7);
    u8 *h = r + 0x7FE0;
    memcpy(h, "SDSC", 4);
    h[4] = 0x01; h[5] = 0x02; h[6] = 0x15; h[7] = 0x10;
    wr16le(h + 8, 0x2026);
    wr16le(h + 0xA, 0x7F00); wr16le(h + 0xC, 0x7F10); wr16le(h + 0xE, 0xFFFF);
    memcpy(r + 0x7F00, "Xalcker", 8);
    memcpy(r + 0x7F10, "Prueba SDSC", 12);
    tmr_sega(r, 0x7FF0, 4, 0xC, 1, 0x7FF0, 0);
    static Sms s;
    if (load(&s, r, size, "sdsc.sms", MODEL_AUTO)) {
        const SdscHeader *d = &s.cart.sdsc;
        CHECK(d->found && !strcmp(d->name, "Prueba SDSC") && !strcmp(d->author, "Xalcker"), "SDSC: '%s' de '%s'", d->name, d->author);
        CHECK(d->major == 1 && d->minor == 2 && d->year == 0x2026, "SDSC: versión o fecha");
        CHECK(!s.cart.cm.found && s.cart.mapper == MAPPER_NONE, "SDSC tomada por Codemasters");
        CHECK(s.cart.hdr.checksum_ok && s.cart.hdr.checksum == s.cart.hdr.checksum_calc, "suma con SDSC");
        sms_free(&s);
    }
    free(r);
}

static void models(void)
{
    /* Una ROM de Game Gear (región 6) y una de SMS (región 4), con varios nombres y --model */
    static const struct { int region; const char *name; int model, expect, sms_mode; } t[] = {
        { 6, "juego.gg",  MODEL_AUTO, MODEL_GG,  0 },
        { 6, "JUEGO.GG",  MODEL_AUTO, MODEL_GG,  0 },
        { 6, "juego.bin", MODEL_AUTO, MODEL_GG,  0 },     /* sin extensión conocida: la región */
        { 7, "juego.bin", MODEL_AUTO, MODEL_GG,  0 },
        { 6, "juego.sms", MODEL_AUTO, MODEL_SMS, 1 },     /* la extensión manda sobre la región */
        { 6, "juego.gg",  MODEL_SMS,  MODEL_SMS, 1 },     /* --model sms */
        { 4, "juego.bin", MODEL_AUTO, MODEL_SMS, 1 },
        { 4, "juego.gg",  MODEL_AUTO, MODEL_GG,  1 },     /* cartucho de SMS en el Game Gear: su modo SMS */
        { 3, "juego.sms", MODEL_GG,   MODEL_GG,  1 },     /* --model gg con un juego de SMS */
        { 5, "juego.sms", MODEL_GG,   MODEL_GG,  0 },
    };
    u8 *r = random_rom(0x8000, 8);
    memset(r + 0x7FE0, 0, 16);
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        tmr_sega(r, 0x7FF0, t[i].region, 0xC, 1, 0x7FF0, 0);
        static Sms s;
        if (!load(&s, r, 0x8000, t[i].name, t[i].model)) continue;
        CHECK(s.model == t[i].expect && s.sms_mode == t[i].sms_mode, "región %d, %s, --model %s: modelo %s modo SMS %d",
              t[i].region, t[i].name, t[i].model == MODEL_AUTO ? "auto" : model_name(t[i].model), model_name(s.model), s.sms_mode);
        sms_free(&s);
    }
    /* Sin cabecera: el .gg corre en modo GG y cualquier otro en modo SMS */
    memset(r + 0x7FF0, 0, 16);
    static Sms s;
    if (load(&s, r, 0x8000, "sin.gg", MODEL_AUTO)) { CHECK(s.model == MODEL_GG && !s.sms_mode, "sin cabecera .gg"); sms_free(&s); }
    if (load(&s, r, 0x8000, "sin.sms", MODEL_GG)) { CHECK(s.model == MODEL_GG && s.sms_mode, "sin cabecera .sms en el GG"); sms_free(&s); }
    free(r);
    CHECK(model_from_name("sms") == MODEL_SMS && model_from_name("gg") == MODEL_GG && model_from_name("sg") < MODEL_AUTO,
          "model_from_name");
}

static void rejects(void)
{
    static Sms s;
    const char *err = NULL;
    CHECK(sms_load(&s, (const u8 *)"no es una rom", 13, "chica.sms", MODEL_AUTO, &err) && err, "aceptó un archivo de 13 bytes");
    size_t big = ROM_LIMIT + PAGE_SIZE;
    u8 *r = calloc(1, big);
    err = NULL;
    CHECK(sms_load(&s, r, big, "grande.sms", MODEL_AUTO, &err) && err, "aceptó una ROM de más de 4 MB");
    free(r);
    CHECK(cart_crc32((const u8 *)"123456789", 9) == 0xCBF43926u, "CRC32 de \"123456789\"");
}

int main(void)
{
    console_utf8();
    headers();
    codemasters();
    sdsc();
    models();
    rejects();
    if (fallas) { printf("carttest: %d fallas\n", fallas); return 1; }
    printf("carttest: cabeceras (TMR SEGA, Codemasters, SDSC), copiador, mapper, modelo y CRC32 bien\n");
    return 0;
}
