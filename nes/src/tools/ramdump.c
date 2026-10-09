/*
 * ramdump: corre una ROM sin ventana y vuelca los 2 KB de RAM (una línea "DIRECCION VALOR" por byte).
 *   ramdump rom.nes cuadros [boton:desde-hasta ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: ramdump rom.nes cuadros [boton:desde-hasta ...]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 2;
    fclose(f);
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, sz, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    struct { int b, f, t; } h[64]; int nh = 0;
    for (int i = 3; i < argc && nh < 64; i++) {
        char name[16]; int a, b;
        if (sscanf(argv[i], "%15[a-z]:%d-%d", name, &a, &b) == 3)
            for (int k = 0; k < 8; k++) if (!strcmp(name, nm[k])) { h[nh].b = 1 << k; h[nh].f = a; h[nh].t = b; nh++; }
    }
    int frames = atoi(argv[2]);
    for (int i = 0; i < frames; i++) {
        u8 pad = 0;
        for (int k = 0; k < nh; k++) if (i >= h[k].f && i <= h[k].t) pad |= h[k].b;
        n.pad[0] = pad;
        nes_run_frame(&n);
        n.apu.nbuf = 0;
    }
    for (int a = 0; a < 0x800; a++) printf("%04X %02X\n", a, n.ram[a]);
    return 0;
}
