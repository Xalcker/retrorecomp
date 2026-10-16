/* Lee una ROM y arma la consola (para las herramientas sin ventana). NULL e imprime el motivo si falla. */
#ifndef TOOLS_ROMLOAD_H
#define TOOLS_ROMLOAD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"

static inline Sms *load_sms(const char *path, int model)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no se pudo leer %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); fprintf(stderr, "no se pudo leer %s\n", path); return NULL; }
    fclose(f);
    Sms *g = calloc(1, sizeof *g);
    const char *err = NULL;
    if (!g || sms_load(g, d, (size_t)n, path, model, &err)) { fprintf(stderr, "%s: %s\n", path, err ? err : "sin memoria"); free(d); free(g); return NULL; }
    free(d);
    return g;
}

/* Saca `--model sms|gg` de los argumentos (antes de cli_parse, que no lo conoce): el modelo, MODEL_AUTO si no
 * está, o -2 si no es uno (ya avisado) */
static inline int take_model(int *argc, char **argv)
{
    int model = MODEL_AUTO;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--model")) continue;
        if (i + 1 >= *argc || (model = model_from_name(argv[i + 1])) < MODEL_AUTO) {
            fprintf(stderr, "--model: sms o gg\n");
            return -2;
        }
        for (int k = i; k + 2 <= *argc; k++) argv[k] = argv[k + 2];
        *argc -= 2;
        i--;
    }
    return model;
}

#endif
