/*
 * neoramdump: corre un juego sin ventana y vuelca los 64 KB de RAM del 68000 (una línea "DIRECCION VALOR"
 * por byte, con la dirección de $100000 a $10FFFF). Como nesramdump de NESRecomp, gbramdump de GBRecomp
 * y genramdump de GENRecomp.
 *   neoramdump juego [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 */
#include <stdio.h>

#include "cli.h"
#include "console.h"
#include "romload.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 60)) {
        fprintf(stderr, "uso: neoramdump juego [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    Neo *g = load_neo(c.pos[0]);
    if (!g) return 2;
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        neo_run_frame(g);
        g->audio.n = 0;
    }
    for (u32 a = 0; a < 0x10000; a++) printf("%06X %02X\n", 0x100000 | a, g->ram[a]);
    return 0;
}
