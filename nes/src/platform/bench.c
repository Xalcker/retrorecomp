/*
 * bench de NES: velocidad de emulación sin ventana ni límite de cuadros (nesbench con el intérprete; en los proyectos generados,
 * bench con el código recompilado, compilado con -DNES_RECOMP). El esqueleto, las opciones y la salida son los de bench_comun.h
 * (en ../shared); aquí va lo propio de la consola.
 */
#define PROF_SAMPLER        /* antes de cualquier include: nes.h incluye prof.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L     /* nanosleep con -std=c99 (el hilo de prof.h) */
#endif
#include "bench_comun.h"

static NES *gg(void *g) { return g; }

static const char *cargar(void *g, const u8 *rom, size_t len, const Cli *c)
{
    (void)c;
    const char *err = NULL;
    if (!nes_load(gg(g), rom, len, &err)) return NULL;
    return err ? err : "no se pudo cargar la ROM";
}

static void reiniciar(void *g) { metrica_reiniciar(g); }       /* todas las corridas dan lo mismo: se queda la última */
static void poner_pad(void *g, unsigned p1, unsigned p2) { gg(g)->pad[0] = (u8)p1; gg(g)->pad[1] = (u8)p2; }
static void cuadro(void *g) { nes_run_frame(gg(g)); }

static void hash(void *g, GoldenHash *gh)       /* lo mismo que nesshot --hash */
{
    NES *n = gg(g);
    golden_fnv(&gh->img, n->ppu.fb, sizeof n->ppu.fb);
    golden_fnv(&gh->aud, n->apu.buf, (size_t)n->apu.nbuf * sizeof n->apu.buf[0]);
}

static void vaciar(void *g) { gg(g)->apu.nbuf = 0; }
static u64 ciclos(const void *g) { return ((const NES *)g)->cyc; }

int main(int argc, char **argv)
{
    BenchOps o = {
        .uso_rom = "rom.nes", .uso_extra = "", .cuadros = 1800, .fps = 60.0988,
#ifdef NES_RECOMP
        .recompilado = 1,
#endif
        .cpu = "6502", .con_z80 = 0, .start = BTN_START, .derecha = BTN_RIGHT, .tam = sizeof(NES),
        .cargar = cargar, .reiniciar = reiniciar, .pad = poner_pad, .cuadro = cuadro, .hash = hash, .vaciar = vaciar,
        .ciclos = ciclos,
    };
    return bench_main(argc, argv, &o);
}
