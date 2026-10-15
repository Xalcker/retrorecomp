/*
 * Lo que el código generado del Z80 (z80_recomp.h, en tools/retrorecomp/shared) toma del Master System y el Game
 * Gear: lo mismo que sms_recomp_step y sms_step (sms.c) antes y después de cada instrucción.
 *
 * Antes: se sale si terminó el cuadro, o si hay algo que atiende z80_step (la NMI de la Pausa, una INT con IFF1
 * fuera de la instrucción que sigue a EI, o el HALT). Después: el reloj, la estadística y el VDP al día
 * (sms_after_step: líneas, contadores, interrupciones); y se sale si la instrucción cambió el mapeo (MAPCHK: la
 * función se compiló para una página en una ranura). Z80RC_BYTE, para la guarda del código de la RAM: lo que lee el
 * Z80 en a (bus_peek: la RAM, su espejo o la del cartucho).
 */
#ifndef SMS_Z80_RECOMP_HOST_H
#define SMS_Z80_RECOMP_HOST_H

#include "recomp.h"

#define Z80RC_CPU(g) (&(g)->z80)
#define Z80RC_BYTE(a) bus_peek(g, (a))

#define Z80RC_BEFORE(pc_) do {                                                          \
        if (g->vdp.frame_done) return r;                                                \
        if (__builtin_expect(z->nmi | (z->irq & z->iff1 & !z->ei) | z->halted, 0)) return r; \
    } while (0)

#define Z80RC_AFTER(pc0_, t_) do {                                                      \
        g->cyc += (u64)(t_);                                                            \
        Z80_STAT(g, native, (u64)(t_));                                                 \
        sms_after_step(g);                                                              \
        if (__builtin_expect(g->map.map_ver != sms_rc_map_ver, 0)) return r;            \
    } while (0)

#endif
