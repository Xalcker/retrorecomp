/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros (neobench con el intérprete; en los
 * proyectos generados, bench con el código recompilado).
 *   neobench juego [--frames N] [--repeat N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * Sin entradas, Start a los 2 s y luego la derecha. Con --repeat se reporta la corrida más rápida.
 */
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
        fprintf(stderr, "uso: neobench juego [--frames N] [--repeat N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    int scripted = c.input || c.nh || c.use_seed;
    double best = 0;
    static Neo g;
    for (int k = 0; k < c.repeat; k++) {
        const char *err = NULL;
        if (k) neo_free(&g);
        if (neo_load(&g, c.pos[0], NULL, &err)) { fprintf(stderr, "%s: %s\n", c.pos[0], err); return 2; }
        cli_rewind(&c);
        clock_t t0 = clock();
        for (long i = 0; i < c.frames; i++) {
            if (scripted) { g.pad[0] = cli_pad(&c, i, 0); g.pad[1] = cli_pad(&c, i, 1); }
            else g.pad[0] = (i > 120 && i < 125) ? NEO_START : (i > 400 ? NEO_RIGHT : 0);
            neo_run_frame(&g);
            g.audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
    }
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", c.frames, best,
           c.repeat > 1 ? " (mejor corrida)" : "", c.frames / best, c.frames / best / (MCLK_HZ / (M68K_PER_FRAME * MCLK_PER_M68K)));
    return 0;
}
