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
    FILE *o = fopen(c.pos[1], "wb");
    if (!o) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    u32 data = (u32)(tot * 4), rate = AUDIO_RATE, br = rate * 4, riff = 36 + data, sixteen = 16;
    u16 fmt = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, o); fwrite(&riff, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    fwrite(&sixteen, 4, 1, o); fwrite(&fmt, 2, 1, o); fwrite(&ch, 2, 1, o); fwrite(&rate, 4, 1, o);
    fwrite(&br, 4, 1, o); fwrite(&align, 2, 1, o); fwrite(&bits, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&data, 4, 1, o); fwrite(pcm, 4, tot, o); fclose(o);
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
