/*
 * gentrace: corre una ROM sin ventana y guarda qué instrucciones de ROM ejecutó (líneas
 * "OFFSET:DIRECCION" en hexadecimal) para guiar a genrecomp.
 *   gentrace rom.bin traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * Con varias corridas se pueden juntar trazas (genrecomp acepta --trace varias veces).
 */
#include <stdio.h>

#include "cli.h"
#include "console.h"
#include "romload.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: gentrace rom.bin traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    Gen *g = load_gen(c.pos[0]);
    if (!g) return 2;
    gen_trace_start(g);
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        gen_run_frame(g);
        g->audio.n = 0;
    }
    if (!gen_trace_write(g, c.pos[1])) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.pos[1], gen_trace_count(g), c.frames);
    return 0;
}
