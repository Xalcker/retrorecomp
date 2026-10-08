/*
 * PPU del DMG.
 *
 * Temporización: avanza punto por punto (456 por línea, 154 líneas) con los
 * modos 2 (búsqueda en OAM, 80 puntos), 3 (dibujo, duración variable) y 0
 * (HBlank), y el modo 1 (VBlank) en las líneas 144-153. La línea de
 * interrupción STAT es el OR de las fuentes habilitadas y solo pide la
 * interrupción en el flanco de subida, como en el hardware.
 *
 * Dibujo: cada línea se compone completa al empezar su modo 3, con los
 * registros vigentes en ese momento. Eso reproduce los cambios entre líneas
 * (SCX/SCY/paletas en HBlank, efectos con LYC). Los cambios a mitad del modo 3
 * requieren la FIFO de píxeles (fase 5).
 */
#include "gb.h"

#include <string.h>

#define LCDC_ON      0x80
#define LCDC_WIN9C00 0x40
#define LCDC_WINON   0x20
#define LCDC_TILE8000 0x10
#define LCDC_BG9C00  0x08
#define LCDC_OBJ16   0x04
#define LCDC_OBJON   0x02
#define LCDC_BGON    0x01

#define LCDC(g) ((g)->io[IO_LCDC])

void ppu_reset(GB *g)
{
    Ppu *p = &g->ppu;
    memset(p->buf, 0, sizeof p->buf);
    p->front = 0;
    p->back = p->buf[1];
    p->dot = 0;
    p->ly = 0;
    p->ly_reg = 0;
    p->mode = 0;
    p->mode3_end = 252;
    p->stat_line = 0;
    p->window_line = 0;
    p->window_active = 0;
    p->lcd_first_line = 0;
    p->frames = 0;
}

static int lyc_match(GB *g)
{
    return g->ppu.ly_reg == g->io[IO_LYC];
}

/* Recalcula la línea STAT y pide la interrupción en el flanco de subida */
static void update_stat(GB *g, int extra_mode2)
{
    Ppu *p = &g->ppu;
    u8 stat = g->io[IO_STAT];
    int line = 0;

    if (LCDC(g) & LCDC_ON) {
        if ((stat & 0x40) && lyc_match(g)) line = 1;
        if ((stat & 0x08) && p->mode == 0) line = 1;
        if ((stat & 0x10) && p->mode == 1) line = 1;
        if ((stat & 0x20) && (p->mode == 2 || extra_mode2)) line = 1;
    }
    if (line && !p->stat_line)
        IF_REG(g) |= INT_STAT;
    p->stat_line = (u8)line;
}

/* --- Dibujo de una línea --- */

static u8 tile_pixel(GB *g, u16 tile_addr, int x, int y)
{
    const u8 *row = &g->vram[(tile_addr - 0x8000) + y * 2];
    int bit = 7 - x;
    return (u8)(((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1));
}

static u16 bg_tile_addr(u8 lcdc, u8 index)
{
    if (lcdc & LCDC_TILE8000)
        return (u16)(0x8000 + index * 16);
    return (u16)(0x9000 + (s8)index * 16);
}

/* Búsqueda en OAM: los primeros 10 sprites que cubren la línea */
static void oam_scan(GB *g)
{
    Ppu *p = &g->ppu;
    int h = (LCDC(g) & LCDC_OBJ16) ? 16 : 8;
    p->nsprites = 0;
    for (int i = 0; i < 40 && p->nsprites < 10; i++) {
        int top = g->oam[i * 4] - 16;
        if (p->ly >= top && p->ly < top + h)
            p->sprites[p->nsprites++] = (u8)i;
    }
}

/* Duración aproximada del modo 3: 172 puntos más las penalizaciones por
 * desplazamiento fino, ventana y sprites. */
static u32 mode3_length(GB *g, int window_here)
{
    Ppu *p = &g->ppu;
    u32 len = 172 + (g->io[IO_SCX] & 7);
    if (window_here) len += 6;
    if (LCDC(g) & LCDC_OBJON) {
        for (int i = 0; i < p->nsprites; i++) {
            int x = g->oam[p->sprites[i] * 4 + 1];
            int phase = (x + g->io[IO_SCX]) & 7;
            len += 6 + (phase < 5 ? 5 - phase : 0);
        }
    }
    return len > 289 ? 289 : len;
}

static void render_line(GB *g, int window_here)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);
    int ly = p->ly;
    u8 bg[GB_SCREEN_W];
    u8 *out = &p->back[ly * GB_SCREEN_W];
    u8 bgp = g->io[IO_BGP];

    if (lcdc & LCDC_BGON) {
        u16 map = (lcdc & LCDC_BG9C00) ? 0x9C00 : 0x9800;
        u8 y = (u8)(ly + g->io[IO_SCY]);
        u8 scx = g->io[IO_SCX];
        for (int x = 0; x < GB_SCREEN_W; x++) {
            u8 px = (u8)(x + scx);
            u8 index = g->vram[map - 0x8000 + (y / 8) * 32 + px / 8];
            bg[x] = tile_pixel(g, bg_tile_addr(lcdc, index), px % 8, y % 8);
        }
        if (window_here) {
            u16 wmap = (lcdc & LCDC_WIN9C00) ? 0x9C00 : 0x9800;
            int start = g->io[IO_WX] - 7;
            int wl = p->window_line;
            for (int x = start < 0 ? 0 : start; x < GB_SCREEN_W; x++) {
                int px = x - start;
                u8 index = g->vram[wmap - 0x8000 + (wl / 8) * 32 + px / 8];
                bg[x] = tile_pixel(g, bg_tile_addr(lcdc, index), px % 8, wl % 8);
            }
        }
    } else {
        memset(bg, 0, sizeof bg);
    }

    for (int x = 0; x < GB_SCREEN_W; x++)
        out[x] = (bgp >> (bg[x] * 2)) & 3;

    if (!(lcdc & LCDC_OBJON) || p->nsprites == 0)
        return;

    /* Prioridad DMG: menor X gana; a igual X, menor índice de OAM.
     * Se dibuja del menos al más prioritario. */
    u8 order[10];
    int n = p->nsprites;
    memcpy(order, p->sprites, (size_t)n);
    for (int a = 1; a < n; a++) {
        u8 v = order[a];
        int b = a - 1;
        while (b >= 0 && g->oam[order[b] * 4 + 1] > g->oam[v * 4 + 1]) {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = v;
    }

    int h = (lcdc & LCDC_OBJ16) ? 16 : 8;
    for (int k = n - 1; k >= 0; k--) {
        const u8 *s = &g->oam[order[k] * 4];
        int top = s[0] - 16, left = s[1] - 8;
        u8 tile = s[2], attr = s[3];
        int row = ly - top;
        if (attr & 0x40) row = h - 1 - row;
        if (h == 16) tile &= 0xFE;
        u16 addr = (u16)(0x8000 + tile * 16 + (row >= 8 ? 16 : 0));
        u8 pal = (attr & 0x10) ? g->io[IO_OBP1] : g->io[IO_OBP0];
        for (int col = 0; col < 8; col++) {
            int x = left + col;
            if (x < 0 || x >= GB_SCREEN_W) continue;
            u8 cidx = tile_pixel(g, addr, (attr & 0x20) ? 7 - col : col, row % 8);
            if (cidx == 0) continue;
            if ((attr & 0x80) && bg[x] != 0) continue;
            out[x] = (pal >> (cidx * 2)) & 3;
        }
    }
}

/* --- Máquina de estados por punto --- */

static void start_mode3(GB *g)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);
    oam_scan(g);
    if (p->ly == g->io[IO_WY]) p->window_active = 1;
    int window_here = (lcdc & LCDC_WINON) && (lcdc & LCDC_BGON) && p->window_active &&
                      g->io[IO_WX] <= 166;
    p->mode = 3;
    p->mode3_end = 80 + mode3_length(g, window_here);
    render_line(g, window_here);
    if (window_here) p->window_line++;
    update_stat(g, 0);
}

static void finish_frame(GB *g)
{
    Ppu *p = &g->ppu;
    int back = p->back == p->buf[0] ? 0 : 1;
    p->front = back;
    p->back = p->buf[back ^ 1];
    p->frames++;
}

static void new_line(GB *g)
{
    Ppu *p = &g->ppu;
    p->dot = 0;
    p->lcd_first_line = 0;
    p->ly = (u8)(p->ly + 1);
    if (p->ly == 154) {
        p->ly = 0;
        p->window_line = 0;
        p->window_active = 0;
    }
    p->ly_reg = p->ly;

    if (p->ly < 144) {
        p->mode = 2;
        update_stat(g, 0);
    } else if (p->ly == 144) {
        p->mode = 1;
        IF_REG(g) |= INT_VBLANK;
        update_stat(g, 1);      /* en DMG la fuente de modo 2 también dispara aquí */
        finish_frame(g);
    } else {
        update_stat(g, 0);
    }
}

static void ppu_dot(GB *g)
{
    Ppu *p = &g->ppu;
    p->dot++;
    if (p->dot == 456) {
        new_line(g);
        return;
    }
    if (p->ly < 144) {
        if (p->dot == 80) start_mode3(g);
        else if (p->dot == p->mode3_end) {
            p->mode = 0;
            update_stat(g, 0);
        }
    } else if (p->ly == 153 && p->dot == 4) {
        /* En la línea 153 LY ya vale 0 casi desde el principio */
        p->ly_reg = 0;
        update_stat(g, 0);
    }
}

void ppu_mcycle(GB *g)
{
    if (!(LCDC(g) & LCDC_ON)) return;
    for (int i = 0; i < 4; i++) ppu_dot(g);
}

u64 ppu_next_event(GB *g)
{
    Ppu *p = &g->ppu;
    if (!(LCDC(g) & LCDC_ON)) return UINT64_MAX;
    u32 next = 456;
    if (p->ly < 144) {
        if (p->dot < 80) next = 80;
        else if (p->dot < p->mode3_end) next = p->mode3_end;
    } else if (p->ly == 153 && p->dot < 4) {
        next = 4;
    }
    return g->hw_cyc + (next - p->dot);
}

int ppu_vram_blocked(GB *g)
{
    return (LCDC(g) & LCDC_ON) && g->ppu.mode == 3;
}

int ppu_oam_blocked(GB *g)
{
    return (LCDC(g) & LCDC_ON) && (g->ppu.mode == 2 || g->ppu.mode == 3);
}

u8 ppu_read(GB *g, u8 reg)
{
    Ppu *p = &g->ppu;
    switch (reg) {
    case IO_STAT: {
        u8 v = (u8)(0x80 | (g->io[IO_STAT] & 0x78));
        if (LCDC(g) & LCDC_ON) {
            v |= p->mode;
            if (lyc_match(g)) v |= 0x04;
        }
        return v;
    }
    case IO_LY:
        return (LCDC(g) & LCDC_ON) ? p->ly_reg : 0;
    default:
        return g->io[reg];
    }
}

void ppu_write(GB *g, u8 reg, u8 v)
{
    Ppu *p = &g->ppu;
    switch (reg) {
    case IO_LCDC: {
        u8 old = LCDC(g);
        g->io[IO_LCDC] = v;
        if ((old & LCDC_ON) && !(v & LCDC_ON)) {
            /* Apagado: LY vuelve a 0 y la pantalla queda en blanco */
            p->ly = 0;
            p->ly_reg = 0;
            p->dot = 0;
            p->mode = 0;
            p->stat_line = 0;
            memset(p->back, 0, GB_SCREEN_W * GB_SCREEN_H);
            finish_frame(g);
        } else if (!(old & LCDC_ON) && (v & LCDC_ON)) {
            /* Encendido: la primera línea empieza sin modo 2 */
            p->ly = 0;
            p->ly_reg = 0;
            p->dot = 0;
            p->mode = 0;
            p->lcd_first_line = 1;
            p->window_line = 0;
            p->window_active = 0;
            update_stat(g, 0);
        }
        return;
    }
    case IO_STAT:
        g->io[IO_STAT] = (u8)((g->io[IO_STAT] & 0x87) | (v & 0x78));
        update_stat(g, 0);
        return;
    case IO_LYC:
        g->io[IO_LYC] = v;
        update_stat(g, 0);
        return;
    case IO_LY:
        return;     /* solo lectura */
    default:
        g->io[reg] = v;
        return;
    }
}
