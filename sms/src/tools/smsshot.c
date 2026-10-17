/*
 * smsshot: corre una ROM sin ventana y guarda una captura en PNG (lo que se ve: 256×192 o 224, o la pantalla de
 * 160×144 del Game Gear).
 *   smsshot rom.sms salida.png [--frames N] [--input archivo] [--seed S] [--model sms|gg] [--expect esperado.png]
 *                              [--hash] [boton:desde-hasta ...]
 * Como gbtest de GBRecomp y genshot de GENRecomp:
 *   --expect esperado.png  la pantalla final debe ser idéntica al PNG.
 *   --hash                 imprime un hash de todos los cuadros y de todo el audio
 *                          ("hash imagen X audio Y"), para comprobar que un cambio no altera nada.
 * Código de salida: 0 pasó, 1 falló (--expect), 2 error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "golden_hash.h"
#include "console.h"
#include "png.h"
#include "romload.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: smsshot rom.sms salida.png [--frames N] [--input archivo] [--seed S] [--model sms|gg]\n"
                        "                [--expect esperado.png] [--hash] [boton:desde-hasta ...]\n");
        return 2;
    }
    Sms *g = load_sms(c.pos[0], c.model);
    if (!g) return 2;
    GoldenHash gh;
    golden_init(&gh);
    static int16_t snd[AUDIO_BINS * 2];
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        sms_run_frame(g);
        int n = sms_audio_take(g, snd, AUDIO_BINS);
        if (c.hash) {
            golden_fnv(&gh.img, g->vdp.fb, sizeof g->vdp.fb);
            golden_fnv(&gh.aud, snd, (size_t)n * 4);
        }
    }
    int x0, y0, w, h;
    vdp_view(g, &x0, &y0, &w, &h);
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t v = vdp_argb(g->vdp.fb[(y + y0) * FB_W + x + x0]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (unsigned char)(v >> 16); p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)v;
        }
    if (!png_write_rgb(c.pos[1], rgb, w, h)) { fprintf(stderr, "no se pudo escribir el PNG\n"); return 2; }
    printf("%s: %ld cuadros, %dx%d\n", c.pos[1], c.frames, w, h);
    if (c.hash) golden_print(&gh);
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
