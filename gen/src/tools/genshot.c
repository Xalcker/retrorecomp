/*
 * genshot: corre una ROM sin ventana y guarda una captura en PNG.
 *   genshot rom.bin salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]
 *                              [boton:desde-hasta ...]
 * Como gbtest de GBRecomp y nesshot de NESRecomp:
 *   --expect esperado.png  la pantalla final debe ser idéntica al PNG.
 *   --hash                 imprime un hash de todos los cuadros y de todo el audio
 *                          ("hash imagen X audio Y"), para comprobar que un cambio no altera nada.
 * Código de salida: 0 pasó, 1 falló (--expect), 2 error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "console.h"
#include "png.h"
#include "romload.h"

/* FNV-1a de 64 bits, acumulado */
static void fnv(unsigned long long *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) { *h ^= p[i]; *h *= 0x100000001B3ULL; }
}

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: genshot rom.bin salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]\n"
                        "                [boton:desde-hasta ...]\n");
        return 2;
    }
    Gen *g = load_gen(c.pos[0]);
    if (!g) return 2;
    unsigned long long h_img = 0xCBF29CE484222325ULL, h_aud = 0xCBF29CE484222325ULL;
    static int16_t snd[AUDIO_BINS * 2];
    int parado = 0;             /* la línea común de las herramientas *shot cuando el CPU se detiene */
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        gen_run_frame(g);
        if (g->cpu.halted && !parado) { printf("CPU detenido en el cuadro %ld (doble falta)\n", f); parado = 1; }
        int n = gen_audio_take(g, snd, AUDIO_BINS);
        if (c.hash) {
            fnv(&h_img, g->vdp.fb, sizeof g->vdp.fb);
            fnv(&h_aud, snd, (size_t)n * 4);
        }
    }
    int w = g->vdp.width, h = g->vdp.height;
    unsigned char *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t v = vdp_argb(g->vdp.fb[y * FB_W + x]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (unsigned char)(v >> 16); p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)v;
        }
    if (!png_write_rgb(c.pos[1], rgb, w, h)) { fprintf(stderr, "no se pudo escribir el PNG\n"); return 2; }
    printf("%s: %ld cuadros, %dx%d\n", c.pos[1], c.frames, w, h);
    if (c.hash) printf("hash imagen %016llx audio %016llx\n", h_img, h_aud);
    if (c.expect) {
        int ew, eh;
        unsigned char *exp = png_read_rgb(c.expect, &ew, &eh);
        if (!exp || ew != w || eh != h) { fprintf(stderr, "no se pudo leer %s (o no mide %dx%d)\n", c.expect, w, h); return 2; }
        long diffs = 0;
        for (long i = 0; i < (long)w * h; i++) diffs += memcmp(rgb + i * 3, exp + i * 3, 3) != 0;
        if (diffs) { printf("FALLA: %ld pixeles distintos de %s\n", diffs, c.expect); return 1; }
        printf("pantalla idéntica a %s\n", c.expect);
    }
    return 0;
}
