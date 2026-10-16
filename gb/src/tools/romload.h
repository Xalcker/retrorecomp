/* Lee una ROM y arma el Game Boy (para las herramientas sin ventana). 0 si todo bien; si no, imprime el
 * motivo y devuelve 2. Como romload.h de GENRecomp, NEORecomp y SMSRecomp. */
#ifndef TOOLS_ROMLOAD_H
#define TOOLS_ROMLOAD_H

#include <stdio.h>
#include <stdlib.h>

#include "gb.h"

static inline int load_gb(GB *g, const char *path, int model)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no se pudo leer %s\n", path); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); fprintf(stderr, "no se pudo leer %s\n", path); return 2; }
    fclose(f);
    const char *err = gb_init_model(g, d, (u32)n, model);
    free(d);
    if (err) { fprintf(stderr, "%s: %s\n", path, err); return 2; }
    return 0;
}

#endif
