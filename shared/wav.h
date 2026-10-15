/*
 * Archivos WAV (PCM de 16 bits, little-endian) para las herramientas: la cabecera de 44 bytes y el
 * archivo entero. Común a los cuatro recompiladores (retrorecomp/shared); antes cada *audio, *run y
 * la exportación de GB escribían la suya. La grabación desde la ventana (export.c) usa wav_fill.
 */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Cabecera de `bytes` bytes de muestras a `rate` Hz y `channels` canales */
static inline void wav_fill(unsigned char h[44], uint32_t rate, int channels, uint32_t bytes)
{
    const uint32_t align = (uint32_t)channels * 2;
    const uint32_t v[] = { 36 + bytes, 16, rate, rate * align, bytes };
    memcpy(h, "RIFF", 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 36, "data", 4);
    const int at[] = { 4, 16, 24, 28, 40 };
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < 4; i++) h[at[k] + i] = (unsigned char)(v[k] >> (8 * i));
    h[20] = 1; h[21] = 0;                               /* PCM */
    h[22] = (unsigned char)channels; h[23] = 0;
    h[32] = (unsigned char)align; h[33] = 0;            /* bytes por cuadro de muestras */
    h[34] = 16; h[35] = 0;                              /* bits */
}

/* Escribe la cabecera al principio del archivo (después de las muestras, cuando ya se sabe cuántas
 * son, o antes con 0 y otra vez al cerrar). Devuelve 1 si todo bien. */
static inline int wav_put_header(FILE *f, uint32_t rate, int channels, uint32_t bytes)
{
    unsigned char h[44];
    wav_fill(h, rate, channels, bytes);
    return fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof h, f) == sizeof h;
}

/* Escribe `frames` cuadros de muestras (`channels` int16 cada uno) en `path`. Devuelve 1 si todo bien. */
static inline int wav_save(const char *path, const int16_t *pcm, size_t frames, uint32_t rate, int channels)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = frames * (size_t)channels;
    int ok = wav_put_header(f, rate, channels, (uint32_t)(n * 2)) && fwrite(pcm, 2, n, f) == n;
    return fclose(f) == 0 && ok;
}

#endif
