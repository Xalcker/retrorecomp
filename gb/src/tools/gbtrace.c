/*
 * gbtrace: corre una ROM con el intérprete, sin ventana, y guarda qué direcciones de ROM
 * ejecutó (líneas BB:AAAA en hexadecimal, runtime/trace_line.h, el formato común con NESRecomp
 * y GENRecomp y el de --uncovered) para guiar a gbrecomp. Si el juego ejecuta código desde WRAM
 * o HRAM, también anota sus bytes en líneas "ram DIRECCION BYTES" (platform/ramtrace.h), las
 * mismas que escriben difftest y el ejecutable del juego con --uncovered: ese código no está en la
 * ROM donde corre y gbrecomp solo puede aprenderlo de lo que se ejecutó.
 *
 * Uso: gbtrace rom.gb traza.txt [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]
 * Con varias corridas se pueden pasar todas las trazas a gbrecomp (--trace se puede repetir) o
 * juntarlas con cat y sort -u, como hace ../shared/recomp_game.py.
 */
#include "cli.h"
#include "console.h"
#include "romload.h"
#include "../platform/ramtrace.h"
#include "../platform/romtrace.h"
#include "trace_line.h"

static unsigned char *executed[ROM_SLOTS];     /* por ranura (romtrace.h) */

static void record(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 slot;
    u16 off;
    if (ramtrace_here(pc)) {
        ramtrace_observe(g, pc);
        return;
    }
    if (!rom_slot(g, pc, &slot, &off)) return;
    if (!executed[slot]) executed[slot] = calloc(0x4000, 1);
    executed[slot][off] = 1;
}

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: gbtrace rom.gb traza.txt [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb]\n");
        return 2;
    }
    static GB g;
    if (load_gb(&g, c.pos[0], c.model)) return 2;
    g.trace = record;
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f, 0));
        gb_run_frame(&g);
    }
    FILE *o = fopen(c.pos[1], "w");
    if (!o) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    size_t n = 0;
    for (u32 s = 0; s < ROM_SLOTS; s++) {
        if (!executed[s]) continue;
        for (u32 off = 0; off < 0x4000; off++) {
            if (!executed[s][off]) continue;
            trace_line_write(o, 14, 4, (unsigned long)rom_slot_bank(s) << 14 | off, rom_slot_addr(s, (u16)off));
            n++;
        }
    }
    RcSummary rs = ramtrace_write(o);
    fclose(o);
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.pos[1], n, c.frames);
    if (rs.fixed || rs.vol || rs.multi || rs.lost)
        printf("Código en WRAM/HRAM: %u instrucciones fijas, %u con operandos variables, %u con varias "
               "versiones, %u que cambian demasiado\n", rs.fixed, rs.vol, rs.multi, rs.lost);
    gb_free(&g);
    return 0;
}
