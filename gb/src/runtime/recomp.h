/*
 * Interfaz entre el runtime y el código que genera gbrecomp.
 *
 * Cada función generada cubre un tramo de instrucciones de un banco. Tiene un
 * switch de entrada (se puede entrar en cualquier instrucción) y una etiqueta
 * por instrucción; al inicio de cada una, CHECK revisa si el hardware tiene
 * un evento pendiente (interrupción, fin de corrida) y, si hace falta, regresa
 * al despachador con c->pc apuntando a esa instrucción, aún sin ejecutar.
 */
#ifndef RECOMP_H
#define RECOMP_H

#include "cpu_ops.h"

#define CHECK(pc_) do {                                         \
        if (UNLIKELY(g->cyc >= g->next_event)) {                \
            c->pc = (pc_);                                      \
            if (gb_check(g)) return;                            \
        }                                                       \
        g->instr_recomp++;                                      \
    } while (0)

/* Generado por gbrecomp (gen/dispatch.c y gen/recomp_info.c) */
RecompFn recomp_lookup(GB *g, u16 pc);
extern const char recomp_rom_sha1[41];
extern const char recomp_rom_title[];
extern const char recomp_name[];          /* nombre del proyecto (carpeta de datos) */
extern const unsigned recomp_instructions;   /* instrucciones recompiladas */

#endif
