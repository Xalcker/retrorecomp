/*
 * Lo que la mezcla común (tools/retrorecomp/shared: audio.c, la de GENRecomp) toma del Neo Geo: el reloj
 * maestro del AES (MCLK_HZ, en neo.h: con él un cuadro, 59,60 por segundo, da 740 muestras) y su chip,
 * el YM2610 (una muestra cada 432 ciclos maestros, 55 944 Hz).
 */
#ifndef NEO_AUDIO_HOST_H
#define NEO_AUDIO_HOST_H

#include "neo.h"

#define AUDIO_HOST Neo
#define AUDIO_NOW(g) ((g)->cyc * MCLK_PER_M68K)
#define AUDIO_TAKE neo_audio_take

/* Lleva el Z80 (sus escrituras al YM2610) y el YM2610 hasta t; devuelve hasta dónde llegó */
static inline u64 audio_sync(Neo *g, u64 t)
{
    z80_sync(g);
    ym_run(g, t);
    return g->ym.mclk;
}
#define AUDIO_SYNC(g, t) audio_sync(g, t)

#endif
