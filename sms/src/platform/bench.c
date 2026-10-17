/*
 * bench de Master System y Game Gear: velocidad de emulación sin ventana ni límite de cuadros (smsbench con el intérprete; en los
 * proyectos que genere smsrecomp, bench con el código recompilado). El esqueleto, las opciones y la salida son los de bench_comun.h
 * (en ../shared); aquí va lo propio de la consola: --model sms|gg y cuánto del Z80 corrió nativo, interpretado o saltado.
 */
#define PROF_SAMPLER        /* antes de cualquier include: sms.h incluye prof.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L     /* nanosleep con -std=c99 (el hilo de prof.h) */
#endif
#include "bench_comun.h"

static Sms *gg(void *g) { return g; }

static const char *cargar(void *g, const u8 *rom, size_t len, const Cli *c)
{
    static char msg[200];
    const char *err = NULL;
    if (!sms_load(gg(g), rom, len, c->pos[0], c->model, &err)) return NULL;
    snprintf(msg, sizeof msg, "ROM: %s", err);
    return msg;
}

static void liberar(void *g) { sms_free(gg(g)); }

static void reiniciar(void *g)
{
    z80_stat_of = g;                /* la línea «Z80: ...»: cuánto corre nativo, interpretado o saltado */
    z80_stat = (Z80Stat){ 0, 0, 0 };
    metrica_reiniciar(g);           /* todas las corridas dan lo mismo: se queda la última (metrica.h) */
}

static void poner_pad(void *g, unsigned p1, unsigned p2) { gg(g)->pad[0] = (u8)p1; gg(g)->pad[1] = (u8)p2; }
static void cuadro(void *g) { sms_run_frame(gg(g)); }

static void hash(void *g, GoldenHash *gh)       /* lo mismo que smsshot --hash */
{
    static int16_t snd[AUDIO_BINS * 2];
    int n = sms_audio_take(gg(g), snd, AUDIO_BINS);
    golden_fnv(&gh->img, gg(g)->vdp.fb, sizeof gg(g)->vdp.fb);
    golden_fnv(&gh->aud, snd, (size_t)n * 4);
}

static void vaciar(void *g) { gg(g)->audio.n = 0; }
static u64 ciclos(const void *g) { return ((const Sms *)g)->cyc; }
static void extra(void *g, const Cli *c) { (void)g; (void)c; z80_stat_print(); }

int main(int argc, char **argv)
{
    BenchOps o = {
        .uso_rom = "rom.sms", .uso_extra = " [--model sms|gg]", .cuadros = 1800, .fps = 59.92,
#ifdef SMS_RECOMP
        .recompilado = 1,
#endif
        .cpu = "Z80", .con_z80 = 0, .start = SMS_START, .derecha = SMS_RIGHT, .tam = sizeof(Sms),
        .cargar = cargar, .liberar = liberar, .reiniciar = reiniciar, .pad = poner_pad, .cuadro = cuadro, .hash = hash,
        .vaciar = vaciar, .ciclos = ciclos, .extra = extra,
    };
    return bench_main(argc, argv, &o);
}
