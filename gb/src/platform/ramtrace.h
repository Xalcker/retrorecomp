/*
 * Código que el intérprete ejecuta desde WRAM o HRAM, para el ciclo de trazas
 * (--uncovered → gbrecomp --trace). Lo comparten difftest y el ejecutable del juego
 * (main_sdl.c), para que las dos herramientas escriban exactamente las mismas líneas.
 *
 * Por qué existe: el código que un juego copia a RAM (manejadores de interrupción,
 * trampolines de cambio de banco, rutinas que se parchan a sí mismas) no está en la ROM en
 * la dirección donde corre. El descubrimiento estático sigue saltos desde la ROM y no puede
 * llegar ahí, y deducir qué bytes habrá en RAM exigiría emular la copia. Quien sí lo ve es el
 * intérprete: lee de RAM los bytes que ejecuta. Se apuntan y gbrecomp los compila con una
 * guarda que comprueba que la RAM siga teniendo esos bytes (emit_ram en gbrecomp.c, y «Código
 * que el juego copia a WRAM o HRAM» en docs/ARQUITECTURA.md).
 *
 * Por qué también en el ejecutable: las entradas aleatorias de difftest no llegan a todo el
 * juego, y jugar con --uncovered es como se completa la cobertura. Si el ejecutable solo
 * anotara ROM, justo lo que no se puede deducir se perdería al jugar.
 *
 * Formato, una línea por versión de cada instrucción: "ram DIRECCION B0 [B1 B2]" en
 * hexadecimal, con ?? en los bytes de operando que cambiaron entre ejecuciones. No empieza con
 * un dígito hexadecimal: los lectores de trazas "BB:AAAA" la ignoran (incluidos los de las
 * otras consolas, que comparten ese formato).
 *
 * Qué identifica una instrucción: su opcode (ram_key_len); en los saltos relativos y los CB, el
 * segundo byte también, porque cambia lo que hace. Los demás bytes son operandos que pueden
 * cambiar sin cambiar el código (el destino de un jp, un valor inmediato que el juego parcha).
 * De una dirección se guardan hasta RAM_GROUPS versiones; con más, es código que el juego
 * genera con estructuras distintas cada vez y no se aprende.
 */
#ifndef RAMTRACE_H
#define RAMTRACE_H

#include <stdio.h>
#include <string.h>

#include "gb.h"

#define RAM_BYTES 8      /* bytes de la primera vez que muestra difftest para reconocer una rutina */
#define RAM_GROUPS 4     /* versiones de opcode por dirección que se guardan */

typedef struct { u32 n; u8 bytes[3]; u8 vol; } RamGroup;      /* vol: bit i = el byte i es variable */
typedef struct {
    u32 n;                   /* ejecuciones */
    u8 ngroups;
    u8 overflow;             /* más versiones de opcode que RAM_GROUPS: no se aprende */
    u8 first[RAM_BYTES];     /* los bytes la primera vez */
    RamGroup g[RAM_GROUPS];
} RamHit;

/* Una entrada por dirección de $8000 a $FFFF */
static RamHit ram_hits[0x8000];

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

/* Cuántos bytes de una instrucción identifican su código. El resto son operandos que pueden
 * cambiar sin cambiar lo que la instrucción hace: el destino de un jp, un valor inmediato. En jr
 * el desplazamiento y en CB el segundo byte sí cambian el código */
static inline int ram_key_len(u8 op)
{
    switch (op) {
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0xCB: return 2;
    default: return 1;
    }
}

/* Apunta la instrucción que el intérprete va a ejecutar en pc ($8000 o más): cuántas veces, sus
 * bytes y cuáles de sus operandos cambian de una ejecución a otra */
static inline void ramtrace_observe(GB *g, u16 pc)
{
    RamHit *h = &ram_hits[pc - 0x8000];
    u8 cur[3];
    for (int i = 0; i < 3; i++) cur[i] = gb_peek(g, (u16)(pc + i));
    if (!h->n)
        for (unsigned i = 0; i < RAM_BYTES; i++) h->first[i] = gb_peek(g, (u16)(pc + i));
    int len = insn_length(cur[0]), key = ram_key_len(cur[0]);
    unsigned v;
    for (v = 0; v < h->ngroups; v++)
        if (memcmp(h->g[v].bytes, cur, (size_t)key) == 0) break;
    if (v == h->ngroups) {
        if (v < RAM_GROUPS) {
            memcpy(h->g[v].bytes, cur, sizeof cur);
            h->ngroups++;
        } else {
            h->overflow = 1;
        }
    }
    if (v < h->ngroups) {
        RamGroup *gr = &h->g[v];
        for (int i = key; i < len; i++)
            if (cur[i] != gr->bytes[i]) gr->vol |= (u8)(1u << i);
        gr->n++;
    }
    h->n++;
}

/* Cuántas direcciones de WRAM y HRAM hay de cada clase (para el resumen de difftest) */
typedef struct { unsigned fixed, vol, multi, lost; } RamSummary;

/* Escribe en f (si no es NULL) una línea "ram DIRECCION BYTES" por versión de cada instrucción
 * de WRAM ($C000-$DFFF) o HRAM ($FF80-$FFFE) que se apuntó, en orden de dirección */
static inline RamSummary ramtrace_write(FILE *f)
{
    RamSummary s = { 0, 0, 0, 0 };
    for (u32 a = 0xC000; a < 0x10000; a++) {
        if (a >= 0xE000 && a < 0xFF80) continue;
        if (a == 0xFFFF) continue;
        const RamHit *h = &ram_hits[a - 0x8000];
        if (!h->n) continue;
        if (h->overflow) {
            s.lost++;
            continue;
        }
        if (h->ngroups > 1) s.multi++;
        else if (h->g[0].vol) s.vol++;
        else s.fixed++;
        if (!f) continue;
        for (unsigned v = 0; v < h->ngroups; v++) {
            const RamGroup *gr = &h->g[v];
            int len = insn_length(gr->bytes[0]);
            fprintf(f, "ram %04X", (unsigned)a);
            for (int i = 0; i < len; i++) {
                if (gr->vol & (1u << i)) fprintf(f, " ??");
                else fprintf(f, " %02X", gr->bytes[i]);
            }
            fprintf(f, "\n");
        }
    }
    return s;
}

#endif
