/*
 * Lo que el código generado del Z80 (z80_recomp.h, en tools/retrorecomp/shared) toma del Genesis: lo mismo
 * que hace z80_run_until (z80bus.c) antes y después de cada z80_step. genrecomp compila el driver que el
 * 68000 sube a los 8 KB de RAM del Z80 (gen/zr*.c, una vista por cada versión que vio el sondeo), con una
 * guarda: cada instrucción compara antes sus bytes con la RAM y, si cambiaron, la ejecuta el intérprete.
 *
 * Antes de cada instrucción: se sale si se llegó al destino, la INT según la ventana que fija el VDP, y se
 * sale si hay una INT que atiende z80_step (con IFF1, fuera de la instrucción que sigue a EI) o el Z80
 * quedó en HALT. El reset y el pedido del bus no cambian mientras el Z80 corre (los toca el 68000).
 * Después: el reloj y la estadística, como el intérprete; las esperas al usar el bus del 68000 las suma
 * z80_read, igual que con el intérprete.
 */
#ifndef GEN_Z80_RECOMP_HOST_H
#define GEN_Z80_RECOMP_HOST_H

#include "gen.h"

#define Z80RC_CPU(g) (&(g)->z80)
#define Z80RC_BYTE(a) (g->zram[(a) & 0x1FFF])

#define Z80RC_BEFORE(pc_) do {                                                          \
        if (g->z80_mclk >= target) return r;                                            \
        z->irq = g->z80_mclk >= g->z80_irq_from && g->z80_mclk < g->z80_irq_until;      \
        if (__builtin_expect(z->nmi | (z->irq & z->iff1 & !z->ei) | z->halted, 0)) return r; \
    } while (0)

#define Z80RC_AFTER(pc0_, t_) do {                                                      \
        g->z80_mclk += (u64)(t_) * MCLK_PER_Z80;                                        \
        Z80_STAT(g, native, (u64)(t_));                                                 \
    } while (0)

#endif
