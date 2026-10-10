/*
 * Velocidad del runtime (intérprete, PPU, APU...) sin ventana ni límite de
 * cuadros.
 *
 * Uso: gbbench rom.gb [--frames N] [--repeat N] [--model dmg|cgb|sgb] [--input archivo]
 *   --frames  cuadros por corrida (por omisión 3600, un minuto de juego).
 *   --repeat  corridas; se reporta la más rápida, que es la menos afectada por
 *             otros programas en la máquina (por omisión 3).
 *   --input   entradas grabadas (src/platform/inputs.h); sin ellas, Start de
 *             vez en cuando y el mando al azar con una semilla fija.
 * El audio se genera y se saca de la cola en cada cuadro, como en la ventana.
 * Para el código recompilado: difftest --bench en el proyecto generado.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../runtime/console.h"
#include "../runtime/gb.h"
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

static u8 random_pad(long frame, unsigned *seed)
{
    if (frame % 240 < 6) return PAD_START;
    if (frame % 30 == 0) *seed = *seed * 1103515245u + 12345u;
    return (u8)((*seed >> 16) & ~PAD_START);
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *input_path = NULL;
    long frames = 3600;
    int repeat = 3, model = GB_MODEL_AUTO;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_path = argv[++i];
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = model_from_name(argv[++i]);
            if (model < GB_MODEL_AUTO) { fprintf(stderr, "modelo desconocido: %s\n", argv[i]); return 2; }
        }
        else rom_path = argv[i];
    }
    if (!rom_path || frames <= 0 || repeat <= 0) {
        fprintf(stderr, "uso: gbbench rom.gb [--frames N] [--repeat N] [--model dmg|cgb|sgb] [--input archivo]\n");
        return 2;
    }
    long size;
    unsigned char *rom = read_file(rom_path, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", rom_path); return 2; }
    InputScript script;
    if (input_path) {
        const char *ierr = inputs_load(&script, input_path);
        if (ierr) { fprintf(stderr, "%s\n", ierr); return 2; }
        if (model == GB_MODEL_AUTO) model = script.model;
    }

    static GB g;
    static float aud[4096 * 2];
    double best = 0;
    for (int k = 0; k < repeat; k++) {
        const char *err = gb_init_model(&g, rom, (u32)size, model);
        if (err) { fprintf(stderr, "%s: %s\n", rom_path, err); return 2; }
        g.apu.enabled = 1;          /* como en la ventana: el APU mezcla y genera muestras */
        if (input_path) script.pos = 0;
        unsigned seed = 1;
        clock_t t0 = clock();
        for (long f = 0; f < frames; f++) {
            gb_set_pad(&g, input_path ? inputs_pad(&script, f) : random_pad(f, &seed));
            gb_run_frame(&g);
            while (gb_audio_pull(&g, aud, 4096) > 0) {}
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
        gb_free(&g);
    }
    /* Un cuadro dura GB_CYCLES_FRAME ciclos a GB_CPU_HZ (59.73 por segundo) */
    double real = frames * ((double)GB_CYCLES_FRAME / GB_CPU_HZ);
    printf("%ld cuadros en %.3f s (mejor de %d): %.0f cuadros/s, %.1fx tiempo real\n",
           frames, best, repeat, best > 0 ? frames / best : 0.0, best > 0 ? real / best : 0.0);
    free(rom);
    return 0;
}
