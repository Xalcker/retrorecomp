/*
 * Lo que el núcleo común del 68000 (tools/retrorecomp/shared: m68k.h, m68k_ops.h, cpu68k.c, el mismo de
 * NEORecomp) toma del Genesis: el tipo de la consola, su cabecera y cómo se anota la traza del intérprete.
 */
#ifndef GEN_M68K_HOST_H
#define GEN_M68K_HOST_H

#define M68K_HOST        Gen
#define M68K_HOST_HEADER "gen.h"

/* Con la traza activa: en ROM ($000000-$3FFFFF) se marca el desplazamiento de la instrucción (con las
 * ventanas de SSF2); en la RAM ($FF0000-$FFFFFF) la anota quien puso m68k_ram_trace (gen_ram_note) */
#define M68K_TRACE(g, pc) do {                                                                       \
        if ((pc) < 0x400000) {                                                                       \
            u32 off_ = cart_rom_offset(&(g)->cart, (pc));                                            \
            if ((g)->cart.mapper == MAPPER_NONE) off_ &= (g)->cart.rom_mask;                         \
            if (off_ < (g)->cart.rom_size && !(g)->trace[off_ >> 1])                                 \
                (g)->trace[off_ >> 1] = 0x80000000u | (pc);                                          \
        } else if (((pc) & 0xFF0000) == 0xFF0000 && m68k_ram_trace) {                                \
            m68k_ram_trace((g), (pc));                                                               \
        }                                                                                            \
    } while (0)

#endif
