/*
 * nesshot: corre una ROM sin ventana y guarda una captura en PNG.
 *   nesshot rom.nes salida.png cuadros [boton:desde-hasta ...]
 * Ejemplo: nesshot smb.nes smb.png 600 start:90-95 right:200-500 a:300-320
 * Botones: a b select start up down left right.
 * Si la ROM de Blargg escribe su resultado en $6000, lo imprime.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"
#include "png.h"

typedef struct { int btn, from, to; } Hold;

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

static int btn_by_name(const char *s)
{
    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    for (int i = 0; i < 8; i++) if (!strcmp(s, nm[i])) return 1 << i;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "uso: nesshot rom.nes salida.png cuadros [boton:desde-hasta ...]\n"); return 2; }
    size_t len; u8 *rom = slurp(argv[1], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", argv[1]); return 2; }
    static NES n; const char *err = NULL;
    if (nes_load(&n, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    int frames = atoi(argv[3]);
    Hold h[64]; int nh = 0;
    for (int i = 4; i < argc && nh < 64; i++) {
        char name[16]; int a, b;
        if (sscanf(argv[i], "%15[a-z]:%d-%d", name, &a, &b) == 3 && btn_by_name(name))
            h[nh++] = (Hold){ btn_by_name(name), a, b };
    }
    for (int f = 0; f < frames; f++) {
        u8 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].from && f <= h[i].to) pad |= h[i].btn;
        n.pad[0] = pad;
        nes_run_frame(&n);
        {   /* ROM de Blargg: terminar en cuanto escribe su resultado final */
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
    if (!png_write_rgb(argv[2], rgb, 256, 240)) { fprintf(stderr, "no pude escribir el PNG\n"); return 2; }
    u8 *s = n.cart.prg_ram;
    if (s[1] == 0xDE && s[2] == 0xB0 && s[3] == 0x61)
        printf("Blargg: estado $%02X\n%s\n", s[0], (char *)s + 4);
    printf("%s: %d cuadros, mapper %d\n", argv[2], frames, n.cart.mapper);
    if (s[1] == 0xDE && s[0] != 0) return 1;
    return 0;
}
