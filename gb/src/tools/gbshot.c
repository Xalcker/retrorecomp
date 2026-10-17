/*
 * gbshot: corre una ROM sin ventana y guarda una captura en PNG.
 *
 * Uso: gbshot rom.gb salida.png [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb] [--hash]
 * --hash: imprime un hash de todos los cuadros (la pantalla del GB y, en el SGB, la de la SNES con el borde) y de
 * todo el audio, como nesshot --hash y el bench del proyecto generado; lo usa tests/golden.sh.
 */
#include "cli.h"
#include "console.h"
#include "romload.h"
#include "gb_golden.h"
#include "png.h"

static const unsigned char shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: gbshot rom.gb salida.png [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb] [--hash]\n");
        return 2;
    }
    static GB g;
    if (load_gb(&g, c.pos[0], c.model)) return 2;
    GoldenHash gh;
    golden_init(&gh);
    if (c.hash) g.apu.enabled = 1;     /* el audio entra al hash */
    int parado = 0;             /* la línea común de las herramientas *shot cuando el CPU se detiene */
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f, 0));
        gb_run_frame(&g);
        if (c.hash) gb_golden_frame(&g, &gh);
        if (g.cpu.locked && !parado) { printf("CPU detenido en el cuadro %ld (opcode ilegal)\n", f); parado = 1; }
    }
    static unsigned char rgb[GB_SCREEN_W * GB_SCREEN_H * 3];
    const u16 *fb = gb_framebuffer(&g);
    for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) {
        u16 v = fb[i];
        for (int k = 0; k < 3; k++) {
            if (g.cgb_hw) { unsigned x = (v >> (5 * k)) & 31; rgb[i * 3 + k] = (unsigned char)((x << 3) | (x >> 2)); }
            else rgb[i * 3 + k] = shade[v & 3];
        }
    }
    if (!png_write_rgb(c.pos[1], rgb, GB_SCREEN_W, GB_SCREEN_H)) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: captura tras %ld cuadros\n", c.pos[1], c.frames);
    if (c.hash) golden_print(&gh);
    gb_free(&g);
    return 0;
}
