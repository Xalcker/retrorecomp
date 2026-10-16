/*
 * statetest: prueba los estados guardados. Corre N cuadros, guarda, corre M más y anota un hash de la imagen y el
 * sonido de cada cuadro, la RAM, el Z80, el VDP (registros, VRAM, CRAM, contadores), el mapeo, la E/S, el PSG y la
 * RAM del cartucho; carga el estado
 * en otra consola con la misma ROM, vuelve a correr M cuadros con las mismas entradas y debe dar exactamente
 * lo mismo. Un estado de otra ROM, o truncado, se rechaza.
 *   statetest rom.sms [N] [M]
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

static unsigned long long run(Sms *g, long frames, long from)
{
    unsigned long long h = 0xCBF29CE484222325ULL;
    for (long i = 0; i < frames; i++) {
        long f = from + i;
        g->pad[0] = (u8)(f % 90 < 3 ? SMS_START : (f / 7) % 3 ? SMS_RIGHT : SMS_A | SMS_B);
        sms_run_frame(g);
        static int16_t snd[AUDIO_BINS * 2];
        int n = sms_audio_take(g, snd, AUDIO_BINS);
        fnv(&h, snd, (size_t)n * 4);
        fnv(&h, g->vdp.fb, sizeof g->vdp.fb);
    }
    fnv(&h, g->ram, sizeof g->ram);
    fnv(&h, &g->z80, sizeof g->z80);
    fnv(&h, &g->cyc, sizeof g->cyc);
    fnv(&h, &g->vdp, sizeof g->vdp);
    fnv(&h, &g->map, sizeof g->map);
    fnv(&h, &g->io, sizeof g->io);
    fnv(&h, &g->psg, sizeof g->psg);
    fnv(&h, g->cart.ram, CART_RAM_SIZE);
    return h;
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc < 2) { fprintf(stderr, "uso: statetest rom.sms [N] [M]\n"); return 2; }
    long n = argc > 2 ? atol(argv[2]) : 300, m = argc > 3 ? atol(argv[3]) : 300;
    Sms *a = load_sms(argv[1], MODEL_AUTO), *b = load_sms(argv[1], MODEL_AUTO);
    if (!a || !b) return 2;
    run(a, n, 0);
    size_t size;
    u8 *st = sms_state_save(a, &size);
    if (!st) { fprintf(stderr, "no se pudo guardar el estado\n"); return 1; }
    unsigned long long ha = run(a, m, n);
    int fallas = 0;
    if (!sms_state_load(b, st, size)) { printf("FALLA: el estado no se cargó\n"); fallas++; }
    else {
        unsigned long long hb = run(b, m, n);
        if (ha != hb) { printf("FALLA: tras cargar, %ld cuadros dan otro resultado (%016llx contra %016llx)\n", m, hb, ha); fallas++; }
    }
    /* Truncado o de otra ROM: se rechaza */
    if (sms_state_load(b, st, size - 1)) { printf("FALLA: aceptó un estado truncado\n"); fallas++; }
    char sha[41];
    memcpy(sha, b->cart.sha1, sizeof sha);
    b->cart.sha1[0] = (char)(sha[0] == '0' ? '1' : '0');
    if (sms_state_load(b, st, size)) { printf("FALLA: aceptó el estado de otra ROM\n"); fallas++; }
    memcpy(b->cart.sha1, sha, sizeof sha);
    free(st);
    if (!fallas) printf("statetest: %ld + %ld cuadros, el estado da exactamente lo mismo (%zu bytes)\n", n, m, size);
    sms_free(a); sms_free(b); free(a); free(b);
    return fallas != 0;
}
