/* Lee una ROM y arma la consola (para las herramientas sin ventana). NULL e imprime el motivo si falla. */
#ifndef TOOLS_ROMLOAD_H
#define TOOLS_ROMLOAD_H

#include <stdio.h>
#include <stdlib.h>

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

#endif
