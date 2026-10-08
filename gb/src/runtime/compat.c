/*
 * Modo de compatibilidad del Game Boy Color: paletas para juegos de DMG.
 *
 * La boot ROM del GBC colorea los juegos de Game Boy original: si el
 * licenciatario es Nintendo, suma los 16 bytes del título y busca esa suma en
 * una tabla (algunas sumas se repiten y se desempatan con la cuarta letra). El
 * resultado elige tres paletas de 4 colores: fondo, sprites 0 y sprites 1. Los
 * demás juegos reciben la combinación 0 (fondo verde y azul, sprites rojos).
 * Al arrancar, una combinación de botones (flecha, flecha + A o flecha + B)
 * elige otra.
 *
 * Las tablas vienen de la boot ROM libre de SameBoy (BootROMs/cgb_boot.asm),
 * que reproduce los datos de la original:
 *
 *   Copyright (c) 2015-2026 Lior Halphon
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <string.h>

#include "gb.h"

/* Sumas de título; desde la 65 se compara también la cuarta letra */
static const u8 title_sums[94] = {
    0x00, 0x88, 0x16, 0x36, 0xD1, 0xDB, 0xF2, 0x3C, 0x8C, 0x92, 0x3D, 0x5C,
    0x58, 0xC9, 0x3E, 0x70, 0x1D, 0x59, 0x69, 0x19, 0x35, 0xA8, 0x14, 0xAA,
    0x75, 0x95, 0x99, 0x34, 0x6F, 0x15, 0xFF, 0x97, 0x4B, 0x90, 0x17, 0x10,
    0x39, 0xF7, 0xF6, 0xA2, 0x49, 0x4E, 0x43, 0x68, 0xE0, 0x8B, 0xF0, 0xCE,
    0x0C, 0x29, 0xE8, 0xB7, 0x86, 0x9A, 0x52, 0x01, 0x9D, 0x71, 0x9C, 0xBD,
    0x5D, 0x6D, 0x67, 0x3F, 0x6B,
    0xB3, 0x46, 0x28, 0xA5, 0xC6, 0xD3, 0x27, 0x61, 0x18, 0x66, 0x6A, 0xBF,
    0x0D, 0xF4, 0xB3, 0x46, 0x28, 0xA5, 0xC6, 0xD3, 0x27, 0x61, 0x18, 0x66,
    0x6A, 0xBF, 0x0D, 0xF4, 0xB3,
};
enum { FIRST_DUP = 65 };
static const char dup_letter[] = "BEFAARBEKEK R-URAR INAILICE R";

/* Combinación para cada suma; con $80 el juego necesita el mapa del logo */
static const u8 sum_combo[94] = {
     0,  4,  5, 35, 34,  3, 31, 15, 10,  5, 19, 36, 7 | 0x80, 37, 30, 44,
    21, 32, 31, 20,  5, 33, 13, 14,  5, 29,  5, 18,  9,  3,  2, 26, 25, 25,
    41, 42, 26, 45, 42, 45, 36, 38, 26 | 0x80, 42, 30, 41, 34, 34,  5, 42,
     6,  5, 33, 25, 42, 42, 40,  2, 16, 25, 42, 42,  5,  0, 39,
    36, 22, 25,  6, 32, 12, 36, 11, 39, 18, 39, 24, 31, 50, 17, 46,  6, 27,
     0, 47, 41, 41,  0,  0, 19, 34, 23, 18, 29,
};

/* Combinaciones: desplazamiento (en colores) de sprites 0, sprites 1 y fondo */
#define C(o0, o1, bg) { (o0) * 4, (o1) * 4, (bg) * 4 }
static const u8 combos[51][3] = {
    C( 4,  4, 29), C(18, 18, 18), C(20, 20, 20), C(24, 24, 24), C( 9,  9,  9),
    C( 0,  0,  0), C(27, 27, 27), C( 5,  5,  5), C(12, 12, 12), C(26, 26, 26),
    C(16,  8,  8), C( 4, 28, 28), C( 4,  2,  2), C( 3,  4,  4), C( 4, 29, 29),
    C(28,  4, 28), C( 2, 17,  2), C(16, 16,  8), C( 4,  4,  7), C( 4,  4, 18),
    C( 4,  4, 20), C(19, 19,  9), { 15, 15, 44 }, C(17, 17,  2), C( 4,  4,  2),
    C( 4,  4,  3), C(28, 28,  0), C( 3,  3,  0), C( 0,  0,  1), C(18, 22, 18),
    C(20, 22, 20), C(24, 22, 24), C(16, 22,  8), C(17,  4, 13), { 111, 0, 56 },
    { 111, 16, 60 }, { 76, 91, 36 }, C(16, 28, 10), C( 4, 23, 28), C(17, 22,  2),
    C( 4,  0,  2), C( 4, 28,  3), C(28,  3,  0), C( 3, 28,  4), C(21, 28,  4),
    C( 3, 28,  0), C(25,  3, 28), C( 0, 28,  8), C( 4,  3, 28), C(28,  3,  6),
    C( 4, 28, 29),
};
#undef C

/* 30 paletas de 4 colores RGB555, seguidas (algunas combinaciones se montan
 * entre dos) */
static const u16 colors[30 * 4] = {
    0x7FFF, 0x32BF, 0x00D0, 0x0000,  0x639F, 0x4279, 0x15B0, 0x04CB,
    0x7FFF, 0x6E31, 0x454A, 0x0000,  0x7FFF, 0x1BEF, 0x0200, 0x0000,
    0x7FFF, 0x421F, 0x1CF2, 0x0000,  0x7FFF, 0x5294, 0x294A, 0x0000,
    0x7FFF, 0x03FF, 0x012F, 0x0000,  0x7FFF, 0x03EF, 0x01D6, 0x0000,
    0x7FFF, 0x42B5, 0x3DC8, 0x0000,  0x7E74, 0x03FF, 0x0180, 0x0000,
    0x67FF, 0x77AC, 0x1A13, 0x2D6B,  0x7ED6, 0x4BFF, 0x2175, 0x0000,
    0x53FF, 0x4A5F, 0x7E52, 0x0000,  0x4FFF, 0x7ED2, 0x3A4C, 0x1CE0,
    0x03ED, 0x7FFF, 0x255F, 0x0000,  0x036A, 0x021F, 0x03FF, 0x7FFF,
    0x7FFF, 0x01DF, 0x0112, 0x0000,  0x231F, 0x035F, 0x00F2, 0x0009,
    0x7FFF, 0x03EA, 0x011F, 0x0000,  0x299F, 0x001A, 0x000C, 0x0000,
    0x7FFF, 0x027F, 0x001F, 0x0000,  0x7FFF, 0x03E0, 0x0206, 0x0120,
    0x7FFF, 0x7EEB, 0x001F, 0x7C00,  0x7FFF, 0x3FFF, 0x7E00, 0x001F,
    0x7FFF, 0x03FF, 0x001F, 0x0000,  0x03FF, 0x001F, 0x000C, 0x0000,
    0x7FFF, 0x033F, 0x0193, 0x0000,  0x0000, 0x4200, 0x037F, 0x7FFF,
    0x7FFF, 0x7E8C, 0x7C00, 0x0000,  0x7FFF, 0x1BEF, 0x6180, 0x0000,
};

/* Combinaciones de botones al arrancar */
static const struct { const char *name; u8 combo; } keys[12] = {
    { "right", 1 },    { "left", 48 },    { "up", 5 },      { "down", 8 },
    { "right+a", 0 },  { "left+a", 40 },  { "up+a", 43 },   { "down+a", 3 },
    { "right+b", 6 },  { "left+b", 7 },   { "up+b", 28 },   { "down+b", 49 },
};

static void load_palette(u8 *pal, int offset)
{
    for (int i = 0; i < 4; i++) {
        pal[i * 2] = (u8)colors[offset + i];
        pal[i * 2 + 1] = (u8)(colors[offset + i] >> 8);
    }
}

static void load_combo(GB *g, int n)
{
    load_palette(g->obj_pal, combos[n][0]);
    load_palette(g->obj_pal + 8, combos[n][1]);
    load_palette(g->bg_pal, combos[n][2]);
}

/* Combinación que elige la boot ROM según la cabecera (bit 7: el juego
 * necesita el mapa del logo) */
int compat_combo(const GB *g)
{
    const u8 *rom = g->rom;
    int nintendo = rom[0x14B] == 0x01 ||
                   (rom[0x14B] == 0x33 && rom[0x144] == '0' && rom[0x145] == '1');
    if (!nintendo) return 0;
    u8 sum = 0;
    for (int a = 0x134; a < 0x144; a++) sum = (u8)(sum + rom[a]);
    for (int i = 0; i < 94; i++) {
        if (title_sums[i] != sum) continue;
        if (i >= FIRST_DUP && rom[0x137] != (u8)dup_letter[i - FIRST_DUP]) continue;
        return sum_combo[i];
    }
    return 0;
}

/* Deja las paletas y los índices como los deja la boot ROM */
void compat_boot(GB *g)
{
    load_combo(g, compat_combo(g) & 0x7F);
    g->bcps = 0x88;     /* 8 bytes de fondo y 16 de sprites, con autoincremento */
    g->ocps = 0x90;
    g->opri = 1;        /* prioridad de sprites por X, como en DMG */
}

int gb_compat_palette(GB *g, const char *combo)
{
    if (!g->cgb_hw || g->cgb) return 0;
    for (int i = 0; i < 12; i++) {
        if (!strcmp(keys[i].name, combo)) {
            load_combo(g, keys[i].combo);
            return 1;
        }
    }
    return 0;
}
