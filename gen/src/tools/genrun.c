/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos,
 * excepciones por vector y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de
 * espera). Sirve para ver hasta dónde llega un juego mientras falta el resto de la consola.
 *
 *   genrun rom.bin [--frames N] [--watch DIR ...] [boton:desde-hasta ...]
 *
 * Botones: up down left right a b c start x y z mode (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $FF0000-$FFFFFF) imprime el byte cada vez que cambia.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"

static u8 *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

typedef struct { int btn; long from, to; } Hold;

/* Histograma de PC del último cuadro (direccionamiento abierto) */
#define HN 8192
static u32 hpc[HN], hcount[HN];
static void hist_add(u32 pc)
{
    u32 i = (pc * 2654435761u) % HN;
    while (hcount[i] && hpc[i] != pc) i = (i + 1) % HN;
    hpc[i] = pc; hcount[i]++;
}

int main(int argc, char **argv)
{
    console_utf8();
    static const char *names[] = { "up", "down", "left", "right", "b", "c", "a", "start", "z", "y", "x", "mode" };
    const char *rom = NULL;
    long frames = 600;
    Hold h[32]; int nh = 0;
    u32 watch[16]; int nw = 0; int wlast[16];
    for (int i = 1; i < argc; i++) {
        char name[16]; long f0, f1;
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc && nw < 16) {
            const char *a = argv[++i];
            if (*a == '$') a++;
            watch[nw] = (u32)strtoul(a, NULL, 16) & 0xFFFF; wlast[nw++] = -1;
        }
        else if (sscanf(argv[i], "%15[a-z]:%ld-%ld", name, &f0, &f1) == 3) {
            int ok = 0;
            for (int k = 0; k < 12 && nh < 32; k++) if (!strcmp(name, names[k])) { h[nh++] = (Hold){ 1 << k, f0, f1 }; ok = 1; }
            if (!ok) { fprintf(stderr, "botón desconocido: %s\n", name); return 2; }
        } else if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); return 2; }
        else rom = argv[i];
    }
    if (!rom || frames <= 0) {
        fprintf(stderr, "uso: genrun rom.bin [--frames N] [--watch DIR ...] [boton:desde-hasta ...]\n"
                        "  botones: up down left right a b c start x y z mode\n");
        return 2;
    }
    size_t len;
    u8 *data = read_file(rom, &len);
    if (!data) { fprintf(stderr, "no se pudo leer %s\n", rom); return 1; }
    Gen *g = malloc(sizeof *g);
    const char *err = NULL;
    if (gen_load(g, data, len, &err)) { fprintf(stderr, "%s: %s\n", rom, err); return 1; }
    free(data);

    u64 steps = 0;
    for (long f = 0; f < frames; f++) {
        u16 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].from && f <= h[i].to) pad |= (u16)h[i].btn;
        g->pad[0] = pad;
        int last = f == frames - 1;
        u64 fr = g->vdp.frame;
        while (g->vdp.frame == fr) {
            if (last) hist_add(g->cpu.pc);
            m68k_step(g);
            vdp_sync(g);
            steps++;
            if (g->cpu.halted) break;
        }
        for (int i = 0; i < nw; i++)
            if (g->ram[watch[i]] != wlast[i]) {
                wlast[i] = g->ram[watch[i]];
                printf("cuadro %5ld: [$FF%04X] = $%02X\n", f, watch[i], wlast[i]);
            }
        if (g->cpu.halted) { printf("el CPU se detuvo (doble falta) en el cuadro %ld\n", f); break; }
    }

    M68k *c = &g->cpu;
    printf("%s\n", g->cart.hdr.name_int);
    printf("cuadros:       %llu\n", (unsigned long long)g->vdp.frame);
    printf("instrucciones: %llu (%.0f por cuadro)\n", (unsigned long long)steps, (double)steps / (double)(g->vdp.frame ? g->vdp.frame : 1));
    printf("ciclos:        %llu\n", (unsigned long long)g->cyc);
    printf("PC $%06X  SR $%04X%s%s\n", c->pc, m68k_get_sr(c), c->stopped ? "  (STOP)" : "", c->halted ? "  (detenido)" : "");
    if (g->cart.mapper == MAPPER_SSF2) printf("mapper SSF2:   %u cambios de mapeo\n", g->cart.map_ver);
    printf("excepciones:\n");
    for (int n = 0; n < 64; n++)
        if (g->exc_count[n]) {
            const char *nm = cart_vector_name(n);
            char buf[16];
            if (!nm) { snprintf(buf, sizeof buf, "vector %d", n); nm = buf; }
            printf("  %-22s %u\n", nm, g->exc_count[n]);
        }
    printf("PC más frecuentes en el último cuadro:\n");
    for (int k = 0; k < 8; k++) {
        u32 best = 0, bi = 0;
        for (u32 i = 0; i < HN; i++) if (hcount[i] > best) { best = hcount[i]; bi = i; }
        if (!best) break;
        printf("  $%06X  %u\n", hpc[bi], best);
        hcount[bi] = 0;
    }
    gen_free(g);
    free(g);
    return 0;
}
