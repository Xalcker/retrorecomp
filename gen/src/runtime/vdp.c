/*
 * VDP: puertos, memorias, DMA, estado, contador HV y el reloj de líneas. El dibujo de cada línea está
 * en vdp_render.c.
 *
 * - Puerto de control: escrituras de registros ($8000 | reg << 8 | valor) y comandos de dirección
 *   de dos palabras (código CD5-CD0 y dirección de 16 bits). Con CD5 y el DMA habilitado (registro
 *   1, bit 4) arranca un DMA según el registro 23: desde el bus del 68000 (el 68000 queda detenido
 *   lo que dura), relleno (espera la escritura en el puerto de datos) o copia dentro de la VRAM.
 * - Puerto de datos: VRAM (una dirección impar cambia el orden de los bytes), CRAM y VSRAM, con el
 *   incremento del registro 15.
 * - Reloj de líneas: 262 por cuadro; cada línea con imagen se dibuja al terminar (así lo que cambia
 *   la HINT de la línea sale en esa línea). VBLANK desde la línea `height`, VINT (nivel 6) al
 *   empezarla y HINT (nivel 4) por el contador del registro 10.
 *
 * Va al día de forma perezosa: vdp_sync avanza las líneas hasta el ciclo actual (después de cada
 * instrucción y antes de cada acceso del 68000 al VDP).
 *
 * Sin emular todavía: la FIFO (las escrituras del 68000 no esperan; el estado dice siempre "vacía"),
 * el modo 2 de entrelazado (doble resolución) y el modo 4 (SMS).
 */
#include <string.h>

#include "gen.h"

static inline int h40(const Vdp *v) { return (v->reg[12] & 0x81) != 0; }

void vdp_reset(Gen *g)
{
    Vdp *v = &g->vdp;
    memset(v, 0, sizeof *v);
    v->next_line = MCLK_PER_LINE;
    v->width = 256;
    v->height = 224;
}

static void update_ipl(Gen *g)
{
    Vdp *v = &g->vdp;
    int l = 0;
    if (v->vint_pending && (v->reg[1] & 0x20)) l = 6;
    else if (v->hint_pending && (v->reg[0] & 0x10)) l = 4;
    g->cpu.ipl = (u8)l;
}

void vdp_sync(Gen *g)
{
    Vdp *v = &g->vdp;
    u64 m = g->cyc * MCLK_PER_M68K;
    if (m < v->next_line) return;
    while (m >= v->next_line) {
        v->next_line += MCLK_PER_LINE;
        if (v->line < v->height) vdp_render_line(g, v->line);
        /* Fin de la línea: el contador de HINT baja en las líneas con imagen y en la primera del
         * VBLANK; en el resto se recarga */
        if (v->line <= v->height) {
            if (--v->hint_counter < 0) { v->hint_counter = v->reg[10]; v->hint_pending = 1; }
        } else v->hint_counter = v->reg[10];
        if (++v->line == LINES_NTSC) {
            v->line = 0;
            v->frame++;
            v->frame_done = 1;
            v->odd ^= 1;
        }
        if (v->line == 0) {             /* el modo de pantalla se fija al empezar el cuadro */
            v->width = h40(v) ? 320 : 256;
            v->height = ((v->reg[1] & 0x08) && (g->version & 0x40)) ? 240 : 224;
        }
        if (v->line == v->height) { v->vint_pending = 1; v->f_flag = 1; }
    }
    update_ipl(g);
}

void vdp_int_ack(Gen *g, int level)
{
    Vdp *v = &g->vdp;
    if (level == 6) { v->vint_pending = 0; v->f_flag = 0; }
    else if (level == 4) v->hint_pending = 0;
    update_ipl(g);
}

/* Ciclo maestro dentro de la línea actual */
static inline u32 line_pos(Gen *g)
{
    return (u32)(g->cyc * MCLK_PER_M68K - (g->vdp.next_line - MCLK_PER_LINE));
}

/* Contador H: en H40 cuenta $00-$B6 y salta a $E4-$FF (210 valores por línea); en H32, $00-$93 y
 * $E9-$FF (171). Se reparte parejo en la línea (en la consola el reloj de pixel cambia durante el
 * HSYNC). */
static int hcounter(Gen *g)
{
    int n = h40(&g->vdp) ? 210 : 171;
    int i = (int)((u64)line_pos(g) * (u64)n / MCLK_PER_LINE);
    if (h40(&g->vdp)) return i <= 0xB6 ? i : i - 0xB7 + 0xE4;
    return i <= 0x93 ? i : i - 0x94 + 0xE9;
}

static u16 status(Gen *g)
{
    Vdp *v = &g->vdp;
    u16 s = (u16)(g->open_bus & 0xFC00);
    s |= 0x0200;                                                       /* FIFO vacía */
    if (v->f_flag) s |= 0x80;
    if (v->spr_overflow) s |= 0x40;
    if (v->spr_collision) s |= 0x20;
    if ((v->reg[12] & 0x02) && v->odd) s |= 0x10;
    if (v->line >= v->height || !(v->reg[1] & 0x40)) s |= 0x08;       /* VBLANK (o pantalla apagada) */
    int hc = hcounter(g);
    if (h40(v) ? (hc >= 0xB3 || hc < 0x06) : (hc >= 0x92 || hc < 0x05)) s |= 0x04;     /* HBLANK */
    if (g->cyc * MCLK_PER_M68K < v->dma_busy_until) s |= 0x02;
    if (g->version & 0x40) s |= 0x01;                                  /* PAL */
    v->spr_overflow = v->spr_collision = 0;
    return s;
}

/* Contador HV: la V salta de $EA a $E5 en NTSC de 224 líneas */
static u16 hv_counter(Gen *g)
{
    Vdp *v = &g->vdp;
    int vc = v->line <= 0xEA ? v->line : v->line - 6;
    return (u16)((vc & 0xFF) << 8 | (hcounter(g) & 0xFF));
}

/* Escritura en la memoria que dice el código (sin avanzar la dirección) */
static void mem_write(Vdp *v, u16 w)
{
    switch (v->code & 0x0F) {
    case 0x1: {
        u16 a = v->addr;
        v->vram[a] = (u8)(w >> 8);
        v->vram[a ^ 1] = (u8)w;
        break;
    }
    case 0x3: v->cram[v->addr >> 1 & 0x3F] = w & 0x0EEE; break;
    case 0x5: if ((v->addr >> 1 & 0x3F) < 40) v->vsram[v->addr >> 1 & 0x3F] = w & 0x07FF; break;
    default: break;
    }
}

/* Accesos por línea que tiene el DMA: con imagen, pocos; en el VBLANK o con la pantalla apagada,
 * casi todos. Un acceso mueve un byte de VRAM o una palabra de CRAM/VSRAM. */
static u64 dma_mclk(Gen *g, u32 slots)
{
    Vdp *v = &g->vdp;
    int active = (v->reg[1] & 0x40) && v->line < v->height;
    u32 rate = h40(v) ? (active ? 18 : 205) : (active ? 16 : 167);
    return (u64)slots * MCLK_PER_LINE / rate;
}

static u32 dma_length(const Vdp *v)
{
    u32 n = (u32)v->reg[20] << 8 | v->reg[19];
    return n ? n : 0x10000;
}

static void dma_done(Vdp *v, u32 src)
{
    v->reg[19] = v->reg[20] = 0;
    v->reg[21] = (u8)src;
    v->reg[22] = (u8)(src >> 8);
}

/* DMA desde el bus del 68000: el 68000 queda detenido lo que dura */
static void dma_68k(Gen *g)
{
    Vdp *v = &g->vdp;
    u32 n = dma_length(v);
    u32 src = ((u32)(v->reg[23] & 0x7F) << 17) | ((u32)v->reg[22] << 9) | ((u32)v->reg[21] << 1);
    for (u32 i = 0; i < n; i++) {
        mem_write(v, m68k_peek16(g, src));
        src = (src & 0xFE0000u) | ((src + 2) & 0x1FFFFu);         /* la fuente da la vuelta en 128 KB */
        v->addr = (u16)(v->addr + v->reg[15]);
    }
    dma_done(v, src >> 1);
    u32 slots = (v->code & 0x0F) == 0x1 ? n * 2 : n;
    g->cyc += (dma_mclk(g, slots) + MCLK_PER_M68K - 1) / MCLK_PER_M68K;
}

/* Relleno: la escritura en el puerto de datos va normal y después se repite su byte alto (en VRAM)
 * o la palabra (en CRAM/VSRAM) `length` veces. El 68000 sigue; el estado dice "DMA" mientras dura. */
static void dma_fill(Gen *g, u16 w)
{
    Vdp *v = &g->vdp;
    u32 n = dma_length(v);
    for (u32 i = 0; i < n; i++) {
        if ((v->code & 0x0F) == 0x1) v->vram[v->addr ^ 1] = (u8)(w >> 8);
        else mem_write(v, w);
        v->addr = (u16)(v->addr + v->reg[15]);
    }
    dma_done(v, ((u32)v->reg[22] << 8 | v->reg[21]) + n);
    v->dma_busy_until = g->cyc * MCLK_PER_M68K + dma_mclk(g, n);
}

/* Copia de VRAM a VRAM, byte por byte */
static void dma_copy(Gen *g)
{
    Vdp *v = &g->vdp;
    u32 n = dma_length(v);
    u16 src = (u16)(v->reg[22] << 8 | v->reg[21]);
    for (u32 i = 0; i < n; i++) {
        v->vram[v->addr] = v->vram[src];
        src++;
        v->addr = (u16)(v->addr + v->reg[15]);
    }
    dma_done(v, src);
    v->dma_busy_until = g->cyc * MCLK_PER_M68K + dma_mclk(g, n * 2);
}

static void control_write(Gen *g, u16 w)
{
    Vdp *v = &g->vdp;
    if (v->pending) {
        v->code = (u8)((v->code & 0x03) | ((w >> 2) & 0x3C));
        v->addr = (u16)((v->addr & 0x3FFF) | ((w & 3) << 14));
        v->pending = 0;
        if ((v->code & 0x20) && (v->reg[1] & 0x10)) {
            switch (v->reg[23] >> 6) {
            case 2: v->fill_pending = 1; break;
            case 3: dma_copy(g); break;
            default: dma_68k(g); break;
            }
        }
        return;
    }
    if ((w & 0xC000) == 0x8000) {
        int r = w >> 8 & 0x1F;
        if (r < 24 && ((v->reg[1] & 0x04) || r <= 10)) v->reg[r] = (u8)w;     /* en modo 4 solo 0-10 */
        if (r <= 1) update_ipl(g);
        return;
    }
    v->code = (u8)((v->code & 0x3C) | (w >> 14));
    v->addr = (u16)((v->addr & 0xC000) | (w & 0x3FFF));
    v->pending = 1;
}

static void data_write(Gen *g, u16 w)
{
    Vdp *v = &g->vdp;
    v->pending = 0;
    mem_write(v, w);
    v->addr = (u16)(v->addr + v->reg[15]);
    if (v->fill_pending) { v->fill_pending = 0; dma_fill(g, w); }
}

static u16 data_read(Gen *g)
{
    Vdp *v = &g->vdp;
    u16 w;
    v->pending = 0;
    switch (v->code & 0x0F) {
    case 0x0: w = rd16(v->vram + (v->addr & 0xFFFE)); break;
    case 0x4: w = (u16)(((v->addr >> 1 & 0x3F) < 40 ? v->vsram[v->addr >> 1 & 0x3F] : 0) | (g->open_bus & 0xF800)); break;
    case 0x8: w = (u16)(v->cram[v->addr >> 1 & 0x3F] | (g->open_bus & 0xF111)); break;
    case 0xC: w = (u16)((g->open_bus & 0xFF00) | v->vram[v->addr ^ 1]); break;   /* VRAM de 8 bits */
    default: w = g->open_bus; break;
    }
    v->addr = (u16)(v->addr + v->reg[15]);
    return w;
}

u16 vdp_read(Gen *g, u32 addr)
{
    vdp_sync(g);
    switch (addr & 0x1C) {
    case 0x00: return data_read(g);
    case 0x04: { u16 s = status(g); g->vdp.pending = 0; return s; }
    case 0x08: case 0x0C: return hv_counter(g);
    default: return g->open_bus;
    }
}

void vdp_write(Gen *g, u32 addr, u16 v)
{
    vdp_sync(g);
    switch (addr & 0x1C) {
    case 0x00: data_write(g, v); return;
    case 0x04: control_write(g, v); return;
    default: return;
    }
}

/* Niveles de la salida de video por componente (3 bits), normal, en sombra y resaltado: la sombra es
 * la mitad y el resalte la mitad más la mitad del máximo, con el DAC no lineal de la consola */
static const u8 lv_normal[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
static const u8 lv_shadow[8] = { 0, 29, 52, 70, 87, 101, 116, 130 };
static const u8 lv_high[8]   = { 130, 144, 158, 172, 187, 206, 228, 255 };

uint32_t vdp_argb(u16 px)
{
    const u8 *lv = (px >> 9) == 1 ? lv_shadow : (px >> 9) == 2 ? lv_high : lv_normal;
    u32 r = lv[px & 7], gr = lv[px >> 3 & 7], b = lv[px >> 6 & 7];
    return 0xFF000000u | r << 16 | gr << 8 | b;
}
