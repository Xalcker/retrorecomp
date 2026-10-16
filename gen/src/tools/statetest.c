/*
 * statetest: prueba los estados guardados. Corre N cuadros, guarda, corre M más y anota un hash de
 * la imagen, la RAM, los dos CPU, el YM2612, el PSG y el sonido; carga el estado, vuelve a correr M
 * cuadros con las mismas entradas y debe dar exactamente lo mismo. Un estado de otra ROM se rechaza.
 *   statetest rom.bin [N] [M]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "romload.h"

static void fnv(unsigned long long *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) { *h ^= p[i]; *h *= 0x100000001B3ULL; }
}

static unsigned long long run(Gen *g, long frames, long from)
{
    unsigned long long h = 0xCBF29CE484222325ULL;
    static int16_t snd[AUDIO_BINS * 2];
    for (long i = 0; i < frames; i++) {
        long f = from + i;
        g->pad[0] = (u16)(f % 90 < 3 ? GEN_START : (f / 7) % 3 ? GEN_RIGHT : GEN_A | GEN_C);
        gen_run_frame(g);
        int n = gen_audio_take(g, snd, AUDIO_BINS);
        fnv(&h, snd, (size_t)n * 4);
        fnv(&h, g->vdp.fb, sizeof g->vdp.fb);
    }
    fnv(&h, g->ram, sizeof g->ram);
    fnv(&h, g->zram, sizeof g->zram);
    fnv(&h, &g->cpu, sizeof g->cpu);
    fnv(&h, &g->z80, sizeof g->z80);
    fnv(&h, &g->ym, sizeof g->ym);
    fnv(&h, &g->psg, sizeof g->psg);
    return h;
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc < 2) { fprintf(stderr, "uso: statetest rom.bin [N] [M]\n"); return 2; }
    long n = argc > 2 ? atol(argv[2]) : 300, m = argc > 3 ? atol(argv[3]) : 300;
    Gen *g = load_gen(argv[1]);
    if (!g) return 2;
    run(g, n, 0);
    size_t size;
    u8 *st = gen_state_save(g, &size);
    if (!st) { printf("FALLA: no se pudo guardar el estado\n"); return 1; }
    unsigned long long a = run(g, m, n);
    if (!gen_state_load(g, st, size)) { printf("FALLA: no se pudo cargar el estado\n"); return 1; }
    unsigned long long b = run(g, m, n);
    if (a != b) { printf("FALLA: tras cargar el estado, %ld cuadros dan otro resultado (%016llx frente a %016llx)\n", m, b, a); return 1; }
    /* Un estado de otra ROM (otro SHA1) no se carga */
    Gen *other = load_gen(argv[1]);
    other->cart.sha1[0] ^= 1;
    int bad = gen_state_load(other, st, size);
    if (bad) { printf("FALLA: se cargó un estado de otra ROM\n"); return 1; }
    printf("statetest: %s: estado de %zu bytes; %ld cuadros después de cargarlo, idénticos\n", argv[1], size, m);
    return 0;
}
