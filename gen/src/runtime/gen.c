/*
 * La consola: carga, reset, un cuadro y la RAM con batería del cartucho.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "sha1.h"

/* Región para el registro de versión: se prefiere EE. UU. (NTSC, en inglés); si el cartucho solo
 * dice Japón, Japón; si solo dice Europa, PAL. Acepta "JUE" y el formato de un dígito hexadecimal. */
static u8 region_bits(const GenHeader *h)
{
    int j, u, e;
    const char *r = h->region;
    if (strlen(r) == 1 && isxdigit((unsigned char)r[0])) {
        int v = (int)strtol(r, NULL, 16);
        j = v & 1; u = v & 4; e = v & 8;
    } else {
        j = strchr(r, 'J') != NULL; u = strchr(r, 'U') != NULL; e = strchr(r, 'E') != NULL;
    }
    if (u || (!j && !e)) return 0x80;
    if (j) return 0x00;
    return 0xC0;
}

int gen_load(Gen *g, const u8 *data, size_t len, const char **err)
{
    memset(g, 0, sizeof *g);
    Cart *c = &g->cart;
    c->rom = cart_normalize(data, len, &c->rom_size, &c->format, err);
    if (!c->rom) return -1;
    sha1_hex(c->rom, c->rom_size, c->sha1);
    cart_parse_header(c);
    cart_setup(c);
    g->version = (u8)(region_bits(&c->hdr) | 0x20);
    g->pad6 = strchr(c->hdr.io, '6') ? 3 : 0;      /* el juego dice que acepta el de 6 botones */
    gen_reset(g);
    return 0;
}

void gen_free(Gen *g)
{
    free(g->cart.rom);
    free(g->cart.sram_data);
    free(g->cart.sram_saved);
    g->cart.rom = g->cart.sram_data = g->cart.sram_saved = NULL;
}

void gen_reset(Gen *g)
{
    g->cyc = 0;
    memset(g->ram, 0, sizeof g->ram);
    memset(g->zram, 0, sizeof g->zram);
    memset(g->exc_count, 0, sizeof g->exc_count);
    g->z80_busreq = 0;
    g->z80_reset = 1;
    g->z80_bank = 0;
    z80_reset(&g->z80);
    g->z80_mclk = 0;
    g->z80_irq_from = g->z80_irq_until = 0;
    psg_reset(&g->psg);
    ym_reset(&g->ym);
    audio_reset(g);
    g->open_bus = 0;
    g->tmss_reg = 0;
    g->tmss_locked = 0;
    g->version = (u8)((g->version & 0xF0) | (g->tmss ? 1 : 0));
    cart_map_reset(&g->cart);
    io_reset(g);
    eeprom_reset(g);
    vdp_reset(g);
    m68k_reset(g);
}

/* Después de cada instrucción del 68000: el VDP y el Z80 al mismo ciclo */
void gen_after_step(Gen *g)
{
    vdp_sync(g);
    z80_sync(g);
}

static int frame_after(Gen *g)
{
    gen_after_step(g);
    return g->vdp.frame_done;
}

void gen_run_frame(Gen *g)
{
    g->vdp.frame_done = 0;
    m68k_run(g, frame_after);
    audio_end_frame(g);
}

int gen_sram_load(Gen *g, const char *path)
{
    Cart *c = &g->cart;
    if (!c->sram_data) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(c->sram_data, 1, c->sram_size, f);
    fclose(f);
    if (n < c->sram_size) memset(c->sram_data + n, 0xFF, c->sram_size - n);
    memcpy(c->sram_saved, c->sram_data, c->sram_size);
    return 1;
}

int gen_sram_save(Gen *g, const char *path)
{
    Cart *c = &g->cart;
    if (!c->sram_data) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(c->sram_data, 1, c->sram_size, f) == c->sram_size;
    ok &= fclose(f) == 0;
    if (ok) memcpy(c->sram_saved, c->sram_data, c->sram_size);
    return ok;
}

int gen_sram_dirty(const Gen *g)
{
    const Cart *c = &g->cart;
    return c->sram_data && memcmp(c->sram_data, c->sram_saved, c->sram_size) != 0;
}
