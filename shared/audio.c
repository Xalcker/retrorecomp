/*
 * Mezcla del sonido: los chips de la consola salen a 44 100 Hz en estéreo de 16 bits. Común a GENRecomp
 * (YM2612 y PSG) y NEORecomp (YM2610) (retrorecomp/shared).
 *
 * Cada chip entrega tramos de tiempo (en ciclos maestros) con un valor constante (audio_add). Cada
 * muestra de salida es el promedio de lo que sonó durante su tiempo: los tramos se reparten en las
 * muestras que tocan, con el peso de la parte que cubren. Al terminar el cuadro los chips se llevan
 * al mismo ciclo y salen las muestras completas, por un pasa-altos de ~10 Hz (como el acople de la
 * salida de la consola: quita la componente continua, por ejemplo la de la DAC quieta del Genesis o la
 * del SSG del Neo Geo, que no tiene signo).
 *
 * El tiempo real sale del reloj maestro de la consola (MCLK_HZ): así un cuadro da las muestras que le
 * tocan, al ritmo de la ventana.
 *
 * Lo propio de cada consola viene de su audio_host.h: su cabecera (con el campo `audio` de tipo Audio),
 * AUDIO_HOST (su tipo), MCLK_HZ, AUDIO_NOW(g) (el ciclo maestro de ahora), AUDIO_SYNC(g, t) (lleva sus
 * chips hasta t y devuelve hasta qué ciclo terminaron todos) y AUDIO_TAKE (el nombre de la función que
 * entrega las muestras).
 */
#include <string.h>

#include "audio_host.h"

#define PERIOD (MCLK_HZ / AUDIO_RATE)          /* ciclos maestros por muestra de salida */
#define HP_R 0.99858f                           /* pasa-altos de un polo: 1 - 2 pi 10 / 44100 */

void audio_reset(AUDIO_HOST *g)
{
    Audio *a = &g->audio;
    uint8_t mute = a->mute;
    memset(a, 0, sizeof *a);
    a->mute = mute;
    a->first = (uint64_t)((double)AUDIO_NOW(g) / PERIOD);
}

void audio_add(AUDIO_HOST *g, uint64_t t0, uint64_t t1, float l, float r)
{
    if (l == 0.0f && r == 0.0f) return;            /* silencio: sumar 0 no cambia nada */
    Audio *a = &g->audio;
    double x = (double)t0 / PERIOD, end = (double)t1 / PERIOD;
    uint64_t k = (uint64_t)x;
    while (x < end) {
        double edge = (double)(k + 1), seg = (end < edge ? end : edge) - x;
        if (k >= a->first && k - a->first < AUDIO_BINS) {
            float *b = a->acc[k % AUDIO_BINS];
            b[0] += (float)(l * seg);
            b[1] += (float)(r * seg);
        }
        x = edge;
        k++;
    }
}

static inline int16_t clip(float v)
{
    return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int16_t)v;
}

void audio_end_frame(AUDIO_HOST *g)
{
    Audio *a = &g->audio;
    uint64_t done = AUDIO_SYNC(g, AUDIO_NOW(g));
    uint64_t upto = (uint64_t)((double)done / PERIOD);     /* las muestras que ya terminaron para todos */
    while (a->first < upto) {
        float *b = a->acc[a->first % AUDIO_BINS];
        float out[2];
        for (int c = 0; c < 2; c++) {
            out[c] = b[c] - a->hp_x[c] + HP_R * a->hp_y[c];
            a->hp_x[c] = b[c];
            a->hp_y[c] = out[c];
        }
        if (a->n < AUDIO_BINS) {
            a->buf[a->n * 2] = clip(out[0]);
            a->buf[a->n * 2 + 1] = clip(out[1]);
            a->n++;
        }
        b[0] = b[1] = 0;
        a->first++;
    }
}

int AUDIO_TAKE(AUDIO_HOST *g, int16_t *out, int max_frames)
{
    Audio *a = &g->audio;
    int n = a->n < max_frames ? a->n : max_frames;
    memcpy(out, a->buf, (size_t)n * 2 * sizeof *out);
    memmove(a->buf, a->buf + n * 2, (size_t)(a->n - n) * 2 * sizeof *out);
    a->n -= n;
    return n;
}
