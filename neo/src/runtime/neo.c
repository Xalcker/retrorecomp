/*
 * La consola: carga, encendido, reset, un cuadro y la tarjeta de memoria.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "trace_line.h"

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
    audio_reset(g);
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
    audio_end_frame(g);
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

/* --- Traza --- */

void neo_trace_start(Neo *g)
{
    if (!g->trace) g->trace = calloc(neo_trace_words(g), sizeof *g->trace);
}

int neo_trace_load(Neo *g, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    neo_trace_start(g);
    if (!g->trace) { fclose(f); return 0; }
    unsigned long off, addr;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr) || (off & 1)) continue;
        int bios = off >= BIOS_SPACE;
        if (bios ? off - BIOS_SPACE >= BIOS_SIZE : off >= g->cart.len[REG_P]) continue;
        size_t i = neo_trace_index(g, (long)off);
        if (!g->trace[i]) g->trace[i] = 0x80000000u | (u32)(addr & 0xFFFFFF);
    }
    fclose(f);
    return 1;
}

int neo_trace_write(const Neo *g, const char *path)
{
    if (!g->trace) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    size_t pw = g->cart.len[REG_P] / 2, n = neo_trace_words(g);
    for (size_t i = 0; i < n; i++)
        if (g->trace[i]) {
            unsigned long off = i < pw ? i * 2 : BIOS_SPACE + (i - pw) * 2;
            trace_line_write(f, TRACE_SHIFT, 6, off, g->trace[i] & 0xFFFFFF);
        }
    return fclose(f) == 0;
}

size_t neo_trace_count(const Neo *g)
{
    size_t n = 0;
    if (g->trace) for (size_t i = 0; i < neo_trace_words(g); i++) n += g->trace[i] != 0;
    return n;
}

/* --- Estados guardados ---
 * Cabecera (magia, versión, tamaño de Neo, SHA1 del juego, tamaño de la tarjeta), la estructura Neo
 * sin la BIOS ni la tabla L0, y el contenido de la tarjeta. Al cargar se conservan los punteros, la
 * BIOS y la tabla L0 de esta corrida. */

#define STATE_MAGIC "NEORSTA1"

typedef struct {
    char magic[8];
    u32  neo_size;
    u32  card_size;
    char sha1[41];
} StateHeader;

u8 *neo_state_save(const Neo *g, size_t *size)
{
    StateHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, STATE_MAGIC, 8);
    h.neo_size = (u32)sizeof *g;
    h.card_size = g->card.data ? g->card.size : 0;
    memcpy(h.sha1, g->cart.sha1, sizeof h.sha1);
    *size = sizeof h + sizeof *g + h.card_size;
    u8 *d = malloc(*size);
    if (!d) return NULL;
    memcpy(d, &h, sizeof h);
    Neo *copy = (Neo *)(d + sizeof h);
    memcpy(copy, g, sizeof *g);
    memset(copy->bios.rom, 0, sizeof copy->bios.rom);     /* la BIOS es de SNK: no va en el estado */
    memset(copy->l0, 0, sizeof copy->l0);
    if (h.card_size) memcpy(d + sizeof h + sizeof *g, g->card.data, h.card_size);
    return d;
}

int neo_state_load(Neo *g, const u8 *data, size_t size)
{
    StateHeader h;
    if (size < sizeof h) return 0;
    memcpy(&h, data, sizeof h);
    if (memcmp(h.magic, STATE_MAGIC, 8) || h.neo_size != sizeof *g || memcmp(h.sha1, g->cart.sha1, sizeof h.sha1) ||
        size != sizeof h + sizeof *g + h.card_size) return 0;
    /* Lo que es de esta corrida y no del estado: el cartucho, la BIOS, la tabla L0, los punteros */
    static Cart cart;
    static Bios bios;
    static u8 l0[L0_SIZE];
    cart = g->cart;
    bios = g->bios;
    memcpy(l0, g->l0, sizeof l0);
    Card card = g->card;
    u64 *spr = g->spr; u32 *fix = g->fix;
    size_t spr_tiles = g->spr_tiles, fix_tiles = g->fix_tiles;
    u32 *trace = g->trace;
    void *jb = g->jb;
    u8 mute = g->audio.mute;
    memcpy(g, data + sizeof h, sizeof *g);
    g->cart = cart; g->bios = bios;
    memcpy(g->l0, l0, sizeof l0);
    g->spr = spr; g->fix = fix; g->spr_tiles = spr_tiles; g->fix_tiles = fix_tiles;
    g->trace = trace; g->jb = jb;
    g->audio.mute = mute;
    /* La tarjeta: si había una en el estado y hay una puesta, toma su contenido */
    g->card = card;
    if (h.card_size && card.data && card.size == h.card_size) {
        memcpy(card.data, data + sizeof h + sizeof *g, h.card_size);
        g->card.dirty = 1;
    }
    return 1;
}
