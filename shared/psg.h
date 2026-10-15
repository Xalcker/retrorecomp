/*
 * PSG: el SN76489 de Sega (dentro del VDP del Genesis, del Master System y del Game Gear). Tres canales de tono
 * y uno de ruido, al reloj del Z80 / 16 (240 ciclos maestros por paso: el reloj maestro es el mismo en las
 * tres consolas). Común a GENRecomp y SMSRecomp (retrorecomp/shared, psg.c).
 *
 * Cada consola pone su psg_host.h, con lo que el PSG toma de ella:
 *   PSG_HOST           el tipo de la consola (Gen, Sms)
 *   PSG_HOST_HEADER    su cabecera, entre comillas (la incluye psg.c: ahí están audio_add y el campo psg)
 *   PSG_OF(g)          el Psg de la consola
 *   PSG_MUTED(g, c)    el canal c (0-2 tonos, 3 ruido) está silenciado (el menú Audio)
 *   PSG_STEREO(g)      el estéreo del Game Gear (puerto $06): bits 4-7 cada canal a la izquierda, 0-3 a la derecha;
 *                      $FF en las consolas sin estéreo (los dos lados iguales)
 *   PSG_LEVEL          la amplitud de un canal a todo volumen, en unidades de la salida de 16 bits
 */
#ifndef PSG_H
#define PSG_H

#include <stdint.h>

#include "psg_host.h"

struct PSG_HOST;

typedef struct {
    uint16_t period[4];         /* tonos: 10 bits; el 3 es el registro de ruido (3 bits) */
    uint8_t  vol[4];            /* atenuación 0-15 (15 = silencio) */
    uint8_t  latch;             /* registro elegido por el último byte con el bit 7 */
    uint16_t counter[4];
    uint8_t  out[4];            /* salida actual de cada canal (0/1) */
    uint8_t  noise_ff;          /* biestable del ruido (el LFSR avanza en cada subida) */
    uint16_t lfsr;
    uint64_t mclk;              /* ciclo maestro hasta el que va */
} Psg;

void psg_reset(Psg *p);
void psg_write(struct PSG_HOST *g, uint8_t v, uint64_t mclk);  /* un byte escrito al PSG en el ciclo maestro mclk */
void psg_run(struct PSG_HOST *g, uint64_t mclk);               /* lo lleva hasta mclk, entregando su sonido a la mezcla */

#endif
