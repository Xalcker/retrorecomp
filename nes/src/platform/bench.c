/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros.
 *   bench rom.nes [cuadros]
 * Compilado con -DNES_RECOMP (proyecto generado) mide el código recompilado;
 * sin él, el intérprete.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "nes.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: bench rom.nes [cuadros]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "no pude leer %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 2;
    fclose(f);
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, sz, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    int frames = argc > 2 ? atoi(argv[2]) : 1800;
    clock_t t0 = clock();
    for (int i = 0; i < frames; i++) {
        n.pad[0] = (i > 120 && i < 125) ? BTN_START : (i > 400 ? BTN_RIGHT : 0);
        nes_run_frame(&n);
        n.apu.nbuf = 0;
    }
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%d cuadros en %.2f s: %.0f cuadros/s (%.1fx tiempo real)\n", frames, s, frames / s, frames / s / 60.0988);
    return 0;
}
