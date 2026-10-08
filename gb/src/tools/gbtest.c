/*
 * Corredor de ROMs de prueba sin ventana.
 *
 * Uso: gbtest rom.gb [--seconds N] [--mooneye | --until-ldbb] [--input archivo]
 *                    [--expect esperado.png] [--dump salida.png] [--serial] [--no-block]
 *
 * Criterios de resultado:
 *   - Mooneye (--mooneye): al ejecutar LD B,B, los registros deben valer
 *     B=3 C=5 D=8 E=13 H=21 L=34.
 *   - Blargg: el texto enviado por el puerto serial contiene "Passed" o
 *     "Failed", o la RAM del cartucho trae la firma DE B0 61 con el resultado
 *     en $A000 (pruebas de sonido).
 *   - Captura: con --expect, al terminar el tiempo (o al ejecutar LD B,B con
 *     --until-ldbb, como en Mealybug) la pantalla debe ser
 *     idéntica al PNG (tonos #FFFFFF, #AAAAAA, #555555, #000000).
 * Código de salida: 0 pasó, 1 falló, 2 sin resultado.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/console.h"
#include "../runtime/gb.h"
#include "png.h"
#include "../platform/inputs.h"

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)*size);
    if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static const unsigned char shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };

static int compare_screen(GB *g, const char *path)
{
    int w, h;
    unsigned char *rgb = png_read_rgb(path, &w, &h);
    if (!rgb) {
        fprintf(stderr, "no se pudo leer %s\n", path);
        return -1;
    }
    if (w != GB_SCREEN_W || h != GB_SCREEN_H) {
        fprintf(stderr, "%s mide %dx%d\n", path, w, h);
        free(rgb);
        return -1;
    }
    const u8 *fb = gb_framebuffer(g);
    int diffs = 0;
    for (int i = 0; i < w * h; i++)
        if (rgb[i * 3] != shade[fb[i] & 3]) diffs++;
    free(rgb);
    return diffs;
}

static int serial_contains(GB *g, const char *text)
{
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= g->serial.out_len; i++)
        if (!memcmp(g->serial.out + i, text, n)) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *expect = NULL, *dump = NULL;
    double seconds = 30;
    int show_serial = 0, no_block = 0, mooneye = 0, until_ldbb = 0;
    const char *input_path = NULL;
    InputScript script;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--expect") && i + 1 < argc) expect = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--serial")) show_serial = 1;
        else if (!strcmp(argv[i], "--no-block")) no_block = 1;
        else if (!strcmp(argv[i], "--mooneye")) mooneye = 1;
        else if (!strcmp(argv[i], "--until-ldbb")) until_ldbb = 1;
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_path = argv[++i];
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "uso: gbtest rom.gb [--seconds N] [--expect png] [--dump png] [--serial]\n");
        return 2;
    }

    long size;
    unsigned char *rom = read_file(rom_path, &size);
    if (!rom) {
        fprintf(stderr, "no se pudo leer %s\n", rom_path);
        return 2;
    }

    static GB g;
    const char *err = gb_init(&g, rom, (u32)size);
    free(rom);
    if (err) {
        fprintf(stderr, "%s: %s\n", rom_path, err);
        return 2;
    }
    if (no_block) g.block_vram = 0;
    if (input_path) {
        const char *ierr = inputs_load(&script, input_path);
        if (ierr) { fprintf(stderr, "%s\n", ierr); return 2; }
    }

    long frames = (long)(seconds * GB_CPU_HZ / GB_CYCLES_FRAME);
    int result = 2;
    const char *why = "sin resultado";

    for (long f = 0; f < frames; f++) {
        if (input_path) gb_set_pad(&g, inputs_pad(&script, f));
        gb_run_frame(&g);
        if (mooneye && g.ld_bb_hit) {
            Cpu *c = &g.cpu;
            int ok = c->b == 3 && c->c == 5 && c->d == 8 && c->e == 13 && c->h == 21 && c->l == 34;
            /* Unos cuadros más para que el texto del resultado llegue a la pantalla */
            for (int k = 0; k < 10; k++) gb_run_frame(&g);
            result = ok ? 0 : 1;
            why = ok ? "registros de Fibonacci" : "LD B,B con registros incorrectos";
            if (!ok)
                fprintf(stderr, "  B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X\n",
                        c->b, c->c, c->d, c->e, c->h, c->l);
            break;
        }
        if (until_ldbb && g.ld_bb_hit) {
            /* El resultado se juzga por la captura, unos cuadros después */
            for (int k = 0; k < 3; k++) gb_run_frame(&g);
            break;
        }
        if (!expect && serial_contains(&g, "Passed")) { result = 0; why = "serial: Passed"; break; }
        if (!expect && serial_contains(&g, "Failed")) { result = 1; why = "serial: Failed"; break; }
        /* Convención de Blargg en RAM del cartucho: firma DE B0 61 en $A001 y
         * el estado en $A000 ($80 = sigue corriendo, 0 = pasó) */
        if (!expect && g.sram && g.sram_size >= 4 && g.sram[1] == 0xDE && g.sram[2] == 0xB0 &&
            g.sram[3] == 0x61 && g.sram[0] != 0x80) {
            result = g.sram[0] == 0 ? 0 : 1;
            why = result == 0 ? "RAM: pasó" : "RAM: falló";
            if (result) fprintf(stderr, "  código %u: %.*s\n", g.sram[0], 200, (const char *)g.sram + 4);
            break;
        }
        if (g.cpu.locked && !expect) { result = 1; why = "opcode ilegal"; break; }
    }

    if (expect && result == 2) {
        int diffs = compare_screen(&g, expect);
        if (diffs == 0) { result = 0; why = "pantalla idéntica"; }
        else if (diffs > 0) { result = 1; why = "pantalla distinta"; fprintf(stderr, "  %d pixeles distintos\n", diffs); }
    }

    if (dump) {
        static unsigned char gray[GB_SCREEN_W * GB_SCREEN_H];
        const u8 *fb = gb_framebuffer(&g);
        for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) gray[i] = shade[fb[i] & 3];
        png_write_gray(dump, gray, GB_SCREEN_W, GB_SCREEN_H);
    }

    if (show_serial && g.serial.out_len) {
        fwrite(g.serial.out, 1, g.serial.out_len, stdout);
        putchar('\n');
    }

    printf("%s %s (%s)\n", result == 0 ? "PASA " : result == 1 ? "FALLA" : "-----", rom_path, why);
    gb_free(&g);
    return result;
}
