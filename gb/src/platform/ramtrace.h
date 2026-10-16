/*
 * Código que el intérprete ejecuta desde WRAM o HRAM, para el ciclo de trazas
 * (--uncovered → gbrecomp --trace). Lo comparten difftest, gbtrace y el ejecutable del juego
 * (main_sdl.c), para que las tres herramientas escriban exactamente las mismas líneas.
 *
 * Las líneas "ram DIRECCION B0 [B1 B2]" y cómo se anotan son las de ram_code.h (común en ../shared,
 * el mismo de las otras consolas; ahí está el porqué). Aquí va lo del Game Boy: qué RAM se anota
 * (WRAM $C000-$DFFF y HRAM $FF80-$FFFE), cuánto mide cada instrucción y qué bytes la identifican.
 *
 * Por qué también en el ejecutable: las entradas aleatorias de difftest no llegan a todo el
 * juego, y jugar con --uncovered es como se completa la cobertura. Si el ejecutable solo
 * anotara ROM, justo lo que no se puede deducir se perdería al jugar.
 */
#ifndef RAMTRACE_H
#define RAMTRACE_H

#include <stdio.h>
#include <string.h>

#include "gb.h"
#include "ram_code.h"

static inline int insn_length(u8 op)
{
    switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x08:
    case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA:
    case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC:
    case 0xEA: case 0xFA:
        return 3;
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
    case 0xE0: case 0xF0: case 0xE8: case 0xF8: case 0xCB: case 0x10:
        return 2;
    default:
        return 1;
    }
}

/* Cuántos bytes de una instrucción identifican su código (la clave de ram_code.h). El resto son
 * operandos que pueden cambiar sin cambiar lo que la instrucción hace: el destino de un jp, un valor
 * inmediato. En jr el desplazamiento y en CB el segundo byte sí cambian el código */
static inline int ram_key_len(u8 op)
{
    switch (op) {
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0xCB: return 2;
    default: return 1;
    }
}

/* WRAM y HRAM: las dos zonas de las que gbrecomp compila código (ram_ptr en gen/ram.c) */
static RcTable ram_wram, ram_hram;

/* ¿Se anota lo que corre en pc? */
static inline int ramtrace_here(u16 pc)
{
    return (pc >= 0xC000 && pc < 0xE000) || (pc >= 0xFF80 && pc < 0xFFFF);
}

/* Anota la instrucción que el intérprete va a ejecutar en pc (WRAM o HRAM; lo demás no se aprende) */
static inline void ramtrace_observe(GB *g, u16 pc)
{
    if (!ramtrace_here(pc)) return;
    RcTable *t = pc < 0xE000 ? &ram_wram : &ram_hram;
    if (!t->slot && !(pc < 0xE000 ? rc_init(t, 0xC000, 0x2000) : rc_init(t, 0xFF80, 0x7F))) return;
    u8 b[3];
    int len = insn_length(gb_peek(g, pc));
    for (int i = 0; i < len; i++) b[i] = gb_peek(g, (u16)(pc + i));
    rc_observe(t, pc, b, (unsigned)len, (unsigned)ram_key_len(b[0]));
}

/* Escribe en f (si no es NULL) las líneas "ram" de lo anotado, en orden de dirección, y devuelve el
 * resumen para difftest y gbtrace */
static inline RcSummary ramtrace_write(FILE *f)
{
    if (f) { rc_write(&ram_wram, f, 4); rc_write(&ram_hram, f, 4); }
    RcSummary a = rc_summary(&ram_wram), b = rc_summary(&ram_hram);
    a.fixed += b.fixed; a.vol += b.vol; a.multi += b.multi; a.lost += b.lost;
    return a;
}

#endif
