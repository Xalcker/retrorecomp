/*
 * Z80: estado y lo que pide del bus. En el Genesis y en el Neo Geo corre el programa de sonido, siempre
 * interpretado (z80.c): en el Genesis desde su RAM de 8 KB, en el Neo Geo desde la M1 ROM del cartucho
 * (con bancos) y 2 KB de RAM. En el Master System es el CPU principal y también se recompila: la semántica
 * de cada instrucción está en z80_ops.h, la misma para el intérprete y el código generado.
 *
 * Común a GENRecomp, NEORecomp y SMSRecomp (retrorecomp/shared). Cada consola pone su z80_host.h, que
 * define Z80_HOST, el tipo de la consola (Gen, Neo, Sms) que reciben z80_step y las funciones del bus.
 */
#ifndef Z80_H
#define Z80_H

#include <stdint.h>

#include "z80_host.h"

struct Z80_HOST;

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

/* Bus: lo implementa la consola (su z80bus.c) o z80test */
uint8_t z80_read(struct Z80_HOST *g, uint16_t addr);
void    z80_write(struct Z80_HOST *g, uint16_t addr, uint8_t v);
uint8_t z80_in(struct Z80_HOST *g, uint16_t port);
void    z80_out(struct Z80_HOST *g, uint16_t port, uint8_t v);

void z80_reset(Z80 *z);
int  z80_step(struct Z80_HOST *g, Z80 *z);   /* una instrucción (o la interrupción pendiente); ciclos T */
int  z80_service(struct Z80_HOST *g, Z80 *z);    /* solo la entrada a NMI/INT o un paso de HALT; 0 si toca una instrucción */

/* Estadística (bench, difftest): ciclos T del Z80 por camino, solo de la consola en z80_stat_of (NULL:
 * de ninguna; difftest mide la máquina B). Va fuera de la estructura de la consola, porque los estados
 * guardados validan su tamaño. En GEN y NEO hoy todo es interpretado o saltado; `native` lo suma el
 * código generado. */
typedef struct {
    uint64_t native;        /* en código recompilado */
    uint64_t interp;        /* en el intérprete (z80_step) */
    uint64_t skipped;       /* saltados de una vez: HALT sin nada que atender y bucles de espera */
} Z80Stat;
extern Z80Stat z80_stat;
extern const void *z80_stat_of;
#define Z80_STAT(g, campo, t) do { if ((const void *)(g) == z80_stat_of) z80_stat.campo += (t); } while (0)
/* Una línea "Z80: N ciclos T: X% nativo, Y% interpretado, Z% saltado (...)" (nada si no corrió) */
void z80_stat_print(void);

/* Código generado (z80_recomp.h): una función corre desde z->pc hasta que se acaba el tiempo (`target`,
 * en las unidades de la consola), hay algo que atiende el intérprete o el destino no es suyo. 0 si el PC
 * no es de la función (no corrió nada); 1 si corrió. */
typedef int (*Z80RecompFn)(struct Z80_HOST *g, uint64_t target);
/* La consola que corre su Z80 solo con el intérprete aunque haya código generado (difftest: la máquina A) */
extern const void *z80_interp_only;

#endif
