/*
 * La consola: carga, encendido, reset, un cuadro y la tarjeta de memoria.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"

int neo_load(Neo *g, const char *rom_path, const char *bios_path, const char **err)
{
    static Cart cart;
    static Bios bios;
    static u8 l0[L0_SIZE];
    if (cart_load_path(&cart, rom_path, err)) return -1;
    l0_generate(l0);
    if (bios_path ? bios_load(&bios, bios_path, l0, err) : bios_find(&bios, rom_path, l0, err)) {
        cart_free(&cart);
        return -1;
    }
    neo_assemble(g, &cart, &bios);
    return 0;
}

void neo_assemble(Neo *g, const Cart *cart, const Bios *bios)
{
    memset(g, 0, sizeof *g);
    g->cart = *cart;
    g->bios = *bios;
    l0_generate(g->l0);
    neo_power_on(g);
}

void neo_free(Neo *g)
{
    cart_free(&g->cart);
    gfx_free(g);
    free(g->card.data);
    free(g->trace);
    g->card.data = NULL;
    g->trace = NULL;
}

/* Lo que comparten el encendido y el botón de reset */
static void reset_common(Neo *g)
{
    g->cyc = 0;
    g->p2_bank = 0;
    g->map_ver++;
    g->latch = 0;                   /* vectores de la BIOS, paleta 0, tarjeta bloqueada */
    g->poutput = g->crdbank = 0;
    g->open_bus = 0;
    memset(g->exc_count, 0, sizeof g->exc_count);
    lspc_reset(g);
    z80_reset(&g->z80);
    g->z80_mclk = 0;
    /* Ventanas del Z80 como si la M1 fuera lineal */
    g->z80_bank[0] = 0x1E; g->z80_bank[1] = 0x0E; g->z80_bank[2] = 0x06; g->z80_bank[3] = 0x02;
    g->sound_cmd = g->sound_reply = 0;
    g->nmi_pending = g->nmi_enabled = g->nmi_line = 0;
    g->sound_cmds = 0;
    ym_reset(&g->ym);
    m68k_reset(g);
}

void neo_power_on(Neo *g)
{
    memset(g->ram, 0, sizeof g->ram);
    memset(g->pal, 0, sizeof g->pal);
    pal_refresh(g);
    memset(g->fb, 0, sizeof g->fb);
    memset(g->zram, 0, sizeof g->zram);
    if (!g->spr && !g->fix) gfx_init(g);
    reset_common(g);
    /* La interrupción de nivel 3 queda pendiente al encender (no con el botón de reset) */
    g->lspc.irq |= IRQ_COLD;
    lspc_sync(g);
}

void neo_reset(Neo *g)
{
    reset_common(g);
    lspc_sync(g);
}

void neo_after_step(Neo *g)
{
    neo_after_step_inline(g);
}

static int frame_after(Neo *g)
{
    neo_after_step_inline(g);
    return g->lspc.frame_done;
}

void neo_run_frame(Neo *g)
{
    g->lspc.frame_done = 0;
    m68k_run(g, frame_after);
}

/* --- Tarjeta de memoria --- */

#define CARD_SIZE 0x800

int neo_card_insert(Neo *g, const char *path)
{
    free(g->card.data);
    g->card.data = malloc(CARD_SIZE);
    if (!g->card.data) return 0;
    g->card.size = CARD_SIZE;
    g->card.dirty = 0;
    memset(g->card.data, 0xFF, CARD_SIZE);
    if (path) {
        size_t n;
        u8 *d = neo_read_file(path, &n);
        if (d) { memcpy(g->card.data, d, n < CARD_SIZE ? n : CARD_SIZE); free(d); }
    }
    return 1;
}

int neo_card_save(Neo *g, const char *path)
{
    if (!g->card.data) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(g->card.data, 1, g->card.size, f) == g->card.size;
    ok &= fclose(f) == 0;
    if (ok) g->card.dirty = 0;
    return ok;
}
