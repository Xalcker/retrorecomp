/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros.
 *   bench rom.nes [--frames N] [--repeat N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * (también vale: bench rom.nes cuadros). Sin entradas, Start y luego la derecha. Con --repeat se
 * reporta la corrida más rápida.
 * Compilado con -DNES_RECOMP (proyecto generado) mide el código recompilado;
 * sin él, el intérprete.
 * Con --hash corre una vez e imprime el hash de todos los cuadros y de todo el audio, el mismo que el *shot
 * --hash del intérprete (golden_hash.h): así tests/golden.sh --recompilado compara el código recompilado con
 * la misma línea base.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "cli.h"
#include "golden_hash.h"

int main(int argc, char **argv)
{
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 1800)) {
        fprintf(stderr, "uso: bench rom.nes [--frames N] [--repeat N] [--input archivo] [--seed S] [--hash] [boton:desde-hasta ...]\n");
        return 2;
    }
    FILE *f = fopen(c.pos[0], "rb");
    if (!f) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 2;
    fclose(f);
    int scripted = c.input || c.nh || c.use_seed;
    long frames = c.frames;
    double best = 0;
    u64 total_cyc = 0;
    GoldenHash gh;
    if (c.hash) c.repeat = 1;
    for (int k = 0; k < c.repeat; k++) {
        static NES n; const char *err = NULL;
        if (nes_load(&n, rom, sz, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
        cli_rewind(&c);
        golden_init(&gh);
        metrica_reiniciar(&n);      /* todas las corridas dan lo mismo: se queda la última */
        u64 cyc0 = n.cyc;
        clock_t t0 = clock();
        for (long i = 0; i < frames; i++) {
            if (scripted) { n.pad[0] = cli_pad(&c, i, 0); n.pad[1] = cli_pad(&c, i, 1); }
            else n.pad[0] = (i > 120 && i < 125) ? BTN_START : (i > 400 ? BTN_RIGHT : 0);
            nes_run_frame(&n);
            if (c.hash) {
                golden_fnv(&gh.img, n.ppu.fb, sizeof n.ppu.fb);
                golden_fnv(&gh.aud, n.apu.buf, (size_t)n.apu.nbuf * sizeof n.apu.buf[0]);
            }
            n.apu.nbuf = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
        total_cyc = n.cyc - cyc0;
    }
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", frames, best,
           c.repeat > 1 ? " (mejor corrida)" : "", frames / best, frames / best / 60.0988);
#ifdef NES_RECOMP
    metrica_imprimir(&metrica, total_cyc);      /* la métrica común (metrica.h) */
#else
    (void)total_cyc;
#endif
    if (c.hash) golden_print(&gh);
    return 0;
}
