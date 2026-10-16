/*
 * Lo que la mezcla común (tools/retrorecomp/shared: audio.c, la misma de GENRecomp y NEORecomp) toma del Master
 * System: el reloj maestro (el del Genesis) y su único chip, el PSG (223 722 pasos por segundo).
 */
#ifndef SMS_AUDIO_HOST_H
#define SMS_AUDIO_HOST_H

#include "sms.h"

#define AUDIO_HOST Sms
#define MCLK_HZ 53693175.0
#define AUDIO_NOW(g) ((g)->cyc * MCLK_PER_Z80)
#define AUDIO_TAKE sms_audio_take

/* Lleva el PSG hasta t; devuelve hasta dónde llegó */
static inline u64 audio_sync(Sms *g, u64 t)
{
    psg_run(g, t);
    return g->psg.mclk;
}
#define AUDIO_SYNC(g, t) audio_sync(g, t)

#endif
