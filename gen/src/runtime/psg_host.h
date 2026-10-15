/*
 * Lo que el PSG común (tools/retrorecomp/shared: psg.h, psg.c, el mismo de SMSRecomp) toma del Genesis: su tipo,
 * el campo psg, el silencio del menú Audio (bit 6 de audio.mute, el PSG entero) y su nivel junto al YM2612. El
 * Genesis no tiene estéreo en el PSG: los dos lados iguales.
 */
#ifndef GEN_PSG_HOST_H
#define GEN_PSG_HOST_H

#define PSG_HOST Gen
#define PSG_HOST_HEADER "gen.h"
#define PSG_OF(g) (&(g)->psg)
#define PSG_MUTED(g, c) ((g)->audio.mute & 0x40)
#define PSG_STEREO(g) 0xFFu
#define PSG_LEVEL 2100.0f           /* amplitud de un canal a todo volumen (en unidades de la salida de 16 bits) */

#endif
