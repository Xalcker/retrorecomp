/*
 * statetest: prueba los estados guardados. Corre N cuadros, guarda, corre M más y anota la imagen
 * y los registros; carga el estado, vuelve a correr M cuadros y debe dar exactamente lo mismo.
 *   statetest rom.nes [N] [M]
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
    u8 *b = malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

static void run(NES *n, int frames, int from)
{
    for (int i = 0; i < frames; i++) {
        n->pad[0] = ((from + i) % 90 < 3) ? BTN_START : (((from + i) / 7) % 3 ? BTN_RIGHT : BTN_A);
        nes_run_frame(n);
        n->apu.nbuf = 0;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: statetest rom.nes [N] [M]\n"); return 2; }
    size_t len; u8 *rom = slurp(argv[1], &len);
    if (!rom) return 2;
    int N = argc > 2 ? atoi(argv[2]) : 400, M = argc > 3 ? atoi(argv[3]) : 200;
    static NES a, b; const char *err;
    if (nes_load(&a, rom, len, &err) || nes_load(&b, rom, len, &err)) { fprintf(stderr, "%s\n", err); return 2; }
    run(&a, N, 0);
    size_t sz; u8 *st = nes_state_save(&a, &sz);
    run(&a, M, N);                                   /* A sigue de largo */
    /* B es otra consola (otro arranque): carga el estado y debe llegar al mismo punto */
    run(&b, 37, 0);
    if (!nes_state_load(&b, st, sz)) { puts("FALLA: no cargó el estado"); return 1; }
    run(&b, M, N);
    int bad = 0;
    if (memcmp(&a.cpu, &b.cpu, sizeof a.cpu) || a.cyc != b.cyc) { puts("FALLA: CPU o ciclos distintos"); bad = 1; }
    if (memcmp(a.ram, b.ram, sizeof a.ram)) { puts("FALLA: RAM distinta"); bad = 1; }
    if (memcmp(a.ppu.fb, b.ppu.fb, sizeof a.ppu.fb)) { puts("FALLA: imagen distinta"); bad = 1; }
    if (a.cart.chr_is_ram && memcmp(a.cart.chr, b.cart.chr, a.cart.chr_size)) { puts("FALLA: CHR RAM distinta"); bad = 1; }
    /* un estado de otra ROM o de una corrupción se rechaza */
    st[sz / 2] ^= 0;                                 /* (cambio nulo: debe seguir cargando) */
    if (!nes_state_load(&b, st, sz)) { puts("FALLA: recarga"); bad = 1; }
    if (nes_state_load(&b, st, sz - 1)) { puts("FALLA: aceptó un estado truncado"); bad = 1; }
    if (!bad) printf("statetest: bien (estado de %zu KB, %d + %d cuadros)\n", sz / 1024, N, M);
    return bad;
}
