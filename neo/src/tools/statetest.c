/*
 * statetest: prueba los estados guardados. Corre N cuadros, guarda, corre M más y anota un hash de
 * la imagen, la RAM, la VRAM, la paleta, los dos CPU, el YM2610 y el sonido; carga el estado, vuelve a
 * correr M cuadros con las mismas entradas y debe dar exactamente lo mismo. Un estado de otro juego se
 * rechaza, y el estado no lleva la BIOS.
 *   statetest juego [N] [M]
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

static unsigned long long run(Neo *g, long frames, long from)
{
    unsigned long long h = 0xCBF29CE484222325ULL;
    static int16_t snd[AUDIO_BINS * 2];
    for (long i = 0; i < frames; i++) {
        long f = from + i;
        g->pad[0] = (u16)(f % 90 < 3 ? NEO_START : (f / 7) % 3 ? NEO_RIGHT : NEO_A | NEO_C);
        neo_run_frame(g);
        int n = neo_audio_take(g, snd, AUDIO_BINS);
        fnv(&h, snd, (size_t)n * 4);
        fnv(&h, g->fb, sizeof g->fb);
    }
    fnv(&h, g->ram, sizeof g->ram);
    fnv(&h, g->zram, sizeof g->zram);
    fnv(&h, g->lspc.vram, sizeof g->lspc.vram);
    fnv(&h, g->pal, sizeof g->pal);
    fnv(&h, &g->cpu, sizeof g->cpu);
    fnv(&h, &g->z80, sizeof g->z80);
    fnv(&h, &g->ym, sizeof g->ym);
    return h;
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc < 2) { fprintf(stderr, "uso: statetest juego [N] [M]\n"); return 2; }
    long n = argc > 2 ? atol(argv[2]) : 300, m = argc > 3 ? atol(argv[3]) : 300;
    Neo *g = load_neo(argv[1]);
    if (!g) return 2;
    run(g, n, 0);
    size_t size;
    u8 *st = neo_state_save(g, &size);
    if (!st) { printf("FALLA: no se pudo guardar el estado\n"); return 1; }
    /* La BIOS no va en el estado: sus primeros 64 bytes no aparecen en ninguna parte */
    for (size_t i = 0; i + 64 <= size; i++)
        if (!memcmp(st + i, g->bios.rom + 0x400, 64)) { printf("FALLA: el estado lleva la BIOS\n"); return 1; }
    unsigned long long a = run(g, m, n);
    if (!neo_state_load(g, st, size)) { printf("FALLA: no se pudo cargar el estado\n"); return 1; }
    unsigned long long b = run(g, m, n);
    if (a != b) { printf("FALLA: tras cargar el estado, %ld cuadros dan otro resultado (%016llx frente a %016llx)\n", m, b, a); return 1; }
    /* Un estado de otro juego (otro SHA1) no se carga */
    g->cart.sha1[0] ^= 1;
    int bad = neo_state_load(g, st, size);
    g->cart.sha1[0] ^= 1;
    if (bad) { printf("FALLA: se cargó un estado de otro juego\n"); return 1; }
    printf("statetest: %s: estado de %zu bytes; %ld cuadros después de cargarlo, idénticos\n", argv[1], size, m);
    return 0;
}
