/*
 * bench de Game Boy: velocidad de emulación (CPU, PPU, APU...) sin ventana ni límite de cuadros (gbbench con el intérprete; en los
 * proyectos generados, bench con el código recompilado). El esqueleto, las opciones y la salida son los de bench_comun.h (en
 * ../shared); aquí va lo propio de la consola: --model dmg|cgb|sgb, y el audio, que se genera y se saca de la cola en cada cuadro,
 * como en la ventana. Compilado con -DGB_RECOMP (el bench del proyecto generado, que solo acepta la ROM que se recompiló) mide el
 * código recompilado. Por omisión corre 3600 cuadros, un minuto de juego.
 */
#define PROF_SAMPLER        /* antes de cualquier include: gb.h incluye prof.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L     /* nanosleep con -std=c99 (el hilo de prof.h) */
#endif
#include "bench_comun.h"
#include "gb.h"
#include "gb_golden.h"
#ifdef GB_RECOMP
#include "recomp.h"
#include "sha1.h"
#endif

static GB *gg(void *g) { return g; }

#ifdef GB_RECOMP
static const char *verificar(const u8 *rom, size_t len)
{
    static char msg[160];
    char sha1[41];
    sha1_hex(rom, len, sha1);
    if (strcmp(sha1, recomp_rom_sha1) == 0) return NULL;
    snprintf(msg, sizeof msg, "la ROM no coincide: SHA1 %s, se esperaba %s", sha1, recomp_rom_sha1);
    return msg;
}
#endif

static const char *cargar(void *g, const u8 *rom, size_t len, const Cli *c)
{
    static char msg[300];
    const char *err = gb_init_model(gg(g), rom, (u32)len, c->model);
    if (err) {
        snprintf(msg, sizeof msg, "%s: %s", c->pos[0], err);
        return msg;
    }
    gg(g)->apu.enabled = 1;         /* como en la ventana: el APU mezcla y genera muestras */
#ifdef GB_RECOMP
    gg(g)->recomp_lookup = recomp_lookup;
#endif
    return NULL;
}

static void liberar(void *g) { gb_free(gg(g)); }
static void reiniciar(void *g) { metrica_reiniciar(g); }       /* todas las corridas dan lo mismo: se queda la última */
static void poner_pad(void *g, unsigned p1, unsigned p2) { (void)p2; gb_set_pad(gg(g), (u8)p1); }
static void cuadro(void *g) { gb_run_frame(gg(g)); }
static void hash(void *g, GoldenHash *gh) { gb_golden_frame(gg(g), gh); }

static void vaciar(void *g)
{
    static float aud[4096 * 2];
    while (gb_audio_pull(gg(g), aud, 4096) > 0) {}
}

static u64 ciclos(const void *g) { return ((const GB *)g)->cyc; }

int main(int argc, char **argv)
{
    BenchOps o = {
        /* Un cuadro dura GB_CYCLES_FRAME ciclos a GB_CPU_HZ (59.73 por segundo) */
        .uso_rom = "rom.gb", .uso_extra = " [--model dmg|cgb|sgb]", .cuadros = 3600, .fps = (double)GB_CPU_HZ / GB_CYCLES_FRAME,
#ifdef GB_RECOMP
        .recompilado = 1, .verificar = verificar,
#endif
        .cpu = "SM83", .con_z80 = 0, .start = PAD_START, .derecha = PAD_RIGHT, .tam = sizeof(GB),
        .cargar = cargar, .liberar = liberar, .reiniciar = reiniciar, .pad = poner_pad, .cuadro = cuadro, .hash = hash,
        .vaciar = vaciar, .ciclos = ciclos,
    };
    return bench_main(argc, argv, &o);
}
