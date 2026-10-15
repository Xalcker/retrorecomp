/*
 * Mezcla del sonido a 44 100 Hz en estéreo de 16 bits (audio.c): el estado que vive dentro de la
 * consola. Común a GENRecomp y NEORecomp (retrorecomp/shared); cada consola pone su audio_host.h (ver
 * audio.c).
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_RATE 44100
#define AUDIO_BINS 4096
typedef struct {
    float acc[AUDIO_BINS][2];   /* muestras de salida que se están formando (por promedio en el tiempo) */
    uint64_t first;             /* índice de la primera de acc */
    int16_t buf[AUDIO_BINS * 2]; /* muestras listas (L R), para quien las tome */
    int  n;
    uint8_t mute;               /* canales silenciados; qué bit es cada uno lo dice la consola */
    float hp_x[2], hp_y[2];     /* filtro pasa-altos de la salida (el acople de la consola) */
} Audio;

#endif
