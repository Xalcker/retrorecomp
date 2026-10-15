/* Un audio_host.h de mentira para tests/comun_test.c: 100 ciclos maestros por muestra de salida y un
 * chip que siempre llega hasta donde se le pide */
#ifndef AUDIO_HOST_H
#define AUDIO_HOST_H

#include "audio.h"

typedef struct { uint64_t now; Audio audio; } Host;

#define AUDIO_HOST Host
#define MCLK_HZ (100.0 * AUDIO_RATE)
#define AUDIO_NOW(g) ((g)->now)
#define AUDIO_SYNC(g, t) (t)
#define AUDIO_TAKE host_audio_take

void audio_reset(Host *g);
void audio_add(Host *g, uint64_t t0, uint64_t t1, float l, float r);
void audio_end_frame(Host *g);
int  host_audio_take(Host *g, int16_t *out, int max_frames);

#endif
