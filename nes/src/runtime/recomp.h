/*
 * Interfaz entre el runtime y el código que genera nesrecomp.
 *
 * Cada función generada cubre un tramo de instrucciones de PRG. Empieza con un
 * switch sobre c->pc (se puede entrar en cualquier instrucción; si el PC no
 * coincide con ninguna, el intérprete ejecuta una instrucción) y tiene una
 * etiqueta por instrucción. El PC solo se actualiza al salir de la función o
 * al atender una interrupción.
 *
 * Cada acceso a memoria cuenta un ciclo igual que en el intérprete. Las
 * lecturas de ROM (opcode, operandos) y las lecturas falsas sin efecto
 * (RAM, ROM) se vuelven TICK(): el ciclo pasa pero no se toca el bus.
 */
#ifndef RECOMP_H
#define RECOMP_H

#include "cpu_ops.h"

#define UNLIKELY(x) __builtin_expect(!!(x), 0)

typedef void (*RecompFn)(NES *n);

#define TICK()      nes_cpu_tick(n)
#define TICKS(k)    nes_cpu_ticks(n, (k))
#define RD(a)       nes_cpu_read(n, (u16)(a))
#define WR(a, v)    nes_cpu_write(n, (u16)(a), (u8)(v))

static inline void push(NES *n, u8 v) { WR(0x100 | n->cpu.sp, v); n->cpu.sp--; }
static inline u8 pull(NES *n) { n->cpu.sp++; return RD(0x100 | n->cpu.sp); }

/* ¿Terminó el cuadro? Se revisa al empezar cada instrucción. */
#define FRAME(pc_) do {                                         \
        if (UNLIKELY(n->ppu.frame_done)) { c->pc = (pc_); return; } \
    } while (0)

/* Fin de una instrucción que sigue en 'pc_': ¿hay interrupción lista? */
#define ICHK(pc_) do {                                          \
        if (UNLIKELY(n->prev_need_nmi | n->prev_run_irq)) {     \
            c->pc = (pc_); nes_interrupt(n, 0); return;         \
        }                                                       \
    } while (0)

/* Fin de una instrucción de salto a 'pc_' fuera de la función */
#define LEAVE(pc_) do {                                         \
        c->pc = (pc_);                                          \
        if (UNLIKELY(n->prev_need_nmi | n->prev_run_irq)) nes_interrupt(n, 0); \
        return;                                                 \
    } while (0)

/* Tras una escritura que pudo cambiar el mapeo de PRG: salir al despachador */
#define MAPCHK(pc_) do {                                        \
        if (UNLIKELY(n->cart.map_ver != ver0)) { c->pc = (pc_); return; } \
    } while (0)

/* Generado por nesrecomp (gen/dispatch.c y gen/info.c) */
RecompFn recomp_lookup(NES *n, u16 pc);
extern const char recomp_rom_sha1[41];
extern const char recomp_name[];
extern const unsigned recomp_instructions;

#endif
