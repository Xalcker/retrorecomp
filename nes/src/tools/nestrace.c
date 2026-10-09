/*
 * nestrace: corre una ROM sin ventana y guarda qué instrucciones de PRG ejecutó
 * (líneas "OFFSET:DIRECCION" en hexadecimal) para guiar a nesrecomp.
 *   nestrace rom.nes traza.txt cuadros [boton:desde-hasta ...]
 * Con varias corridas se pueden juntar trazas (nesrecomp acepta --trace varias veces).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "uso: nestrace rom.nes traza.txt cuadros [boton:desde-hasta ...]\n"); return 2; }
    size_t len; u8 *rom = slurp(argv[1], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", argv[1]); return 2; }
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    nes_trace_start(&n);
    int frames = atoi(argv[3]);
    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    struct { int b, f, t; } h[64]; int nh = 0;
    for (int i = 4; i < argc && nh < 64; i++) {
        char name[16]; int a, b;
        if (sscanf(argv[i], "%15[a-z]:%d-%d", name, &a, &b) == 3)
            for (int k = 0; k < 8; k++) if (!strcmp(name, nm[k])) { h[nh].b = 1 << k; h[nh].f = a; h[nh].t = b; nh++; }
    }
    for (int f = 0; f < frames; f++) {
        u8 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].f && f <= h[i].t) pad |= h[i].b;
        n.pad[0] = pad;
        nes_run_frame(&n);
    }
    size_t cnt = 0;
    for (size_t i = 0; i < n.cart.prg_size; i++) cnt += n.trace[i] != 0;
    if (!nes_trace_write(&n, argv[2])) { fprintf(stderr, "no pude escribir %s\n", argv[2]); return 2; }
    printf("%s: %zu instrucciones distintas en %d cuadros\n", argv[2], cnt, frames);
    return 0;
}
