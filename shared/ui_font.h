#ifndef UI_FONT_H
#define UI_FONT_H

#include <SDL.h>

#define UI_GLYPH_W 5
#define UI_GLYPH_H 10
#define UI_ADVANCE 6    /* avance horizontal por carácter, en pixeles de la fuente */

/* Ancho en pixeles de un texto UTF-8 dibujado a `scale` */
int ui_text_width(const char *s, int scale);

/* Dibuja un texto UTF-8 con la esquina superior izquierda en (x, y) */
void ui_draw_text(SDL_Renderer *ren, int x, int y, int scale, const char *s, SDL_Color color);

#endif
