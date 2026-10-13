/*
 * Decodificador, desensamblador y emisor de C para el SM83.
 *
 * Es la única fuente de verdad sobre qué hace cada opcode y en qué M-ciclo
 * ocurre cada acceso a memoria. Lo usan:
 *   - gen_interp: emite el switch del intérprete (operandos leídos en tiempo
 *     de ejecución, PC dinámico);
 *   - gbrecomp: emite el C de cada instrucción descubierta (operandos y PC
 *     constantes).
 * Así el intérprete y el código recompilado no pueden discrepar en semántica
 * ni en temporización.
 */
#ifndef SM83_EMIT_H
#define SM83_EMIT_H

#include <stdint.h>
#include <stdio.h>

typedef enum {
    INS_NORMAL,     /* sigue a la siguiente instrucción */
    INS_JP,         /* jp nn / jp cc,nn */
    INS_JR,         /* jr e / jr cc,e */
    INS_CALL,       /* call nn / call cc,nn */
    INS_RST,        /* rst n */
    INS_RET,        /* ret / ret cc */
    INS_RETI,
    INS_JP_HL,
    INS_HALT,
    INS_STOP,
    INS_ILLEGAL,
} InsKind;

typedef struct {
    uint8_t len;        /* bytes */
    uint8_t kind;       /* InsKind */
    uint8_t cond;       /* 0 = incondicional; 1 NZ, 2 Z, 3 NC, 4 C */
    uint16_t target;    /* destino constante (JP, JR, CALL, RST) */
} Insn;

/* Decodifica la instrucción en `addr` (bytes[0..2] deben ser legibles). */
void sm83_decode(const uint8_t *bytes, uint16_t addr, Insn *out);

/* Texto en sintaxis de RGBDS, p. ej. "ld a, [$FF44]" */
void sm83_disasm(const uint8_t *bytes, uint16_t addr, char *buf, size_t size);

/* La instrucción termina el flujo secuencial (no hay "caída" a la siguiente) */
int sm83_ends_block(const Insn *in);

typedef struct Emit Emit;
struct Emit {
    FILE *f;
    int recomp;             /* 0 = intérprete, 1 = código recompilado */
    const char *indent;
    /* Solo recompilado: */
    uint16_t addr;          /* dirección de la instrucción */
    uint8_t b[3];           /* sus bytes */
    /*
     * Transferencia de control incondicional (ya pagados todos los ciclos).
     * `expr` es una expresión C con el destino; si is_const, `target` es su
     * valor. En el intérprete basta con asignar c->pc.
     */
    void (*jump)(Emit *e, const char *expr, int is_const, uint16_t target);
    void *user;
    /*
     * Solo recompilado: si no es NULL, expresión C que sustituye al operando inmediato de
     * 8 bits de la instrucción (código que el juego copia a RAM y puede modificar después).
     */
    const char *imm8_expr;
    /* Igual para el operando inmediato de 16 bits; un jp o call con ese operando salta a un
     * destino que solo se conoce al ejecutar */
    const char *imm16_expr;
};

/* Emite el cuerpo C de una instrucción. En modo intérprete, `op` es el
 * opcode (ya leído) y para $CB se emite un switch completo; en modo
 * recompilado se usa e->b. */
void emit_insn(Emit *e, uint8_t op);

/* Emite el printf con indentación */
void emit_line(Emit *e, const char *fmt, ...);

#endif
