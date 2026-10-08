/*
 * Núcleo del runtime: inicialización, bus de memoria (ruta lenta),
 * sincronización perezosa del hardware, interrupciones y el despachador que
 * alterna entre código recompilado y el intérprete.
 */
#include "cpu_ops.h"

#include <stdlib.h>
#include <string.h>

#define PENDING(g) ((g)->ie & IF_REG(g) & 0x1F)

/* ------------------------------------------------------------------ */
/* Mapa de memoria rápido                                              */
/* ------------------------------------------------------------------ */

/* Byte de WRAM para una dirección de $C000-$FDFF ($E000+ refleja $C000):
 * $C000-$CFFF es el banco 0 y $D000-$DFFF el banco elegido (1 en DMG) */
static u8 *wram_at(GB *g, u16 a)
{
    u16 off = (u16)((a - 0xC000) & 0x1FFF);
    if (off < 0x1000) return &g->wram[off];
    return &g->wram[g->wram_bank * 0x1000 + (off - 0x1000)];
}

static u8 *vram_at(GB *g, u16 a)
{
    return &g->vram[g->vram_bank * 0x2000 + ((a - 0x8000) & 0x1FFF)];
}

void gb_remap(GB *g)
{
    memset(g->rpage, 0, sizeof g->rpage);
    memset(g->wpage, 0, sizeof g->wpage);
    if (g->dma.active) return;  /* durante el DMA todo pasa por la ruta lenta */

    const u8 *bank0 = g->rom + (size_t)g->mbc.bank0 * 0x4000;
    const u8 *bankx = g->rom + (size_t)g->mbc.bankx * 0x4000;
    for (int p = 0; p < 0x40; p++) {
        g->rpage[p] = bank0 + p * 256;
        g->rpage[0x40 + p] = bankx + p * 256;
    }
    for (int p = 0xC0; p < 0xFE; p++) {
        u8 *page = wram_at(g, (u16)(p << 8));
        g->rpage[p] = page;
        g->wpage[p] = page;
    }
}

/* ------------------------------------------------------------------ */
/* DMA de OAM                                                          */
/* ------------------------------------------------------------------ */

/* Lectura del DMA: sin efectos y sin bloqueos */
static u8 dma_source_read(GB *g, u16 a)
{
    if (a < 0x4000) return g->rom[(size_t)g->mbc.bank0 * 0x4000 + a];
    if (a < 0x8000) return g->rom[(size_t)g->mbc.bankx * 0x4000 + (a - 0x4000)];
    if (a < 0xA000) return *vram_at(g, a);
    if (a < 0xC000) return mbc_sram_read(g, a);
    return *wram_at(g, a);     /* $E000+ refleja WRAM */
}

static void dma_start(GB *g, u8 v)
{
    Dma *d = &g->dma;
    if (d->running) {
        /* El DMA anterior sigue hasta que arranca el nuevo */
        d->pending_source = v;
        d->pending_delay = 2;
    } else {
        d->source = v;
        d->index = 0;
        d->start_delay = 2;
    }
    if (!d->active) {
        d->active = 1;
        gb_remap(g);
    }
}

static void dma_mcycle(GB *g)
{
    Dma *d = &g->dma;
    if (d->pending_delay && --d->pending_delay == 0) {
        d->source = d->pending_source;
        d->index = 0;
        d->running = 1;
        return;     /* igual que un DMA nuevo: copia a partir del M-ciclo siguiente */
    }
    if (!d->running) {
        if (d->start_delay && --d->start_delay == 0)
            d->running = 1;
        return;
    }
    u16 src = (u16)(d->source << 8 | d->index);
    d->last_byte = dma_source_read(g, src);
    g->oam[d->index] = d->last_byte;
    if (++d->index == 0xA0 && !d->pending_delay) {
        d->running = 0;
        d->active = 0;
        gb_remap(g);
    }
}

/* El CPU choca con el DMA si usa el mismo bus que el origen */
static int dma_conflict(GB *g, u16 a)
{
    if (!g->dma.running || a >= 0xFE00) return 0;
    int src_vram = g->dma.source >= 0x80 && g->dma.source < 0xA0;
    int cpu_vram = a >= 0x8000 && a < 0xA000;
    return src_vram == cpu_vram;
}

/* ------------------------------------------------------------------ */
/* Serial (sin cable: se reciben $FF)                                  */
/* ------------------------------------------------------------------ */

static void serial_write_sc(GB *g, u8 v)
{
    Serial *s = &g->serial;
    g->io[IO_SC] = v;
    if ((v & 0x81) == 0x81) {
        s->active = 1;
        s->bits_left = 8;
        if (s->out_len == s->out_cap) {
            s->out_cap = s->out_cap ? s->out_cap * 2 : 256;
            s->out = realloc(s->out, s->out_cap);
        }
        if (s->out) s->out[s->out_len++] = g->io[IO_SB];
    } else {
        s->active = 0;
    }
}

static void serial_mcycle(GB *g)
{
    Serial *s = &g->serial;
    if (!s->active) return;
    /* Reloj interno de 8192 Hz: flanco de bajada del bit 8 del contador del
     * timer, alineado con el arranque y no con la escritura en SC */
    u16 now = g->timer.sys, old = s->last_sys;
    s->last_sys = g->timer.sys;
    if (!((old & 0x100) && !(now & 0x100))) return;
    if (--s->bits_left == 0) {
        s->active = 0;
        g->io[IO_SB] = 0xFF;
        g->io[IO_SC] &= 0x7F;
        IF_REG(g) |= INT_SERIAL;
    }
}

/* ------------------------------------------------------------------ */
/* Joypad                                                              */
/* ------------------------------------------------------------------ */

static u8 joypad_lines(GB *g)
{
    u8 sel = g->io[IO_P1] & 0x30;
    u8 low = 0x0F;
    if (!(sel & 0x10)) low &= (u8)~(g->pad >> 4);
    if (!(sel & 0x20)) low &= (u8)~(g->pad & 0x0F);
    return low & 0x0F;
}

static void joypad_update(GB *g)
{
    u8 lines = joypad_lines(g);
    if (g->p1_lines & ~lines)       /* alguna línea bajó */
        IF_REG(g) |= INT_JOYPAD;
    g->p1_lines = lines;
}

void gb_set_pad(GB *g, u8 pad)
{
    gb_sync(g);
    g->pad = pad;
    joypad_update(g);
    if (g->cpu.stopped && pad) g->cpu.stopped = 0;
    g->next_event = 0;
}

/* ------------------------------------------------------------------ */
/* Sincronización                                                      */
/* ------------------------------------------------------------------ */

/*
 * Algunas escrituras a registros de la PPU surten efecto hasta 2 puntos antes
 * del M-ciclo de la escritura (ver ppu_register_write). Por eso, durante la
 * sincronización la PPU se queda PPU_HOLD puntos atrás y solo se pone al
 * corriente al final; las escrituras a sus registros la adelantan hasta el
 * punto exacto.
 */
#define PPU_HOLD 2

/*
 * Dos relojes: g->hw_cyc cuenta ciclos del CPU (el timer, el serial y el DMA de
 * OAM van a ese ritmo) y g->rt cuenta puntos de tiempo real (la PPU y el APU).
 * Son iguales salvo en doble velocidad (CGB), donde un M-ciclo del CPU dura
 * 2 puntos en vez de 4.
 */
static u32 dots_per_mcycle(const GB *g)
{
    return g->double_speed ? 2 : 4;
}

/* Tiempo real que corresponde a un ciclo del CPU cercano (ya sincronizado) */
static u64 rt_at(const GB *g, u64 cpu)
{
    u64 d = cpu >= g->hw_cyc ? cpu - g->hw_cyc : 0;
    return g->rt + (g->double_speed ? d / 2 : d);
}

static void sync_hardware(GB *g)
{
    while (g->hw_cyc < g->cyc) {
        u32 dots = dots_per_mcycle(g);
        g->hw_cyc += 4;
        g->rt += dots;
        timer_mcycle(g);
        if (g->serial.active) serial_mcycle(g);
        else g->serial.last_sys = g->timer.sys;
        if (g->dma.active) dma_mcycle(g);
        ppu_advance(g, g->rt - PPU_HOLD);
        apu_mcycle(g, dots);
    }
}

void gb_sync(GB *g)
{
    sync_hardware(g);
    ppu_advance(g, g->rt);
}

static u64 next_hw_event(GB *g)
{
    u64 t = g->stop_at;
    u64 x = timer_next_event(g);
    if (x < t) t = x;
    x = ppu_next_event(g);      /* en tiempo real: se pasa a ciclos del CPU */
    if (x != UINT64_MAX) {
        u64 d = x > g->rt ? x - g->rt : 0;
        x = g->hw_cyc + (g->double_speed ? d * 2 : d);
    }
    if (x < t) t = x;
    if (g->serial.active) {
        /* Siguiente flanco del bit 8, más los bits que faltan */
        u32 first = 512 - (g->timer.sys & 511);
        x = g->hw_cyc + first + (u64)(g->serial.bits_left - 1) * 512;
        if (x < t) t = x;
    }
    return t;
}

/* El CPU queda detenido mientras la HDMA copia */
static void hdma_wait(GB *g)
{
    if (!g->hdma_stall) return;
    g->cyc += g->hdma_stall;
    g->hdma_stall = 0;
    gb_sync(g);
}

int gb_check(GB *g)
{
    Cpu *c = &g->cpu;
    gb_sync(g);
    hdma_wait(g);
    if (g->cyc >= g->stop_at) return 1;
    if (c->ime && PENDING(g)) return 1;
    if (c->ei_pend) {
        c->ei_pend = 0;
        c->ime = 1;
        g->next_event = 0;
        return 0;
    }
    g->next_event = next_hw_event(g);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Bus: ruta lenta                                                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Game Boy Color: HDMA y registros propios                            */
/* ------------------------------------------------------------------ */

/* Copia un bloque de 16 bytes de HDMA (el origen no puede ser VRAM) */
static void hdma_block(GB *g)
{
    for (int i = 0; i < 16; i++) {
        u16 src = (u16)(g->hdma_src + i);
        u8 v = (src >= 0x8000 && src < 0xA000) ? 0xFF : dma_source_read(g, src);
        g->vram[g->vram_bank * 0x2000 + ((g->hdma_dst + i) & 0x1FFF)] = v;
    }
    g->hdma_src = (u16)(g->hdma_src + 16);
    g->hdma_dst = (u16)(0x8000 | ((g->hdma_dst + 16) & 0x1FFF));
    /* El CPU espera 8 µs por bloque: 32 ciclos, o 64 en doble velocidad */
    g->hdma_stall += g->double_speed ? 64 : 32;
    g->next_event = 0;
}

/* Llamado por la PPU al empezar cada HBlank de las líneas visibles */
void gb_hdma_hblank(GB *g)
{
    if (!g->hdma_hblank || g->cpu.halted) return;
    hdma_block(g);
    if (g->hdma_left == 0) {
        g->hdma_hblank = 0;
        g->hdma_left = 0x7F;    /* terminó: HDMA5 se lee $FF */
    } else {
        g->hdma_left--;
    }
}

static void hdma_write(GB *g, u8 v)
{
    if (g->hdma_hblank && !(v & 0x80)) {
        g->hdma_hblank = 0;     /* cancelar: HDMA5 queda con bit 7 = 1 */
        return;
    }
    g->hdma_left = v & 0x7F;
    if (v & 0x80) {
        g->hdma_hblank = 1;
        /* Con el LCD apagado no hay HBlank: el primer bloque va de inmediato */
        if (!(g->io[IO_LCDC] & 0x80)) gb_hdma_hblank(g);
        return;
    }
    /* DMA general: todo de una vez, con el CPU detenido */
    for (int n = g->hdma_left; n >= 0; n--) hdma_block(g);
    g->hdma_left = 0x7F;
}

static int cgb_palette_blocked(GB *g)
{
    return (g->io[IO_LCDC] & 0x80) && g->ppu.mode == 3;
}

static u8 cgb_io_read(GB *g, u8 reg)
{
    switch (reg) {
    case 0x4D: return (u8)(0x7E | (g->double_speed ? 0x80 : 0) | (g->key1 & 1));
    case 0x4F: return (u8)(0xFE | g->vram_bank);
    case 0x55: return g->hdma_hblank ? g->hdma_left : (u8)(0x80 | g->hdma_left);
    case 0x56: return (u8)((g->io[0x56] & 0xC1) | 0x3C);
    case 0x68: return (u8)(g->bcps | 0x40);
    case 0x69: return cgb_palette_blocked(g) ? 0xFF : g->bg_pal[g->bcps & 0x3F];
    case 0x6A: return (u8)(g->ocps | 0x40);
    case 0x6B: return cgb_palette_blocked(g) ? 0xFF : g->obj_pal[g->ocps & 0x3F];
    case 0x6C: return (u8)(0xFE | g->opri);
    case 0x70: return (u8)(0xF8 | (g->io[0x70] & 7));
    case 0x72: case 0x73: case 0x74: return g->undoc[reg - 0x72];
    case 0x75: return (u8)(0x8F | g->undoc[3]);
    case 0x76: case 0x77: return 0x00;
    default: return 0xFF;
    }
}

/* Escribe en una paleta CGB; el índice avanza aunque la escritura se ignore */
static void palette_write(GB *g, u8 *pal, u8 *ps, u8 v)
{
    if (!cgb_palette_blocked(g)) pal[*ps & 0x3F] = v;
    if (*ps & 0x80) *ps = (u8)(0x80 | ((*ps + 1) & 0x3F));
}

/* Devuelve 1 si el registro es del CGB (y ya lo manejó) */
static int cgb_io_write(GB *g, u8 reg, u8 v)
{
    switch (reg) {
    case 0x4D: g->key1 = v & 1; return 1;
    case 0x4F: g->vram_bank = v & 1; return 1;
    case 0x51: g->hdma_src = (u16)((g->hdma_src & 0x00FF) | v << 8); return 1;
    case 0x52: g->hdma_src = (u16)((g->hdma_src & 0xFF00) | (v & 0xF0)); return 1;
    case 0x53: g->hdma_dst = (u16)(0x8000 | ((v & 0x1F) << 8) | (g->hdma_dst & 0x00FF)); return 1;
    case 0x54: g->hdma_dst = (u16)((g->hdma_dst & 0xFF00) | (v & 0xF0)); return 1;
    case 0x55: hdma_write(g, v); return 1;
    case 0x56: g->io[0x56] = v; return 1;
    case 0x68: g->bcps = v & 0xBF; return 1;
    case 0x69: palette_write(g, g->bg_pal, &g->bcps, v); return 1;
    case 0x6A: g->ocps = v & 0xBF; return 1;
    case 0x6B: palette_write(g, g->obj_pal, &g->ocps, v); return 1;
    case 0x6C: g->opri = v & 1; return 1;
    case 0x70:
        g->io[0x70] = v & 7;
        g->wram_bank = (v & 7) ? (u8)(v & 7) : 1;
        gb_remap(g);
        return 1;
    case 0x72: case 0x73: case 0x74: g->undoc[reg - 0x72] = v; return 1;
    case 0x75: g->undoc[3] = v & 0x70; return 1;
    default: return 0;
    }
}

static u8 io_read(GB *g, u8 reg)
{
    if (reg >= 0x10 && reg < 0x40) return apu_read(g, (u16)(0xFF00 | reg));
    if (g->cgb && reg >= 0x4C) return cgb_io_read(g, reg);
    switch (reg) {
    case IO_P1:   return (u8)(0xC0 | (g->io[IO_P1] & 0x30) | joypad_lines(g));
    case IO_SB:   return g->io[IO_SB];
    case IO_SC:   return (u8)(g->io[IO_SC] | 0x7E);
    case IO_DIV: case IO_TIMA: case IO_TMA: case IO_TAC:
        return timer_read(g, reg);
    case IO_IF:   return (u8)(IF_REG(g) | 0xE0);
    case IO_STAT: case IO_LY:
        return ppu_read(g, reg);
    case IO_LCDC: case IO_SCY: case IO_SCX: case IO_LYC: case IO_DMA:
    case IO_BGP: case IO_OBP0: case IO_OBP1: case IO_WY: case IO_WX:
        return g->io[reg];
    default:      return 0xFF;
    }
}

static void io_write(GB *g, u8 reg, u8 v)
{
    if (reg >= 0x10 && reg < 0x40) { apu_write(g, (u16)(0xFF00 | reg), v); return; }
    if (g->cgb && reg >= 0x4C && cgb_io_write(g, reg, v)) return;
    switch (reg) {
    case IO_P1:
        g->io[IO_P1] = v & 0x30;
        joypad_update(g);
        return;
    case IO_SB:   g->io[IO_SB] = v; return;
    case IO_SC:   serial_write_sc(g, v); return;
    case IO_DIV: case IO_TIMA: case IO_TMA: case IO_TAC:
        timer_write(g, reg, v);
        return;
    case IO_IF:   IF_REG(g) = v & 0x1F; return;
    case IO_DMA:
        g->io[IO_DMA] = v;
        dma_start(g, v);
        return;
    default:
        if (reg >= 0x40 && reg <= 0x4B) ppu_write(g, reg, v);
        return;
    }
}

u8 gb_read_slow(GB *g, u16 a)
{
    if (a >= 0xFF80 && a != 0xFFFF) return g->hram[a - 0xFF80];
    gb_sync(g);
    if (dma_conflict(g, a)) return g->dma.last_byte;

    if (a < 0x4000) return g->rom[(size_t)g->mbc.bank0 * 0x4000 + a];
    if (a < 0x8000) return g->rom[(size_t)g->mbc.bankx * 0x4000 + (a - 0x4000)];
    if (a < 0xA000) {
        if (g->block_vram && ppu_vram_blocked(g, 0)) return 0xFF;
        return *vram_at(g, a);
    }
    if (a < 0xC000) return mbc_sram_read(g, a);
    if (a < 0xFE00) return *wram_at(g, a);
    if (a < 0xFEA0) {
        if (g->dma.running) return 0xFF;
        if (g->block_vram && ppu_oam_blocked(g, 0)) return 0xFF;
        return g->oam[a - 0xFE00];
    }
    if (a < 0xFF00) return 0x00;
    if (a == 0xFFFF) return g->ie;
    return io_read(g, (u8)a);
}

void gb_write_slow(GB *g, u16 a, u8 v)
{
    if (a >= 0xFF80 && a != 0xFFFF) { g->hram[a - 0xFF80] = v; return; }
    if (a >= 0xFF40 && a <= 0xFF4B && a != 0xFF46) {
        /* Registro de la PPU: cada uno surte efecto en su punto del M-ciclo */
        sync_hardware(g);
        g->next_event = 0;
        ppu_write_timed(g, (u8)a, v, g->rt);
        return;
    }
    if (a == 0xFF0F) {
        /* IF: la escritura gana a lo que la PPU pida en el mismo punto */
        sync_hardware(g);
        ppu_advance(g, g->rt + 1);
        IF_REG(g) = v & 0x1F;
        g->next_event = 0;
        return;
    }
    gb_sync(g);
    g->next_event = 0;  /* la escritura puede cambiar interrupciones o eventos */
    if (dma_conflict(g, a)) return;

    if (a < 0x8000) { mbc_write(g, a, v); return; }
    if (a < 0xA000) {
        if (g->block_vram && ppu_vram_blocked(g, 1)) return;
        *vram_at(g, a) = v;
        return;
    }
    if (a < 0xC000) { mbc_sram_write(g, a, v); return; }
    if (a < 0xFE00) { *wram_at(g, a) = v; return; }
    if (a < 0xFEA0) {
        if (g->dma.running) return;
        if (g->block_vram && ppu_oam_blocked(g, 1)) return;
        g->oam[a - 0xFE00] = v;
        return;
    }
    if (a < 0xFF00) return;
    if (a == 0xFFFF) { g->ie = v; return; }
    io_write(g, (u8)a, v);
}

u8 gb_peek(GB *g, u16 a)
{
    if (a < 0x8000 || (a >= 0xA000 && a < 0xFE00)) return dma_source_read(g, a);
    if (a < 0xA000) return *vram_at(g, a);
    if (a < 0xFEA0) return g->oam[a - 0xFE00];
    if (a < 0xFF00) return 0x00;
    if (a == 0xFFFF) return g->ie;
    if (a >= 0xFF80) return g->hram[a - 0xFF80];
    return io_read(g, (u8)a);
}

/* ------------------------------------------------------------------ */
/* CPU: interrupciones, HALT, STOP                                     */
/* ------------------------------------------------------------------ */

/* Despacho de interrupción: 5 M-ciclos. El vector se decide después de
 * empujar el byte alto de PC; si esa escritura cambió IE y ya no hay nada
 * pendiente, el CPU salta a $0000. */
static void service_irq(GB *g)
{
    Cpu *c = &g->cpu;
    u16 pc = c->pc;
    c->ime = 0;
    g->cyc += 8;
    mem_write(g, --c->sp, (u8)(pc >> 8));
    gb_sync(g);
    u8 pend = PENDING(g);
    mem_write(g, --c->sp, (u8)pc);
    if (pend) {
        int bit = 0;
        while (!(pend & (1 << bit))) bit++;
        IF_REG(g) &= (u8)~(1 << bit);
        c->pc = (u16)(0x40 + bit * 8);
    } else {
        c->pc = 0x0000;
    }
    g->cyc += 4;
}

void gb_halt(GB *g)
{
    Cpu *c = &g->cpu;
    gb_sync(g);
    if (!c->ime && PENDING(g)) {
        c->halt_bug = 1;    /* no se detiene y repite el byte siguiente */
        return;
    }
    c->halted = 1;
    c->halt_first = 1;
    g->next_event = 0;
}

void gb_stop(GB *g)
{
    Cpu *c = &g->cpu;
    gb_sync(g);
    timer_write(g, IO_DIV, 0);
    if (g->cgb && (g->key1 & 1)) {
        /* Cambio de velocidad: el CPU se detiene unos 2050 M-ciclos */
        g->double_speed = !g->double_speed;
        g->key1 = 0;
        g->cyc += 2050 * 4;
        g->next_event = 0;
        return;
    }
    if (!g->pad) c->stopped = 1;
    g->next_event = 0;
}

void gb_lockup(GB *g)
{
    g->cpu.locked = 1;
    g->next_event = 0;
}

/* ------------------------------------------------------------------ */
/* Despachador                                                         */
/* ------------------------------------------------------------------ */

void gb_run(GB *g, u64 until)
{
    Cpu *c = &g->cpu;
    g->stop_at = until;
    g->next_event = 0;

    for (;;) {
        if (g->cyc >= g->next_event) {
            gb_sync(g);
            hdma_wait(g);
            if (g->cyc >= g->stop_at) break;
            u8 pend = PENDING(g);

            if (UNLIKELY(c->halted | c->stopped | c->locked)) {
                if (c->halted) {
                    /* En HALT, IF se muestrea 2 ciclos dentro de cada M-ciclo
                     * (el primero tras HALT, al inicio) y el CPU despierta al
                     * terminar ese M-ciclo */
                    u64 sample = c->halt_first ? g->cyc : g->cyc + 2;
                    c->halt_first = 0;
                    ppu_advance(g, rt_at(g, sample));
                    if (PENDING(g)) {
                        c->halted = 0;
                        g->cyc += 4;
                        g->next_event = 0;
                        continue;
                    }
                    /* Dormido: saltar al M-ciclo en cuyo muestreo ya se vería
                     * el siguiente evento */
                    u64 t = next_hw_event(g);
                    t = t > 2 ? (t - 2 + 3) & ~(u64)3 : 0;
                    g->cyc = t > g->cyc ? t : g->cyc + 4;
                    g->next_event = 0;
                    continue;
                }
                if (c->stopped && g->pad) {
                    c->stopped = 0;
                } else {
                    u64 t = (g->stop_at + 3) & ~(u64)3;
                    g->cyc = t > g->cyc ? t : g->cyc + 4;
                    g->next_event = 0;
                    continue;
                }
            }
            if (c->ime && pend) {
                service_irq(g);
                g->next_event = 0;
                continue;
            }
            if (c->ei_pend) {
                c->ei_pend = 0;
                c->ime = 1;
                g->next_event = 0;
            } else {
                g->next_event = next_hw_event(g);
            }
        }

        RecompFn fn = NULL;
        if (g->recomp_lookup && !c->halt_bug)
            fn = g->recomp_lookup(g, c->pc);
        if (fn) fn(g);
        else gb_interp_step(g);
    }
}

void gb_run_frame(GB *g)
{
    gb_run(g, g->cyc + GB_CYCLES_FRAME);
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

/*
 * VRAM como la deja la boot ROM del DMG: el logo de la cabecera del cartucho
 * ampliado al doble (mosaicos 1 a 24) y el símbolo ® (mosaico $19), con su
 * mapa en $9904. Algunos juegos y pruebas usan esos mosaicos.
 */
static void boot_vram(GB *g)
{
    static const u8 registered[8] = { 0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C };
    for (int i = 0; i < 48; i++) {
        u8 b = g->rom[0x104 + i];
        for (int half = 0; half < 2; half++) {
            u8 nib = half ? (b & 0x0F) : (b >> 4);
            u8 wide = 0;
            for (int bit = 0; bit < 4; bit++)
                if (nib & (1 << bit)) wide |= (u8)(3 << (bit * 2));
            int addr = 0x10 + i * 8 + half * 4;
            g->vram[addr] = wide;       /* cada fila se dibuja dos veces */
            g->vram[addr + 2] = wide;
        }
    }
    for (int i = 0; i < 8; i++) g->vram[0x190 + i * 2] = registered[i];
    for (int t = 0; t < 12; t++) {
        g->vram[0x1904 + t] = (u8)(1 + t);
        g->vram[0x1924 + t] = (u8)(13 + t);
    }
    g->vram[0x1910] = 0x19;
}

const char *gb_init(GB *g, const u8 *rom, u32 size)
{
    return gb_init_model(g, rom, size, GB_MODEL_AUTO);
}

const char *gb_init_model(GB *g, const u8 *rom, u32 size, int model)
{
    memset(g, 0, sizeof *g);
    if (size < 0x150) return "la ROM es demasiado chica";
    /* Automático: color si el cartucho lo admite (cabecera $143, bit 7) */
    g->cgb = model == GB_MODEL_AUTO ? (rom[0x143] & 0x80) != 0 : model == GB_MODEL_CGB;
    g->wram_bank = 1;

    /* Copia con tamaño de potencia de 2 (al menos 2 bancos) */
    u32 banks = 2;
    while (banks * 0x4000 < size) banks *= 2;
    u8 *copy = malloc((size_t)banks * 0x4000);
    if (!copy) return "sin memoria";
    memset(copy, 0xFF, (size_t)banks * 0x4000);
    memcpy(copy, rom, size);
    g->rom = copy;
    g->rom_size = banks * 0x4000;
    g->rom_banks = banks;
    g->block_vram = 1;

    mbc_init(g);
    timer_reset(g);
    ppu_reset(g);
    apu_reset(g);

    /* Estado al terminar la boot ROM */
    Cpu *c = &g->cpu;
    if (g->cgb) {
        /* CGB con un juego de color: A = $11 identifica al Game Boy Color */
        c->a = 0x11; c->f = 0x80;
        c->b = 0x00; c->c = 0x00;
        c->d = 0xFF; c->e = 0x56;
        c->h = 0x00; c->l = 0x0D;
    } else {
        c->a = 0x01; c->f = 0xB0;
        c->b = 0x00; c->c = 0x13;
        c->d = 0x00; c->e = 0xD8;
        c->h = 0x01; c->l = 0x4D;
    }
    c->sp = 0xFFFE;
    c->pc = 0x0100;

    g->io[IO_P1] = 0x00;    /* P1 se lee como $CF */
    g->io[IO_SC] = 0x00;
    IF_REG(g) = 0x01;
    g->io[IO_LCDC] = 0x91;
    g->io[IO_STAT] = 0x00;
    g->io[IO_BGP] = 0xFC;
    g->io[IO_DMA] = 0xFF;
    g->p1_lines = 0x0F;
    if (g->cgb) {
        /* La boot ROM del CGB deja las paletas en blanco */
        memset(g->bg_pal, 0xFF, sizeof g->bg_pal);
        memset(g->obj_pal, 0xFF, sizeof g->obj_pal);
        g->io[0x70] = 1;
        g->hdma_left = 0x7F;
    } else {
        boot_vram(g);
    }
    gb_remap(g);
    return NULL;
}

void gb_free(GB *g)
{
    free((void *)g->rom);
    free(g->sram);
    free(g->serial.out);
    g->rom = NULL;
    g->sram = NULL;
    g->serial.out = NULL;
}

const u16 *gb_framebuffer(const GB *g)
{
    return g->ppu.buf[g->ppu.front];
}

int gb_audio_pull(GB *g, float *out, int max)
{
    Apu *a = &g->apu;
    int n = 0;
    while (n < max && a->ring_tail != a->ring_head) {
        out[n * 2] = a->ring[a->ring_tail * 2];
        out[n * 2 + 1] = a->ring[a->ring_tail * 2 + 1];
        a->ring_tail = (a->ring_tail + 1) % APU_RING;
        n++;
    }
    return n;
}

void gb_copy_state(GB *dst, const GB *src)
{
    const u8 *rom = dst->rom;
    u8 *sram = dst->sram;
    u8 *serial_out = dst->serial.out;
    size_t serial_cap = dst->serial.out_cap;
    RecompFn (*lookup)(GB *, u16) = dst->recomp_lookup;
    void (*trace)(GB *) = dst->trace;

    memcpy(dst, src, sizeof *dst);

    dst->rom = rom;
    dst->sram = sram;
    if (sram && src->sram) memcpy(sram, src->sram, src->sram_size);
    dst->serial.out = serial_out;
    dst->serial.out_cap = serial_cap;
    if (dst->serial.out_len > serial_cap) dst->serial.out_len = serial_cap;
    if (serial_out && src->serial.out) memcpy(serial_out, src->serial.out, dst->serial.out_len);
    dst->recomp_lookup = lookup;
    dst->trace = trace;
    /* Punteros internos que apuntaban a src */
    dst->ppu.back = dst->ppu.buf[src->ppu.back == src->ppu.buf[0] ? 0 : 1];
    gb_remap(dst);
}
