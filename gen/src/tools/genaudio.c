/*
 * genaudio: corre una ROM sin ventana y guarda el sonido en WAV (estéreo, 44 100 Hz, 16 bits).
 *   genaudio rom.bin salida.wav [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * Imprime el nivel (RMS, pico) por segundo para ver de un vistazo que suena.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cli.h"
#include "console.h"
#include "romload.h"
#include "wav.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: genaudio rom.bin salida.wav [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    Gen *g = load_gen(c.pos[0]);
    if (!g) return 2;
    size_t cap = (size_t)c.frames * 800 + 4096, tot = 0;    /* pares L R */
    int16_t *pcm = malloc(cap * 4);
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        gen_run_frame(g);
        tot += (size_t)gen_audio_take(g, pcm + tot * 2, (int)(cap - tot));
    }
    if (!wav_save(c.pos[1], pcm, tot, AUDIO_RATE, 2)) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu muestras (%.1f s)\n", c.pos[1], tot, tot / (double)AUDIO_RATE);
    for (size_t s = 0; s + AUDIO_RATE <= tot || s == 0; s += AUDIO_RATE) {
        size_t e = s + AUDIO_RATE < tot ? s + AUDIO_RATE : tot;
        double sum = 0;
        int peak = 0;
        for (size_t i = s * 2; i < e * 2; i++) { int v = pcm[i]; sum += (double)v * v; if (abs(v) > peak) peak = abs(v); }
        printf("  s%2zu: rms %6.0f  pico %5d\n", s / AUDIO_RATE, e > s ? sqrt(sum / (double)((e - s) * 2)) : 0.0, peak);
        if (e == tot) break;
    }
    return 0;
}
