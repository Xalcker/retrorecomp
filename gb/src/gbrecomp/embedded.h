/* Archivos que gbrecomp copia a cada proyecto (los genera src/tools/embed.c) */
#ifndef EMBEDDED_H
#define EMBEDDED_H

#include <stddef.h>

typedef struct {
    const char *name;           /* ruta dentro del proyecto, p. ej. runtime/gb.c */
    const unsigned char *data;
    size_t size;
} EmbeddedFile;

extern const EmbeddedFile embedded_files[];

#endif
