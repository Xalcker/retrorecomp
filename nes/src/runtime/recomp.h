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
        metrica.ins_nat++;                                      \
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

/* Un paso del despachador: la función recompilada `fn` o, si es NULL, una instrucción del intérprete, con la
 * métrica (metrica.h). La entrada a una interrupción (nes_interrupt, dentro del paso) va aparte; el intérprete es
 * de ROM con el PC en $8000-$FFFF y de RAM en el resto (RAM, RAM del cartucho). Con el CPU detenido (KIL) no
 * cuenta: sus ciclos salen como «otros» */
static inline void nes_paso(NES *n, RecompFn fn)
{
    u64 c0 = n->cyc, irq0 = metrica.irq;
    u16 pc0 = n->cpu.pc;
    int parado = n->cpu.jammed;
    if (fn) { fn(n); n->cyc_recomp += n->cyc - c0; }
    else cpu_step(n);
    if (n != metrica_de || parado) return;
    u64 d = n->cyc - c0 - (metrica.irq - irq0);
    if (fn) metrica.nat += d;
    else {
        metrica.ins_int++;
        if (pc0 >= 0x8000) metrica.rom += d; else metrica.ram += d;
    }
}
extern const char recomp_rom_sha1[41];
extern const char recomp_name[];
extern const unsigned recomp_instructions;

#endif
