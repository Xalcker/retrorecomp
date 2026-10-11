/*
 * VDP. Fase 2: el esqueleto que necesita el bus para correr juegos de verdad.
 *
 * - Puerto de control: escrituras de registros ($8000 | reg << 8 | valor) y comandos de dirección
 *   de dos palabras (código CD5-CD0 y dirección de 16 bits).
 * - Puerto de datos: VRAM, CRAM y VSRAM con el incremento del registro 15 (sin FIFO ni DMA: fase 3).
 * - Estado, contador HV (aproximado) y el reloj de líneas: 262 por cuadro, VBLANK desde la 224,
 *   VINT (nivel 6) al empezar la 224 y HINT (nivel 4) por el contador del registro 10.
 *
 * Va al día de forma perezosa: vdp_sync avanza las líneas hasta el ciclo actual (después de cada
 * instrucción y antes de cada acceso del 68000 al VDP).
 */
#include <string.h>

#include "gen.h"

#define VBLANK_LINE 224

void vdp_reset(Gen *g)
{
    Vdp *v = &g->vdp;
    memset(v, 0, sizeof *v);
    v->next_line = MCLK_PER_LINE;
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
        /* Fin de la línea: el contador de HINT baja en las líneas con imagen y en la primera del
         * VBLANK; en el resto se recarga */
        if (v->line <= VBLANK_LINE) {
            if (--v->hint_counter < 0) { v->hint_counter = v->reg[10]; v->hint_pending = 1; }
        } else v->hint_counter = v->reg[10];
        if (++v->line == LINES_NTSC) { v->line = 0; v->frame++; v->frame_done = 1; }
        if (v->line == VBLANK_LINE) { v->vint_pending = 1; v->f_flag = 1; }
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

static u16 status(Gen *g)
{
    Vdp *v = &g->vdp;
    u64 pos = g->cyc * MCLK_PER_M68K - (v->next_line - MCLK_PER_LINE);   /* ciclo maestro dentro de la línea */
    u16 s = (u16)(g->open_bus & 0xFC00);
    s |= 0x0200;                                                       /* FIFO vacía (fase 3) */
    if (v->f_flag) s |= 0x80;
    if (v->line >= VBLANK_LINE || !(v->reg[1] & 0x40)) s |= 0x08;     /* VBLANK (o pantalla apagada) */
    if (pos >= 2800) s |= 0x04;                                        /* HBLANK (aproximado) */
    if (g->version & 0x40) s |= 0x01;                                  /* PAL */
    return s;
}

/* Contador HV: la V salta de $EA a $E5 en NTSC de 224 líneas; la H es aproximada (fase 3) */
static u16 hv_counter(Gen *g)
{
    Vdp *v = &g->vdp;
    u64 pos = g->cyc * MCLK_PER_M68K - (v->next_line - MCLK_PER_LINE);
    int vc = v->line <= 0xEA ? v->line : v->line - 6;
    int hc = (int)(pos * 256 / MCLK_PER_LINE);
    return (u16)((vc & 0xFF) << 8 | (hc & 0xFF));
}

static void control_write(Gen *g, u16 w)
{
    Vdp *v = &g->vdp;
    if (v->pending) {
        v->code = (u8)((v->code & 0x03) | ((w >> 2) & 0x3C));
        v->addr = (u16)((v->addr & 0x3FFF) | ((w & 3) << 14));
        v->pending = 0;
        /* CD5 con el DMA habilitado: transferencia (fase 3) */
        return;
    }
    if ((w & 0xC000) == 0x8000) {
        int r = w >> 8 & 0x1F;
        if (r < 24) v->reg[r] = (u8)w;
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
    switch (v->code & 0x0F) {
    case 0x1: {                                     /* VRAM: una dirección impar cambia el orden de los bytes */
        u16 a = v->addr;
        v->vram[a] = (u8)(w >> 8);
        v->vram[a ^ 1] = (u8)w;
        break;
    }
    case 0x3: v->cram[v->addr >> 1 & 0x3F] = w & 0x0EEE; break;
    case 0x5: if ((v->addr >> 1 & 0x3F) < 40) v->vsram[v->addr >> 1 & 0x3F] = w & 0x07FF; break;
    default: break;
    }
    v->addr = (u16)(v->addr + v->reg[15]);
}

static u16 data_read(Gen *g)
{
    Vdp *v = &g->vdp;
    u16 w;
    v->pending = 0;
    switch (v->code & 0x0F) {
    case 0x0: w = rd16(v->vram + (v->addr & 0xFFFE)); break;
    case 0x8: w = v->cram[v->addr >> 1 & 0x3F]; break;
    case 0x4: w = (v->addr >> 1 & 0x3F) < 40 ? v->vsram[v->addr >> 1 & 0x3F] : 0; break;
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
