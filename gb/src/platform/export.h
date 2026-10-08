/*
 * Exportar la música y los gráficos de un juego mientras se juega.
 *
 * Música: dos archivos a la vez.
 *   - VGM: cada escritura del juego a los registros de sonido, con su tiempo
 *     exacto. Es la música "como partitura" del chip: la reproducen sin
 *     pérdida foobar2000 (con foo_input_vgm), vgmplay y otros, y se puede
 *     convertir. Lleva el título del juego en la etiqueta GD3.
 *   - WAV: el audio ya mezclado (estéreo, 16 bits), tal como suena.
 *
 * Gráficos: cada cuadro se revisan los mosaicos de VRAM y se guardan los que
 * no se habían visto (sin repetir, por contenido). Al detener se escriben en
 * hojas PNG de 16 mosaicos de ancho en 4 tonos de gris, editables con
 * cualquier programa de dibujo. Los colores no van en el mosaico (el mismo
 * mosaico se dibuja con distintas paletas); para verlos, están las capturas.
 *
 * Todo va a una carpeta "exportado" dentro de la carpeta de datos del juego.
 */
#ifndef EXPORT_H
#define EXPORT_H

#include <stdio.h>

#include "gb.h"

/* --- Música --- */

typedef struct {
    FILE *vgm, *wav;
    u64 start_rt;       /* instante en que empezó la grabación */
    u64 vgm_samples;    /* muestras de 44.1 kHz ya escritas en esperas */
    u32 wav_frames;     /* pares estéreo escritos en el WAV */
    char base[1024];    /* ruta sin extensión */
} MusicRecorder;

/* Empieza a grabar en `base`.vgm y `base`.wav. Devuelve 1 si todo bien. */
int  music_start(MusicRecorder *m, GB *g, const char *base);
/* Una escritura del juego a un registro de sonido (gancho g->apu_log) */
void music_register(MusicRecorder *m, GB *g, u16 addr, u8 value);
/* Muestras ya mezcladas del cuadro (pares estéreo en punto flotante) */
void music_samples(MusicRecorder *m, const float *samples, int frames);
/* Cierra los archivos; `title` va en la etiqueta del VGM */
void music_stop(MusicRecorder *m, GB *g, const char *title);
int  music_active(const MusicRecorder *m);

/* --- Gráficos --- */

typedef struct {
    u8 (*tiles)[16];    /* mosaicos únicos, en el orden en que aparecieron */
    int count, cap;
    u32 *table;         /* tabla hash: índice + 1, o 0 si está libre */
    int table_size;
    int active;
} TileCollector;

void tiles_start(TileCollector *t);
/* Revisa la VRAM del cuadro actual */
void tiles_scan(TileCollector *t, const GB *g);
/* Escribe las hojas `base`_1.png, `base`_2.png... Devuelve cuántas */
int  tiles_stop(TileCollector *t, const char *base);

/* Captura de la imagen que se ve (RGB de 8 bits por canal) */
int  screenshot_write(const char *path, const u32 *argb, int w, int h);

#endif
