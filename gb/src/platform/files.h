/*
 * Archivos con rutas en UTF-8 (en Windows, fopen no las entiende si llevan
 * caracteres fuera de ASCII) y diálogo para elegir la ROM.
 */
#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <stdio.h>

FILE *u8_fopen(const char *path, const char *mode);

/* Lee el archivo completo (malloc). NULL si no existe o no se pudo leer. */
unsigned char *u8_read_file(const char *path, size_t *size);

/* Escribe a un temporal y lo renombra, para que un corte a la mitad no deje
 * el archivo dañado. Devuelve 1 si todo bien. */
int u8_write_file(const char *path, const void *data, size_t size);

int u8_remove(const char *path);

/* Crea una carpeta (si ya existe no es error). Devuelve 1 si todo bien. */
int u8_mkdir(const char *path);

/* Diálogo nativo para elegir la ROM (solo en Windows; en otros sistemas
 * devuelve 0 y la ROM se arrastra a la ventana). `title` es el del diálogo. */
int ask_rom_path(const char *title, char *out, size_t size);

#endif
