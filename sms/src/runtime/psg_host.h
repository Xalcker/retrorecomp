/*
 * Lo que el PSG común (tools/retrorecomp/shared: psg.h, psg.c, el mismo de GENRecomp) toma del Master System y del
 * Game Gear: su tipo, el campo psg, el silencio de cada canal (bits 0-3 de audio.mute, del menú Audio) y el
 * estéreo del Game Gear en modo GG (el puerto $06: bits 4-7 cada canal a la izquierda, 0-3 a la derecha). El
 * PSG es todo el sonido de la consola: suena más fuerte que en el Genesis, donde va junto al YM2612.
 */
#ifndef SMS_PSG_HOST_H
#define SMS_PSG_HOST_H

#define PSG_HOST Sms
#define PSG_HOST_HEADER "sms.h"
#define PSG_OF(g) (&(g)->psg)
#define PSG_MUTED(g, c) (((g)->audio.mute >> (c)) & 1)
#define PSG_STEREO(g) ((g)->model == MODEL_GG && !(g)->sms_mode ? (unsigned)(g)->io.gg[6] : 0xFFu)
#define PSG_LEVEL 4000.0f           /* amplitud de un canal a todo volumen (cuatro canales: 16 000 de 32 767) */

#endif
