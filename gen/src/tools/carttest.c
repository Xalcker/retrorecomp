/*
 * Prueba sintética del cartucho (make test): arma una ROM en memoria con su cabecera y su
 * suma, la carga como .bin, como .smd (con y sin la marca $AA $BB) y como .bin con cabecera de
 * copiador, y comprueba que las cuatro dan la misma imagen, el mismo SHA1 y la misma cabecera.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void wr32(u8 *p, u32 v) { wr16(p, (u16)(v >> 16)); wr16(p + 2, (u16)v); }
static void text(u8 *p, const char *s, size_t n) { memset(p, ' ', n); memcpy(p, s, strlen(s)); }

#define SIZE 0x40000    /* 256 KB */

static u8 *make_rom(void)
{
    u8 *r = malloc(SIZE);
    unsigned x = 12345;
    for (size_t i = 0; i < SIZE; i++) { x = x * 1103515245u + 12345u; r[i] = (u8)(x >> 16); }
    for (int n = 0; n < VEC_COUNT; n++) wr32(r + n * 4, 0x000200 + (u32)n * 2);
    wr32(r + 0, 0x00FFFE00);                    /* SSP */
    wr32(r + 4, 0x00000206);                    /* reset */
    text(r + 0x100, "SEGA MEGA DRIVE", 16);
    text(r + 0x110, "(C)TEST 2026.OCT", 16);
    text(r + 0x120, "PRUEBA", 48);
    text(r + 0x150, "TEST ROM", 48);
    text(r + 0x180, "GM 00000000-00", 14);
    text(r + 0x190, "J6", 16);
    wr32(r + 0x1A0, 0); wr32(r + 0x1A4, SIZE - 1);
    wr32(r + 0x1A8, 0xFF0000); wr32(r + 0x1AC, 0xFFFFFF);
    r[0x1B0] = 'R'; r[0x1B1] = 'A'; r[0x1B2] = 0xF8; r[0x1B3] = 0x20;
    wr32(r + 0x1B4, 0x200001); wr32(r + 0x1B8, 0x203FFF);
    text(r + 0x1F0, "JUE", 16);
    wr16(r + 0x18E, cart_checksum(r, SIZE));
    return r;
}

/* .smd: 512 bytes de cabecera y cada bloque de 16 KB con los impares primero y los pares después */
static u8 *make_smd(const u8 *rom, int magic, size_t *len)
{
    *len = 0x200 + SIZE;
    u8 *s = calloc(1, *len);
    s[0] = SIZE / 0x4000; s[1] = 3;
    if (magic) { s[8] = 0xAA; s[9] = 0xBB; }
    for (size_t b = 0; b < SIZE / 0x4000; b++)
        for (size_t i = 0; i < 0x2000; i++) {
            s[0x200 + b * 0x4000 + i] = rom[b * 0x4000 + i * 2 + 1];
            s[0x200 + b * 0x4000 + 0x2000 + i] = rom[b * 0x4000 + i * 2];
        }
    return s;
}

static void same(const char *what, const Gen *a, const Gen *b, int format)
{
    CHECK(b->cart.rom_size == a->cart.rom_size, "%s: tamaño %zu", what, b->cart.rom_size);
    CHECK(b->cart.rom_size == a->cart.rom_size && !memcmp(a->cart.rom, b->cart.rom, a->cart.rom_size), "%s: imagen distinta", what);
    CHECK(!strcmp(a->cart.sha1, b->cart.sha1), "%s: SHA1 distinto", what);
    CHECK(b->cart.format == format, "%s: formato %d", what, b->cart.format);
    CHECK(!memcmp(&a->cart.hdr, &b->cart.hdr, sizeof a->cart.hdr), "%s: cabecera distinta", what);
}

int main(void)
{
    console_utf8();
    static Gen bin, smd, smd2, hdr, odd, bad;
    const char *err = NULL;
    u8 *rom = make_rom();

    CHECK(gen_load(&bin, rom, SIZE, &err) == 0, ".bin: %s", err);
    const GenHeader *h = &bin.cart.hdr;
    CHECK(h->sega, "no encontró SEGA");
    CHECK(!strcmp(h->system, "SEGA MEGA DRIVE"), "sistema '%s'", h->system);
    CHECK(!strcmp(h->name_int, "TEST ROM"), "nombre '%s'", h->name_int);
    CHECK(!strcmp(h->serial, "GM 00000000-00"), "serie '%s'", h->serial);
    CHECK(!strcmp(h->region, "JUE"), "región '%s'", h->region);
    CHECK(h->checksum == h->checksum_calc, "suma $%04X frente a $%04X", h->checksum, h->checksum_calc);
    CHECK(bin.cart.sram && bin.cart.sram_start == 0x200001 && bin.cart.sram_end == 0x203FFF && bin.cart.sram_type == 0xF8, "SRAM");
    CHECK(cart_vector(&bin.cart, VEC_SSP) == 0xFFFE00, "SSP");
    CHECK(cart_vector(&bin.cart, VEC_RESET) == 0x206, "reset");
    CHECK(cart_vector(&bin.cart, VEC_AUTO1 + 5) == 0x200 + 30u * 2, "VINT");
    CHECK(!strcmp(cart_vector_name(VEC_AUTO1 + 5), "VINT (nivel 6)"), "nombre de VINT");

    size_t n;
    u8 *s = make_smd(rom, 1, &n);
    CHECK(gen_load(&smd, s, n, &err) == 0, ".smd: %s", err);
    same(".smd", &bin, &smd, ROMFMT_SMD);
    free(s);

    s = make_smd(rom, 0, &n);       /* sin $AA $BB: se reconoce por "SEGA" tras deshacer el entrelazado */
    CHECK(gen_load(&smd2, s, n, &err) == 0, ".smd sin marca: %s", err);
    same(".smd sin marca", &bin, &smd2, ROMFMT_SMD);
    free(s);

    s = calloc(1, 0x200 + SIZE);    /* .bin con cabecera de copiador */
    memcpy(s + 0x200, rom, SIZE);
    CHECK(gen_load(&hdr, s, 0x200 + SIZE, &err) == 0, ".bin con cabecera: %s", err);
    same(".bin con cabecera", &bin, &hdr, ROMFMT_BIN);
    free(s);

    CHECK(gen_load(&odd, rom, SIZE - 1, &err) == 0 && odd.cart.rom_size == SIZE, "tamaño impar: se rellena a par");
    CHECK(gen_load(&bad, rom, 0x100, &err) != 0, "un archivo de 256 bytes debe rechazarse");

    gen_free(&bin); gen_free(&smd); gen_free(&smd2); gen_free(&hdr); gen_free(&odd);
    free(rom);
    if (!fallas) printf("carttest: todo bien\n");
    return fallas != 0;
}
