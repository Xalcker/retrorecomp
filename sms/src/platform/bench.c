/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros (smsbench con el intérprete; en los proyectos
 * que genere smsrecomp, bench con el código recompilado).
 *   smsbench rom.sms [--frames N] [--repeat N] [--input archivo] [--seed S] [--model sms|gg] [boton:desde-hasta ...]
 * Sin entradas, Start a los 2 s y luego la derecha. Con --repeat se reporta la corrida más rápida.
 * Con --hash corre una vez e imprime el hash de todos los cuadros y de todo el audio, el mismo que el *shot
 * --hash del intérprete (golden_hash.h): así tests/golden.py --recompilado compara el código recompilado con
 * la misma línea base.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli.h"
#include "golden_hash.h"
#include "console.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 1800)) {
        fprintf(stderr, "uso: %s rom.sms [--frames N] [--repeat N] [--input archivo] [--seed S] [--model sms|gg] [--hash]\n"
                        "                [boton:desde-hasta ...]\n", cli_prog(argv[0]));
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
    u64 total_cyc = 0;
    GoldenHash gh;
    static int16_t snd[AUDIO_BINS * 2];
    if (c.hash) c.repeat = 1;
    static Sms sms;
    Sms *g = &sms;
    z80_stat_of = g;                    /* la línea "Z80: ...": cuánto corre nativo, interpretado o saltado */
    for (int k = 0; k < c.repeat; k++) {
        z80_stat = (Z80Stat){ 0, 0, 0 };
        metrica_reiniciar(g);           /* todas las corridas dan lo mismo: se queda la última (metrica.h) */
        const char *err = NULL;
        if (k) sms_free(g);
        if (sms_load(g, rom, (size_t)sz, c.pos[0], c.model, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
        cli_rewind(&c);
        golden_init(&gh);
        u64 cyc0 = g->cyc;
        clock_t t0 = clock();
        for (long i = 0; i < c.frames; i++) {
            if (scripted) { g->pad[0] = cli_pad(&c, i, 0); g->pad[1] = cli_pad(&c, i, 1); }
            else g->pad[0] = (i > 120 && i < 125) ? SMS_START : (i > 400 ? SMS_RIGHT : 0);
            sms_run_frame(g);
            if (c.hash) {                /* lo mismo que smsshot --hash */
                int n = sms_audio_take(g, snd, AUDIO_BINS);
                golden_fnv(&gh.img, g->vdp.fb, sizeof g->vdp.fb);
                golden_fnv(&gh.aud, snd, (size_t)n * 4);
            }
            g->audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
        total_cyc = g->cyc - cyc0;
    }
    const Metrica *mres = NULL;
#ifdef SMS_RECOMP
    metrica_imprimir(&metrica, total_cyc);
    mres = &metrica;
#endif
    z80_stat_print();
    double xtr = c.frames / best / 59.92;
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", c.frames, best,
           c.repeat > 1 ? " (mejor corrida)" : "", c.frames / best, xtr);
    if (c.hash) golden_print(&gh);
    char res[200];
    int nres = snprintf(res, sizeof res, "\"cuadros\":%ld,\"segundos\":%.3f,\"x_tiempo_real\":%.1f", c.frames, best, xtr);
    if (c.hash) snprintf(res + nres, sizeof res - (size_t)nres, ",\"hash_imagen\":\"%016llx\",\"hash_audio\":\"%016llx\"", gh.img, gh.aud);
    metrica_resumen("bench", 1, res, mres, total_cyc);     /* la última línea (metrica.h) */
    return 0;
}
