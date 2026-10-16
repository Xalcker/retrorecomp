/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros (smsbench con el intérprete; en los proyectos
 * que genere smsrecomp, bench con el código recompilado).
 *   smsbench rom.sms [--frames N] [--repeat N] [--input archivo] [--seed S] [--model sms|gg] [boton:desde-hasta ...]
 * Sin entradas, Start a los 2 s y luego la derecha. Con --repeat se reporta la corrida más rápida.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli.h"
#include "console.h"

/* Va también en los proyectos que genere smsrecomp, sin src/tools: --model se lee aquí (cli.h no lo conoce) */
static int take_model(int *argc, char **argv)
{
    int model = MODEL_AUTO;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--model")) continue;
        if (i + 1 >= *argc || (model = model_from_name(argv[i + 1])) < MODEL_AUTO) return -2;
        for (int k = i; k + 2 <= *argc; k++) argv[k] = argv[k + 2];
        *argc -= 2;
        i--;
    }
    return model;
}

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    int model = take_model(&argc, argv);
    if (model < MODEL_AUTO || cli_parse(&c, argc, argv, 1, 1800)) {
        fprintf(stderr, "uso: smsbench rom.sms [--frames N] [--repeat N] [--input archivo] [--seed S] [--model sms|gg]\n"
                        "                [boton:desde-hasta ...]\n");
        return 2;
    }
    FILE *f = fopen(c.pos[0], "rb");
    if (!f) { fprintf(stderr, "no se pudo leer %s\n", c.pos[0]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = sz > 0 ? malloc((size_t)sz) : NULL;
    if (!rom || fread(rom, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "no se pudo leer %s\n", c.pos[0]); return 2; }
    fclose(f);
    int scripted = c.input || c.nh || c.use_seed;
    double best = 0;
    static Sms sms;
    Sms *g = &sms;
    z80_stat_of = g;                    /* la línea "Z80: ...": cuánto corre nativo, interpretado o saltado */
    for (int k = 0; k < c.repeat; k++) {
        z80_stat = (Z80Stat){ 0, 0, 0 };
        const char *err = NULL;
        if (k) sms_free(g);
        if (sms_load(g, rom, (size_t)sz, c.pos[0], model, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
        cli_rewind(&c);
        clock_t t0 = clock();
        for (long i = 0; i < c.frames; i++) {
            if (scripted) { g->pad[0] = cli_pad(&c, i, 0); g->pad[1] = cli_pad(&c, i, 1); }
            else g->pad[0] = (i > 120 && i < 125) ? SMS_START : (i > 400 ? SMS_RIGHT : 0);
            sms_run_frame(g);
            g->audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
    }
    z80_stat_print();
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", c.frames, best,
           c.repeat > 1 ? " (mejor corrida)" : "", c.frames / best, c.frames / best / 59.92);
    return 0;
}
