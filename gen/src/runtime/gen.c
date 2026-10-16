/*
 * La consola: carga, reset, un cuadro y la RAM con batería del cartucho.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "ram_code.h"
#include "trace_line.h"
#include "sha1.h"

PROF_DEFINE;            /* la zona que muestrea --perfil (prof.h) */

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
    free(g->trace);
    g->cart.rom = g->cart.sram_data = g->cart.sram_saved = NULL;
    g->trace = NULL;
}

void gen_reset(Gen *g)
{
    g->cyc = 0;
    g->refresh_at = REFRESH_EVERY;
    memset(g->ram, 0, sizeof g->ram);
    memset(g->zram, 0, sizeof g->zram);
    memset(g->exc_count, 0, sizeof g->exc_count);
    g->z80_busreq = 0;
    g->z80_reset = 1;
    g->z80_bank = 0;
    z80_reset(&g->z80);
    g->z80_mclk = g->z80_due = 0;
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
    gen_after_step_inline(g);
}

static int frame_after(Gen *g)
{
    gen_after_step_inline(g);
    return g->vdp.frame_done;
}

int gen_force_interp;

#ifdef GEN_RECOMP
#include "recomp.h"
/* Código recompilado donde lo hay; el intérprete en el resto (RAM, lo no descubierto) */
static int native(Gen *g)
{
    if (gen_force_interp) return 0;
    RecompFn fn = recomp_lookup(g, g->cpu.pc);
    if (!fn) return 0;
    u64 c0 = g->cyc;
    int r = fn(g);
    if (r) { g->cyc_recomp += g->cyc - c0; g->recomp_calls++; }
    return r;
}

void gen_run_frame(Gen *g)
{
    g->vdp.frame_done = 0;
    m68k_run_hybrid(g, frame_after, native);
    audio_end_frame(g);
}
#else
void gen_run_frame(Gen *g)
{
    g->vdp.frame_done = 0;
    m68k_run(g, frame_after);
    audio_end_frame(g);
}
#endif

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

/* --- Traza --- */

/* Código ejecutado desde RAM, por máquina (difftest tiene dos): t[0] con el PC en $00FFxxxx y t[1] en
 * $FFFFxxxx (gen.h) */
static struct { const Gen *g; RcTable t[2]; } ram_tabs[4];

static RcTable *ram_table(const Gen *g, int create)
{
    for (unsigned i = 0; i < sizeof ram_tabs / sizeof *ram_tabs; i++)
        if (ram_tabs[i].g == g) return ram_tabs[i].t;
    if (!create) return NULL;
    for (unsigned i = 0; i < sizeof ram_tabs / sizeof *ram_tabs; i++)
        if (!ram_tabs[i].g) {
            if (!rc_init(&ram_tabs[i].t[0], 0x00FF0000, 0x10000) || !rc_init(&ram_tabs[i].t[1], 0xFFFF0000, 0x10000))
                return NULL;
            ram_tabs[i].g = g;
            return ram_tabs[i].t;
        }
    return NULL;
}

/* Lo que identifica una instrucción del 68000 es su opcode: la longitud depende solo de él, y las
 * palabras de extensión (inmediatos, direcciones, desplazamientos) las lee el código recompilado de
 * RAM al ejecutar, como el intérprete */
void gen_ram_note(Gen *g, u32 pc)
{
    RcTable *t = ram_table(g, 0);
    u32 a = pc & 0xFFFF;
    if (!t || (a & 1) || ((pc >> 24) != 0x00 && (pc >> 24) != 0xFF)) return;
    unsigned len = 2 + 2 * (unsigned)m68k_ext_words((u16)(g->ram[a] << 8 | g->ram[a + 1]));
    if (a + len <= sizeof g->ram) rc_observe(&t[pc >> 24 ? 1 : 0], pc, g->ram + a, len, 2);
}

void gen_ram_summary(const Gen *g, unsigned *fixed, unsigned *vol, unsigned *multi, unsigned *lost)
{
    RcTable *t = ram_table(g, 0);
    *fixed = *vol = *multi = *lost = 0;
    for (int k = 0; t && k < 2; k++) {
        RcSummary s = rc_summary(&t[k]);
        *fixed += s.fixed; *vol += s.vol; *multi += s.multi; *lost += s.lost;
    }
}

void gen_trace_start(Gen *g)
{
    if (!g->trace) g->trace = calloc(g->cart.rom_size / 2 + 1, sizeof *g->trace);
    if (ram_table(g, 1)) m68k_ram_trace = gen_ram_note;
}

int gen_trace_load(Gen *g, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    gen_trace_start(g);
    unsigned long off, addr;
    char line[128];
    RcTable *t = ram_table(g, 0);
    RcIns in;
    while (fgets(line, sizeof line, f))
        if (trace_line_read(line, GEN_TRACE_SHIFT, &off, &addr)) {
            if (off < g->cart.rom_size && !(off & 1) && !g->trace[off >> 1])
                g->trace[off >> 1] = 0x80000000u | (addr & 0xFFFFFF);
        } else if (t && rc_parse(line, &in) && in.len >= 2) {
            in.key = 2;
            rc_add(&t[in.addr >> 24 ? 1 : 0], &in);
        }
    fclose(f);
    return 1;
}

int gen_trace_write(const Gen *g, const char *path)
{
    if (!g->trace) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (size_t i = 0; i < g->cart.rom_size / 2; i++)
        if (g->trace[i]) trace_line_write(f, GEN_TRACE_SHIFT, 6, i * 2, g->trace[i] & 0xFFFFFF);
    RcTable *t = ram_table(g, 0);
    for (int k = 0; t && k < 2; k++) rc_write(&t[k], f, 8);
    return fclose(f) == 0;
}

size_t gen_trace_count(const Gen *g)
{
    size_t n = 0;
    if (g->trace) for (size_t i = 0; i < g->cart.rom_size / 2; i++) n += g->trace[i] != 0;
    return n;
}

/* --- Estados guardados ---
 * Cabecera (magia, versión, tamaño de Gen, tamaño y SHA1 de la ROM, tamaño de la SRAM), la estructura
 * Gen tal cual y el contenido de la SRAM/EEPROM. Al cargar se conservan los punteros de esta corrida. */

#define STATE_MAGIC "GENRSTA1"

typedef struct {
    char magic[8];
    u32  gen_size;
    u32  rom_size, sram_size;
    char sha1[41];
} StateHeader;

u8 *gen_state_save(const Gen *g, size_t *size)
{
    StateHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, STATE_MAGIC, 8);
    h.gen_size = (u32)sizeof *g;
    h.rom_size = (u32)g->cart.rom_size;
    h.sram_size = (u32)g->cart.sram_size;
    memcpy(h.sha1, g->cart.sha1, sizeof h.sha1);
    *size = sizeof h + sizeof *g + g->cart.sram_size;
    u8 *d = malloc(*size);
    if (!d) return NULL;
    memcpy(d, &h, sizeof h);
    memcpy(d + sizeof h, g, sizeof *g);
    if (g->cart.sram_size) memcpy(d + sizeof h + sizeof *g, g->cart.sram_data, g->cart.sram_size);
    return d;
}

int gen_state_load(Gen *g, const u8 *data, size_t size)
{
    StateHeader h;
    if (size < sizeof h) return 0;
    memcpy(&h, data, sizeof h);
    if (memcmp(h.magic, STATE_MAGIC, 8) || h.gen_size != sizeof *g || h.rom_size != g->cart.rom_size ||
        h.sram_size != g->cart.sram_size || memcmp(h.sha1, g->cart.sha1, sizeof h.sha1) ||
        size != sizeof h + sizeof *g + h.sram_size) return 0;
    /* Lo que es de esta corrida y no del estado: punteros */
    u8 *rom = g->cart.rom, *sram = g->cart.sram_data, *saved = g->cart.sram_saved;
    u32 *trace = g->trace;
    void *jb = g->jb;
    memcpy(g, data + sizeof h, sizeof *g);
    g->cart.rom = rom; g->cart.sram_data = sram; g->cart.sram_saved = saved;
    g->trace = trace; g->jb = jb;
    if (h.sram_size) memcpy(sram, data + sizeof h + sizeof *g, h.sram_size);
    return 1;
}
