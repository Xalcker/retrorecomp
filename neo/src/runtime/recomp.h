/*
 * Interfaz entre el runtime y el código que genera neorecomp (la de GENRecomp).
 *
 * Cada función generada cubre un tramo contiguo de instrucciones de ROM del juego o de la BIOS.
 * Empieza con un switch sobre el PC (se puede entrar en cualquier instrucción; si el PC no es de la
 * función devuelve 0 y el intérprete ejecuta una instrucción) y tiene una etiqueta por instrucción.
 * Cada instrucción llama a su k_* de m68k_ops.h con el opcode, las palabras de extensión y el PC
 * constantes: hace los mismos accesos al bus, en el mismo orden y con los mismos ciclos que el
 * intérprete.
 *
 * Después de cada instrucción, como el intérprete: el LSPC y el Z80 se ponen al día
 * (neo_after_step_inline). Se sale de la función (y el intérprete sigue) si terminó el cuadro, si hay
 * una interrupción que atender, con el modo traza, con STOP, si el destino no es de la función o, en
 * las funciones de la ventana P2 de los juegos con bancos, si cambió el banco. El PC y la cola de
 * prefetch (ir, irc) solo se rehacen al salir.
 *
 * Lo incluyen la ventana, bench y difftest (solo las declaraciones de abajo) y el código generado
 * (gen/gen.h y bios/bios.h, que antes incluyen m68k_ops.h).
 */
#ifndef RECOMP_H
#define RECOMP_H

#include "neo.h"

#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* 0: el PC no es de la función (no se ejecutó nada); 1: corrió y salió; 2: corrió y terminó el cuadro */
typedef int (*RecompFn)(Neo *g);

/* Sale de la función generada con el 68000 en `pc_` */
#define LEAVE(pc_) do { m68k_leave(g, (pc_)); return 1; } while (0)

/* Fin de una instrucción que sigue en npc: LSPC y Z80 al día; ¿hay que volver al despachador? */
#define AFTER(npc_) do {                                                            \
        neo_after_step_inline(g);                                                   \
        if (UNLIKELY(g->lspc.frame_done)) { m68k_leave(g, (npc_)); return 2; }      \
        if (UNLIKELY((c->ipl > c->ipm) | c->nmi_edge | c->t | c->stopped)) LEAVE(npc_); \
    } while (0)

/* Tras una instrucción de la ventana P2 en un juego con bancos: si cambió el banco, el código que sigue
 * es otro */
#define MAPCHK(npc_) do {                                                           \
        if (UNLIKELY(g->map_ver != ver0)) LEAVE(npc_);                              \
    } while (0)

/* PC y cola de prefetch al salir del código generado */
static inline void m68k_leave(Neo *g, u32 pc)
{
    M68k *c = &g->cpu;
    c->pc = pc;
    c->ir = m68k_peek16(g, pc & 0xFFFFFF);
    c->irc = m68k_peek16(g, (pc + 2) & 0xFFFFFF);
}

/* Generado por neorecomp (gen/dispatch.c, gen/info.c, bios/bios.c) */
RecompFn recomp_lookup(Neo *g, u32 pc);
RecompFn recomp_bios_lookup(Neo *g, u32 off);      /* off: desplazamiento en la BIOS */
extern const char recomp_rom_sha1[41];
extern const char recomp_name[];
extern const unsigned recomp_instructions;
extern const unsigned recomp_bios_count;           /* versiones de la BIOS recompiladas */
extern const char *const recomp_bios_sha1[];

#endif
