/*
 * El hash de la línea base exacta (tests/golden.sh, en shared/golden.sh): FNV-1a de 64 bits de todos los cuadros
 * y de todo el audio de una corrida, impreso como "hash imagen X audio Y". Lo usan las herramientas *shot con
 * --hash y el bench del proyecto generado con --hash: si el código recompilado hace lo mismo que el intérprete, el
 * hash es el mismo y la misma línea base vale para los dos.
 */
#ifndef GOLDEN_HASH_H
#define GOLDEN_HASH_H

#include <stdio.h>
#include <stddef.h>

typedef struct { unsigned long long img, aud; } GoldenHash;

static inline void golden_init(GoldenHash *h) { h->img = h->aud = 0xCBF29CE484222325ULL; }

static inline void golden_fnv(unsigned long long *h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { *h ^= p[i]; *h *= 0x100000001B3ULL; }
}

static inline void golden_print(const GoldenHash *h)
{
    printf("hash imagen %016llx audio %016llx\n", h->img, h->aud);
}

#endif
