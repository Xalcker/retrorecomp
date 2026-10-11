/*
 * gbramdump: corre una ROM sin ventana y vuelca la RAM de trabajo como la ve el CPU: WRAM
 * ($C000-$DFFF; en el GBC, $D000-$DFFF es el banco seleccionado) y HRAM ($FF80-$FFFE), una
 * línea "DIRECCION VALOR" por byte. Es el equivalente de ramdump de NESRecomp.
 *
 * Uso: gbramdump rom.gb [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]
 */
#include "cli.h"

int main(int argc, char **argv)
{
    console_utf8();
    Cli c;
    if (cli_parse_n(&c, argc, argv, "uso: gbramdump rom.gb [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]", 1))
        return 2;
    long size;
    unsigned char *rom = cli_read_file(c.rom, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", c.rom); return 2; }
    static GB g;
    const char *err = gb_init_model(&g, rom, (u32)size, c.model);
    if (err) { fprintf(stderr, "%s: %s\n", c.rom, err); return 2; }
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f));
        gb_run_frame(&g);
    }
    for (unsigned a = 0xC000; a < 0xE000; a++) printf("%04X %02X\n", a, gb_peek(&g, (u16)a));
    for (unsigned a = 0xFF80; a < 0xFFFF; a++) printf("%04X %02X\n", a, gb_peek(&g, (u16)a));
    gb_free(&g);
    free(rom);
    return 0;
}
