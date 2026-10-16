/*
 * gbshot: corre una ROM sin ventana y guarda una captura en PNG.
 *
 * Uso: gbshot rom.gb salida.png [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb] [--hash]
 * --hash: imprime un hash de todos los cuadros (la pantalla del GB y, en el SGB, la de la SNES con el borde) y de
 * todo el audio, como nesshot --hash y el bench del proyecto generado; lo usa tests/golden.sh.
 */
#include "cli.h"
#include "gb_golden.h"
#include "png.h"

static const unsigned char shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };

int main(int argc, char **argv)
{
    console_utf8();
    Cli c;
    if (cli_parse(&c, argc, argv, "uso: gbshot rom.gb salida.png [--frames N] [--input archivo] [--seed S] [--model dmg|cgb|sgb] [--hash]")) return 2;
    long size;
    unsigned char *rom = cli_read_file(c.rom, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", c.rom); return 2; }
    static GB g;
    const char *err = gb_init_model(&g, rom, (u32)size, c.model);
    if (err) { fprintf(stderr, "%s: %s\n", c.rom, err); return 2; }
    GoldenHash gh;
    golden_init(&gh);
    if (c.hash) g.apu.enabled = 1;     /* el audio entra al hash */
    int parado = 0;             /* la línea común de las herramientas *shot cuando el CPU se detiene */
    for (long f = 0; f < c.frames; f++) {
        gb_set_pad(&g, cli_pad(&c, f));
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
    if (!png_write_rgb(c.out, rgb, GB_SCREEN_W, GB_SCREEN_H)) { fprintf(stderr, "no se pudo escribir %s\n", c.out); return 2; }
    printf("%s: captura tras %ld cuadros\n", c.out, c.frames);
    if (c.hash) golden_print(&gh);
    gb_free(&g);
    free(rom);
    return 0;
}
