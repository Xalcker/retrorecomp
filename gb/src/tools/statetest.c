/*
 * statetest: prueba los estados guardados. Corre N cuadros, guarda, corre M más y anota un hash de la imagen y el
 * sonido de cada cuadro, el CPU, la RAM, la VRAM, la OAM, la E/S, las paletas y la RAM del cartucho; carga el
 * estado en otra máquina con la misma ROM, vuelve a correr M cuadros con las mismas entradas y debe dar
 * exactamente lo mismo. Un estado de otra ROM, o truncado, se rechaza.
 *   statetest rom.gb [N] [M]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "romload.h"
#include "gb_golden.h"

static void fnv(unsigned long long *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) { *h ^= p[i]; *h *= 0x100000001B3ULL; }
}

static unsigned long long run(GB *g, long frames, long from)
{
    unsigned long long h = 0xCBF29CE484222325ULL;
    for (long i = 0; i < frames; i++) {
        long f = from + i;
        gb_set_pad(g, (u8)(f % 90 < 3 ? PAD_START : (f / 7) % 3 ? PAD_RIGHT : PAD_A | PAD_B));
        gb_run_frame(g);
        GoldenHash gh;
        golden_init(&gh);
        gb_golden_frame(g, &gh);
        fnv(&h, &gh.img, sizeof gh.img);
        fnv(&h, &gh.aud, sizeof gh.aud);
    }
    fnv(&h, &g->cpu, sizeof g->cpu);
    fnv(&h, &g->cyc, sizeof g->cyc);
    fnv(&h, g->vram, sizeof g->vram);
    fnv(&h, g->wram, sizeof g->wram);
    fnv(&h, g->oam, sizeof g->oam);
    fnv(&h, g->hram, sizeof g->hram);
    fnv(&h, g->io, sizeof g->io);
    fnv(&h, g->bg_pal, sizeof g->bg_pal);
    fnv(&h, g->obj_pal, sizeof g->obj_pal);
    fnv(&h, &g->mbc, sizeof g->mbc);
    if (g->sram_size) fnv(&h, g->sram, g->sram_size);
    return h;
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc < 2) { fprintf(stderr, "uso: statetest rom.gb [N] [M]\n"); return 2; }
    long n = argc > 2 ? atol(argv[2]) : 300, m = argc > 3 ? atol(argv[3]) : 300;
    static GB a, b;
    if (load_gb(&a, argv[1], GB_MODEL_AUTO) || load_gb(&b, argv[1], GB_MODEL_AUTO)) return 2;
    a.apu.enabled = b.apu.enabled = 1;
    run(&a, n, 0);
    size_t size;
    u8 *st = gb_state_save(&a, &size);
    if (!st) { fprintf(stderr, "no se pudo guardar el estado\n"); return 1; }
    unsigned long long ha = run(&a, m, n);
    int fallas = 0;
    if (!gb_state_load(&b, st, size)) { printf("FALLA: el estado no se cargó\n"); fallas++; }
    else {
        unsigned long long hb = run(&b, m, n);
        if (ha != hb) { printf("FALLA: tras cargar, %ld cuadros dan otro resultado (%016llx contra %016llx)\n", m, hb, ha); fallas++; }
    }
    /* Truncado o de otra ROM: se rechaza */
    if (gb_state_load(&b, st, size - 1)) { printf("FALLA: aceptó un estado truncado\n"); fallas++; }
    st[20] ^= 1;        /* la cabecera de la ROM ($134 en adelante) va en el encabezado del estado */
    if (gb_state_load(&b, st, size)) { printf("FALLA: aceptó el estado de otra ROM\n"); fallas++; }
    free(st);
    if (!fallas) printf("statetest: %ld + %ld cuadros, el estado da exactamente lo mismo (%zu bytes)\n", n, m, size);
    gb_free(&a); gb_free(&b);
    return fallas != 0;
}
