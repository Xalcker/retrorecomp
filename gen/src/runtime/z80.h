/*
 * Z80: estado y lo que pide del bus. En la Mega Drive corre el programa de sonido desde su RAM de
 * 8 KB; siempre va interpretado (z80.c).
 */
#ifndef Z80_H
#define Z80_H

#include <stdint.h>

struct Gen;

typedef struct {
    uint8_t  a, f, b, c, d, e, h, l;
    uint16_t af_, bc_, de_, hl_;        /* registros alternos */
    uint16_t ix, iy, sp, pc;
    uint16_t wz;                        /* MEMPTR: registro interno (se ve en las banderas X/Y de BIT n,(HL)) */
    uint8_t  i, r;
    uint8_t  iff1, iff2, im;
    uint8_t  halted;
    uint8_t  ei;                        /* la instrucción anterior fue EI: no se atiende una interrupción todavía */
    uint8_t  q;                         /* F si la instrucción anterior cambió las banderas, si no 0 (SCF/CCF) */
    uint8_t  p;                         /* la instrucción anterior fue LD A,I o LD A,R */
    uint8_t  irq;                       /* línea INT (nivel) */
} Z80;

/* Bus: lo implementa la consola (z80bus.c) o z80test */
uint8_t z80_read(struct Gen *g, uint16_t addr);
void    z80_write(struct Gen *g, uint16_t addr, uint8_t v);
uint8_t z80_in(struct Gen *g, uint16_t port);
void    z80_out(struct Gen *g, uint16_t port, uint8_t v);

void z80_reset(Z80 *z);
int  z80_step(struct Gen *g, Z80 *z);   /* una instrucción (o la interrupción pendiente); ciclos T */

#endif
