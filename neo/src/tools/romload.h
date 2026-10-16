/* Carga un juego y la BIOS y arma la consola (para las herramientas sin ventana). La BIOS se busca como
 * en el runtime: $NEO_BIOS, junto al juego, roms/. NULL e imprime el motivo si falla. */
#ifndef TOOLS_ROMLOAD_H
#define TOOLS_ROMLOAD_H

#include <stdio.h>
#include <stdlib.h>

#include "neo.h"

static inline Neo *load_neo(const char *path)
{
    Neo *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || neo_load(g, path, NULL, &err)) { fprintf(stderr, "%s: %s\n", path, err ? err : "sin memoria"); free(g); return NULL; }
    int fatal;
    const char *u = cart_unsupported(&g->cart, &fatal);
    if (u && fatal) { fprintf(stderr, "%s: %s\n", path, u); neo_free(g); free(g); return NULL; }
    return g;
}

#endif
