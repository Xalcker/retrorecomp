/*
 * Z80: estado y lo que pide del bus. En el Neo Geo corre el programa de sonido desde la M1 ROM del
 * cartucho (con bancos) y 2 KB de RAM; va interpretado (z80.c). El mismo de GENRecomp, más la NMI.
 */
#ifndef Z80_H
#define Z80_H

#include <stdint.h>

struct Neo;

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
    uint8_t  nmi;                       /* NMI pendiente (por flanco: z80_step la atiende y la borra) */
} Z80;

/* Bus: lo implementa la consola (z80bus.c) o z80test */
uint8_t z80_read(struct Neo *g, uint16_t addr);
void    z80_write(struct Neo *g, uint16_t addr, uint8_t v);
uint8_t z80_in(struct Neo *g, uint16_t port);
void    z80_out(struct Neo *g, uint16_t port, uint8_t v);

void z80_reset(Z80 *z);
int  z80_step(struct Neo *g, Z80 *z);   /* una instrucción (o la interrupción pendiente); ciclos T */

#endif
