#ifndef PNG_H
#define PNG_H

/* Lee un PNG y lo devuelve como RGB de 8 bits (malloc). NULL si no se pudo. */
unsigned char *png_read_rgb(const char *path, int *w, int *h);

/* Escribe pixeles en gris de 8 bits. Devuelve 1 si todo bien. */
int png_write_gray(const char *path, const unsigned char *pixels, int w, int h);

/* Escribe pixeles RGB de 8 bits por canal. Devuelve 1 si todo bien. */
int png_write_rgb(const char *path, const unsigned char *pixels, int w, int h);

#endif
