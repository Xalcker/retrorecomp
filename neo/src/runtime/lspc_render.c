/*
 * LSPC: el dibujo. Una línea a la vez, al empezar la línea (como los búferes de línea de la consola,
 * que llenan la línea siguiente mientras se muestra la actual): así lo que cambia una interrupción
 * del temporizador sale desde la línea que sigue.
 *
 * Sprites (VRAM, ver la wiki de NeoGeoDev, "Sprites"):
 *   SCB1 ($0000-$6FFF)  64 palabras por sprite: por cada uno de sus 32 mosaicos, el número (20 bits),
 *                       la paleta, la autoanimación de 4 u 8 cuadros y los volteos
 *   SCB2 ($8000)        reducción horizontal (bits 11-8; $F = 16 pixeles) y vertical (7-0; $FF = entera)
 *   SCB3 ($8200)        Y (15-7: arriba de la pantalla está en 496 - Y), bit pegajoso (6), alto en
 *                       mosaicos (5-0; 33 = 32 que se repiten al reducir)
 *   SCB4 ($8400)        X (15-7)
 * Un sprite pegajoso toma Y, alto y reducción vertical del anterior y va a su derecha (X del anterior
 * más su ancho reducido). Se recorren los sprites 1-381 en orden (el último queda encima) y en cada
 * línea se dibujan los primeros 96 que la tocan. La reducción vertical sale de la tabla L0 (qué línea
 * de qué mosaico va en cada línea del sprite; la mitad de abajo se lee al revés) y la horizontal de una
 * máscara fija del LSPC (qué pixeles de los 16 se dibujan).
 *
 * Capa fija: 40 × 32 mosaicos de 8 × 8 de la S ROM en $7000 (por columnas: $7000 + x × 32 + y),
 * encima de los sprites; se ven las filas 2-29. Paleta en los bits 15-12, mosaico en 11-0.
 *
 * Color: el 0 de cada paleta es transparente; el fondo es el último color de la paleta ($0FFF). Cada
 * componente tiene 5 bits y el bit de oscurecido es un bit más, común y al revés (más oscuro con él).
 */
#include <stdlib.h>
#include <string.h>

#include "neo.h"

/* --- Gráficos convertidos al cargar --- */

/* Sprites: por mosaico, 16 filas de 16 pixeles de 4 bits (el pixel x en los bits 4x-4x+3). En la C
 * ROM cada mosaico son 128 bytes: cuatro bloques de 8 × 8 (arriba a la derecha, abajo a la derecha,
 * arriba a la izquierda, abajo a la izquierda), cada fila en dos bytes por chip (los planos 0 y 1 en
 * el chip impar, el 2 y 3 en el par; intercalados en la imagen) con el pixel de la izquierda en el bit 0 */
static void convert_sprites(Neo *g)
{
    const Cart *c = &g->cart;
    size_t tiles = c->len[REG_C] / 128;
    g->spr_tiles = tiles;
    free(g->spr);
    g->spr = tiles ? malloc(tiles * 16 * sizeof *g->spr) : NULL;
    if (!g->spr) { g->spr_tiles = 0; return; }
    const u8 *src = c->data + c->off[REG_C];
    for (size_t t = 0; t < tiles; t++) {
        const u8 *tile = src + t * 128;
        for (int y = 0; y < 16; y++) {
            u64 row = 0;
            for (int half = 0; half < 2; half++) {          /* 0: pixeles 8-15, 1: pixeles 0-7 */
                int block = half * 2 + (y >= 8);
                int k = block * 16 + (y & 7) * 2;           /* byte del chip */
                u8 p0 = tile[2 * k], p2 = tile[2 * k + 1], p1 = tile[2 * k + 2], p3 = tile[2 * k + 3];
                for (int b = 0; b < 8; b++) {
                    u64 px = (u64)((p0 >> b & 1) | (p1 >> b & 1) << 1 | (p2 >> b & 1) << 2 | (p3 >> b & 1) << 3);
                    int x = (half ? 0 : 8) + b;
                    row |= px << (4 * x);
                }
            }
            g->spr[t * 16 + (size_t)y] = row;
        }
    }
}

/* Capa fija: por mosaico, 8 filas de 8 pixeles de 4 bits (el pixel x en los bits 4x-4x+3). En la S ROM
 * cada mosaico son 32 bytes por columnas de dos pixeles (el de la izquierda en el nibble bajo): los
 * bytes $00-$07 son los pixeles 4-5, $08-$0F los 6-7, $10-$17 los 0-1 y $18-$1F los 2-3 */
static void convert_fix(Neo *g)
{
    const Cart *c = &g->cart;
    size_t tiles = c->len[REG_S] / 32;
    g->fix_tiles = tiles;
    free(g->fix);
    g->fix = tiles ? malloc(tiles * 8 * sizeof *g->fix) : NULL;
    if (!g->fix) { g->fix_tiles = 0; return; }
    const u8 *src = c->data + c->off[REG_S];
    for (size_t t = 0; t < tiles; t++)
        for (int y = 0; y < 8; y++) {
            u32 row = 0;
            for (int x = 0; x < 8; x++) {
                u8 b = src[t * 32 + (x < 4 ? 16 : 0) + ((x & 2) ? 8 : 0) + (size_t)y];
                row |= (u32)((x & 1) ? b >> 4 : b & 15) << (4 * x);
            }
            g->fix[t * 8 + (size_t)y] = row;
        }
}

void gfx_init(Neo *g)
{
    convert_sprites(g);
    convert_fix(g);
}

void gfx_free(Neo *g)
{
    free(g->spr);
    free(g->fix);
    g->spr = NULL;
    g->fix = NULL;
    g->spr_tiles = g->fix_tiles = 0;
}

/* --- Paleta --- */

u32 neo_color(u16 c)
{
    int dark = c >> 15;
    int r5 = ((c >> 7) & 0x1E) | ((c >> 14) & 1);
    int g5 = ((c >> 3) & 0x1E) | ((c >> 13) & 1);
    int b5 = ((c << 1) & 0x1E) | ((c >> 12) & 1);
    #define C8(v5) ((((v5) << 1 | !dark) << 2) | (((v5) << 1 | !dark) >> 4))
    return 0xFF000000u | (u32)C8(r5) << 16 | (u32)C8(g5) << 8 | (u32)C8(b5);
    #undef C8
}

void pal_refresh(Neo *g)
{
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < 0x1000; i++) g->pal_argb[b][i] = neo_color(g->pal[b][i]);
}

/* --- Una línea --- */

/* Qué pixeles de los 16 se dibujan con cada reducción horizontal (wiki de NeoGeoDev, "Sprite
 * shrinking"): por cada valor, los índices de los pixeles que quedan, en orden */
static const u8 hshrink[16][16] = {
    { 8 },
    { 4, 8 },
    { 4, 8, 12 },
    { 2, 4, 8, 12 },
    { 2, 4, 8, 12, 14 },
    { 2, 4, 6, 8, 12, 14 },
    { 2, 4, 6, 8, 10, 12, 14 },
    { 0, 2, 4, 6, 8, 10, 12, 14 },
    { 0, 2, 4, 6, 8, 9, 10, 12, 14 },
    { 0, 2, 3, 4, 6, 8, 9, 10, 12, 14 },
    { 0, 2, 3, 4, 6, 8, 9, 10, 12, 14, 15 },
    { 0, 2, 3, 4, 6, 7, 8, 9, 10, 12, 14, 15 },
    { 0, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 14, 15 },
    { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 14, 15 },
    { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
};

#define MAX_PER_LINE 96
#define LAST_SPRITE  381

void lspc_render_line(Neo *g, int sy)
{
    const u16 *vram = g->lspc.vram;
    u16 buf[SCREEN_W];
    for (int x = 0; x < SCREEN_W; x++) buf[x] = 0x0FFF;            /* el fondo */

    /* Sprites: la línea en las coordenadas de los sprites (la primera visible es la 16) */
    int line = (sy + 16) & 0x1FF;
    int y = 0, size = 0, vz = 0xFF, x = 0, prev_w = 0, count = 0;
    u8 anim = g->lspc.anim;
    int anim_on = !(g->lspc.mode & 8);
    for (int s = 1; s <= LAST_SPRITE; s++) {
        u16 scb2 = vram[0x8000 + s], scb3 = vram[0x8200 + s], scb4 = vram[0x8400 + s];
        if (scb3 & 0x40) x = (x + prev_w) & 0x1FF;                  /* pegajoso */
        else { y = scb3 >> 7; size = scb3 & 0x3F; vz = scb2 & 0xFF; x = scb4 >> 7; }
        int hz = (scb2 >> 8) & 0xF;
        prev_w = hz + 1;
        if (!size) continue;
        int rel = (line - ((0x200 - y) & 0x1FF)) & 0x1FF;          /* línea dentro del sprite */
        if (size < 32 && rel >= size * 16) continue;
        if (++count > MAX_PER_LINE) break;
        /* Qué mosaico y qué fila: la tabla L0 da la mitad de arriba; la de abajo, al revés */
        int zl = rel & 0xFF, inv = rel & 0x100;
        if (inv) zl ^= 0xFF;
        if (size > 32) {                                            /* 33: el sprite se repite cada 2 × (vz + 1) líneas */
            int period = (vz + 1) * 2;
            zl %= period;
            if (zl > vz) { zl = period - 1 - zl; inv = !inv; }
        }
        u8 e = g->l0[vz * 256 + zl];
        int row = e & 15, ti = e >> 4;
        if (inv) { row ^= 15; ti ^= 0x1F; }
        u16 w0 = vram[s * 64 + ti * 2], w1 = vram[s * 64 + ti * 2 + 1];
        u32 tile = w0 | (u32)(w1 & 0xF0) << 12;
        if (anim_on) {
            if (w1 & 8) tile = (tile & ~7u) | (anim & 7);
            else if (w1 & 4) tile = (tile & ~3u) | (anim & 3);
        }
        if (w1 & 2) row ^= 15;
        if (!g->spr_tiles) continue;
        u64 bits = g->spr[(tile % g->spr_tiles) * 16 + (u32)row];
        if (!bits) continue;
        u16 pal = (u16)((w1 >> 8) << 4);
        const u8 *src = hshrink[hz];
        int flip = w1 & 1;
        for (int k = 0; k <= hz; k++) {
            int px = (x + k) & 0x1FF;
            if (px >= SCREEN_W) continue;
            int sx = flip ? 15 - src[hz - k] : src[k];
            int c = (int)(bits >> (4 * sx)) & 15;
            if (c) buf[px] = (u16)(pal | c);
        }
    }

    /* Capa fija */
    if (g->fix_tiles) {
        int fy = (sy >> 3) + 2, ly = sy & 7;
        for (int col = 0; col < 40; col++) {
            u16 e = vram[0x7000 + col * 32 + fy];
            u32 bits = g->fix[(size_t)(e & 0xFFF) % g->fix_tiles * 8 + (size_t)ly];
            if (!bits) continue;
            u16 pal = (u16)((e >> 12) << 4);
            for (int k = 0; k < 8; k++) {
                int c = (int)(bits >> (4 * k)) & 15;
                if (c) buf[col * 8 + k] = (u16)(pal | c);
            }
        }
    }

    /* A color, con la paleta del banco actual (y la sombra, que oscurece todo a la mitad) */
    const u32 *argb = g->pal_argb[(g->latch & LATCH_PALBANK1) ? 1 : 0];
    u32 *out = g->fb + sy * SCREEN_W;
    if (g->latch & LATCH_SHADOW)
        for (int px = 0; px < SCREEN_W; px++) out[px] = 0xFF000000u | ((argb[buf[px]] >> 1) & 0x7F7F7F);
    else
        for (int px = 0; px < SCREEN_W; px++) out[px] = argb[buf[px]];
}
