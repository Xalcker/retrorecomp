/*
 * gbaudio: corre una ROM sin ventana y guarda el audio en WAV (estéreo, 48000 Hz, 16 bits).
 * Imprime el nivel (RMS, pico) por segundo para ver de un vistazo que suena.
 *
 * Uso: gbaudio rom.gb salida.wav [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]
 */
#include <math.h>
#include <stdint.h>

#include "cli.h"
#include "console.h"
#include "romload.h"
#include "wav.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: gbaudio rom.gb salida.wav [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]\n");
        return 2;
    }
    static GB g;
    if (load_gb(&g, c.pos[0], c.model)) return 2;
    g.apu.enabled = 1;
    size_t cap = (size_t)c.frames * 1100 + 8192, tot = 0;      /* pares estéreo */
    int16_t *pcm = malloc(cap * 2 * sizeof *pcm);
    static float tmp[4096 * 2];
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f, 0));
        gb_run_frame(&g);
        int n;
        while ((n = gb_audio_pull(&g, tmp, 4096)) > 0) {
            for (int i = 0; i < n * 2 && tot * 2 + (size_t)i < cap * 2; i++) {
                float v = tmp[i] * 32767.0f;
                pcm[tot * 2 + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            tot += (size_t)n;
            if (tot > cap - 4096) break;
        }
    }
    u32 rate = GB_SAMPLE_RATE;
    if (!wav_save(c.pos[1], pcm, tot, rate, 2)) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu muestras (%.1f s)\n", c.pos[1], tot, tot / (double)rate);
    for (size_t s = 0; s + rate <= tot || s == 0; s += rate) {
        size_t e = s + rate < tot ? s + rate : tot;
        double sum = 0; int peak = 0;
        for (size_t i = s * 2; i < e * 2; i++) { int v = pcm[i]; sum += (double)v * v; if (abs(v) > peak) peak = abs(v); }
        size_t cnt = (e - s) * 2;
        printf("  s %3zu: rms %6.0f  pico %5d\n", s / rate, cnt ? sqrt(sum / cnt) : 0.0, peak);
        if (s + rate >= tot) break;
    }
    gb_free(&g);
    free(pcm);
    return 0;
}
