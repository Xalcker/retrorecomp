/*
 * VDP: dibujo de una línea en fb.
 *
 * Capas, de adelante hacia atrás: sprites con prioridad, plano A (o la ventana) con prioridad, plano
 * B con prioridad, y lo mismo sin prioridad; detrás, el color de fondo (registro 7).
 *
 * - Planos: mosaicos de 8x8 de 4 bits; tamaño del registro 16 (32, 64 o 128 celdas); scroll
 *   horizontal de la tabla del registro 13 (de pantalla, por celda de 8 líneas o por línea: registro 11
 *   bits 0-1) y vertical de la VSRAM (de pantalla o por columna de 16 pixeles: registro 11 bit 2).
 * - Ventana (registros 17 y 18): reemplaza al plano A en una franja horizontal o vertical; no tiene
 *   scroll.
 * - Sprites: lista enlazada desde el 0 en la tabla del registro 5 (80 en H40, 64 en H32). Por línea,
 *   como mucho 20 (16) sprites y 320 (256) pixeles; el que no entra levanta el desborde. Un sprite en
 *   X = 0 tapa a los que siguen en la línea si ya hubo otro antes. Dos pixeles opacos de sprites en el
 *   mismo lugar levantan la colisión.
 * - Sombra y resalte (registro 12 bit 3): un pixel queda en sombra si ni el plano A ni el B tienen
 *   prioridad ahí (salvo que lo cubra un sprite con prioridad); los colores 14 y 15 de la paleta 3 en
 *   un sprite no se dibujan: resaltan o sombrean lo que hay debajo. El color 14 de las paletas 0-2 en un
 *   sprite sin prioridad no se sombrea.
 * - Columna izquierda en blanco (registro 0 bit 5) y pantalla apagada (registro 1 bit 6): color de
 *   fondo.
 */
#include <string.h>

#include "gen.h"

static inline u16 vram16(const Vdp *v, u32 a) { a &= 0xFFFE; return (u16)(v->vram[a] << 8 | v->vram[a + 1]); }

/* Pixel (0-15) de un mosaico sin voltear */
static inline u8 pattern(const Vdp *v, u32 tile, int px, int py)
{
    u8 b = v->vram[((tile & 0x7FF) << 5 | (u32)py << 2 | (u32)px >> 1) & 0xFFFF];
    return (px & 1) ? b & 15 : b >> 4;
}

static int cells(int bits) { return bits == 1 ? 64 : bits == 3 ? 128 : 32; }

/* Pixeles [x, x + n) de la fila `row` de la entrada de nombres `e`, empezando en la columna `cx0`
 * del mosaico (n <= 8 - cx0) */
static inline void tile_row(const Vdp *v, u16 e, int row, int cx0, int n, u8 *col, u8 *pri)
{
    if (e & 0x1000) row ^= 7;
    const u8 *p = &v->vram[((u32)(e & 0x7FF) << 5 | (u32)row << 2) & 0xFFFF];
    u8 pal = (u8)(e >> 9 & 0x30), pr = (u8)(e >> 15);
    int hf = (e & 0x0800) ? 7 : 0;
    if (n == 8) {                                   /* el mosaico entero: la fila de 32 bits de una vez */
        u32 w = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
        memset(pri, pr, 8);
        if (!w) { memset(col, 0, 8); return; }
        for (int k = 0; k < 8; k++) {
            u8 c = (u8)(w >> (hf ? 4 * k : 28 - 4 * k) & 15);
            col[k] = c ? (u8)(pal | c) : 0;
        }
        return;
    }
    for (int k = 0; k < n; k++) {
        int cx = (cx0 + k) ^ hf;
        u8 b = p[cx >> 1];
        u8 c = (cx & 1) ? b & 15 : b >> 4;
        col[k] = c ? (u8)(pal | c) : 0;
        pri[k] = pr;
    }
}

/* Plano A (0) o B (1) en los pixeles [x0, x1) de la línea y, de a un mosaico: la entrada de la tabla
 * de nombres se lee una vez por mosaico (o por tramo de 16 pixeles con scroll vertical por columna) */
static void plane(const Vdp *v, int y, int which, int x0, int x1, u8 *col, u8 *pri)
{
    u32 base = which ? (u32)(v->reg[4] & 7) << 13 : (u32)(v->reg[2] & 0x38) << 10;
    int pw = cells(v->reg[16] & 3), ph = cells(v->reg[16] >> 4 & 3);
    int wmask = pw * 8 - 1, hmask = ph * 8 - 1;
    u32 hsbase = (u32)(v->reg[13] & 0x3F) << 10;
    int hline;
    switch (v->reg[11] & 3) {
    case 0: hline = 0; break;
    case 1: hline = y & 7; break;
    case 2: hline = y & ~7; break;
    default: hline = y; break;
    }
    int hs = vram16(v, hsbase + (u32)hline * 4 + (u32)which * 2) & 0x3FF;
    int column_vs = v->reg[11] & 4;
    int vs = v->vsram[which] & 0x3FF;
    int x = x0;
    while (x < x1) {
        int lim = x1;
        if (column_vs) {
            int c = x >> 4;
            vs = c < 20 ? v->vsram[c * 2 + which] & 0x3FF : 0;
            if ((c + 1) * 16 < lim) lim = (c + 1) * 16;
        }
        int py = (y + vs) & hmask;
        int px = (x - hs) & wmask;
        u16 e = vram16(v, base + (u32)(((py >> 3) * pw + (px >> 3)) * 2));
        int n = 8 - (px & 7);
        if (x + n > lim) n = lim - x;
        tile_row(v, e, py & 7, px & 7, n, col + x, pri + x);
        x += n;
    }
}

static void window(const Vdp *v, int y, int x0, int x1, int wide, u8 *col, u8 *pri)
{
    u32 base = (u32)(v->reg[3] & (wide ? 0x3C : 0x3E)) << 10;
    int ww = wide ? 64 : 32;
    int x = x0;
    while (x < x1) {
        u16 e = vram16(v, base + (u32)(((y >> 3) * ww + (x >> 3)) * 2));
        int n = 8 - (x & 7);
        if (x + n > x1) n = x1 - x;
        tile_row(v, e, y & 7, x & 7, n, col + x, pri + x);
        x += n;
    }
}

static void sprites(Vdp *v, int y, int W, int wide, u8 *col, u8 *pri)
{
    u32 sat = (u32)(v->reg[5] & (wide ? 0x7E : 0x7F)) << 9;
    int max_total = wide ? 80 : 64, max_line = wide ? 20 : 16;
    int link = 0, found = 0, dots = 0, masked = 0;
    for (int i = 0; i < max_total; i++) {
        u32 e = sat + (u32)link * 8;
        int sy = (vram16(v, e) & 0x3FF) - 128;
        u16 size = vram16(v, e + 2);
        int hs = size >> 10 & 3, vs = size >> 8 & 3;
        int h = (vs + 1) * 8;
        if (y >= sy && y < sy + h) {
            if (found == max_line) { v->spr_overflow = 1; break; }
            found++;
            u16 attr = vram16(v, e + 4);
            int sxr = vram16(v, e + 6) & 0x1FF, sx = sxr - 128;
            int w = (hs + 1) * 8;
            if (sxr == 0 && found > 1) masked = 1;
            int draw = w;
            if (dots + w > W) { draw = W - dots; v->spr_overflow = 1; }
            dots += w;
            if (!masked) {
                int r = y - sy;
                if (attr & 0x1000) r = h - 1 - r;
                for (int px = 0; px < draw; px++) {
                    int x = sx + px;
                    if (x < 0 || x >= W) continue;
                    int c = (attr & 0x0800) ? w - 1 - px : px;
                    u8 p = pattern(v, (u32)(attr & 0x7FF) + (u32)((c >> 3) * (vs + 1) + (r >> 3)), c & 7, r & 7);
                    if (!p) continue;
                    if (col[x]) { v->spr_collision = 1; continue; }
                    col[x] = (u8)((attr >> 9 & 0x30) | p);
                    pri[x] = (u8)(attr >> 15);
                }
            }
            if (dots >= W) break;
        }
        link = size & 0x7F;
        if (link == 0 || link >= max_total) break;
    }
}

static inline u16 color9(u16 c) { return (u16)((c >> 1 & 7) | (c >> 2 & 0x38) | (c >> 3 & 0x1C0)); }

void vdp_render_line(Gen *g, int y)
{
    Vdp *v = &g->vdp;
    int wide = (v->reg[12] & 0x81) != 0, W = wide ? 320 : 256;
    u16 *out = v->fb + y * FB_W;
    u8 bgi = v->reg[7] & 0x3F;
    if (!(v->reg[1] & 0x40)) {                      /* pantalla apagada: color de fondo */
        u16 bg = color9(v->cram[bgi]);
        for (int x = 0; x < W; x++) out[x] = bg;
        return;
    }
    u8 ca[FB_W], pa[FB_W], cb[FB_W], pb[FB_W], cs[FB_W], ps[FB_W];
    memset(cs, 0, sizeof cs);
    memset(ps, 0, sizeof ps);

    plane(v, y, 1, 0, W, cb, pb);
    /* Ventana: toda la línea si cae en la franja vertical; si no, la franja horizontal */
    int vpos = (v->reg[18] & 0x1F) * 8, hpos = (v->reg[17] & 0x1F) * 16;
    int vwin = (v->reg[18] & 0x80) ? y >= vpos : y < vpos;
    int wx0 = 0, wx1 = 0;
    if (vwin) wx1 = W;
    else if (v->reg[17] & 0x80) { wx0 = hpos < W ? hpos : W; wx1 = W; }
    else wx1 = hpos < W ? hpos : W;
    if (wx0 > 0) plane(v, y, 0, 0, wx0, ca, pa);
    if (wx1 < W) plane(v, y, 0, wx1, W, ca, pa);
    if (wx1 > wx0) window(v, y, wx0, wx1, wide, ca, pa);
    sprites(v, y, W, wide, cs, ps);

    int sh = v->reg[12] & 0x08;
    int blank_left = v->reg[0] & 0x20;
    u16 pal9[64];                                   /* la CRAM en color de 9 bits, una vez por línea */
    for (int i = 0; i < 64; i++) pal9[i] = color9(v->cram[i]);
    if (!sh) {
        /* Sin sombra ni resalte: el primer pixel opaco en el orden de prioridad (sprites, A, B con
         * prioridad; sprites, A, B sin ella; fondo). Se pinta de atrás hacia adelante, sin ramas. */
        for (int x = 0; x < W; x++) {
            u8 s = cs[x], a = ca[x], b = cb[x], c = bgi;
            c = b ? b : c;
            c = a ? a : c;
            c = s ? s : c;
            c = (b && pb[x]) ? b : c;
            c = (a && pa[x]) ? a : c;
            c = (s && ps[x]) ? s : c;
            out[x] = pal9[c];
        }
        if (blank_left) for (int x = 0; x < 8; x++) out[x] = pal9[bgi];
        return;
    }
    for (int x = 0; x < W; x++) {
        u8 s = cs[x], a = ca[x], b = cb[x], c;
        int mode = 0, op = 0;
        if (sh && (s == 0x3E || s == 0x3F)) { op = s == 0x3E ? 2 : 1; s = 0; }
        int from_sprite = 0;
        if (s && ps[x]) { c = s; from_sprite = 1; }
        else if (a && pa[x]) c = a;
        else if (b && pb[x]) c = b;
        else if (s) { c = s; from_sprite = 1; }
        else if (a) c = a;
        else if (b) c = b;
        else c = bgi;
        if (sh) {
            int shadow = !(pa[x] || pb[x]);
            if (from_sprite) mode = ps[x] || (c & 0x0F) == 14 ? 0 : shadow;
            else mode = shadow;
            /* El operador actúa si el sprite habría quedado encima */
            if (op && (ps[x] || !((a && pa[x]) || (b && pb[x])))) mode = op == 1 ? 1 : (mode == 1 ? 0 : 2);
        }
        if (blank_left && x < 8) { c = bgi; mode = 0; }
        out[x] = (u16)(pal9[c] | mode << 9);
    }
}
