/*
 * neotrace: corre un juego sin ventana y guarda qué instrucciones de ROM (del juego y de la BIOS) ejecutó (líneas
 * "BANCO:DIRECCION" en hexadecimal, runtime/trace_line.h) para guiar a neorecomp.
 *   neotrace juego traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * Con varias corridas se pueden juntar trazas (neorecomp acepta --trace varias veces).
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
        fprintf(stderr, "uso: neotrace juego traza.txt [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    Neo *g = load_neo(c.pos[0]);
    if (!g) return 2;
    neo_trace_start(g);
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        neo_run_frame(g);
        g->audio.n = 0;
    }
    if (!neo_trace_write(g, c.pos[1])) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.pos[1], neo_trace_count(g), c.frames);
    return 0;
}
