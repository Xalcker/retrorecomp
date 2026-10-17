/*
 * bench de Neo Geo: velocidad de emulación sin ventana ni límite de cuadros (neobench con el intérprete; en los proyectos generados,
 * bench con el código recompilado). El esqueleto, las opciones y la salida son los de bench_comun.h (en ../shared); aquí va lo propio
 * de la consola. También dice cuánto del Z80 corrió nativo, interpretado o saltado.
 */
#define PROF_SAMPLER        /* antes de cualquier include: neo.h incluye prof.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L     /* nanosleep con -std=c99 (el hilo de prof.h) */
#endif
#include "bench_comun.h"

static Neo *gg(void *g) { return g; }

static const char *cargar(void *g, const u8 *rom, size_t len, const Cli *c)
{
    (void)rom; (void)len;
    static char msg[1300];
    const char *err = NULL;
    if (!neo_load(gg(g), c->pos[0], NULL, &err)) return NULL;
    snprintf(msg, sizeof msg, "%s: %s", c->pos[0], err);
    return msg;
}

static void liberar(void *g) { neo_free(gg(g)); }

static void reiniciar(void *g)
{
    z80_stat_of = g;
    z80_stat = (Z80Stat){ 0 };      /* todas las corridas dan lo mismo: se queda la última */
    metrica_reiniciar(g);           /* lo mismo con la métrica común (metrica.h) */
}

static void poner_pad(void *g, unsigned p1, unsigned p2) { gg(g)->pad[0] = (u16)p1; gg(g)->pad[1] = (u16)p2; }
static void cuadro(void *g) { neo_run_frame(gg(g)); }

static void hash(void *g, GoldenHash *gh)       /* lo mismo que neoshot --hash */
{
    static int16_t snd[AUDIO_BINS * 2];
    int n = neo_audio_take(gg(g), snd, AUDIO_BINS);
    golden_fnv(&gh->img, gg(g)->fb, sizeof gg(g)->fb);
    golden_fnv(&gh->aud, snd, (size_t)n * 4);
}

static void vaciar(void *g) { gg(g)->audio.n = 0; }
static u64 ciclos(const void *g) { return ((const Neo *)g)->cyc; }
static void extra(void *g, const Cli *c) { (void)g; (void)c; z80_stat_print(); }

int main(int argc, char **argv)
{
    BenchOps o = {
        .uso_rom = "juego", .uso_extra = "", .cuadros = 1800, .fps = MCLK_HZ / (M68K_PER_FRAME * MCLK_PER_M68K),
#ifdef NEO_RECOMP
        .recompilado = 1,
#endif
        .cpu = "68000", .con_z80 = 1, .sin_leer = 1, .start = NEO_START, .derecha = NEO_RIGHT, .tam = sizeof(Neo),
        .cargar = cargar, .liberar = liberar, .reiniciar = reiniciar, .pad = poner_pad, .cuadro = cuadro, .hash = hash,
        .vaciar = vaciar, .ciclos = ciclos, .extra = extra,
    };
    return bench_main(argc, argv, &o);
}
