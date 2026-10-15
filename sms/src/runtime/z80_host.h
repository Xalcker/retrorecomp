/*
 * Lo que el núcleo común del Z80 (tools/retrorecomp/shared: z80.h, z80.c, el mismo de GENRecomp y
 * NEORecomp) toma del Master System: el tipo de la consola que reciben z80_step y el bus. Aquí el Z80 es el
 * CPU principal; la Pausa del SMS (y Start del Game Gear en su modo SMS) es la NMI.
 */
#ifndef SMS_Z80_HOST_H
#define SMS_Z80_HOST_H

#define Z80_HOST Sms

#endif
