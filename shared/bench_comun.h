/*
 * bench: velocidad de emulación sin ventana ni límite de cuadros, común a los cinco recompiladores (retrorecomp/shared). Es el
 * *bench de cada consola (con el intérprete) y el bench de los proyectos generados (con el código recompilado, -D<CONSOLA>_RECOMP).
 *   <sistema>bench rom [--frames N] [--repeat N] [--input archivo] [--seed S] [--profile] [--hash] [boton:desde-hasta ...]
 * (también vale: bench rom cuadros). Sin entradas, Start a los 2 s y luego la derecha. Con --repeat se reporta la corrida más
 * rápida. Con --profile, en qué se fue el tiempo (prof.h). Con --hash corre una vez e imprime el hash de todos los cuadros y de
 * todo el audio, el mismo que el *shot --hash del intérprete (golden_hash.h): así tests/golden.py --recompilado compara el código
 * recompilado con la misma línea base. La última línea es el RESUMEN {json} (metrica.h).
 *
 * Cada bench.c define PROF_SAMPLER (y, fuera de Windows, _POSIX_C_SOURCE 200809L) antes de cualquier include, para tener el hilo de
 * prof.h; incluye el encabezado de su consola, después este, llena un BenchOps y llama a bench_main.
 */
#ifndef BENCH_COMUN_H
#define BENCH_COMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli.h"
#include "console.h"
#include "golden_hash.h"
#include "metrica.h"
#include "prof.h"

typedef struct {
    const char *uso_rom;        /* «rom.bin», «juego»...: lo primero del uso */
    const char *uso_extra;      /* opciones propias de la consola para el uso, p. ej. " [--model sms|gg]" ("" si no hay) */
    long cuadros;               /* por omisión */
    double fps;                 /* cuadros por segundo de la consola real */
    int recompilado;            /* el bench del proyecto generado: imprime la métrica común */
    const char *cpu;            /* el procesador principal, para --profile: «68000», «6502»... */
    int con_z80;                /* la consola marca PROF_Z80 (una segunda CPU, la del sonido) */
    int sin_leer;               /* el juego no es un archivo que se lea entero (el Neo Geo: zip o carpeta): cargar recibe solo la ruta */
    unsigned start, derecha;    /* los botones de las entradas por omisión */
    size_t tam;                 /* sizeof del estado de la consola */
    const char *(*verificar)(const u8 *rom, size_t len);                    /* NULL, o el motivo si no es la ROM que se recompiló */
    const char *(*cargar)(void *g, const u8 *rom, size_t len, const Cli *c); /* NULL, o el motivo si no la pudo cargar */
    void (*liberar)(void *g);                                               /* antes de volver a cargar (puede ser NULL) */
    void (*reiniciar)(void *g);                                             /* tras cargar: la métrica, las estadísticas del Z80... */
    void (*pad)(void *g, unsigned p1, unsigned p2);
    void (*cuadro)(void *g);
    void (*hash)(void *g, GoldenHash *gh);                                  /* lo que entra al hash de ese cuadro */
    void (*vaciar)(void *g);                                                /* tras cada cuadro: el audio que no se usa */
    u64 (*ciclos)(const void *g);
    void (*extra)(void *g, const Cli *c);                                   /* líneas propias tras la métrica (puede ser NULL) */
} BenchOps;

static inline u8 *bench_leer(const char *ruta, size_t *len)
{
    FILE *f = fopen(ruta, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = sz > 0 ? malloc((size_t)sz) : NULL;
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = (size_t)sz;
    return b;
}

static inline int bench_main(int argc, char **argv, const BenchOps *o)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, o->cuadros)) {
        fprintf(stderr, "uso: %s %s [--frames N] [--repeat N] [--input archivo] [--seed S]%s [--profile] [--hash]\n"
                        "          [boton:desde-hasta ...]\n", cli_prog(argv[0]), o->uso_rom, o->uso_extra);
        return 2;
    }
    size_t len = 0;
    u8 *rom = NULL;
    if (!o->sin_leer) {
        rom = bench_leer(c.pos[0], &len);
        if (!rom) { fprintf(stderr, "no se pudo leer %s\n", c.pos[0]); return 2; }
    }
    if (o->verificar) {
        const char *mal = o->verificar(rom, len);
        if (mal) { fprintf(stderr, "%s\n", mal); return 2; }
    }
    void *g = calloc(1, o->tam);
    if (!g) { fprintf(stderr, "sin memoria\n"); return 2; }
    int guion = c.input || c.nh || c.use_seed;
    double mejor = 0;
    u64 total_cyc = 0;
    GoldenHash gh;
    if (c.hash) c.repeat = 1;
    if (c.profile && !prof_start()) { fprintf(stderr, "no se pudo arrancar el hilo de --profile\n"); return 2; }
    for (int k = 0; k < c.repeat; k++) {
        if (k && o->liberar) o->liberar(g);
        const char *err = o->cargar(g, rom, len, &c);
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
        cli_rewind(&c);
        golden_init(&gh);
        o->reiniciar(g);                    /* todas las corridas dan lo mismo: se queda la última */
        u64 cyc0 = o->ciclos(g);
        clock_t t0 = clock();
        for (long i = 0; i < c.frames; i++) {
            if (guion) o->pad(g, cli_pad(&c, i, 0), cli_pad(&c, i, 1));
            else o->pad(g, (i > 120 && i < 125) ? o->start : (i > 400 ? o->derecha : 0), 0);
            o->cuadro(g);
            if (c.hash) o->hash(g, &gh);
            o->vaciar(g);
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < mejor) mejor = s;
        total_cyc = o->ciclos(g) - cyc0;
    }
    if (c.profile) prof_stop();
    double xtr = mejor > 0 ? (double)c.frames / mejor / o->fps : 0.0;
    printf("%ld cuadros en %.2f s%s: %.0f cuadros/s (%.1fx tiempo real)\n", c.frames, mejor, c.repeat > 1 ? " (mejor corrida)" : "",
           mejor > 0 ? (double)c.frames / mejor : 0.0, xtr);
    const Metrica *mres = NULL;
    if (o->recompilado) {
        metrica_imprimir(&metrica, total_cyc);      /* la métrica común (metrica.h) */
        mres = &metrica;
    }
    if (o->extra) o->extra(g, &c);
    if (c.profile) prof_print(o->cpu, o->con_z80);
    if (c.hash) golden_print(&gh);
    char res[200];
    int nres = snprintf(res, sizeof res, "\"cuadros\":%ld,\"segundos\":%.3f,\"x_tiempo_real\":%.1f", c.frames, mejor, xtr);
    if (c.hash) snprintf(res + nres, sizeof res - (size_t)nres, ",\"hash_imagen\":\"%016llx\",\"hash_audio\":\"%016llx\"", gh.img, gh.aud);
    metrica_resumen("bench", 1, res, mres, total_cyc);     /* la última línea (metrica.h) */
    return 0;
}

#endif
