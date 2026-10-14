/* Exportar: captura de pantalla (PNG) y grabación del audio (WAV) */
#ifndef EXPORT_H
#define EXPORT_H

#include <stdint.h>
#include <stdio.h>

/* Escribe pixeles ARGB como PNG. Devuelve 1 si todo bien. */
int screenshot_write(const char *path, const uint32_t *argb, int w, int h);

/* Grabadora de WAV mono de 16 bits */
typedef struct {
    FILE *f;
    uint32_t bytes;
    int rate;
} WavRecorder;

int  wav_start(WavRecorder *w, const char *path, int rate);
void wav_samples(WavRecorder *w, const int16_t *s, int n);
void wav_stop(WavRecorder *w);
int  wav_active(const WavRecorder *w);

#endif
