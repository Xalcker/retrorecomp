/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros (genbench con el intérprete; en los
 * proyectos generados, bench con el código recompilado).
 *   genbench rom.bin [--frames N] [--repeat N] [--input archivo] [--seed S] [--perfil] [boton:desde-hasta ...]
 * Sin entradas, Start a los 2 s y luego la derecha. Con --repeat se reporta la corrida más rápida.
 * También dice cuánto del Z80 corrió nativo, interpretado o saltado; con --perfil, en qué se fue el
 * tiempo (prof.h).
 */
#define PROF_SAMPLER        /* antes de cualquier include: gen.h incluye prof.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L     /* nanosleep con -std=c99 (el hilo de prof.h) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cli.h"
#include "console.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 1800)) {
        fprintf(stderr, "uso: genbench rom.bin [--frames N] [--repeat N] [--input archivo] [--seed S] [--perfil] [boton:desde-hasta ...]\n");
        return 2;
    }
    FILE *f = fopen(c.pos[0], "rb");
    if (!f) { fprintf(stderr, "no se pudo leer %s\n", c.pos[0]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc((size_t)sz);
    if (fread(rom, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    int scripted = c.input || c.nh || c.use_seed;
    double best = 0;
    static Gen g;
    z80_stat_of = &g;
    if (c.perfil && !prof_start()) { fprintf(stderr, "no se pudo arrancar el hilo de --perfil\n"); return 2; }
    for (int k = 0; k < c.repeat; k++) {
        const char *err = NULL;
        if (k) gen_free(&g);
        if (gen_load(&g, rom, (size_t)sz, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
        cli_rewind(&c);
        z80_stat = (Z80Stat){ 0 };      /* todas las corridas dan lo mismo: se queda la última */
        clock_t t0 = clock();
        for (long i = 0; i < c.frames; i++) {
            if (scripted) { g.pad[0] = cli_pad(&c, i, 0); g.pad[1] = cli_pad(&c, i, 1); }
            else g.pad[0] = (i > 120 && i < 125) ? GEN_START : (i > 400 ? GEN_RIGHT : 0);
            gen_run_frame(&g);
            g.audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
    }
    if (c.perfil) prof_stop();
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", c.frames, best,
           c.repeat > 1 ? " (mejor corrida)" : "", c.frames / best, c.frames / best / 59.92);
    z80_stat_print();
    if (gen_z80_window_ext)
        printf("Z80: %llu accesos por la ventana de $8000 a la RAM, el VDP, la E/S o la SRAM del 68000 (con el Z80 atrasado, "
               "en otro momento)\n", gen_z80_window_ext / (unsigned long long)c.repeat);
    if (c.perfil) prof_print();
    return 0;
}
