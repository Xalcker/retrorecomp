/*
 * Intérprete del SM83. El switch de opcodes lo genera gen_interp con el mismo
 * emisor que produce el código recompilado, así que ambos tienen exactamente
 * la misma semántica y la misma temporización por M-ciclo.
 */
#include "cpu_ops.h"

void gb_interp_step(GB *g)
{
    Cpu *c = &g->cpu;
    u8 op;

    if (g->trace) g->trace(g);

    if (UNLIKELY(c->halt_bug)) {
        /* Bug de HALT: el byte siguiente se lee sin avanzar PC */
        c->halt_bug = 0;
        op = RD(c->pc);
    } else {
        op = FETCH8();
    }
    g->instr_interp++;

    switch (op) {
#include "interp_ops.inc"
    }
}
