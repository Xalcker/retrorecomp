/*
 * El dibujo del VDP, de a una línea (msvdp-20021112.txt de Charles MacDonald). vdp.c llama a vdp_render_line al
 * empezar cada línea de la imagen, con los registros de ese momento: lo que cambia una interrupción de línea se
 * ve desde la línea siguiente.
 *
 * Modo 4 (el del Master System y el Game Gear):
 *   fondo   tabla de nombres de 32×28 palabras (32×32 con 224 o 240 líneas, en $x700) con el número de patrón
 *           (9 bits), los volteos, la paleta y la prioridad; patrones de 8×8 en 4 planos. El scroll horizontal
 *           (registro 8) mueve las columnas a la derecha y deja los primeros pixeles con el color de fondo; el
 *           bit 6 del registro 0 lo deja en 0 en las filas 0-15. El vertical (registro 9) se toma al empezar el
 *           cuadro (no cambia durante la imagen) y da la vuelta en 224 (o 256); el bit 7 del registro 0 lo deja
 *           en 0 en las columnas 24-31.
 *   sprites 64 en la tabla de atributos (registro 5); Y + 1, y $D0 termina la lista (solo con 192 líneas). Hasta
 *           8 por línea: con más, el bit 6 del estado (desborde). Dos pixeles opacos de sprites en el mismo lugar
 *           ponen el bit 5 (colisión). 8×8 u 8×16 (registro 1, bit 1), el doble con el zoom (bit 0), 8 pixeles a
 *           la izquierda con el bit 3 del registro 0, patrones de la mitad que dice el bit 2 del registro 6. El
 *           sprite de menor número queda encima; un mosaico de fondo con prioridad tapa a los sprites salvo en
 *           su color 0.
 *   el bit 5 del registro 0 tapa la columna 0 con el color de fondo (registro 7, de la paleta de los sprites);
 *   con la pantalla apagada (registro 1, bit 6) toda la línea es el color de fondo.
 * Modos del TMS9918 (registro 0, bit 2 en 0): los gráficos I y II, con sus 32 sprites (4 por línea) y la
 *   paleta fija del SMS2 (SMS Power!, Palette). El texto y el multicolor quedan con el color de fondo.
 *
 * fb guarda el color de cada pixel en 12 bits (----BBBBGGGGRRRR): el del SMS (2 bits por canal) se lleva a 4 bits
 * como 0, 5, 10 y 15; el del Game Gear en modo GG es su CRAM tal cual.
 */
#include <string.h>

#include "sms.h"

/* La bandera de colisión se ve 9 ciclos después del pixel donde chocan dos sprites: medido con «Spr col correct HC»
 * del VDPTEST de FluBBa (con el choque en x = $83, el estado la muestra a partir de la segunda vez que el contador H
 * vale $47) */
#define COL_DELAY 9

/* El TMS9918 en el SMS2 (en --BBGGRR) */
static const u8 tms_pal[16] = {
    0x00, 0x00, 0x08, 0x0C, 0x10, 0x30, 0x01, 0x3C, 0x02, 0x03, 0x05, 0x0F, 0x04, 0x33, 0x15, 0x3F
};

static inline u16 sms_color(u8 c)
{
    return (u16)(((c >> 4) & 3) * 5 << 8 | ((c >> 2) & 3) * 5 << 4 | (c & 3) * 5);
}

/* Las 32 entradas de la CRAM como colores de 12 bits */
static void palette(const Sms *g, u16 pal[32])
{
    const Vdp *v = &g->vdp;
    if (g->model == MODEL_GG && !g->sms_mode)
        for (int i = 0; i < 32; i++) pal[i] = (u16)((v->cram[2 * i] | v->cram[2 * i + 1] << 8) & 0x0FFF);
    else
        for (int i = 0; i < 32; i++) pal[i] = sms_color(v->cram[i]);
}

/* La tabla de nombres de 32×32 (224 y 240 líneas): también en el modo GG, aunque la imagen siga en 192 */
int vdp_extended(const Vdp *v)
{
    return (v->reg[0] & 0x06) == 0x06 && ((v->reg[1] & 0x18) == 0x10 || (v->reg[1] & 0x18) == 0x08);
}

/* Los sprites del modo 4 en la línea cuyo contador V es vc: hasta 8 (con más, el desborde, si overflow), en spr
 * (16 + color; el de menor número queda encima) y la colisión, que se ve en el ciclo del primer pixel donde dos
 * opacos coinciden */
static void m4_sprites(Sms *g, int vc, u8 spr[256], int overflow)
{
    Vdp *v = &g->vdp;
    const u8 *vram = v->vram;
    int ext = vdp_extended(v);
    int h = (v->reg[1] & 0x02) ? 16 : 8, zoom = v->reg[1] & 0x01, sh = h << zoom;
    u32 sat = (u32)(v->reg[5] & 0x7E) << 7;
    int list[8], n = 0, col = -1;
    for (int i = 0; i < 64; i++) {
        u8 sy = vram[sat + i];
        if (!ext && sy == 0xD0) break;
        int dy = (vc - (sy + 1)) & 0xFF;
        if (dy >= sh) continue;
        if (n == 8) { if (overflow) v->status |= 0x40; break; }
        list[n++] = i;
    }
    for (int k = 0; k < n; k++) {
        int i = list[k];
        int x = vram[sat + 0x80 + 2 * i], pat = vram[sat + 0x81 + 2 * i];
        if (v->reg[0] & 0x08) x -= 8;
        if (v->reg[6] & 0x04) pat |= 0x100;
        int row = ((vc - (vram[sat + i] + 1)) & 0xFF) >> zoom;
        if (h == 16) { pat &= ~1; if (row >= 8) { pat++; row -= 8; } }
        u32 a = (u32)pat * 32 + (u32)row * 4;
        u8 p0 = vram[a], p1 = vram[a + 1], p2 = vram[a + 2], p3 = vram[a + 3];
        for (int px = 0; px < (8 << zoom); px++) {
            int sx = x + px;
            if (sx < 0 || sx > 255) continue;
            int b = 7 - (px >> zoom);
            u8 c = (u8)(((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3);
            if (!c) continue;
            if (spr[sx]) { if (col < 0 || sx < col) col = sx; }
            else spr[sx] = (u8)(16 + c);
        }
    }
    if (col >= 0 && !v->col_at && !(v->status & 0x20)) v->col_at = v->line_start + vdp_pixel_cycle(col) + COL_DELAY;
}

void vdp_border_sprites(Sms *g, int vcount)
{
    /* Fuera de la imagen los sprites no se ven ni desbordan, pero sí chocan (VDPTEST: «Offscreen Y, col») */
    u8 spr[256];
    if (!(g->vdp.reg[1] & 0x40) || !(g->vdp.reg[0] & 0x04)) return;
    memset(spr, 0, sizeof spr);
    m4_sprites(g, vcount, spr, 0);
}

static void render_m4(Sms *g, int y, u16 *out)
{
    Vdp *v = &g->vdp;
    const u8 *vram = v->vram;
    u16 pal[32];
    palette(g, pal);
    u16 backdrop = pal[16 + (v->reg[7] & 15)];
    u8 spr[256];                        /* 0: nada; si no, 16 + color */
    memset(spr, 0, sizeof spr);
    if (!(v->reg[1] & 0x40)) {
        /* Con la pantalla apagada no hay colisión, pero sí desborde (VDPTEST: «No disp, spr ovr») */
        int h = (v->reg[1] & 0x02) ? 16 : 8, sh = h << (v->reg[1] & 0x01), n = 0, ext = vdp_extended(v);
        u32 sat = (u32)(v->reg[5] & 0x7E) << 7;
        for (int i = 0; i < 64; i++) {
            u8 sy = vram[sat + i];
            if (!ext && sy == 0xD0) break;
            if (((y - (sy + 1)) & 0xFF) < sh && ++n > 8) { v->status |= 0x40; break; }
        }
        for (int x = 0; x < 256; x++) out[x] = backdrop;
        return;
    }
    int ext = vdp_extended(v);
    m4_sprites(g, y, spr, 1);

    /* Fondo, mezclado con los sprites */
    int hs = ((v->reg[0] & 0x40) && y < 16) ? 0 : v->reg[8];
    int fine = hs & 7, start = (32 - (hs >> 3)) & 31;
    u32 nt = ext ? ((u32)(v->reg[2] & 0x0C) << 10) | 0x0700 : (u32)(v->reg[2] & 0x0E) << 10;
    for (int x = 0; x < 256; x++) {
        u16 c;
        if (x < fine) c = spr[x] ? pal[spr[x]] : backdrop;
        else {
            int cc = (x - fine) >> 3, col = (start + cc) & 31;
            int vs = ((v->reg[0] & 0x80) && cc >= 24) ? 0 : v->vscroll;
            int ry = y + vs;
            ry = ext ? ry & 0xFF : ry % 224;
            u32 e = nt + (u32)((ry >> 3) * 32 + col) * 2;
            u16 w = (u16)(vram[e] | vram[(e + 1) & 0x3FFF] << 8);
            int py = (w & 0x400) ? 7 - (ry & 7) : ry & 7;
            int px = (x - fine) & 7, b = (w & 0x200) ? px : 7 - px;
            u32 a = (u32)(w & 0x1FF) * 32 + (u32)py * 4;
            u8 ci = (u8)(((vram[a] >> b) & 1) | ((vram[a + 1] >> b) & 1) << 1 | ((vram[a + 2] >> b) & 1) << 2 | ((vram[a + 3] >> b) & 1) << 3);
            if (spr[x] && !((w & 0x1000) && ci)) c = pal[spr[x]];
            else c = pal[ci + ((w & 0x800) ? 16 : 0)];
        }
        out[x] = c;
    }
    if (v->reg[0] & 0x20) for (int x = 0; x < 8; x++) out[x] = backdrop;
}

/* Los modos del TMS9918: gráficos I (M1 = M2 = M3 = 0) y II (M2) */
static void render_tms(Sms *g, int y, u16 *out)
{
    Vdp *v = &g->vdp;
    const u8 *vram = v->vram;
    u16 backdrop = sms_color(tms_pal[v->reg[7] & 15]);
    int mode = ((v->reg[1] >> 4) & 1) | (v->reg[0] & 0x02) | ((v->reg[1] >> 1) & 4);
    if (!(v->reg[1] & 0x40) || (mode != 0 && mode != 2)) {
        for (int x = 0; x < 256; x++) out[x] = backdrop;
        return;
    }
    u32 nt = (u32)(v->reg[2] & 0x0F) << 10;
    for (int col = 0; col < 32; col++) {
        u8 name = vram[nt + (u32)(y >> 3) * 32 + (u32)col];
        u8 pat, colr;
        if (mode == 2) {
            u32 idx = (u32)(y >> 6) * 256 + name;
            pat = vram[((u32)(v->reg[4] & 0x04) << 11) + idx * 8 + (u32)(y & 7)];
            colr = vram[((u32)(v->reg[3] & 0x80) << 6) + idx * 8 + (u32)(y & 7)];
        } else {
            pat = vram[((u32)(v->reg[4] & 0x07) << 11) + (u32)name * 8 + (u32)(y & 7)];
            colr = vram[((u32)v->reg[3] << 6) + (name >> 3)];
        }
        for (int b = 0; b < 8; b++) {
            u8 c = (pat >> (7 - b) & 1) ? colr >> 4 : colr & 15;
            out[col * 8 + b] = c ? sms_color(tms_pal[c]) : backdrop;
        }
    }
    /* Sprites: 32, 4 por línea, de un color */
    int size = (v->reg[1] & 0x02) ? 16 : 8, zoom = v->reg[1] & 0x01, sh = size << zoom;
    u32 sat = (u32)(v->reg[5] & 0x7F) << 7, pg = (u32)(v->reg[6] & 0x07) << 11;
    u8 drawn[256];
    memset(drawn, 0, sizeof drawn);
    int n = 0;
    for (int i = 0; i < 32; i++) {
        u8 sy = vram[sat + (u32)i * 4];
        if (sy == 0xD0) break;
        int dy = (y - (sy + 1)) & 0xFF;
        if (dy >= sh) continue;
        if (++n > 4) { v->status |= 0x40; break; }
        int x = vram[sat + (u32)i * 4 + 1];
        u8 pat = vram[sat + (u32)i * 4 + 2], attr = vram[sat + (u32)i * 4 + 3];
        if (attr & 0x80) x -= 32;
        if (size == 16) pat &= 0xFC;
        int row = dy >> zoom;
        for (int px = 0; px < (size << zoom); px++) {
            int sx = x + px;
            if (sx < 0 || sx > 255) continue;
            int bx = px >> zoom;
            u8 bits = vram[pg + (u32)pat * 8 + (u32)(bx >= 8 ? 16 : 0) + (u32)row];
            if (!(bits >> (7 - (bx & 7)) & 1)) continue;
            if (drawn[sx]) { v->status |= 0x20; continue; }
            drawn[sx] = 1;
            if (attr & 15) out[sx] = sms_color(tms_pal[attr & 15]);
        }
    }
}

void vdp_render_line(Sms *g, int y)
{
    Vdp *v = &g->vdp;
    u16 *out = v->fb + y * FB_W;
    if (v->reg[0] & 0x04) render_m4(g, y, out);
    else render_tms(g, y, out);
}

uint32_t vdp_argb(u16 c)
{
    u32 r = (c & 15) * 17u, gr = ((c >> 4) & 15) * 17u, b = ((c >> 8) & 15) * 17u;
    return 0xFF000000u | r << 16 | gr << 8 | b;
}

void vdp_view(const Sms *g, int *x0, int *y0, int *w, int *h)
{
    if (g->model == MODEL_GG && !g->sms_mode) { *x0 = 48; *y0 = 24; *w = 160; *h = 144; }
    else { *x0 = 0; *y0 = 0; *w = 256; *h = g->vdp.height; }
}
