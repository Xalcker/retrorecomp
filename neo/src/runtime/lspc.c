/*
 * LSPC: los registros de $3C0000, la VRAM, el contador de líneas, el temporizador y las
 * interrupciones del 68000. El dibujo está en lspc_render.c: cada línea visible se dibuja al empezar.
 *
 *   $3C0000 VRAMADDR   dirección de la VRAM (palabras)
 *   $3C0002 VRAMRW     dato: escribir avanza la dirección VRAMMOD (sin pasar de una mitad a la otra)
 *   $3C0004 VRAMMOD    incremento con signo
 *   $3C0006 LSPCMODE   al leer: contador de líneas (bits 15-7) y de autoanimación (2-0); al escribir:
 *                      velocidad de la autoanimación (15-8), recarga del temporizador al llegar a 0 (7),
 *                      al empezar el cuadro (6) y al escribir TIMERLOW (5), interrupción del
 *                      temporizador (4), autoanimación apagada (3)
 *   $3C0008/A TIMERHIGH/TIMERLOW: valor de recarga del temporizador
 *   $3C000C IRQACK     borra las interrupciones pendientes: bit 0 la de arranque, 1 el temporizador, 2 el VBLANK
 *   $3C000E TIMERSTOP  (solo PAL)
 *
 * Los registros son de 16 bits: un byte escrito llega a las dos mitades (el 68000 lo pone en las dos).
 *
 * El temporizador es un contador de 32 bits que baja con el reloj de pixel (6 MHz: un paso cada dos
 * ciclos del 68000). No se lleva paso a paso: se guarda el ciclo en que llegará a 0 (timer_at). Desde
 * que se carga con N tarda N + 1 pixeles (el ejemplo de la wiki de NeoGeoDev con Neo Turf Masters y la
 * cuenta de MAME coinciden en el + 1).
 */
#include "neo.h"

#include <string.h>

static void update_ipl(Neo *g)
{
    u8 p = g->lspc.irq;
    g->cpu.ipl = (u8)(p & IRQ_COLD ? 3 : p & IRQ_TIMER ? 2 : p & IRQ_VBL ? 1 : 0);
}

static void timer_load(Lspc *l, u64 at)
{
    l->timer_at = at + ((u64)l->timer_reload + 1) * 2;
}

void lspc_reset(Neo *g)
{
    Lspc *l = &g->lspc;
    memset(l->vram, 0, sizeof l->vram);
    l->vram_addr = 0;
    l->vram_mod = 0;
    l->mode = 0;
    l->timer_reload = 0;
    l->timer_at = NEVER;
    l->timer_stop = 0;
    l->irq = 0;
    l->line = 0;
    l->line_start = g->cyc;
    l->vbl_done = l->reload_done = 0;
    l->anim = l->anim_wait = 0;
    l->frame_done = 0;
    l->frame = 0;
    l->next_event = 0;
    update_ipl(g);
}

/* Autoanimación: avanza cada (velocidad + 1) cuadros, en el VBLANK */
static void anim_tick(Lspc *l)
{
    if (l->anim_wait) { l->anim_wait--; return; }
    l->anim = (u8)((l->anim + 1) & 7);
    l->anim_wait = (u8)(l->mode >> 8);
}

void lspc_sync(Neo *g)
{
    Lspc *l = &g->lspc;
    for (;;) {
        u64 line_end = l->line_start + M68K_PER_LINE;
        u64 vbl = l->line == 0 && !l->vbl_done ? l->line_start + VBL_IRQ_AT : NEVER;
        u64 rel = l->line == 0 && !l->reload_done ? l->line_start + TIMER_VBL_AT : NEVER;
        u64 next = line_end;
        if (vbl < next) next = vbl;
        if (rel < next) next = rel;
        if (l->timer_at < next) next = l->timer_at;
        if (g->cyc < next) { l->next_event = next; break; }
        if (next == l->timer_at) {
            if (l->mode & 0x10) l->irq |= IRQ_TIMER;
            l->timer_at = NEVER;
            if (l->mode & 0x80) timer_load(l, next);
        } else if (next == vbl) {
            l->vbl_done = 1;
            l->irq |= IRQ_VBL;
            anim_tick(l);
        } else if (next == rel) {
            l->reload_done = 1;
            if (l->mode & 0x40) timer_load(l, next);
        } else {
            l->line_start = line_end;
            if (++l->line == LINES_PER_FRAME) {
                l->line = 0;
                l->vbl_done = l->reload_done = 0;
                l->frame++;
                l->frame_done = 1;
            } else if (l->line >= FIRST_VISIBLE_LINE)
                lspc_render_line(g, l->line - FIRST_VISIBLE_LINE);
        }
    }
    update_ipl(g);
}

/* --- Registros --- */

/* La siguiente dirección de la VRAM: el incremento no pasa de la mitad lenta a la rápida ni al revés */
static u16 vram_next(const Lspc *l)
{
    if (l->vram_addr & 0x8000) return (u16)(0x8000 | ((l->vram_addr + (u16)l->vram_mod) & 0x07FF));
    return (u16)((l->vram_addr + (u16)l->vram_mod) & 0x7FFF);
}

static u16 vram_index(u16 a) { return a & 0x8000 ? (u16)(0x8000 | (a & 0x07FF)) : a; }

u16 lspc_read(Neo *g, u32 a)
{
    Lspc *l = &g->lspc;
    switch ((a >> 1) & 3) {
    case 0: case 1: return l->vram[vram_index(l->vram_addr)];
    case 2: return (u16)l->vram_mod;
    default:
        if (g->cyc >= l->next_event) lspc_sync(g);
        return (u16)(lspc_counter(l) << 7 | l->anim);
    }
}

void lspc_write(Neo *g, u32 a, u16 v)
{
    Lspc *l = &g->lspc;
    if (g->cyc >= l->next_event) lspc_sync(g);
    switch ((a >> 1) & 7) {
    case 0: l->vram_addr = v; break;
    case 1: l->vram[vram_index(l->vram_addr)] = v; l->vram_addr = vram_next(l); break;
    case 2: l->vram_mod = (s16)v; break;
    case 3: l->mode = v; break;
    case 4: l->timer_reload = (l->timer_reload & 0xFFFF) | (u32)v << 16; break;
    case 5:
        l->timer_reload = (l->timer_reload & 0xFFFF0000u) | v;
        if (l->mode & 0x20) timer_load(l, g->cyc);
        break;
    case 6: l->irq &= (u8)~(v & 7); update_ipl(g); break;
    default: l->timer_stop = v; break;
    }
    /* El temporizador pudo cambiar: el próximo evento se recalcula */
    if (l->timer_at < l->next_event) l->next_event = l->timer_at;
}
