/*
 * nestrace: corre una ROM sin ventana y guarda qué instrucciones de PRG ejecutó
 * (líneas "BANCO:DIRECCION" en hexadecimal, runtime/trace_line.h) para guiar a nesrecomp.
 *   nestrace rom.nes traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: nestrace rom.nes traza.txt cuadros [boton:desde-hasta ...])
 * Con varias corridas se pueden juntar trazas (nesrecomp acepta --trace varias veces).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

int main(int argc, char **argv)
{
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: nestrace rom.nes traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    size_t len; u8 *rom = slurp(c.pos[0], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    nes_trace_start(&n);
    for (long f = 0; f < c.frames; f++) {
        n.pad[0] = cli_pad(&c, f, 0);
        n.pad[1] = cli_pad(&c, f, 1);
        nes_run_frame(&n);
    }
    size_t cnt = 0;
    for (size_t i = 0; i < n.cart.prg_size; i++) cnt += n.trace[i] != 0;
    if (!nes_trace_write(&n, c.pos[1])) { fprintf(stderr, "no pude escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.pos[1], cnt, c.frames);
    return 0;
}
