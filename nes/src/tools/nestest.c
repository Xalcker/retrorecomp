/*
 * nestest: corre nestest.nes en modo automático ($C000) y compara cada
 * instrucción con nestest.log (PC, A, X, Y, P, SP, ciclos y posición de la PPU).
 *   nestest nestest.nes nestest.log
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz + 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); b[sz] = 0; *len = sz;
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: nestest nestest.nes nestest.log\n"); return 2; }
    size_t rl, ll;
    u8 *rom = slurp(argv[1], &rl);
    u8 *log = slurp(argv[2], &ll);
    if (!rom || !log) { fprintf(stderr, "no pude leer los archivos\n"); return 2; }

    static NES n;
    const char *err = NULL;
    if (nes_load(&n, rom, rl, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    n.cpu.pc = 0xC000;                  /* modo automático */

    char *line = (char *)log;
    int lineno = 0;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        lineno++;
        unsigned pc, a, x, y, p, sp, psl, pdot; unsigned long long cyc;
        char *s;
        if (sscanf(line, "%x", &pc) != 1 || !(s = strstr(line, " A:")) ||
            sscanf(s, " A:%x X:%x Y:%x P:%x SP:%x", &a, &x, &y, &p, &sp) != 5 ||
            !(s = strstr(line, "PPU:")) || sscanf(s, "PPU:%u,%u", &psl, &pdot) != 2 ||
            !(s = strstr(line, "CYC:")) || sscanf(s, "CYC:%llu", &cyc) != 1) {
            fprintf(stderr, "línea %d del log ilegible: %s\n", lineno, line);
            return 2;
        }
        ppu_flush(&n);
        Cpu *c = &n.cpu;
        if (c->pc != pc || c->a != a || c->x != x || c->y != y || c->p != p || c->sp != sp || n.cyc != cyc ||
            (unsigned)n.ppu.scanline != psl || (unsigned)n.ppu.dot != pdot) {
            printf("DIFERENCIA en la línea %d\n  log:  %s\n  nes:  PC:%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X PPU:%d,%d CYC:%llu\n",
                   lineno, line, c->pc, c->a, c->x, c->y, c->p, c->sp, n.ppu.scanline, n.ppu.dot, (unsigned long long)n.cyc);
            return 1;
        }
        cpu_step(&n);
        if (!eol) break;
        line = eol + 1;
    }
    printf("nestest: %d instrucciones iguales al log. Resultado $02=%02X $03=%02X (00 = bien)\n",
           lineno, n.ram[2], n.ram[3]);
    return (n.ram[2] || n.ram[3]) ? 1 : 0;
}
