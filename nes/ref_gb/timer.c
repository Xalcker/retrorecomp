/*
 * Timer del DMG: un contador interno de 16 bits que avanza cada T-ciclo
 * (DIV son sus 8 bits altos). TIMA aumenta en el flanco de bajada del bit que
 * elige TAC, combinado con el bit de habilitación; por eso escribir en DIV o
 * en TAC puede generar un incremento "fantasma", igual que en el hardware.
 *
 * Al desbordarse, TIMA queda en $00 durante un M-ciclo; en el siguiente se
 * recarga con TMA y se pide la interrupción. Durante el M-ciclo de recarga,
 * escribir en TIMA no tiene efecto y escribir en TMA también cambia TIMA.
 */
#include "gb.h"

static const u16 tac_bit[4] = { 1u << 9, 1u << 3, 1u << 5, 1u << 7 };

/* Entrada del detector de flanco para un contador y TAC dados */
static int timer_input(u16 sys, u8 tac)
{
    return (tac & 4) && (sys & tac_bit[tac & 3]);
}

static void tima_increment(Timer *t)
{
    if (++t->tima == 0)
        t->state = TIMA_OVERFLOW;
}

/* El bit 12 del contador (bit 4 de DIV) marca el ritmo del secuenciador del APU */
static void check_apu(GB *g, u16 old, u16 now)
{
    /* En doble velocidad DIV corre al doble: se usa el bit 13 para seguir a 512 Hz */
    u16 bit = g->double_speed ? 0x2000 : 0x1000;
    if ((old & bit) && !(now & bit))
        apu_div_event(g);
}

void timer_reset(GB *g)
{
    Timer *t = &g->timer;
    t->sys = 0xABCC;    /* contador al llegar a $0100 en DMG (boot_div de Mooneye) */
    if (g->sgb) {
        /* La boot ROM del SGB manda la cabecera ($104-$14F) a la SNES y cada
         * bit en 0 tarda un M-ciclo más (boot_div-S y boot_div2-S) */
        int zeros = 0;
        for (int a = 0x104; a < 0x150; a++)
            for (int b = 0; b < 8; b++) zeros += !((g->rom[a] >> b) & 1);
        t->sys = (u16)(0xD308 + zeros * 4);
    }
    if (g->cgb_hw && !g->cgb)
        t->sys = 0x2678;    /* GBC con un juego de DMG (boot_div-cgbABCDE) */
    t->tima = 0;
    t->tma = 0;
    t->tac = 0xF8;
    t->state = TIMA_NORMAL;
}

void timer_mcycle(GB *g)
{
    Timer *t = &g->timer;

    if (t->state == TIMA_RELOADING) {
        t->state = TIMA_NORMAL;
    } else if (t->state == TIMA_OVERFLOW) {
        t->tima = t->tma;
        IF_REG(g) |= INT_TIMER;
        t->state = TIMA_RELOADING;
    }

    u16 old = t->sys;
    t->sys = (u16)(old + 4);
    if (timer_input(old, t->tac) && !timer_input(t->sys, t->tac))
        tima_increment(t);
    check_apu(g, old, t->sys);
}

u8 timer_read(GB *g, u8 reg)
{
    Timer *t = &g->timer;
    switch (reg) {
    case IO_DIV:  return (u8)(t->sys >> 8);
    case IO_TIMA: return t->tima;
    case IO_TMA:  return t->tma;
    default:      return (u8)(t->tac | 0xF8);
    }
}

void timer_write(GB *g, u8 reg, u8 v)
{
    Timer *t = &g->timer;
    switch (reg) {
    case IO_DIV: {
        u16 old = t->sys;
        if (timer_input(old, t->tac))
            tima_increment(t);
        t->sys = 0;
        check_apu(g, old, 0);
        break;
    }
    case IO_TIMA:
        if (t->state == TIMA_RELOADING) break;     /* gana TMA */
        if (t->state == TIMA_OVERFLOW) t->state = TIMA_NORMAL;  /* se cancela */
        t->tima = v;
        break;
    case IO_TMA:
        t->tma = v;
        if (t->state == TIMA_RELOADING) t->tima = v;
        break;
    default: {
        u8 old = t->tac;
        t->tac = (u8)(v | 0xF8);
        if (timer_input(t->sys, old) && !timer_input(t->sys, t->tac))
            tima_increment(t);
        break;
    }
    }
}

/* Primer ciclo en que el timer podría pedir una interrupción */
u64 timer_next_event(GB *g)
{
    Timer *t = &g->timer;
    if (t->state != TIMA_NORMAL) return g->hw_cyc + 4;
    if (!(t->tac & 4)) return UINT64_MAX;
    u32 period = (u32)tac_bit[t->tac & 3] * 2;
    u32 first = period - (t->sys & (period - 1));
    u32 total = first + (u32)(255 - t->tima) * period;
    return g->hw_cyc + total;
}
