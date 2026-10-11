/*
 * ramdump: corre una ROM sin ventana y vuelca los 2 KB de RAM (una línea "DIRECCION VALOR" por byte).
 *   ramdump rom.nes [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: ramdump rom.nes cuadros [boton:desde-hasta ...])
 * Lo usa tests/accuracycoin.py. En GBRecomp, gbramdump hace lo mismo con la WRAM y la HRAM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

int main(int argc, char **argv)
{
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 60)) {
        fprintf(stderr, "uso: ramdump rom.nes [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n");
        return 2;
    }
    FILE *f = fopen(c.pos[0], "rb");
    if (!f) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 2;
    fclose(f);
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, sz, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    for (long i = 0; i < c.frames; i++) {
        n.pad[0] = cli_pad(&c, i, 0);
        n.pad[1] = cli_pad(&c, i, 1);
        nes_run_frame(&n);
        n.apu.nbuf = 0;
    }
    for (int a = 0; a < 0x800; a++) printf("%04X %02X\n", a, n.ram[a]);
    return 0;
}
