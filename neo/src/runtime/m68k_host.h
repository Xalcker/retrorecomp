/*
 * Lo que el núcleo común del 68000 (../shared: m68k.h, m68k_ops.h, cpu68k.c, el mismo de
 * GENRecomp) toma del Neo Geo: el tipo de la consola, su cabecera y cómo se anota la traza del intérprete.
 */
#ifndef NEO_M68K_HOST_H
#define NEO_M68K_HOST_H

#define M68K_HOST        Neo
#define M68K_HOST_HEADER "neo.h"

/* Para la métrica (metrica.h): el intérprete corre ROM si en el PC hay programa (P1, el banco de P2) o BIOS */
#define M68K_PC_EN_ROM(g, pc) (neo_code_offset((g), (pc)) >= 0)

/* Con la traza activa: P1, los bancos de P2 y la BIOS los marca neo_trace_mark; la RAM ($100000-$10FFFF)
 * la anota quien puso m68k_ram_trace */
#define M68K_TRACE(g, pc) do {                                                                       \
        neo_trace_mark((g), (pc));                                                                   \
        if (((pc) >> 16) == 0x0010 && m68k_ram_trace) m68k_ram_trace((g), (pc));                     \
    } while (0)

#endif
