/*
 * Mezcla del sonido: el YM2610 (una muestra cada 432 ciclos maestros, 55 944 Hz con el reloj del AES)
 * sale a 44 100 Hz en estéreo de 16 bits. Es la de GENRecomp.
 *
 * El chip entrega tramos de tiempo (en ciclos maestros) con un valor constante (audio_add). Cada
 * muestra de salida es el promedio de lo que sonó durante su tiempo: los tramos se reparten en las
 * muestras que tocan, con el peso de la parte que cubren. Al terminar el cuadro el chip se lleva al
 * mismo ciclo y salen las muestras completas, por un pasa-altos de ~10 Hz (como el acople de la salida
 * de la consola: quita la componente continua, por ejemplo la del SSG, que no tiene signo).
 *
 * El tiempo real sale del reloj maestro del AES (MCLK_HZ, 24,167829 MHz): así un cuadro (59,60 por
 * segundo) da 740 muestras, al ritmo de la ventana.
 */
#include <string.h>

#include "neo.h"

#define PERIOD (MCLK_HZ / AUDIO_RATE)          /* ciclos maestros por muestra de salida */
#define HP_R 0.99858f                           /* pasa-altos de un polo: 1 - 2 pi 10 / 44100 */

void audio_reset(Neo *g)
{
    Audio *a = &g->audio;
    u8 mute = a->mute;
    memset(a, 0, sizeof *a);
    a->mute = mute;
    a->first = (u64)((double)(g->cyc * MCLK_PER_M68K) / PERIOD);
}

void audio_add(Neo *g, u64 t0, u64 t1, float l, float r)
{
    if (l == 0.0f && r == 0.0f) return;            /* silencio: sumar 0 no cambia nada */
    Audio *a = &g->audio;
    double x = (double)t0 / PERIOD, end = (double)t1 / PERIOD;
    u64 k = (u64)x;
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

void audio_end_frame(Neo *g)
{
    Audio *a = &g->audio;
    u64 t = g->cyc * MCLK_PER_M68K;
    ym_run(g, t);
    u64 upto = (u64)((double)g->ym.mclk / PERIOD);  /* las muestras que ya terminaron */
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

int neo_audio_take(Neo *g, int16_t *out, int max_frames)
{
    Audio *a = &g->audio;
    int n = a->n < max_frames ? a->n : max_frames;
    memcpy(out, a->buf, (size_t)n * 2 * sizeof *out);
    memmove(a->buf, a->buf + n * 2, (size_t)(a->n - n) * 2 * sizeof *out);
    a->n -= n;
    return n;
}
