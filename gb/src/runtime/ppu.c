/*
 * PPU del DMG con FIFO de píxeles, punto por punto.
 *
 * La línea de tiempo de cada línea (456 puntos) sigue la del hardware tal
 * como la documentan las pruebas de Mooneye y Mealybug (y el emulador
 * SameBoy, usado como referencia de tiempos):
 *
 *   punto 3    LY cambia; la interrupción de modo 2 dispara un punto antes de
 *              que STAT muestre el modo 2; la comparación LY=LYC se suspende
 *   punto 4    STAT muestra el modo 2; vuelve la comparación con LYC
 *   6..84      búsqueda en OAM: una entrada cada 2 puntos
 *   punto 84   STAT muestra el modo 3
 *   punto 89   empieza el dibujo: la FIFO trae 8 píxeles basura, el fetcher
 *              lee mosaicos (6 puntos y empuja cuando la FIFO está vacía) y
 *              sale un píxel por punto. Los primeros píxeles se descartan
 *              (basura, SCX & 7 y 8 más), así que la columna 0 sale en el
 *              punto 97 y el modo 3 dura 172 puntos sin sprites ni SCX.
 *   fin        STAT pasa a modo 0; la interrupción de HBlank, un punto después
 *
 * Los sprites detienen la salida mientras el fetcher termina su mosaico y
 * se leen en 6 puntos más; la ventana vacía la FIFO y reinicia el fetcher.
 * Cada registro se lee en el punto en que el hardware lo usa (SCX/SCY y los
 * bits de mapa y datos de LCDC al leer cada mosaico; paletas y los bits de
 * fondo/sprites de LCDC al sacar cada píxel), así que los cambios a mitad de
 * línea se reproducen.
 *
 * La PPU lleva su propio reloj (p->cyc): ppu_advance la lleva hasta un ciclo
 * dado, lo que permite aplicar una escritura en un punto dentro del M-ciclo.
 */
#include "gb.h"

#include <stdlib.h>
#include <string.h>

#define LCDC_ON       0x80
#define LCDC_WIN9C00  0x40
#define LCDC_WINON    0x20
#define LCDC_TILE8000 0x10
#define LCDC_BG9C00   0x08
#define LCDC_OBJ16    0x04
#define LCDC_OBJON    0x02
#define LCDC_BGON     0x01

#define LCDC(g) ((g)->io[IO_LCDC])
#define POS(v)  ((u8)(v))   /* posiciones negativas en 8 bits, como el hardware */

enum { FETCH_PUSH = 6 };

/* Lectura de un sprite en curso */
enum { OBJ_NONE, OBJ_WAIT, OBJ_B, OBJ_B2, OBJ_C, OBJ_C2, OBJ_D };

static void spread_init(void);

void ppu_reset(GB *g)
{
    Ppu *p = &g->ppu;
    spread_init();
    memset(p, 0, sizeof *p);
    p->back = p->buf[1];
    p->line_len = 456;
    p->mode_irq = -1;
    p->window_y = -1;
    p->pos = POS(-16);
}

/* Bits de LYC y línea de interrupción STAT; pide la interrupción en el flanco */
static void stat_update(GB *g)
{
    Ppu *p = &g->ppu;
    u8 *stat = &g->io[IO_STAT];
    if (!(LCDC(g) & LCDC_ON)) return;

    if (p->ly_cmp == g->io[IO_LYC]) {
        p->lyc_line = 1;
        *stat |= 0x04;
    } else {
        if (p->ly_cmp != -1) p->lyc_line = 0;
        *stat &= (u8)~0x04;
    }

    int line;
    switch (p->mode_irq) {
    case 0: line = (*stat & 0x08) != 0; break;
    case 1: line = (*stat & 0x10) != 0; break;
    case 2: line = (*stat & 0x20) != 0; break;
    default: line = 0; break;
    }
    if ((*stat & 0x40) && p->lyc_line) line = 1;

    if (line && !p->stat_line) IF_REG(g) |= INT_STAT;
    p->stat_line = (u8)line;
}

static void wy_check(GB *g)
{
    Ppu *p = &g->ppu;
    int cmp = p->ly_cmp != -1 ? p->ly_cmp : p->ly;
    if ((LCDC(g) & LCDC_WINON) && g->io[IO_WY] == cmp) p->wy_triggered = 1;
}

static void finish_frame(GB *g)
{
    Ppu *p = &g->ppu;
    int back = p->back == p->buf[0] ? 0 : 1;
    p->front = back;
    p->back = p->buf[back ^ 1];
    p->frames++;
    if (g->sgb) sgb_frame(g, p->buf[back]);
}

/* Lectura de VRAM de la PPU: `addr` va de 0 a $3FFF (banco 1 desde $2000) */
static u8 vram(GB *g, u16 addr)
{
    return g->vram[addr & 0x3FFF];
}

/* ------------------------------------------------------------------ */
/* Búsqueda en OAM                                                     */
/* ------------------------------------------------------------------ */

static void add_object(GB *g, int index)
{
    Ppu *p = &g->ppu;
    u8 y = g->oam[index * 4], x = g->oam[index * 4 + 1];
    if (p->n_objs == 10) return;
    int h = (LCDC(g) & LCDC_OBJ16) ? 16 : 8;
    int top = y - 16;
    if (top > p->ly || top + h <= p->ly) return;

    /* Orden descendente por X; a igual X, el de menor índice queda al final
     * (se lee primero) */
    int j = 0;
    while (j < p->n_objs && p->objs_x[j] > x) j++;
    memmove(p->objs + j + 1, p->objs + j, (size_t)(p->n_objs - j));
    memmove(p->objs_x + j + 1, p->objs_x + j, (size_t)(p->n_objs - j));
    memmove(p->objs_y + j + 1, p->objs_y + j, (size_t)(p->n_objs - j));
    p->objs[j] = (u8)index;
    p->objs_x[j] = x;
    p->objs_y[j] = y;
    p->n_objs++;
}

/* ------------------------------------------------------------------ */
/* Modo 3                                                              */
/* ------------------------------------------------------------------ */

/* spread[f][v]: el bit 7-i de v (o el bit i, con giro) en el byte i */
static u64 spread[2][256];

static void spread_init(void)
{
    if (spread[0][0x80]) return;
    for (int v = 0; v < 256; v++)
        for (int i = 0; i < 8; i++) {
            spread[0][v] |= (u64)((v >> (7 - i)) & 1) << (i * 8);
            spread[1][v] |= (u64)((v >> i) & 1) << (i * 8);
        }
}

static void bg_push_row(Ppu *p, u8 lo, u8 hi, u8 attr)
{
    int flip = (attr & 0x20) != 0;              /* CGB: giro horizontal */
    u64 px = spread[flip][lo] | spread[flip][hi] << 1;
    for (int i = 0; i < 8; i++) {
        int slot = (p->bg_head + p->bg_size++) & 15;
        p->bg_fifo[slot] = (u8)(px >> (i * 8));
        p->bg_attr[slot] = attr;
    }
}

static void start_rendering(GB *g)
{
    Ppu *p = &g->ppu;
    p->rendering = 1;
    p->bg_head = 0;
    p->bg_size = 0;
    bg_push_row(p, 0, 0, 0);    /* 8 píxeles basura que se descartan */
    p->obj_head = 0;
    p->obj_size = 0;
    p->lcd_x = 0;
    p->fetch_state = 0;
    p->obj_state = OBJ_NONE;
    p->resume = 0;
    p->insert_bg_pixel = 0;
    p->window_fetching = 0;
    p->no_win_glitch = 0;
}

static u8 fetcher_y(GB *g)
{
    Ppu *p = &g->ppu;
    return p->wx_triggered ? (u8)p->window_y : (u8)(p->ly + g->io[IO_SCY]);
}

static u16 tile_row_addr(GB *g)
{
    Ppu *p = &g->ppu;
    u16 base = (LCDC(g) & LCDC_TILE8000) ? (u16)(p->fetch_tile * 16)
                                         : (u16)(0x1000 + (s8)p->fetch_tile * 16);
    u8 row = fetcher_y(g) & 7;
    if (p->fetch_attr & 0x40) row ^= 7;         /* CGB: giro vertical */
    if (p->fetch_attr & 0x08) base += 0x2000;   /* CGB: banco 1 */
    return (u16)(base + row * 2);
}

/* Un punto del fetcher del fondo/ventana */
static void fetcher_step(GB *g)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);

    switch (p->fetch_state) {
    case 0: {
        if (!(lcdc & LCDC_WINON)) p->wx_triggered = 0;
        u16 map = 0x1800;
        if (p->wx_triggered ? (lcdc & LCDC_WIN9C00) : (lcdc & LCDC_BG9C00)) map = 0x1C00;
        u8 y = fetcher_y(g), x;
        if (p->wx_triggered) x = p->window_tile_x;
        else if (POS(p->pos + 16) < 8) x = g->io[IO_SCX] >> 3;
        else x = (u8)(((g->io[IO_SCX] + p->pos + 8) / 8) & 0x1F);
        p->fetch_addr = (u16)(map + x + (y / 8) * 32);
        p->fetch_state = 1;
        return;
    }
    case 1:
        p->fetch_tile = vram(g, p->fetch_addr);
        p->fetch_attr = g->cgb ? vram(g, (u16)(p->fetch_addr + 0x2000)) : 0;
        p->fetch_state = 2;
        return;
    case 2:
        p->fetch_addr = tile_row_addr(g);
        p->fetch_state = 3;
        return;
    case 3:
        p->fetch_data[0] = vram(g, p->fetch_addr);
        p->fetch_state = 4;
        return;
    case 4:
        p->fetch_addr = (u16)(tile_row_addr(g) + 1);
        p->fetch_state = 5;
        return;
    case 5:
        p->fetch_data[1] = vram(g, p->fetch_addr);
        if (p->wx_triggered) p->window_tile_x = (p->window_tile_x + 1) & 0x1F;
        /* sigue con el empuje en el mismo punto */
        /* fallthrough */
    default:
        p->fetch_state = FETCH_PUSH;
        if (p->bg_size > 0) return;
        if (!g->cgb_hw && p->wy_triggered && !(lcdc & LCDC_WINON) && !p->no_win_glitch) {
            /* Falla del DMG: con la ventana apagada a mitad de línea, en la
             * columna de WX se mete un solo píxel de color 0 */
            u8 logical = POS(p->pos + 7);
            if (logical > 167) logical = 0;
            if (g->io[IO_WX] == logical) {
                p->bg_fifo[p->bg_head] = 0;
                p->bg_size = 1;
                return;
            }
        }
        bg_push_row(p, p->fetch_data[0], p->fetch_data[1], p->fetch_attr);
        p->fetch_state = 0;
        return;
    }
}

static void end_rendering(GB *g)
{
    Ppu *p = &g->ppu;
    p->rendering = 0;
    p->pos = POS(-16);
    if (p->ly == 143) p->window_y = -1;
    if (p->wy_triggered && (LCDC(g) & LCDC_WINON) && g->io[IO_WX] == 166) {
        p->wx_triggered = 1;
        p->window_tile_x = 1;
        p->window_y++;
    } else {
        p->wx_triggered = 0;
    }
    p->mode = 0;
    p->mode_irq = 0;
    p->hblank_irq_dot = (u16)(p->dot + 1);
    p->oam_rb = p->oam_wb = p->vram_rb = p->vram_wb = 0;
    if (g->cgb && p->ly < 144) gb_hdma_hblank(g);
}

/* Saca un píxel de la FIFO (si se puede) */
static void render_pixel(GB *g)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);

    /* Un sprite en X = 0 pendiente detiene la salida */
    if (p->n_objs && ((lcdc & LCDC_OBJON) || g->cgb_hw) && p->objs_x[p->n_objs - 1] == 0) return;
    if (!p->bg_size) return;

    u8 bg, bg_attr = 0;
    if (p->insert_bg_pixel) {
        p->insert_bg_pixel = 0;
        bg = 0;
    } else {
        bg = p->bg_fifo[p->bg_head];
        bg_attr = p->bg_attr[p->bg_head];
        p->bg_head = (p->bg_head + 1) & 15;
        p->bg_size--;
    }
    ObjPixel o = { 0, 0, 0 };
    if (p->obj_size) {
        o = p->obj_fifo[p->obj_head];
        p->obj_head = (p->obj_head + 1) & 7;
        p->obj_size--;
    }

    /* Descarte inicial: basura y desplazamiento fino (SCX & 7) */
    if (POS(p->pos + 16) < 8) {
        if ((p->pos & 7) == (g->io[IO_SCX] & 7)) {
            p->pos = POS(-8);
        } else if (p->window_fetching && (p->pos & 7) == 6 && (g->io[IO_SCX] & 7) == 7) {
            p->pos = POS(-8);
        } else if (p->pos == POS(-9)) {
            p->pos = POS(-16);
            return;
        }
    }
    p->window_fetching = 0;

    if (p->pos >= 160) {    /* posiciones negativas: se descartan */
        p->pos++;
        return;
    }

    u16 color;
    if (g->cgb) {
        /* CGB: LCDC.0 es la prioridad maestra del fondo; con él apagado los
         * sprites siempre quedan encima */
        int bg_wins = (lcdc & LCDC_BGON) && bg && ((bg_attr & 0x80) || (o.attr & 0x80));
        if (o.pixel && (lcdc & LCDC_OBJON) && !bg_wins) {
            const u8 *c = &g->obj_pal[(o.attr & 7) * 8 + o.pixel * 2];
            color = (u16)(c[0] | c[1] << 8) & 0x7FFF;
        } else {
            const u8 *c = &g->bg_pal[(bg_attr & 7) * 8 + bg * 2];
            color = (u16)(c[0] | c[1] << 8) & 0x7FFF;
        }
    } else {
        if (!(lcdc & LCDC_BGON)) bg = 0;    /* DMG: sin fondo, color 0 */
        color = (g->io[IO_BGP] >> (bg * 2)) & 3;
        int obj = 0;
        if (o.pixel && (lcdc & LCDC_OBJON) && !((o.attr & 0x80) && bg)) {
            obj = (o.attr & 0x10) ? 2 : 1;
            u8 pal = obj == 2 ? g->io[IO_OBP1] : g->io[IO_OBP0];
            color = (pal >> (o.pixel * 2)) & 3;
        }
        if (g->cgb_hw) {
            /* GBC en compatibilidad: el tono elige un color de la paleta 0 de
             * fondo o de las paletas 0 y 1 de sprites */
            const u8 *c = obj ? &g->obj_pal[(obj - 1) * 8 + color * 2] : &g->bg_pal[color * 2];
            color = (u16)(c[0] | c[1] << 8) & 0x7FFF;
        }
    }
    if (p->lcd_x < GB_SCREEN_W && p->ly < GB_SCREEN_H)
        p->back[p->ly * GB_SCREEN_W + p->lcd_x] = color;
    p->pos++;
    p->lcd_x++;
}

/* Mezcla el sprite recién leído en la FIFO de sprites */
static void overlay_object(GB *g, u8 lo, u8 hi)
{
    Ppu *p = &g->ppu;
    /* CGB (con OPRI = 0): gana el de menor índice de OAM; DMG: el que ya estaba */
    int by_index = g->cgb_hw && !g->opri;
    u8 index = p->objs[p->n_objs - 1];
    while (p->obj_size < 8) {
        p->obj_fifo[(p->obj_head + p->obj_size) & 7].pixel = 0;
        p->obj_size++;
    }
    int flip = p->obj_flags & 0x20;
    for (int i = 0; i < 8; i++) {
        int bit = flip ? i : 7 - i;     /* i = columna dentro del sprite */
        u8 px = (u8)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        ObjPixel *t = &p->obj_fifo[(p->obj_head + i) & 7];
        if (px && (!t->pixel || (by_index && t->index > index))) {
            t->pixel = px;
            t->attr = p->obj_flags;
            t->index = index;
        }
    }
}

static u16 object_row_addr(GB *g)
{
    Ppu *p = &g->ppu;
    int h16 = (LCDC(g) & LCDC_OBJ16) != 0;
    u8 row = (u8)((p->ly - p->objs_y[p->n_objs - 1]) & (h16 ? 0xF : 7));
    if (p->obj_flags & 0x40) row ^= h16 ? 0xF : 7;
    u16 addr = (u16)((h16 ? p->obj_tile & 0xFE : p->obj_tile) * 16 + row * 2);
    if (g->cgb && (p->obj_flags & 0x08)) addr += 0x2000;    /* CGB: banco 1 */
    return addr;
}

/* El fetcher ya terminó su mosaico y hay píxeles: se puede leer el sprite */
static int fetcher_idle(Ppu *p)
{
    return p->fetch_state >= 5 && p->bg_size > 0;
}

static u8 object_match_x(Ppu *p)
{
    u8 x = POS(p->pos + 8);
    return x > POS(-16) ? 0 : x;
}

static void window_check(GB *g)
{
    Ppu *p = &g->ppu;
    if (p->wx_triggered || !p->wy_triggered || !(LCDC(g) & LCDC_WINON)) return;
    u8 wx = g->io[IO_WX];
    int activate = 0;
    if (wx == 0) {
        if (p->pos == POS(-7)) activate = 1;
        else if (p->pos == POS(-16) && (g->io[IO_SCX] & 7)) activate = 1;
        else if (p->pos >= POS(-15) && p->pos <= POS(-8)) activate = 1;
    } else if (wx < 166) {
        if (wx == POS(p->pos + 7)) {
            activate = 1;
        } else if (!g->cgb_hw && wx == POS(p->pos + 6) && !p->wx_just_changed) {
            /* Desfase del DMG entre la PPU y el LCD */
            activate = 1;
            if (p->lcd_x > 0) p->lcd_x--;
        }
    }
    if (activate) {
        p->window_y++;
        p->window_tile_x = 0;
        p->bg_size = 0;
        p->wx_triggered = 1;
        p->fetch_state = 0;
        p->window_fetching = 1;
        if (wx == 0 && (g->io[IO_SCX] & 7)) p->resume = 2;   /* cuesta un punto */
    } else if (wx == 166 && wx == POS(p->pos + 7)) {
        p->window_y++;
    }
}

static void mode3_dot(GB *g)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);

    switch (p->obj_state) {
    case OBJ_WAIT: {
        /* Mientras el fetcher no termine, avanza; el punto en que ya
         * terminó también lo avanza una vez más */
        int idle = fetcher_idle(p);
        fetcher_step(g);
        if (idle) p->obj_state = OBJ_B;
        return;
    }
    case OBJ_B:
        fetcher_step(g);
        p->obj_tile = g->oam[p->objs[p->n_objs - 1] * 4 + 2];
        p->obj_flags = g->oam[p->objs[p->n_objs - 1] * 4 + 3];
        p->obj_state = OBJ_B2;
        return;
    case OBJ_B2:
        p->obj_state = OBJ_C;
        return;
    case OBJ_C:
        p->obj_lo = vram(g, object_row_addr(g));
        p->obj_state = OBJ_C2;
        return;
    case OBJ_C2:
        p->obj_state = OBJ_D;
        return;
    case OBJ_D:
        overlay_object(g, p->obj_lo, vram(g, (u16)(object_row_addr(g) + 1)));
        p->n_objs--;
        p->obj_state = OBJ_NONE;
        p->resume = 1;      /* el punto siguiente no revisa la ventana */
        return;
    default:
        break;
    }

    if (p->resume == 2) {
        p->resume = 0;      /* punto extra al activar la ventana con WX = 0 */
        return;
    }
    if (p->resume) {
        p->resume = 0;
    } else {
        window_check(g);
        if (p->resume == 2) return;
        if (g->io[IO_WX] == POS(p->pos + 7) && (!g->cgb_hw || g->io[IO_WX] == 0) &&
            p->wx_triggered && !p->window_fetching &&
            p->fetch_state == 0 && p->bg_size == 8)
            p->insert_bg_pixel = 1;
    }

    /* Sprites: se descartan los que ya pasaron; el que coincide se lee */
    u8 mx = object_match_x(p);
    while (p->n_objs && p->objs_x[p->n_objs - 1] < mx) p->n_objs--;
    if (p->n_objs && ((lcdc & LCDC_OBJON) || g->cgb_hw) && p->objs_x[p->n_objs - 1] == mx) {
        /* El fetcher termina el mosaico que estaba leyendo; luego, 6 puntos */
        int idle = fetcher_idle(p);
        fetcher_step(g);
        p->obj_state = idle ? OBJ_B : OBJ_WAIT;
        return;
    }

    render_pixel(g);
    fetcher_step(g);
    if (p->pos == 160) end_rendering(g);
}

/* ------------------------------------------------------------------ */
/* Línea de tiempo                                                     */
/* ------------------------------------------------------------------ */

static void visible_line_dot(GB *g)
{
    Ppu *p = &g->ppu;
    u32 d = p->dot;

    if (p->lcd_first_line) {
        /* Primera línea tras encender el LCD: sin modo 2 y más corta */
        if (d == 1) {
            p->ly = 0;
            p->window_y = -1;
            p->wy_triggered = 0;
            p->pos = POS(-16);
            p->ly_cmp = 0;
            p->mode = 0;
            p->mode_irq = -1;
            p->n_objs = 0;
            p->oam_rb = p->oam_wb = p->vram_rb = p->vram_wb = 0;
            stat_update(g);
        } else if (d == 77) {
            p->oam_wb = 1;
        } else if (d == 79) {
            p->oam_rb = p->oam_wb = p->vram_rb = p->vram_wb = 1;
            p->mode = 3;
            p->mode_irq = 3;
        } else if (d == 84) {
            p->wx_triggered = 0;
            start_rendering(g);
        }
    } else {
        switch (d) {
        case 0:
            wy_check(g);
            p->oam_wb = 0;
            break;
        case 3:
            p->ly_reg = p->ly;
            p->ly_cmp = p->ly ? -1 : 0;
            p->oam_rb = 1;
            if (p->ly != 0) p->mode_irq = 2;
            p->mode = 0;
            stat_update(g);
            break;
        case 4:
            p->mode = 2;
            p->mode_irq = 2;
            p->oam_rb = p->oam_wb = 1;
            p->ly_cmp = p->ly;
            wy_check(g);
            stat_update(g);
            p->mode_irq = -1;
            stat_update(g);
            p->n_objs = 0;
            break;
        case 84:
            add_object(g, 39);
            p->oam_rb = p->oam_wb = p->vram_rb = p->vram_wb = 1;
            p->mode = 3;
            p->mode_irq = 3;
            stat_update(g);
            break;
        case 89:
            start_rendering(g);
            break;
        default:
            if (d >= 6 && d < 84 && !(d & 1)) add_object(g, (int)(d - 6) / 2);
            if (d == 80 && !g->cgb_hw) {
                /* Al leer la entrada 37: VRAM se bloquea para leer y la OAM se
                 * libera para escribir hasta el modo 3 */
                p->vram_rb = 1;
                p->vram_wb = 0;
                p->oam_wb = 0;
            }
            break;
        }
    }

    if (p->rendering) mode3_dot(g);
    else if (p->hblank_irq_dot && d == p->hblank_irq_dot) {
        p->hblank_irq_dot = 0;
        stat_update(g);
    }
}

static void vblank_line_dot(GB *g)
{
    Ppu *p = &g->ppu;
    u32 d = p->dot;

    if (p->ly < 153) {
        switch (d) {
        case 0:
            p->ly_cmp = -1;
            stat_update(g);
            break;
        case 1:
            /* GBC: la interrupción de modo 2 de la línea 144 llega un M-ciclo
             * antes que la de VBlank (vblank_stat_intr-C) */
            if (g->cgb_hw && p->ly == 144 && !p->stat_line && (g->io[IO_STAT] & 0x20)) IF_REG(g) |= INT_STAT;
            break;
        case 2:
            p->ly_reg = p->ly;
            if (!g->cgb_hw && p->ly == 144 && !p->stat_line && (g->io[IO_STAT] & 0x20)) IF_REG(g) |= INT_STAT;
            break;
        case 4:
            p->ly_cmp = p->ly;
            stat_update(g);
            break;
        case 5:
            if (p->ly == 144) {
                /* Entrar a VBlank también dispara la interrupción de modo 2 */
                p->mode = 1;
                IF_REG(g) |= INT_VBLANK;
                if (!p->stat_line && (g->io[IO_STAT] & 0x20)) IF_REG(g) |= INT_STAT;
                p->mode_irq = 1;
                stat_update(g);
                finish_frame(g);
            }
            break;
        default:
            break;
        }
        return;
    }

    /* Línea 153: LY vale 153 solo unos puntos y luego 0 */
    switch (d) {
    case 0:
        p->ly_cmp = -1;
        stat_update(g);
        break;
    case 2:
        p->ly_reg = 153;
        break;
    case 4:
        if (g->cgb_hw) {       /* CGB: LY sigue en 153 hasta el punto 8 */
            p->ly_cmp = 153;
            stat_update(g);
        }
        break;
    case 6:
        if (!g->cgb_hw) {
            p->ly_reg = 0;
            p->ly_cmp = 153;
            stat_update(g);
        }
        break;
    case 8:
        p->ly_reg = 0;
        p->ly_cmp = g->cgb_hw ? 153 : -1;
        stat_update(g);
        break;
    case 12:
        p->ly_cmp = 0;
        stat_update(g);
        break;
    default:
        break;
    }
}

static void end_line(GB *g)
{
    Ppu *p = &g->ppu;
    if (p->rendering) {
        /* El modo 3 no terminó (no debería pasar): se cierra */
        end_rendering(g);
    }
    if (p->ly < 143) p->mode_irq = 2;
    p->dot = 0;
    p->line_len = 456;
    p->lcd_first_line = 0;
    p->hblank_irq_dot = 0;
    if (++p->ly == 154) {
        p->ly = 0;
        p->wy_triggered = 0;
    }
}

static void ppu_dot(GB *g)
{
    Ppu *p = &g->ppu;
    if (p->ly < 144) visible_line_dot(g);
    else vblank_line_dot(g);
    p->wx_just_changed = 0;
    /* Tras escribir LCDC o WY, WY se vuelve a comparar en la rejilla de 8 puntos */
    p->wy_mod++;
    if (p->wy_pending && ((p->wy_mod + (g->cgb_hw ? 0 : 2)) & 7) == 0) {
        p->wy_pending = 0;
        if (!p->wy_triggered) wy_check(g);
    }
    if (++p->dot >= p->line_len) end_line(g);
}

/* Puntos que se pueden saltar sin que pase nada (HBlank y casi todo el
 * VBlank); el último punto de la línea siempre se procesa */
static u32 idle_dots(const Ppu *p)
{
    if (p->wy_pending || p->wx_just_changed || p->rendering || p->hblank_irq_dot) return 0;
    if (p->ly < 144 ? (p->lcd_first_line || p->dot < 90) : p->dot < 13) return 0;
    return p->line_len - 1 - p->dot;
}

/*
 * Los 8 puntos de un mosaico de una vez. Con el fetcher recién empujado
 * (estado 0, 8 píxeles en la FIFO, todos del mismo mosaico) y la FIFO de
 * sprites vacía, los 8 puntos siguientes sacan esos 8 píxeles (solo fondo:
 * el color sale de una tabla de 4) mientras el fetcher lee el mosaico
 * siguiente (estados 0 a 5, con la posición del punto 1) y lo empuja en el
 * punto 8, cuando la FIFO se vacía. Es lo mismo que hacen render_pixel y
 * fetcher_step punto a punto en ese caso; mode3_fast revisa antes que ningún
 * sprite, la ventana ni el fin de la línea caiga en esos 8 puntos.
 */
static void mode3_tile(GB *g)
{
    Ppu *p = &g->ppu;
    u8 lcdc = LCDC(g);

    /* Píxeles: los 8 de la FIFO, con la misma paleta (mismo mosaico) */
    u8 attr = p->bg_attr[p->bg_head];
    u16 lut[4];
    for (int c = 0; c < 4; c++) {
        if (g->cgb) {
            const u8 *e = &g->bg_pal[(attr & 7) * 8 + c * 2];
            lut[c] = (u16)(e[0] | e[1] << 8) & 0x7FFF;
        } else {
            u16 color = (g->io[IO_BGP] >> (((lcdc & LCDC_BGON) ? c : 0) * 2)) & 3;
            if (g->cgb_hw) {
                const u8 *e = &g->bg_pal[color * 2];
                color = (u16)(e[0] | e[1] << 8) & 0x7FFF;
            }
            lut[c] = color;
        }
    }
    u16 *row = &p->back[p->ly * GB_SCREEN_W];
    u8 pos0 = p->pos;
    for (int i = 0; i < 8; i++) {
        u8 px = p->bg_fifo[(p->bg_head + i) & 15];
        if (p->lcd_x < GB_SCREEN_W) row[p->lcd_x] = lut[px];
        p->lcd_x++;
    }
    p->bg_head = (p->bg_head + 8) & 15;
    p->bg_size = 0;
    p->pos = (u8)(pos0 + 8);
    p->window_fetching = 0;

    /* Fetcher. Estado 0 (punto 1, la posición ya avanzó uno) */
    if (!(lcdc & LCDC_WINON)) p->wx_triggered = 0;
    u16 map = 0x1800;
    if (p->wx_triggered ? (lcdc & LCDC_WIN9C00) : (lcdc & LCDC_BG9C00)) map = 0x1C00;
    u8 y = fetcher_y(g), x;
    if (p->wx_triggered) x = p->window_tile_x;
    else x = (u8)(((g->io[IO_SCX] + (u8)(pos0 + 1) + 8) / 8) & 0x1F);
    p->fetch_addr = (u16)(map + x + (y / 8) * 32);
    /* Estados 1 a 5 */
    p->fetch_tile = vram(g, p->fetch_addr);
    p->fetch_attr = g->cgb ? vram(g, (u16)(p->fetch_addr + 0x2000)) : 0;
    p->fetch_addr = tile_row_addr(g);
    p->fetch_data[0] = vram(g, p->fetch_addr);
    p->fetch_addr = (u16)(tile_row_addr(g) + 1);
    p->fetch_data[1] = vram(g, p->fetch_addr);
    if (p->wx_triggered) p->window_tile_x = (p->window_tile_x + 1) & 0x1F;
    /* Punto 8: la FIFO quedó vacía y se empuja */
    bg_push_row(p, p->fetch_data[0], p->fetch_data[1], p->fetch_attr);
    p->fetch_state = 0;
}

/*
 * Camino rápido del modo 3: mientras el dibujo va "en régimen" (ya pasó el
 * descarte inicial, no hay sprite por leer cerca, la ventana no puede
 * activarse y no hay nada pendiente), cada punto es solo sacar un píxel y
 * avanzar el fetcher. Hace exactamente lo mismo que ppu_dot en esos puntos
 * (mismas render_pixel y fetcher_step) sin pasar por el despacho general, y
 * le devuelve el control en cuanto algo de eso deja de cumplirse. Devuelve
 * cuántos puntos hizo (hasta `n`).
 */
static u32 mode3_fast(GB *g, u32 n)
{
    Ppu *p = &g->ppu;
    u32 done = 0;
    while (done < n) {
        u8 lcdc = LCDC(g), wx = g->io[IO_WX];
        /* visible_line_dot no hace nada más en estos puntos; la línea no termina */
        if (!p->rendering || p->dot < 90 || p->dot + 1 >= p->line_len) break;
        if (p->obj_state != OBJ_NONE || p->resume || p->insert_bg_pixel || p->wy_pending) break;
        if (p->pos >= 160) break;                           /* descarte inicial (posiciones negativas) */
        /* window_check no puede activar nada y no se mete un píxel de la ventana */
        if (!p->wx_triggered && p->wy_triggered && (lcdc & LCDC_WINON)) break;
        if (p->wx_triggered && wx == POS(p->pos + 7)) break;
        if (!g->cgb_hw && p->wy_triggered && !(lcdc & LCDC_WINON) && !p->no_win_glitch) break;
        /* Ningún sprite por descartar ni por leer en este punto */
        if (p->n_objs && p->objs_x[p->n_objs - 1] <= POS(p->pos + 8)) break;

        /* Un mosaico entero si en los 8 puntos no hay sprite, ventana ni fin
         * de línea (pos + 7 < 159: el último píxel no es el 159) */
        if (p->fetch_state == 0 && p->bg_size == 8 && p->obj_size == 0 && n - done >= 8 &&
            p->dot + 8 < p->line_len && p->pos <= 151 &&
            (!p->n_objs || p->objs_x[p->n_objs - 1] > p->pos + 15) &&
            (!p->wx_triggered || (u8)(wx - p->pos - 7) >= 8)) {
            mode3_tile(g);
            p->wx_just_changed = 0;
            p->wy_mod = (u8)(p->wy_mod + 8);
            p->dot += 8;
            p->cyc += 8;
            done += 8;
            continue;
        }

        render_pixel(g);
        fetcher_step(g);
        if (p->pos == 160) end_rendering(g);
        p->wx_just_changed = 0;
        p->wy_mod++;
        p->dot++;
        p->cyc++;
        done++;
    }
    return done;
}

/* Camino rápido de la búsqueda en OAM (puntos 6 a 83 de una línea visible):
 * lo mismo que ppu_dot en esos puntos (una entrada cada 2 puntos y, en el
 * DMG, los bloqueos del punto 80) sin el despacho general. Devuelve cuántos
 * puntos hizo (hasta `n`). */
static u32 mode2_fast(GB *g, u32 n)
{
    Ppu *p = &g->ppu;
    u32 done = 0;
    while (done < n) {
        u32 d = p->dot;
        if (d < 6 || d >= 84 || p->rendering || p->lcd_first_line || p->ly >= 144) break;
        if (p->wy_pending || p->hblank_irq_dot) break;
        if (!(d & 1)) add_object(g, (int)(d - 6) / 2);
        if (d == 80 && !g->cgb_hw) {
            p->vram_rb = 1;
            p->vram_wb = 0;
            p->oam_wb = 0;
        }
        p->wx_just_changed = 0;
        p->wy_mod++;
        p->dot++;
        p->cyc++;
        done++;
    }
    return done;
}

void ppu_advance(GB *g, u64 until)
{
    Ppu *p = &g->ppu;
    if (!(LCDC(g) & LCDC_ON)) {
        if (until > p->cyc) p->cyc = until;
        return;
    }
    while (p->cyc < until) {
        u32 n = idle_dots(p);
        if (n > 1) {
            u64 k = until - p->cyc < n ? until - p->cyc : n;
            p->dot += (u32)k;
            p->wy_mod = (u8)(p->wy_mod + k);
            p->cyc += k;
            continue;
        }
        if (p->rendering) {
            u64 left = until - p->cyc;
            if (mode3_fast(g, left > 456 ? 456 : (u32)left)) continue;
        } else if (p->dot >= 6 && p->dot < 84) {
            u64 left = until - p->cyc;
            if (mode2_fast(g, left > 456 ? 456 : (u32)left)) continue;
        }
        ppu_dot(g);
        p->cyc++;
    }
}

u64 ppu_next_event(GB *g)
{
    Ppu *p = &g->ppu;
    if (!(LCDC(g) & LCDC_ON)) return UINT64_MAX;
    /* Los primeros puntos de cada línea tienen eventos casi seguidos */
    if (p->dot < 13 || p->lcd_first_line) return p->cyc + 1;
    if (p->ly < 144 && (g->io[IO_STAT] & 0x08)) {
        /* Interrupción de HBlank: el modo 3 no puede terminar antes de que
         * salgan los píxeles que faltan */
        if (p->rendering) return p->cyc + (u64)(GB_SCREEN_W - p->lcd_x) + 1;
        /* El modo 3 empieza a dibujar al procesar el punto 89 (p->dot es el
         * siguiente punto): hay que volver a mirar después de ese punto. Si
         * se mira con p->dot == 89, todavía no está dibujando y caería al fin
         * de la línea, y la interrupción de HBlank llegaría tarde (Warlocked
         * se quedaba en blanco al correr cuadros enteros, #24) */
        if (p->dot <= 89) return p->cyc + (89 - p->dot) + 1;
        if (p->hblank_irq_dot) return p->cyc + 1;
    }
    return p->cyc + (p->line_len - p->dot);
}

int ppu_vram_blocked(GB *g, int write)
{
    if (!(LCDC(g) & LCDC_ON)) return 0;
    return write ? g->ppu.vram_wb : g->ppu.vram_rb;
}

int ppu_oam_blocked(GB *g, int write)
{
    if (!(LCDC(g) & LCDC_ON)) return 0;
    return write ? g->ppu.oam_wb : g->ppu.oam_rb;
}

u8 ppu_read(GB *g, u8 reg)
{
    Ppu *p = &g->ppu;
    switch (reg) {
    case IO_STAT: {
        u8 v = (u8)(0x80 | (g->io[IO_STAT] & 0x7C));
        if (LCDC(g) & LCDC_ON) v |= p->mode;
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
            /* Apagado: LY y el modo vuelven a 0 y la pantalla queda en blanco */
            p->ly = 0;
            p->ly_reg = 0;
            p->dot = 0;
            p->mode = 0;
            p->ly_cmp = 0;
            p->wy_triggered = 0;
            p->rendering = 0;
            for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++)
                p->back[i] = g->cgb_hw ? 0x7FFF : 0;   /* pantalla en blanco */
            p->oam_rb = p->oam_wb = p->vram_rb = p->vram_wb = 0;
            finish_frame(g);
        } else if (!(old & LCDC_ON) && (v & LCDC_ON)) {
            p->ly = 0;
            p->ly_reg = 0;
            p->dot = 0;
            p->line_len = 449;
            p->wy_mod = 0;
            p->lcd_first_line = 1;
            p->rendering = 0;
            p->hblank_irq_dot = 0;
            stat_update(g);
        }
        return;
    }
    case IO_STAT:
        g->io[IO_STAT] = (u8)((g->io[IO_STAT] & 0x87) | (v & 0x78));
        stat_update(g);
        return;
    case IO_LYC:
        g->io[IO_LYC] = v;
        stat_update(g);
        return;
    case IO_WY:
        g->io[IO_WY] = v;
        p->wy_pending = 1;
        return;
    case IO_LY:
        return;     /* solo lectura */
    default:
        g->io[reg] = v;
        return;
    }
}

/*
 * Escritura del CPU a un registro de la PPU, en el M-ciclo que empieza en s.
 * En el DMG cada registro "entra" en un punto distinto respecto a s (medido
 * con Mealybug; mismos tiempos que SameBoy):
 *   BGP, OBP0, OBP1  s-2: se ve (viejo | nuevo); s-1: el nuevo
 *   LCDC             s-2: viejo con el bit de fondo nuevo; s-1: el nuevo
 *   SCX, SCY         s-2 (SCY: un punto antes que en SameBoy; calibrado con
 *                    m3_scy_change)
 *   STAT             s: todas las fuentes habilitadas (falla de escritura
 *                    del DMG, puede pedir la interrupción); s+1: el valor
 *   WX               s, y durante ese punto cuenta como "recién cambiado"
 *   resto            s
 * La PPU llega aquí PPU_HOLD puntos atrás (ver gb.c).
 */
void ppu_write_timed(GB *g, u8 reg, u8 v, u64 s)
{
    Ppu *p = &g->ppu;
    if (g->cgb_hw) {
        /* CGB (D/E) a velocidad normal: paletas DMG en s-2; LYC y WX en
         * s+1; STAT conserva un punto el bit de LYC viejo; el resto en s */
        switch (reg) {
        case IO_BGP: case IO_OBP0: case IO_OBP1:
            ppu_advance(g, s - 2);
            break;
        case IO_LYC: case IO_WX:
            ppu_advance(g, s + 1);
            break;
        case IO_STAT:
            ppu_advance(g, s);
            ppu_write(g, IO_STAT, (u8)((g->io[IO_STAT] & 0x40) | (v & ~0x40)));
            ppu_advance(g, s + 1);
            break;
        default:
            ppu_advance(g, s);
            break;
        }
        ppu_write(g, reg, v);
        if (reg == IO_LCDC || reg == IO_WY) p->wy_pending = 1;
        return;
    }
    switch (reg) {
    case IO_BGP: case IO_OBP0: case IO_OBP1:
        ppu_advance(g, s - 2);
        ppu_write(g, reg, (u8)(g->io[reg] | v));
        ppu_advance(g, s - 1);
        ppu_write(g, reg, v);
        break;
    case IO_LCDC: {
        ppu_advance(g, s - 2);
        u8 old = LCDC(g);
        /* Apagar los sprites corta de inmediato una lectura de sprite en
         * curso o el primer píxel de la línea */
        if (!(v & LCDC_OBJON) && p->rendering &&
            (p->pos == 0 || (p->obj_state != OBJ_NONE && p->obj_state != OBJ_D)))
            old &= (u8)~LCDC_OBJON;
        {
            /* Los bits que lee el fetcher (mapas y datos de mosaicos) ya cuentan
             * desde aquí; calibrado con m3_lcdc_bg_map_change */
            const u8 fetch_bits = LCDC_BG9C00 | LCDC_TILE8000 | LCDC_WIN9C00;
            u8 first = (u8)((old & ~fetch_bits) | (v & fetch_bits));
            ppu_write(g, IO_LCDC, first);
        }
        ppu_advance(g, s - 1);
        ppu_write(g, IO_LCDC, v);
        p->wy_pending = 1;
        if ((old & LCDC_WINON) && !(v & LCDC_WINON) && p->window_fetching) p->no_win_glitch = 1;
        break;
    }
    case IO_SCX:
        ppu_advance(g, s - 2);
        ppu_write(g, reg, v);
        break;
    case IO_SCY:
        ppu_advance(g, s - 2);
        ppu_write(g, reg, v);
        break;
    case IO_STAT:
        ppu_advance(g, s);
        ppu_write(g, IO_STAT, 0xFF);
        ppu_advance(g, s + 1);
        ppu_write(g, IO_STAT, v);
        break;
    case IO_WX:
        ppu_advance(g, s);
        ppu_write(g, reg, v);
        p->wx_just_changed = 1;
        break;
    default:
        ppu_advance(g, s);
        ppu_write(g, reg, v);
        break;
    }
}
