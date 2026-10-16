/*
 * Lo que el código generado del Z80 (z80_recomp.h, en tools/retrorecomp/shared) toma del Neo Geo: lo mismo
 * que hace z80_run_until (z80bus.c) antes y después de cada z80_step. neorecomp compila los primeros 32 KB
 * de la M1 ($0000-$7FFF, fijos; gen/zm1*.c); las ventanas con bancos y la RAM siguen en el intérprete.
 *
 * Antes de cada instrucción: se sale si se llegó al destino, los temporizadores del YM2610 al día, la INT
 * según sus banderas, y se sale si hay algo que atiende z80_step (NMI, INT con IFF1 fuera de la
 * instrucción que sigue a EI) o el Z80 quedó en HALT. Después: el reloj, la estadística y los bucles de
 * espera (z80_idle_loop), como el intérprete.
 */
#ifndef NEO_Z80_RECOMP_HOST_H
#define NEO_Z80_RECOMP_HOST_H

#include "neo.h"

#define Z80RC_CPU(g) (&(g)->z80)

#define Z80RC_BEFORE(pc_) do {                                                          \
        if (g->z80_mclk >= target) return r;                                            \
        if (g->z80_mclk >= ym_next_event(&g->ym)) ym_update(&g->ym, g->z80_mclk);       \
        z->irq = g->ym.flags != 0;                                                      \
        if (__builtin_expect(z->nmi | (z->irq & z->iff1 & !z->ei) | z->halted, 0)) return r; \
    } while (0)

/* z80_idle_loop no hace nada si no hay un bucle armado y no se saltó hacia atrás: solo se la llama si no */
#define Z80RC_AFTER(pc0_, t_) do {                                                      \
        g->z80_mclk += (u64)(t_) * MCLK_PER_Z80;                                        \
        Z80_STAT(g, native, (u64)(t_));                                                 \
        if (z->pc < (pc0_) || g->z80_loop.armed) z80_idle_loop(g, (pc0_), target);      \
    } while (0)

#endif
