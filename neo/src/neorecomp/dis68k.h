/*
 * Desensamblador del 68000 (sintaxis de Motorola en minúsculas, números en hexadecimal con $), para
 * neorecomp --dis, --why, --events y --html y para los comentarios del código generado.
 */
#ifndef DIS68K_H
#define DIS68K_H

#include <stddef.h>
#include <stdint.h>

/* Desensambla la instrucción cuyas palabras son w[0..] (opcode y extensión) y que está en `pc`.
 * Escribe el texto en out y devuelve cuántas palabras ocupa. */
int dis68k(const uint16_t *w, uint32_t pc, char *out, size_t size);

#endif
