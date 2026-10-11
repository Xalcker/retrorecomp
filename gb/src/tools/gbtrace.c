/*
 * gbtrace: corre una ROM con el intérprete, sin ventana, y guarda qué direcciones de ROM
 * ejecutó (líneas BB:AAAA en hexadecimal, el formato de --uncovered) para guiar a gbrecomp.
 *
 * Uso: gbtrace rom.gb traza.txt [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]
 * Con varias corridas se pueden juntar trazas (gbrecomp acepta --trace varias veces).
 */
#include "cli.h"

#define MAX_BANKS 512
static unsigned char *executed[MAX_BANKS];

static void record(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 bank;
    u16 off;
    if (pc < 0x4000) { bank = g->mbc.bank0; off = pc; }
    else if (pc < 0x8000) { bank = g->mbc.bankx; off = (u16)(pc - 0x4000); }
    else return;
    if (bank >= MAX_BANKS) return;
    if (!executed[bank]) executed[bank] = calloc(0x4000, 1);
    executed[bank][off] = 1;
}

int main(int argc, char **argv)
{
    console_utf8();
    Cli c;
    if (cli_parse(&c, argc, argv, "uso: gbtrace rom.gb traza.txt [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]")) return 2;
    long size;
    unsigned char *rom = cli_read_file(c.rom, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", c.rom); return 2; }
    static GB g;
    const char *err = gb_init_model(&g, rom, (u32)size, c.model);
    if (err) { fprintf(stderr, "%s: %s\n", c.rom, err); return 2; }
    g.trace = record;
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f));
        gb_run_frame(&g);
    }
    FILE *o = fopen(c.out, "w");
    if (!o) { fprintf(stderr, "no se pudo escribir %s\n", c.out); return 2; }
    size_t n = 0;
    for (u32 b = 0; b < MAX_BANKS; b++) {
        if (!executed[b]) continue;
        for (u32 off = 0; off < 0x4000; off++) {
            if (!executed[b][off]) continue;
            fprintf(o, "%02X:%04X\n", b, b == 0 ? off : 0x4000 + off);
            n++;
        }
    }
    fclose(o);
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.out, n, c.frames);
    gb_free(&g);
    free(rom);
    return 0;
}
