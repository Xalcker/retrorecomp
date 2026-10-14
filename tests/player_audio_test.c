/*
 * Prueba de shared/player_audio.h con una tarjeta de sonido simulada (sin SDL): el juego manda un cuadro de
 * muestras a su ritmo y la tarjeta se lleva bloques al suyo, durante 10 minutos de juego por caso. Pasa si,
 * tras el arranque, la cola nunca se vacía sonando ni se descarta un cuadro, y lo que toca la tarjeta no
 * salta (un chasquido) ni al pausar y volver. Los casos:
 *
 * - escritorio: consolas de 59.60 a 60.10 Hz con el bucle a 59.6 o 60.1 Hz (hasta ±0.84 % de diferencia),
 *   a 44.1 y 48 kHz, con pausas de vez en cuando;
 * - navegador: el bucle sigue al refresco de la pantalla (60, 120 o 144 Hz, hasta cuatro cuadros juntos),
 *   la tarjeta pide bloques con retraso variable y su reloj difiere ±0.2 %;
 * - una pestaña oculta 3 segundos: la cola se vacía una vez y vuelve sin chasquido.
 *
 * Todos van con la cola del escritorio (PA_AHEAD 1); la del navegador es más larga y solo agrega margen.
 *
 *     cc -std=c99 -O2 -Wall -Wextra -o player_audio_test tests/player_audio_test.c -lm && ./player_audio_test
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Lo poco de SDL que usa player_audio.h; la cola es la de la tarjeta simulada */
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int16_t Sint16;
typedef Uint32 SDL_AudioDeviceID;
typedef struct {
    int freq;
    Uint16 format;
    Uint8 channels, silence;
    Uint16 samples, padding;
    Uint32 size;
    void *callback, *userdata;
} SDL_AudioSpec;
#define AUDIO_F32SYS 0x8120
#define SDL_zero(x) memset(&(x), 0, sizeof(x))
#define SDL_zerop(x) memset((x), 0, sizeof(*(x)))
#define SDL_memset memset
#define SDL_memmove memmove

#define QSIZE (1 << 17)
static float queue[QSIZE][2];
static long q_in, q_out;    /* pares que entraron y que salieron */

static SDL_AudioDeviceID SDL_OpenAudioDevice(const char *name, int capture, const SDL_AudioSpec *want,
                                             SDL_AudioSpec *have, int changes)
{
    (void)name, (void)capture, (void)changes;
    *have = *want;
    q_in = q_out = 0;
    return 1;
}

static void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause)
{
    (void)dev, (void)pause;
}

static void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
}

static int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len)
{
    (void)dev;
    const float *f = data;
    for (Uint32 i = 0; i < len / sizeof queue[0]; i++, q_in++) {
        queue[q_in % QSIZE][0] = f[i * 2];
        queue[q_in % QSIZE][1] = f[i * 2 + 1];
    }
    if (q_in - q_out > QSIZE) {
        fprintf(stderr, "la cola simulada se desbordó\n");
        exit(1);
    }
    return 0;
}

static Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev)
{
    (void)dev;
    return (Uint32)((q_in - q_out) * sizeof queue[0]);
}

#include "../shared/player_audio.h"

typedef struct {
    const char *name;
    int rate;           /* muestras por segundo de la consola */
    double console_hz;  /* cuadros por segundo de la consola: rate / console_hz muestras por cuadro */
    double loop_hz;     /* a qué ritmo corre de verdad el bucle */
    double card_err;    /* el reloj de la tarjeta, adelantado (+) o atrasado (-) */
    double block;       /* pares que pide la tarjeta de una vez */
    double jitter;      /* retraso de cada pedido de la tarjeta, de 0 a esto (segundos) */
    int display_hz;     /* 0: escritorio; si no, el refresco de la pantalla del navegador */
    int format;         /* 0 float estéreo (GB), 1 enteros mono (NES), 2 enteros estéreo (GEN, NEO) */
    int pauses;         /* pausas de 0.7 s cada 37 s */
    int hidden;         /* la pestaña oculta de 20 a 23 s */
} Case;

static unsigned rng = 12345;
static double rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) / 16777216.0;
}

/* Lo que manda el juego en un cuadro: dos tonos, uno por canal */
static long phase;
static double acc;
static void game_frame(PlayerAudio *pa, const Case *c)
{
    static float f[8192][2];
    static Sint16 s[8192][2];
    acc += c->rate / c->console_hz;
    int n = (int)acc;
    acc -= n;
    for (int i = 0; i < n; i++, phase++) {
        double t = (double)phase / c->rate;
        f[i][0] = (float)(0.5 * sin(2 * M_PI * 440 * t));
        f[i][1] = (float)(0.4 * sin(2 * M_PI * 660 * t));
        s[i][0] = (Sint16)(f[i][0] * 32767);
        s[i][1] = (Sint16)(f[i][1] * 32767);
    }
    if (c->format == 0) {
        player_audio_push(pa, &f[0][0], n);
    } else if (c->format == 1) {
        static Sint16 mono[8192];
        for (int i = 0; i < n; i++) mono[i] = s[i][0];
        player_audio_push_s16(pa, mono, n, 1);
    } else {
        player_audio_push_s16(pa, &s[0][0], n, 2);
    }
}

static int run(const Case *c, double seconds)
{
    PlayerAudio pa;
    player_audio_open(&pa, c->rate);
    phase = 0;
    acc = 0;
    double card_rate = c->rate * (1 + c->card_err);
    double t_game = 0, t_card = c->block / card_rate, pending = 0, t_last = 0;
    long block_n = 0, tick = 0;
    float prev[2] = {0, 0};
    double max_jump = 0, ratio_lo = 1, ratio_hi = 1;
    long silences = 0, q_lo = QSIZE, q_hi = 0;
    while (t_game < seconds || t_card < seconds) {
        if (t_card <= t_game) {
            /* La tarjeta se lleva un bloque; lo que falte suena como silencio */
            int want = (int)((block_n + 1) * c->block) - (int)(block_n * c->block);
            for (int i = 0; i < want; i++) {
                float x[2] = {0, 0};
                if (q_out < q_in) {
                    x[0] = queue[q_out % QSIZE][0];
                    x[1] = queue[q_out % QSIZE][1];
                    q_out++;
                } else if (pa.playing) {
                    silences++;
                }
                for (int k = 0; k < 2; k++) {
                    double d = fabs(x[k] - prev[k]);
                    if (d > max_jump && !(c->hidden && t_card > 20 && t_card < 23.5)) max_jump = d;
                    prev[k] = x[k];
                }
            }
            block_n++;
            t_card = block_n * c->block / card_rate + rnd() * c->jitter;
            continue;
        }
        /* El juego: un cuadro (escritorio) o los que tocan en este refresco (navegador) */
        int paused = c->pauses && fmod(t_game, 37) > 36.3;
        int hidden = c->hidden && t_game > 20 && t_game < 23;
        if (paused) {
            player_audio_stop(&pa);
        } else if (!hidden && !c->display_hz) {
            game_frame(&pa, c);
        } else if (!hidden) {
            double frame = 1 / c->console_hz;
            pending += t_last ? t_game - t_last : frame;
            if (pending > 4 * frame) pending = frame;
            for (int n = 0; pending >= frame && n < 4; n++, pending -= frame) game_frame(&pa, c);
        }
        if (!hidden) t_last = t_game;
        if (t_game > 30 && pa.playing) {
            long q = q_in - q_out;
            if (q < q_lo) q_lo = q;
            if (q > q_hi) q_hi = q;
            if (pa.ratio < ratio_lo) ratio_lo = pa.ratio;
            if (pa.ratio > ratio_hi) ratio_hi = pa.ratio;
        }
        /* Cada cuadro tiene su hora fija (el bucle hace next += cuadro; el navegador sigue al refresco) y
         * llega algo tarde: hasta 2 ms en el escritorio, hasta un 10 % del refresco en el navegador */
        tick++;
        if (c->display_hz) t_game = (tick + rnd() * 0.1) / c->display_hz;
        else t_game = tick / c->loop_hz + rnd() * 0.002;
    }
    long expect = c->hidden ? 1 : 0;
    int ok = silences == 0 && pa.drops == 0 && pa.underruns == expect && max_jump < 0.06;
    if (c->hidden) ok = pa.drops == 0 && pa.underruns == expect && max_jump < 0.06;
    printf("%-4s %-22s %5d Hz  vacía %ld  silencio %ld  descartes %ld  salto %.3f  cola %4ld-%4ld (%2.0f-%2.0f ms)  "
           "razón %+.3f%%..%+.3f%%\n",
           ok ? "ok" : "FALLA", c->name, c->rate, pa.underruns, silences, pa.drops, max_jump, q_lo, q_hi,
           1000.0 * q_lo / c->rate, 1000.0 * q_hi / c->rate, 100 * (ratio_lo - 1), 100 * (ratio_hi - 1));
    player_audio_close(&pa);
    return ok;
}

int main(void)
{
    static const double consoles[] = {59.60, 59.7275, 59.92, 60.0988};
    static const double loops[] = {59.6, 60.1};
    static const int rates[] = {44100, 48000};
    static const int displays[] = {60, 120, 144};
    char name[64];
    int fails = 0, cases = 0;
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 4; k++)
            for (int l = 0; l < 2; l++) {
                snprintf(name, sizeof name, "consola %.2f, bucle %.1f", consoles[k], loops[l]);
                Case c = {name, rates[r], consoles[k], loops[l], 0, 1024, 0.001, 0, (k + l) % 3, 1, 0};
                fails += !run(&c, 600);
                cases++;
            }
    for (int r = 0; r < 2; r++)
        for (int d = 0; d < 3; d++)
            for (int e = -1; e <= 1; e += 2) {
                snprintf(name, sizeof name, "navegador %d Hz, %+.1f%%", displays[d], 0.2 * e);
                /* La tarjeta del navegador va a 48 kHz: pide 1024 de los suyos */
                Case c = {name, rates[r], consoles[r * 2 + 1], consoles[r * 2 + 1], 0.002 * e,
                          1024.0 * rates[r] / 48000, 0.008, displays[d], d % 3, 1, 0};
                fails += !run(&c, 600);
                cases++;
            }
    Case hidden = {"pestaña oculta 3 s", 44100, 60.0988, 60.0988, 0, 1024.0 * 44100 / 48000, 0.008, 60, 1, 0, 1};
    fails += !run(&hidden, 60);
    cases++;
    printf("%d de %d casos pasan\n", cases - fails, cases);
    return fails != 0;
}
