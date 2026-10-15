/*
 * Desensamblador del Z80 (sintaxis de Zilog en minúsculas, números en hexadecimal con $), para smsrecomp (--dis,
 * --why, --events y --html) y para los comentarios del código generado; el de disz80.c. Como dis68k para el 68000.
 */
#ifndef DISZ80_H
#define DISZ80_H

#include <stddef.h>
#include <stdint.h>

/* Desensambla la instrucción de los bytes b[0..3] que está en `pc`. Escribe el texto en out y devuelve cuántos
 * bytes ocupa (lo mismo que el len de z80_decode) */
int disz80(const uint8_t b[4], uint16_t pc, char *out, size_t size);

#endif
