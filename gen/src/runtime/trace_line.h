/*
 * Una línea de traza, el formato común de NESRecomp, GBRecomp y GENRecomp (este archivo es idéntico
 * en los tres): "BANCO:DIRECCION" en hexadecimal.
 *   banco      desplazamiento en la ROM >> shift, en ventanas fijas de cada consola:
 *              GB 14 (bancos de 16 KB), NES 13 (8 KB del PRG), Genesis 19 (512 KB, las del mapper de SSF2)
 *   dirección  la del CPU con que se ejecutó
 *   desplazamiento = banco << shift | (dirección & (ventana - 1))
 * Si la dirección no cae en la misma posición de su ventana que el desplazamiento (no pasa con los
 * mappers conocidos), la línea va en el formato viejo de NES y Genesis, "DESPLAZAMIENTO:DIRECCION"
 * con 6 dígitos o más; trace_line_read lee los dos: un primer campo de 5 dígitos o más es un desplazamiento.
 */
#ifndef TRACE_LINE_H
#define TRACE_LINE_H

#include <stdio.h>
#include <stdlib.h>

/* 1 si la línea es una instrucción de la traza; deja su desplazamiento en la ROM y su dirección */
static inline int trace_line_read(const char *line, unsigned shift, unsigned long *off, unsigned long *addr)
{
    char *end;
    while (*line == ' ' || *line == '\t') line++;
    unsigned long first = strtoul(line, &end, 16);
    long digits = end - line;
    if (digits == 0 || *end != ':') return 0;
    const char *p = end + 1;
    unsigned long a = strtoul(p, &end, 16);
    if (end == p) return 0;
    *addr = a;
    *off = digits >= 5 ? first : first << shift | (a & ((1ul << shift) - 1));
    return 1;
}

/* Escribe una línea; addr_digits: 4 en GB y NES, 6 en Genesis */
static inline void trace_line_write(FILE *f, unsigned shift, int addr_digits, unsigned long off, unsigned long addr)
{
    unsigned long mask = (1ul << shift) - 1;
    if ((off & mask) == (addr & mask)) fprintf(f, "%02lX:%0*lX\n", off >> shift, addr_digits, addr);
    else fprintf(f, "%06lX:%0*lX\n", off, addr_digits, addr);
}

#endif
