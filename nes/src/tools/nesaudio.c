/*
 * nesaudio: corre una ROM sin ventana y guarda el audio en WAV (mono, 44100 Hz, 16 bits).
 *   nesaudio rom.nes salida.wav cuadros [boton:desde-hasta ...]
 * Imprime el nivel (RMS, pico) por segundo para ver de un vistazo que suena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nes.h"

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "uso: nesaudio rom.nes salida.wav cuadros [boton:desde-hasta ...]\n"); return 2; }
    size_t len; u8 *rom = slurp(argv[1], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", argv[1]); return 2; }
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    int frames = atoi(argv[3]);
    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    struct { int b, f, t; } h[64]; int nh = 0;
    for (int i = 4; i < argc && nh < 64; i++) {
        char name[16]; int a, b;
        if (sscanf(argv[i], "%15[a-z]:%d-%d", name, &a, &b) == 3)
            for (int k = 0; k < 8; k++) if (!strcmp(name, nm[k])) { h[nh].b = 1 << k; h[nh].f = a; h[nh].t = b; nh++; }
    }
    size_t cap = (size_t)frames * 800 + 4096, tot = 0;
    int16_t *pcm = malloc(cap * 2);
    for (int f = 0; f < frames; f++) {
        u8 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].f && f <= h[i].t) pad |= h[i].b;
        n.pad[0] = pad;
        nes_run_frame(&n);
        tot += apu_take(&n, pcm + tot, (int)(cap - tot));
    }
    FILE *o = fopen(argv[2], "wb");
    if (!o) { fprintf(stderr, "no pude escribir %s\n", argv[2]); return 2; }
    u32 data = (u32)(tot * 2), rate = APU_RATE, br = rate * 2, riff = 36 + data;
    u16 fmt = 1, ch = 1, align = 2, bits = 16; u32 sixteen = 16;
    fwrite("RIFF", 1, 4, o); fwrite(&riff, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o);
    fwrite(&sixteen, 4, 1, o); fwrite(&fmt, 2, 1, o); fwrite(&ch, 2, 1, o); fwrite(&rate, 4, 1, o);
    fwrite(&br, 4, 1, o); fwrite(&align, 2, 1, o); fwrite(&bits, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&data, 4, 1, o); fwrite(pcm, 2, tot, o); fclose(o);
    printf("%s: %zu muestras (%.1f s)\n", argv[2], tot, tot / (double)APU_RATE);
    for (size_t s = 0; s + APU_RATE <= tot || s == 0; s += APU_RATE) {
        size_t e = s + APU_RATE < tot ? s + APU_RATE : tot;
        double sum = 0; int peak = 0, zc = 0;
        for (size_t i = s; i < e; i++) {
            int v = pcm[i]; sum += (double)v * v; if (abs(v) > peak) peak = abs(v);
            if (i > s && ((pcm[i - 1] < 0) != (v < 0))) zc++;
        }
        printf("  s%2zu: rms %6.0f  pico %5d  cruces/s %d\n", s / APU_RATE, sqrt(sum / (e - s + 1)), peak, zc);
        if (e == tot) break;
    }
    return 0;
}
