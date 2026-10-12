/*
 * nesshot: corre una ROM sin ventana y guarda una captura en PNG.
 *   nesshot rom.nes salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]
 *                              [--entradas-241] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: nesshot rom.nes salida.png cuadros [boton:desde-hasta ...])
 * Ejemplo: nesshot smb.nes smb.png 600 start:90-95 right:200-500 a:300-320
 * Botones: a b select start up down left right.
 * Zapper: zapper:X,Y,desde-hasta apunta a (X,Y) y aprieta el gatillo en esos cuadros (activa el Zapper en el puerto 2);
 * zapper:X,Y,desde-hasta,mira deja de apretar el gatillo pero sigue apuntando.
 * Si la ROM de Blargg escribe su resultado en $6000, lo imprime.
 *
 * Como gbtest de GBRecomp:
 *   --expect esperado.png  la pantalla final (con la paleta predeterminada) debe ser idéntica al PNG.
 *                          Corre todos los cuadros (no se detiene con el resultado de Blargg).
 *   --hash                 imprime un hash de todos los cuadros y de todo el audio
 *                          ("hash imagen X audio Y"), para comprobar que un cambio no altera nada.
 *   --entradas-241         los botones del cuadro N cambian en la línea 241 del cuadro N (al empezar el vblank) y no
 *                          al empezar el cuadro: así lo hace Mesen2, y con eso tools/compat/vsmesen.py ve las mismas
 *                          entradas en los dos emuladores lea el juego el control donde lo lea.
 * Código de salida: 0 pasó, 1 falló (Blargg o --expect), 2 error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "png.h"

typedef struct { int x, y, from, to; } Zap;

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

/* FNV-1a de 64 bits, acumulado */
static void fnv(unsigned long long *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) { *h ^= p[i]; *h *= 0x100000001B3ULL; }
}

/* Pixeles distintos entre la captura y el PNG esperado; -1 si no se pudo leer o el tamaño no coincide */
static long compare_png(const u8 *rgb, const char *path)
{
    int w, h;
    unsigned char *exp = png_read_rgb(path, &w, &h);
    if (!exp) return -1;
    if (w != 256 || h != 240) { free(exp); return -1; }
    long diffs = 0;
    for (int i = 0; i < 256 * 240; i++) diffs += memcmp(rgb + i * 3, exp + i * 3, 3) != 0;
    free(exp);
    return diffs;
}

int main(int argc, char **argv)
{
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: nesshot rom.nes salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]\n"
                        "                [boton:desde-hasta ...] [zapper:X,Y,desde-hasta]\n");
        return 2;
    }
    size_t len; u8 *rom = slurp(c.pos[0], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    long frames = c.frames;
    Zap z[64]; int nz = 0, zap_aim[64];
    for (int i = 0; i < c.nextra; i++) {
        int zx, zy, za, zb;
        if (!strncmp(c.extra[i], "zapper:", 7) && sscanf(c.extra[i] + 7, "%d,%d,%d-%d", &zx, &zy, &za, &zb) == 4 && nz < 64) {
            zap_aim[nz] = strstr(c.extra[i], ",mira") != NULL;
            z[nz++] = (Zap){ zx, zy, za, zb };
        }
    }
    unsigned long long h_img = 0xCBF29CE484222325ULL, h_aud = 0xCBF29CE484222325ULL;
    for (long f = 0; f < frames; f++) {
        if (c.entradas_241) {
            /* El cuadro empieza con los botones del anterior; cambian al llegar a la línea 241 */
            n.ppu.frame_done = 0;
            do { nes_step(&n); ppu_flush(&n); } while (n.ppu.scanline < 241 && !n.ppu.frame_done);
        }
        n.pad[0] = cli_pad(&c, f, 0);
        n.pad[1] = cli_pad(&c, f, 1);
        if (nz) {
            n.zapper_on = 1; n.zap_x = n.zap_y = -1; n.zap_trigger = 0;
            for (int i = 0; i < nz; i++) if (f >= z[i].from && f <= z[i].to) { n.zap_x = z[i].x; n.zap_y = z[i].y; n.zap_trigger = !zap_aim[i]; }
        }
        if (!c.entradas_241 || !n.ppu.frame_done) nes_run_frame(&n);
        if (c.hash) {
            fnv(&h_img, n.ppu.fb, sizeof n.ppu.fb);
            fnv(&h_aud, n.apu.buf, (size_t)n.apu.nbuf * sizeof n.apu.buf[0]);
            n.apu.nbuf = 0;
        }
        if (!c.expect) {   /* ROM de Blargg: terminar en cuanto escribe su resultado final */
            u8 *r = n.cart.prg_ram;
            if (f > 10 && r[1] == 0xDE && r[2] == 0xB0 && r[3] == 0x61 && r[0] < 0x80) { frames = f + 1; break; }
        }
    }
    static u8 rgb[256 * 240 * 3];
    u32 pal[64];
    ppu_make_palette(0, pal);
    for (int i = 0; i < 256 * 240; i++) {
        u32 c = ppu_argb(pal, n.ppu.fb[i]);
        rgb[i * 3] = c >> 16; rgb[i * 3 + 1] = c >> 8; rgb[i * 3 + 2] = c;
    }
    if (!png_write_rgb(c.pos[1], rgb, 256, 240)) { fprintf(stderr, "no pude escribir el PNG\n"); return 2; }
    u8 *s = n.cart.prg_ram;
    if (s[1] == 0xDE && s[2] == 0xB0 && s[3] == 0x61)
        printf("Blargg: estado $%02X\n%s\n", s[0], (char *)s + 4);
    printf("%s: %ld cuadros, mapper %d\n", c.pos[1], frames, n.cart.mapper);
    if (c.hash) printf("hash imagen %016llx audio %016llx\n", h_img, h_aud);
    if (c.expect) {
        long diffs = compare_png(rgb, c.expect);
        if (diffs < 0) { fprintf(stderr, "no pude leer %s (o no mide 256x240)\n", c.expect); return 2; }
        if (diffs) { printf("FALLA: %ld pixeles distintos de %s\n", diffs, c.expect); return 1; }
        printf("pantalla idéntica a %s\n", c.expect);
        return 0;
    }
    if (s[1] == 0xDE && s[0] != 0) return 1;
    return 0;
}
