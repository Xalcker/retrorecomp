/*
 * La consola: carga, encendido, reset, un cuadro y la tarjeta de memoria.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "ram_code.h"
#include "trace_line.h"

PROF_DEFINE;            /* la zona que muestrea --perfil (prof.h) */

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
    g->bios.code = -2;
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

/*
 * Bucles de espera del 68000 en el código generado: tst.b $2785(a5) / beq.s *-4 hasta que una
 * interrupción cambie la RAM. Si el bucle es puro, cada vuelta deja el 68000 igual salvo el tiempo: se
 * saltan de una vez las vueltas enteras que terminan antes del próximo evento del LSPC (la línea que
 * se dibuja, el VBLANK, el temporizador), como el HALT del Z80. Puro: solo TST, CMP, CMPI, BTST y MOVE a
 * un registro de datos, con operandos Dn, inmediatos, (An), d16(An), absolutos o d16(PC); cada Dn que
 * se lee se escribió antes en la misma vuelta o el bucle no lo toca; la condición del Bcc sale de la
 * última instrucción que puso las banderas (BTST solo pone Z: BEQ/BNE). En cada llegada se miran las
 * direcciones con los registros de ese momento (RAM o ROM, alineadas: sin efectos); lo que dura una
 * vuelta lo mide el código generado (IDLE68, recomp.h) entre dos llegadas seguidas.
 */
static int idle_ea(Neo *g, M68Loop *lp, u32 *pc, int mode, int reg, int size, u32 *dreads)
{
    switch (mode) {
    case 0: *dreads |= 1u << reg; return 1;
    case 2: case 5: {
        if (lp->nea == 6) return 0;
        u32 d = mode == 5 ? (u32)(int32_t)(int16_t)m68k_peek16(g, *pc) : 0;
        if (mode == 5) *pc += 2;
        lp->ea[lp->nea].mode = (u8)mode; lp->ea[lp->nea].reg = (u8)reg; lp->ea[lp->nea].size = (u8)size;
        lp->ea[lp->nea++].at = d;
        return 1; }
    case 7: {
        u32 a;
        if (reg == 4) { *pc += size == 2 ? 4 : 2; return 1; }                  /* inmediato */
        if (reg == 0) { a = (u32)(int32_t)(int16_t)m68k_peek16(g, *pc); *pc += 2; }
        else if (reg == 1) { a = (u32)m68k_peek16(g, *pc) << 16 | m68k_peek16(g, *pc + 2); *pc += 4; }
        else if (reg == 2) { a = *pc + (u32)(int32_t)(int16_t)m68k_peek16(g, *pc); *pc += 2; }
        else return 0;
        if (lp->nea == 6) return 0;
        lp->ea[lp->nea].mode = 7; lp->ea[lp->nea].reg = 0; lp->ea[lp->nea].size = (u8)size;
        lp->ea[lp->nea++].at = a & 0xFFFFFF;
        return 1; }
    default: return 0;
    }
}

static int idle_pure(Neo *g, M68Loop *lp, u32 head, u32 br)
{
    u32 pc = head, rd[6], wr[6];
    int n = 0, flags = 0;                       /* 0: nada puso las banderas; 1: todas; 2: solo Z (BTST) */
    lp->nea = 0;
    while (pc < br) {
        if (n == 6) return 0;
        u16 op = m68k_peek16(g, pc);
        int mode = op >> 3 & 7, reg = op & 7;
        u32 r = 0, w = 0;
        pc += 2;
        if ((op & 0xFF00) == 0x4A00 && (op & 0xC0) != 0xC0) {                  /* TST */
            if (!idle_ea(g, lp, &pc, mode, reg, op >> 6 & 3, &r)) return 0;
            flags = 1;
        } else if ((op & 0xFF00) == 0x0C00 && (op & 0xC0) != 0xC0) {           /* CMPI */
            pc += (op >> 6 & 3) == 2 ? 4 : 2;
            if (!idle_ea(g, lp, &pc, mode, reg, op >> 6 & 3, &r)) return 0;
            flags = 1;
        } else if ((op & 0xFFC0) == 0x0800 || ((op & 0xF1C0) == 0x0100 && mode != 1)) {   /* BTST #n / Dn */
            if (op & 0x0100) r |= 1u << (op >> 9 & 7); else pc += 2;
            if (!idle_ea(g, lp, &pc, mode, reg, mode ? 0 : 2, &r)) return 0;
            flags = 2;
        } else if ((op & 0xF000) == 0xB000 && (op >> 6 & 7) <= 2) {           /* CMP <ea>,Dn */
            r |= 1u << (op >> 9 & 7);
            if (mode == 1 || !idle_ea(g, lp, &pc, mode, reg, op >> 6 & 3, &r)) return 0;
            flags = 1;
        } else if ((op >> 12) >= 1 && (op >> 12) <= 3 && (op >> 6 & 7) == 0) {   /* MOVE <ea>,Dn */
            int sz = (op >> 12) == 1 ? 0 : (op >> 12) == 3 ? 1 : 2;
            if (mode == 1 || !idle_ea(g, lp, &pc, mode, reg, sz, &r)) return 0;
            w = 1u << (op >> 9 & 7);
            flags = 1;
        } else return 0;
        rd[n] = r; wr[n] = w; n++;
    }
    if (pc != br) return 0;
    u16 op = m68k_peek16(g, br);
    int cc = op >> 8 & 15;
    u32 d8 = op & 0xFF, to = br + 2 + (d8 ? (u32)(int32_t)(int8_t)d8 : (u32)(int32_t)(int16_t)m68k_peek16(g, br + 2));
    if ((op & 0xF000) != 0x6000 || cc == 1 || (to & 0xFFFFFF) != head) return 0;
    if (cc >= 2 && (flags == 0 || (flags == 2 && cc != 6 && cc != 7))) return 0;
    u32 all = 0, done = 0;
    for (int i = 0; i < n; i++) all |= wr[i];
    for (int i = 0; i < n; i++) {
        if (rd[i] & all & ~done) return 0;      /* lee lo que dejó la vuelta anterior */
        done |= wr[i];
    }
    return 1;
}

void m68k_idle_skip(Neo *g, u32 head, u32 br, u64 at)
{
    M68Loop *lp = &g->m68_loop;
    M68k *c = &g->cpu;
    if (head != lp->head || br != lp->br || g->map_ver != lp->map_ver) {
        lp->head = head; lp->br = br; lp->map_ver = g->map_ver;
        lp->ok = (u8)idle_pure(g, lp, head, br);
    }
    if (!lp->ok || (c->ipl > c->ipm) | c->nmi_edge | c->t | c->stopped) return;
    for (int i = 0; i < lp->nea; i++) {
        u32 a = lp->ea[i].mode == 7 ? lp->ea[i].at : (c->a[lp->ea[i].reg] + lp->ea[i].at) & 0xFFFFFF;
        if (lp->ea[i].size && (a & 1)) return;                                  /* error de dirección */
        if (!(a < 0x200000 || (a >= 0xC00000 && a < 0xD00000))) return;         /* solo el programa, la RAM y la BIOS */
    }
    u64 len = g->cyc - at, e = g->lspc.next_event;
    if (len && e > g->cyc + len) g->cyc += (e - 1 - g->cyc) / len * len;
}

static int frame_after(Neo *g)
{
    neo_after_step_inline(g);
    return g->lspc.frame_done;
}

int neo_force_interp;

#ifdef NEO_RECOMP
#include "recomp.h"
/* Código recompilado donde lo hay; el intérprete en el resto (RAM, lo no descubierto) */
static int native(Neo *g)
{
    if (neo_force_interp) return 0;
    RecompFn fn = recomp_lookup(g, g->cpu.pc);
    if (!fn) return 0;
    u64 c0 = g->cyc;
    int r = fn(g);
    if (r) { g->cyc_recomp += g->cyc - c0; g->recomp_calls++; }
    return r;
}

void neo_run_frame(Neo *g)
{
    g->lspc.frame_done = 0;
    m68k_run_hybrid(g, frame_after, native);
    audio_end_frame(g);
}
#else
void neo_run_frame(Neo *g)
{
    g->lspc.frame_done = 0;
    m68k_run(g, frame_after);
    audio_end_frame(g);
}
#endif

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

/* Código ejecutado desde RAM, por máquina (difftest tiene dos) */
static struct { const Neo *g; RcTable t; } ram_tabs[4];

static RcTable *ram_table(const Neo *g, int create)
{
    for (unsigned i = 0; i < sizeof ram_tabs / sizeof *ram_tabs; i++)
        if (ram_tabs[i].g == g) return &ram_tabs[i].t;
    if (!create) return NULL;
    for (unsigned i = 0; i < sizeof ram_tabs / sizeof *ram_tabs; i++)
        if (!ram_tabs[i].g) {
            if (!rc_init(&ram_tabs[i].t, 0x100000, 0x10000)) return NULL;
            ram_tabs[i].g = g;
            return &ram_tabs[i].t;
        }
    return NULL;
}

/* Lo que identifica una instrucción del 68000 es su opcode: la longitud depende solo de él, y las
 * palabras de extensión (inmediatos, direcciones, desplazamientos) las lee el código recompilado de
 * RAM al ejecutar, como el intérprete */
void neo_ram_note(Neo *g, u32 pc)
{
    RcTable *t = ram_table(g, 0);
    u32 a = pc & 0xFFFF;
    if (!t || (a & 1) || (pc >> 16) != 0x0010) return;
    unsigned len = 2 + 2 * (unsigned)m68k_ext_words((u16)(g->ram[a] << 8 | g->ram[a + 1]));
    if (a + len <= sizeof g->ram) rc_observe(t, pc, g->ram + a, len, 2);
}

void neo_ram_summary(const Neo *g, unsigned *fixed, unsigned *vol, unsigned *multi, unsigned *lost)
{
    RcTable *t = ram_table(g, 0);
    RcSummary s = { 0, 0, 0, 0 };
    if (t) s = rc_summary(t);
    *fixed = s.fixed; *vol = s.vol; *multi = s.multi; *lost = s.lost;
}

void neo_trace_start(Neo *g)
{
    if (!g->trace) g->trace = calloc(neo_trace_words(g), sizeof *g->trace);
    if (ram_table(g, 1)) m68k_ram_trace = neo_ram_note;
}

int neo_trace_load(Neo *g, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    neo_trace_start(g);
    if (!g->trace) { fclose(f); return 0; }
    unsigned long off, addr;
    char line[128];
    RcTable *t = ram_table(g, 0);
    RcIns in;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr)) {
            if (t && rc_parse(line, &in) && in.len >= 2) { in.key = 2; rc_add(t, &in); }
            continue;
        }
        if (off & 1) continue;
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
    RcTable *t = ram_table(g, 0);
    if (t) rc_write(t, f, 6);
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
