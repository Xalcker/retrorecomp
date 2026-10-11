/*
 * Opciones comunes de las herramientas sin ventana (gbtrace, gbshot, gbaudio, gbramdump), las
 * mismas que tienen nestrace, nesshot, nesaudio y ramdump en NESRecomp:
 *   --frames N     cuadros a correr (por omisión 600)
 *   --input ARCH   entradas grabadas (src/platform/inputs.h); sin ellas no se pulsa nada
 *   --seed S       en vez de un guion, un mando al azar reproducible con esa semilla
 *   --model M      dmg | cgb | sgb (por omisión, el del archivo de entradas o el de la ROM)
 */
#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/console.h"
#include "../runtime/gb.h"
#include "../platform/inputs.h"

typedef struct {
    const char *rom, *out;
    long frames;
    const char *input;
    int model, use_seed;
    unsigned seed;
    InputScript script;
} Cli;

static unsigned char *cli_read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)*size);
    if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

/* Mismo mando al azar que difftest --seed */
static u8 cli_random_pad(long frame, unsigned *seed)
{
    if (frame % 240 < 6) return PAD_START;
    if (frame % 30 == 0) *seed = *seed * 1103515245u + 12345u;
    return (u8)((*seed >> 16) & ~PAD_START);
}

/* Devuelve 0 si todo bien; 2 si hay que salir (ya imprimió el motivo). `nreq`: cuántos
 * argumentos sueltos se exigen (rom y salida; gbramdump solo la rom) */
static int cli_parse_n(Cli *c, int argc, char **argv, const char *usage, int nreq)
{
    memset(c, 0, sizeof *c);
    c->frames = 600;
    c->model = GB_MODEL_AUTO;
    const char *pos[2]; int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) c->frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) c->input = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) { c->seed = (unsigned)strtoul(argv[++i], NULL, 0); c->use_seed = 1; }
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            c->model = model_from_name(argv[++i]);
            if (c->model < GB_MODEL_AUTO) { fprintf(stderr, "modelo desconocido: %s\n", argv[i]); return 2; }
        }
        else if (npos < nreq) pos[npos++] = argv[i];
        else { fprintf(stderr, "argumento de más: %s\n", argv[i]); return 2; }
    }
    if (npos < nreq || c->frames <= 0) { fprintf(stderr, "%s\n", usage); return 2; }
    c->rom = pos[0];
    c->out = nreq > 1 ? pos[1] : NULL;
    if (c->input) {
        const char *err = inputs_load(&c->script, c->input);
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
        if (c->model == GB_MODEL_AUTO) c->model = c->script.model;
    }
    return 0;
}

static inline int cli_parse(Cli *c, int argc, char **argv, const char *usage)
{
    return cli_parse_n(c, argc, argv, usage, 2);
}

static u8 cli_pad(Cli *c, long frame, unsigned *seed)
{
    if (c->input) return inputs_pad(&c->script, frame);
    if (c->use_seed) return cli_random_pad(frame, seed);
    return 0;
}

#endif
