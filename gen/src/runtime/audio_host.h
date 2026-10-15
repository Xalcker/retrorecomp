/*
 * Lo que la mezcla común (tools/retrorecomp/shared: audio.c, la misma de NEORecomp) toma del Genesis: el
 * reloj maestro y sus dos chips, el YM2612 (53 267 muestras por segundo) y el PSG (223 722).
 */
#ifndef GEN_AUDIO_HOST_H
#define GEN_AUDIO_HOST_H

#include "gen.h"

#define AUDIO_HOST Gen
#define MCLK_HZ 53693175.0
#define AUDIO_NOW(g) ((g)->cyc * MCLK_PER_M68K)
#define AUDIO_TAKE gen_audio_take

/* Lleva el YM2612 y el PSG hasta t; devuelve hasta dónde llegaron los dos */
static inline u64 audio_sync(Gen *g, u64 t)
{
    ym_run(g, t);
    psg_run(g, t);
    return g->ym.mclk < g->psg.mclk ? g->ym.mclk : g->psg.mclk;
}
#define AUDIO_SYNC(g, t) audio_sync(g, t)

#endif
