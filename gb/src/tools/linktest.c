/*
 * Dos Game Boy conectados por cable link, sin ventana.
 *
 * Uso: linktest rom.gb [rom2.gb] [--frames N] [--input-a archivo] [--input-b archivo]
 *               [--model dmg|cgb|sgb] [--screens carpeta [--every N]] [--dump captura.png]
 *               [--min-bytes N]
 *
 * Con una sola ROM, los dos corren el mismo juego. Cada uno sigue su guion
 * de entradas (formato de tests/inputs). Las capturas ponen las dos pantallas
 * lado a lado (izquierda A, derecha B).
 *
 * Imprime cuántos bytes pasaron por el cable. Con --min-bytes N, termina con
 * código 1 si pasaron menos de N (para pruebas automáticas).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../runtime/console.h"
#include "../runtime/gb.h"
#include "png.h"
#include "../platform/inputs.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

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

static void pixel_rgb(const GB *g, u16 v, unsigned char out[3])
{
    if (g->cgb_hw) {
        for (int c = 0; c < 3; c++) {
            unsigned x = (v >> (5 * c)) & 31;
            out[c] = (unsigned char)((x << 3) | (x >> 2));
        }
    } else {
        out[0] = out[1] = out[2] = shade[v & 3];
    }
}

#define GAP 8
#define W (GB_SCREEN_W * 2 + GAP)

/* Las dos pantallas lado a lado, con una franja gris en medio */
static int save_pair(GB *a, GB *b, const char *path)
{
    static unsigned char rgb[W * GB_SCREEN_H * 3];
    memset(rgb, 0x80, sizeof rgb);
    GB *gbs[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        const u16 *fb = gb_framebuffer(gbs[k]);
        for (int y = 0; y < GB_SCREEN_H; y++)
            for (int x = 0; x < GB_SCREEN_W; x++)
                pixel_rgb(gbs[k], fb[y * GB_SCREEN_W + x],
                          rgb + ((size_t)y * W + x + k * (GB_SCREEN_W + GAP)) * 3);
    }
    return png_write_rgb(path, rgb, W, GB_SCREEN_H);
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path[2] = { NULL, NULL }, *input_path[2] = { NULL, NULL };
    const char *screens = NULL, *dump = NULL;
    long frames = 600, every = 60, min_bytes = -1;
    int model = GB_MODEL_AUTO, nroms = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--input-a") && i + 1 < argc) input_path[0] = argv[++i];
        else if (!strcmp(argv[i], "--input-b") && i + 1 < argc) input_path[1] = argv[++i];
        else if (!strcmp(argv[i], "--screens") && i + 1 < argc) screens = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--min-bytes") && i + 1 < argc) min_bytes = atol(argv[++i]);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = model_from_name(argv[++i]);
            if (model == -2) { fprintf(stderr, "modelo desconocido: %s\n", argv[i]); return 2; }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "opción desconocida: %s\n", argv[i]);
            return 2;
        } else if (nroms < 2) {
            rom_path[nroms++] = argv[i];
        }
    }
    if (!nroms) {
        fprintf(stderr, "uso: linktest rom.gb [rom2.gb] [--frames N] [--input-a archivo] "
                        "[--input-b archivo] [--screens carpeta [--every N]] [--dump png] [--min-bytes N]\n");
        return 2;
    }
    if (nroms == 1) rom_path[1] = rom_path[0];

    static GB gbs[2];
    static InputScript script[2];
    unsigned char *roms[2];
    for (int k = 0; k < 2; k++) {
        long size;
        int m = model;
        if (input_path[k]) {
            const char *ierr = inputs_load(&script[k], input_path[k]);
            if (ierr) { fprintf(stderr, "%s\n", ierr); return 2; }
            if (m == GB_MODEL_AUTO) m = script[k].model;
        }
        roms[k] = read_file(rom_path[k], &size);
        if (!roms[k]) { fprintf(stderr, "no se pudo leer %s\n", rom_path[k]); return 2; }
        const char *err = gb_init_model(&gbs[k], roms[k], (u32)size, m);
        if (err) { fprintf(stderr, "%s: %s\n", rom_path[k], err); return 2; }
    }

    GbLink link;
    gb_link_connect(&link, &gbs[0], &gbs[1]);
    if (screens) MKDIR(screens);

    for (long f = 0; f < frames; f++) {
        for (int k = 0; k < 2; k++)
            if (input_path[k]) gb_set_pad(&gbs[k], inputs_pad(&script[k], f));
        gb_link_run_frame(&link);
        if (screens && every > 0 && (f + 1) % every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/cuadro_%06ld.png", screens, f + 1);
            save_pair(&gbs[0], &gbs[1], path);
        }
    }
    if (dump && !save_pair(&gbs[0], &gbs[1], dump)) {
        fprintf(stderr, "no se pudo escribir %s\n", dump);
        return 2;
    }

    printf("%ld cuadros, %llu bytes por el cable\n", frames, (unsigned long long)link.bytes);
    gb_link_disconnect(&link);
    for (int k = 0; k < 2; k++) {
        gb_free(&gbs[k]);
        free(roms[k]);
    }
    if (min_bytes >= 0 && (long long)link.bytes < min_bytes) {
        printf("FALLA: se esperaban al menos %ld bytes\n", min_bytes);
        return 1;
    }
    return 0;
}
