/*
 * Lo que entra al hash de la línea base en cada cuadro (golden_hash.h): la pantalla del GB, la de la SNES con el
 * borde en el SGB y el audio que salió (hay que encender g->apu.enabled). Lo comparten gbshot --hash, gbtest --hash
 * y el bench del proyecto generado, así que el intérprete y el código recompilado dan el mismo hash.
 */
#ifndef GB_GOLDEN_H
#define GB_GOLDEN_H

#include "gb.h"
#include "golden_hash.h"

static inline void gb_golden_frame(GB *g, GoldenHash *h)
{
    static float aud[4096 * 2];
    int n;
    golden_fnv(&h->img, gb_framebuffer(g), GB_SCREEN_W * GB_SCREEN_H * sizeof(u16));
    if (gb_sgb_screen(g)) golden_fnv(&h->img, gb_sgb_screen(g), SGB_SCREEN_W * SGB_SCREEN_H * sizeof(u16));
    while ((n = gb_audio_pull(g, aud, 4096)) > 0) golden_fnv(&h->aud, aud, (size_t)n * 2 * sizeof(float));
}

#endif
