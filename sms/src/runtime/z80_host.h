/*
 * Lo que el núcleo común del Z80 (tools/retrorecomp/shared: z80.h, z80_ops.h, z80.c, el mismo de GENRecomp y
 * NEORecomp) toma del Master System: el tipo de la consola que reciben z80_step y el bus, y el ciclo de cada
 * acceso a un puerto dentro de la instrucción (Z80_IO_AT), del que dependen los contadores y las
 * interrupciones del VDP. Aquí el Z80 es el CPU principal; la Pausa del SMS (y Start del Game Gear en su
 * modo SMS) es la NMI.
 */
#ifndef SMS_Z80_HOST_H
#define SMS_Z80_HOST_H

#define Z80_HOST Sms

struct Sms;
void sms_io_at(struct Sms *g, int t);           /* z80_host.c: el acceso al puerto ocurre t ciclos después del comienzo de la instrucción */
#define Z80_IO_AT(g, t) sms_io_at((g), (t))

#endif
