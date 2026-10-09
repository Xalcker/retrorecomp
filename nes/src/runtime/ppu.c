/*
 * PPU 2C02. Un punto de reloj por llamada a ppu_tick, con la tubería real de
 * búsqueda del fondo (registros de desplazamiento y direcciones "loopy").
 * Los sprites de cada línea se evalúan de una vez en el punto 257.
 * El CPU y la PPU se sincronizan por instrucción (nes_step).
 */
#include <string.h>
#include "nes.h"

static const u32 pal_default[64] = {
    0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
    0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
    0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
    0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
    0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
    0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
    0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
    0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000,
};

void ppu_make_palette(int kind, u32 out[64])
{
    for (int i = 0; i < 64; i++) {
        u32 c = pal_default[i];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        if (kind == 1) {                         /* grises: luminancia */
            int y = (r * 30 + g * 59 + b * 11) / 100;
            r = g = b = y;
        } else if (kind == 2) {                  /* saturada: se aleja del gris un 30% */
            int y = (r * 30 + g * 59 + b * 11) / 100;
            r = y + (r - y) * 13 / 10; g = y + (g - y) * 13 / 10; b = y + (b - y) * 13 / 10;
            r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        }
        out[i] = 0xFF000000u | (u32)r << 16 | (u32)g << 8 | (u32)b;
    }
}

int ppu_load_palette(const u8 *data, size_t len, u32 out[64])
{
    if (len < 192) return 0;
    for (int i = 0; i < 64; i++)
        out[i] = 0xFF000000u | (u32)data[i * 3] << 16 | (u32)data[i * 3 + 1] << 8 | data[i * 3 + 2];
    return 1;
}

/* Énfasis de color: cada bit baja un 25% los otros dos canales */
u32 ppu_argb(const u32 pal[64], u16 px)
{
    u32 c = pal[px & 0x3F];
    int e = (px >> 6) & 7;
    if (!e) return c;
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (e & 1) { g = g * 3 / 4; b = b * 3 / 4; }     /* énfasis rojo */
    if (e & 2) { r = r * 3 / 4; b = b * 3 / 4; }     /* verde */
    if (e & 4) { r = r * 3 / 4; g = g * 3 / 4; }     /* azul */
    return 0xFF000000u | (u32)r << 16 | (u32)g << 8 | (u32)b;
}

static void ppu_tile(NES *n);
static void ppu_prefetch_tile(NES *n);
static void ppu_tile_bg(NES *n);

/* Próximo punto en que algo visible desde fuera de la PPU puede pasar: la bandera de vblank sube
 * (241, 1), baja (261, 1) o el cuadro termina (261, 339 y 340: el cuadro impar pierde un punto). */
static void set_event(Ppu *p)
{
    static const int ev[4] = { 241 * 341 + 1, 261 * 341 + 1, 261 * 341 + 339, 261 * 341 + 340 };
    int pos = p->scanline * 341 + p->dot, best = 1 << 30;
    for (int i = 0; i < 4; i++) {
        int d = ev[i] - pos;
        if (d < 0) d += 262 * 341;
        if (d < best) best = d;
    }
    p->event_dot = p->clock + (u64)best + 1;
}

/* Avanza hasta clock == target. Fuera del MMC3 (sin eventos de A12) y con el dibujo activo, las líneas
 * visibles se hacen por baldosas de 8 puntos, se salta la búsqueda de sprites (258-320, donde no pasa
 * nada) y se avanzan de golpe las líneas de vblank. Cada atajo da exactamente el mismo estado que
 * ejecutar los puntos uno a uno. */
void ppu_sync(NES *n, u64 target)
{
    Ppu *p = &n->ppu;
    if (p->clock >= target) return;
    const int fast = n->cart.mapper != 4;
    do {
        u64 left = target - p->clock;
        int sl = p->scanline, d = p->dot;
        if (fast) {
            if (sl < 240) {
                if (p->mask & 0x18) {
                    if (d >= 1 && d <= 249 && ((d - 1) & 7) == 0 && left >= 8) {
                        if (p->spr_count == 0 && (p->mask & 8) && (d > 8 || (p->mask & 2))) ppu_tile_bg(n);
                        else ppu_tile(n);
                        continue;
                    }
                    if (d == 258 && left >= 63) { p->dot = 321; p->clock += 63; continue; }
                    if ((d == 321 || d == 329) && left >= 8) { ppu_prefetch_tile(n); continue; }
                }
            } else if (sl < 261) {
                /* línea 240 y vblank: lo único que pasa es la bandera en (241, 1) */
                int pos = sl * 341 + d, ev = 241 * 341 + 1, end = 261 * 341;
                int room = pos < ev ? ev - pos : pos == ev ? 0 : end - pos;
                if (room > 0) {
                    u64 k = left < (u64)room ? left : (u64)room;
                    int np = pos + (int)k;
                    p->scanline = np / 341; p->dot = np % 341;
                    p->clock += k;
                    continue;
                }
            }
        }
        ppu_tick(n);
    } while (p->clock < target);
    set_event(p);
}

void ppu_flush(NES *n)
{
    ppu_sync(n, 3 * n->cyc);
}

void ppu_reset(NES *n)
{
    Ppu *p = &n->ppu;
    memset(p, 0, sizeof *p);
    p->scanline = 0; p->dot = 0;
}

static int nt_index(NES *n, u16 a)
{
    int t = (a >> 10) & 3;
    switch (n->cart.mirroring) {
    case MIRROR_HORIZONTAL: return t >> 1;
    case MIRROR_VERTICAL:   return t & 1;
    case MIRROR_SINGLE0:    return 0;
    case MIRROR_SINGLE1:    return 1;
    default:                return t;
    }
}

static inline int pal_idx(u16 a)
{
    int i = a & 0x1F;
    return (i & 0x13) == 0x10 ? i & 0x0F : i;
}

static u8 vrd(NES *n, u16 a)
{
    a &= 0x3FFF;
    if (a < 0x2000) return n->cart.chr_page[a >> 10][a & 0x3FF];
    if (a < 0x3F00) return n->ppu.vram[nt_index(n, a) * 0x400 + (a & 0x3FF)];
    return n->ppu.pal[pal_idx(a)];
}

static void vwr(NES *n, u16 a, u8 v)
{
    a &= 0x3FFF;
    if (a < 0x2000) mapper_ppu_write(n, a, v);
    else if (a < 0x3F00) n->ppu.vram[nt_index(n, a) * 0x400 + (a & 0x3FF)] = v;
    else n->ppu.pal[pal_idx(a)] = v & 0x3F;
}

static inline void a12(NES *n, int bit) { if (n->cart.mapper == 4) mapper_a12(n, bit); }

/* ---- Registros ---- */

u8 ppu_reg_read(NES *n, u16 addr)
{
    Ppu *p = &n->ppu;
    u8 r = p->latch;
    switch (addr & 7) {
    case 2:
        r = (p->status & 0xE0) | (p->latch & 0x1F);
        p->status &= ~0x80;
        p->w = 0;
        if (p->scanline == 241 && p->dot == 1) p->suppress_vbl = 1;   /* carrera con el vblank */
        break;
    case 4:
        r = p->oam[p->oam_addr];
        if ((p->oam_addr & 3) == 2) r &= 0xE3;
        break;
    case 7:
        if ((p->v & 0x3FFF) >= 0x3F00) {
            r = vrd(n, p->v);
            p->read_buf = vrd(n, p->v - 0x1000);
        } else {
            r = p->read_buf;
            p->read_buf = vrd(n, p->v);
        }
        p->v += (p->ctrl & 4) ? 32 : 1;
        a12(n, (p->v >> 12) & 1);
        break;
    default: return r;
    }
    p->latch = r;
    return r;
}

void ppu_reg_write(NES *n, u16 addr, u8 v)
{
    Ppu *p = &n->ppu;
    p->latch = v;
    switch (addr & 7) {
    case 0:
        p->ctrl = v;
        p->t = (p->t & 0xF3FF) | ((v & 3) << 10);
        break;
    case 1: p->mask = v; break;
    case 3: p->oam_addr = v; break;
    case 4: p->oam[p->oam_addr++] = v; break;
    case 5:
        if (!p->w) { p->fine_x = v & 7; p->t = (p->t & 0xFFE0) | (v >> 3); }
        else       { p->t = (p->t & 0x8C1F) | ((v & 7) << 12) | ((v & 0xF8) << 2); }
        p->w ^= 1;
        break;
    case 6:
        if (!p->w) { p->t = (p->t & 0x00FF) | ((v & 0x3F) << 8); }
        else       { p->t = (p->t & 0xFF00) | v; p->v = p->t; a12(n, (p->v >> 12) & 1); }
        p->w ^= 1;
        break;
    case 7:
        vwr(n, p->v, v);
        p->v += (p->ctrl & 4) ? 32 : 1;
        a12(n, (p->v >> 12) & 1);
        break;
    }
}

/* ---- Dibujo ---- */

static void inc_x(Ppu *p)
{
    if ((p->v & 0x1F) == 31) { p->v &= ~0x1F; p->v ^= 0x400; }
    else p->v++;
}

static void inc_y(Ppu *p)
{
    if ((p->v & 0x7000) != 0x7000) { p->v += 0x1000; return; }
    p->v &= ~0x7000;
    int y = (p->v & 0x3E0) >> 5;
    if (y == 29) { y = 0; p->v ^= 0x800; }
    else if (y == 31) y = 0;
    else y++;
    p->v = (p->v & ~0x3E0) | (y << 5);
}

static void load_bg(Ppu *p)
{
    p->bg_lo = (p->bg_lo & 0xFF00) | p->nt_lo;
    p->bg_hi = (p->bg_hi & 0xFF00) | p->nt_hi;
    p->at_lo = (p->at_lo & 0xFF00) | ((p->nt_attr & 1) ? 0xFF : 0);
    p->at_hi = (p->at_hi & 0xFF00) | ((p->nt_attr & 2) ? 0xFF : 0);
}

static u8 rev8(u8 b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

/* Evalúa y carga los sprites de la línea siguiente */
static void eval_sprites(NES *n, int line)
{
    Ppu *p = &n->ppu;
    int h = (p->ctrl & 0x20) ? 16 : 8, cnt = 0;
    p->spr0_possible = 0;
    for (int i = 0; i < 8; i++) p->spr_a12[i] = (h == 8) ? (p->ctrl >> 3) & 1 : 1;
    for (int i = 0; i < 64; i++) {
        int diff = line - p->oam[i * 4];
        if (diff < 0 || diff >= h) continue;
        if (cnt == 8) { p->status |= 0x20; break; }
        u8 tile = p->oam[i * 4 + 1], attr = p->oam[i * 4 + 2];
        int row = (attr & 0x80) ? h - 1 - diff : diff;
        u16 addr;
        if (h == 8) addr = ((p->ctrl & 8) << 9) | (tile << 4) | row;
        else {
            addr = ((tile & 1) << 12) | ((tile & 0xFE) << 4) | ((row & 8) << 1) | (row & 7);
        }
        u8 lo = vrd(n, addr), hi = vrd(n, addr + 8);
        if (attr & 0x40) { lo = rev8(lo); hi = rev8(hi); }
        p->spr[cnt].x = p->oam[i * 4 + 3];
        p->spr[cnt].attr = attr;
        p->spr[cnt].lo = lo; p->spr[cnt].hi = hi;
        if (h == 16) p->spr_a12[cnt] = tile & 1;
        if (i == 0) p->spr0_possible = 1;
        cnt++;
    }
    p->spr_count = cnt;
}

static inline void draw_pixel(NES *n)
{
    Ppu *p = &n->ppu;
    const u8 mask = p->mask;
    const int x = p->dot - 1;
    int bg = 0, bgpal = 0, fg = 0, fgpal = 0, fgpri = 0, zero = 0;

    if ((mask & 8) && (x >= 8 || (mask & 2))) {
        const u16 m = 0x8000 >> p->fine_x;
        bg = ((p->bg_hi & m) ? 2 : 0) | ((p->bg_lo & m) ? 1 : 0);
        bgpal = ((p->at_hi & m) ? 2 : 0) | ((p->at_lo & m) ? 1 : 0);
    }
    if (p->spr_count && (mask & 0x10) && (x >= 8 || (mask & 4))) {
        for (int i = 0; i < p->spr_count; i++) {
            if (p->spr[i].x) continue;
            int px = ((p->spr[i].hi & 0x80) ? 2 : 0) | ((p->spr[i].lo & 0x80) ? 1 : 0);
            if (!px) continue;
            fg = px; fgpal = (p->spr[i].attr & 3) + 4; fgpri = !(p->spr[i].attr & 0x20);
            zero = (i == 0 && p->spr0_possible);
            break;
        }
    }
    int c;
    if (!fg) {
        c = bg ? p->pal[(bgpal << 2) | bg] : p->pal[0];
    } else if (!bg) {
        c = p->pal[(fgpal << 2) | fg];
    } else {
        if (fgpri) c = p->pal[(fgpal << 2) | fg]; else c = p->pal[(bgpal << 2) | bg];
        if (zero && (mask & 0x18) == 0x18 && x < 255 && !(p->status & 0x40)) p->status |= 0x40;
    }
    if (mask & 1) c &= 0x30;
    p->fb[p->scanline * 256 + x] = (u16)((c & 0x3F) | ((mask & 0xE0) << 1));
}

/* Ocho puntos de la búsqueda previa (321-328 y 329-336): desplaza el fondo y busca el siguiente mosaico */
static void ppu_prefetch_tile(NES *n)
{
    Ppu *p = &n->ppu;
    const u8 mask = p->mask;
    for (int k = 0; k < 8; k++) {
        if (mask & 8) { p->bg_lo <<= 1; p->bg_hi <<= 1; p->at_lo <<= 1; p->at_hi <<= 1; }
        switch (k) {
        case 0: load_bg(p); p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF)); break;
        case 2: {
            u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
            if (p->v & 0x40) a >>= 4;
            if (p->v & 2) a >>= 2;
            p->nt_attr = a & 3; break; }
        case 4: p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7)); break;
        case 6: p->nt_hi = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8); break;
        case 7: inc_x(p); break;
        }
    }
    p->dot += 8;
    p->clock += 8;
}

/* Baldosa de 8 puntos en una línea sin sprites y con el fondo activo (y sin recorte a la izquierda en
 * estas columnas). Mismo resultado que ppu_tile, pero sin desplazar los registros punto a punto: tras
 * cargar el mosaico en el primer punto, el pixel k sale del bit (15 - fine_x - k) de cada registro, y al final
 * los registros quedan desplazados 7 lugares más. */
static void ppu_tile_bg(NES *n)
{
    Ppu *p = &n->ppu;
    const int c = p->dot, y = p->scanline;
    const u8 mask = p->mask;
    u32 lo = p->bg_lo, hi = p->bg_hi, alo = p->at_lo, ahi = p->at_hi;
    if (c >= 2) { lo = (lo << 1) & 0xFFFF; hi = (hi << 1) & 0xFFFF; alo = (alo << 1) & 0xFFFF; ahi = (ahi << 1) & 0xFFFF; }
    /* punto 0: carga el mosaico que se buscó en la baldosa anterior y empieza la siguiente búsqueda */
    lo = (lo & 0xFF00) | p->nt_lo;
    hi = (hi & 0xFF00) | p->nt_hi;
    alo = (alo & 0xFF00) | ((p->nt_attr & 1) ? 0xFF : 0);
    ahi = (ahi & 0xFF00) | ((p->nt_attr & 2) ? 0xFF : 0);
    p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF));
    {   /* punto 2: atributo */
        u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
        if (p->v & 0x40) a >>= 4;
        if (p->v & 2) a >>= 2;
        p->nt_attr = a & 3;
    }
    p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7));
    p->nt_hi = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8);
    const int top = 15 - p->fine_x;
    const u16 emph = (u16)((mask & 0xE0) << 1);
    u16 *out = p->fb + y * 256 + (c - 1);
    for (int k = 0; k < 8; k++) {
        const int m = top - k;
        int bg = (int)(((hi >> m) & 1) << 1 | ((lo >> m) & 1));
        int col = bg ? p->pal[((((ahi >> m) & 1) << 1 | ((alo >> m) & 1)) << 2) | bg] : p->pal[0];
        if (mask & 1) col &= 0x30;
        out[k] = (u16)((col & 0x3F) | emph);
    }
    inc_x(p);                                      /* punto 7 */
    if (c == 249) inc_y(p);                        /* punto 256 */
    p->bg_lo = (u16)(lo << 7); p->bg_hi = (u16)(hi << 7);
    p->at_lo = (u16)(alo << 7); p->at_hi = (u16)(ahi << 7);
    p->dot = c + 8;
    p->clock += 8;
}

/* Ocho puntos seguidos de una línea visible con el dibujo activo, empezando en el punto 1, 9, ... 249:
 * hace lo mismo que ocho llamadas a ppu_tick (mismo orden de operaciones), sin revisar en cada punto en
 * qué parte de la línea está. Solo para mappers distintos del MMC3 (sin eventos de A12). */
static void ppu_tile(NES *n)
{
    Ppu *p = &n->ppu;
    const int sl = p->scanline, c = p->dot;
    const u8 mask = p->mask;
    for (int k = 0; k < 8; k++) {
        const int cy = c + k;
        if (cy >= 2) {
            if (mask & 8) { p->bg_lo <<= 1; p->bg_hi <<= 1; p->at_lo <<= 1; p->at_hi <<= 1; }
            if (mask & 0x10)
                for (int i = 0; i < p->spr_count; i++) {
                    if (p->spr[i].x) p->spr[i].x--;
                    else { p->spr[i].lo <<= 1; p->spr[i].hi <<= 1; }
                }
        }
        switch (k) {
        case 0: load_bg(p); p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF)); break;
        case 2: {
            u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
            if (p->v & 0x40) a >>= 4;
            if (p->v & 2) a >>= 2;
            p->nt_attr = a & 3; break; }
        case 4: p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7)); break;
        case 6: p->nt_hi = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8); break;
        case 7: inc_x(p); break;
        }
        if (cy == 256) inc_y(p);
        p->dot = cy;                       /* draw_pixel lee la columna de p->dot */
        draw_pixel(n);
    }
    (void)sl;
    p->dot = c + 8;
    p->clock += 8;
}

void ppu_tick(NES *n)
{
    Ppu *p = &n->ppu;
    int sl = p->scanline, cy = p->dot;
    int rend = p->mask & 0x18;
    p->clock++;

    if (sl < 240 || sl == 261) {
        int pre = (sl == 261);
        if (pre && cy == 1) {
            p->status &= ~0xE0;
            p->spr_count = 0; p->spr0_possible = 0;
        }
        if (rend) {
            if ((cy >= 2 && cy < 258) || (cy >= 321 && cy < 338)) {
                if (p->mask & 8) { p->bg_lo <<= 1; p->bg_hi <<= 1; p->at_lo <<= 1; p->at_hi <<= 1; }
                if ((p->mask & 0x10) && cy < 258)
                    for (int i = 0; i < p->spr_count; i++) {
                        if (p->spr[i].x) p->spr[i].x--;
                        else { p->spr[i].lo <<= 1; p->spr[i].hi <<= 1; }
                    }
                switch ((cy - 1) & 7) {
                case 0: a12(n, 0); load_bg(p); p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF)); break;
                case 2: {
                    a12(n, 0);
                    u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
                    if (p->v & 0x40) a >>= 4;
                    if (p->v & 2) a >>= 2;
                    p->nt_attr = a & 3; break; }
                case 4: a12(n, (p->ctrl >> 4) & 1); p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7)); break;
                case 6: a12(n, (p->ctrl >> 4) & 1); p->nt_hi = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8); break;
                case 7: inc_x(p); break;
                }
            }
            if (cy >= 257 && cy <= 320) {            /* búsqueda de sprites (solo importa A12) */
                int k = (cy - 257) & 7;
                if (cy == 257 && pre) { int h = (p->ctrl & 0x20) ? 16 : 8;
                    for (int i = 0; i < 8; i++) p->spr_a12[i] = (h == 8) ? (p->ctrl >> 3) & 1 : 1; }
                if (k == 0 || k == 2) a12(n, 0);
                else if (k == 4 || k == 6) a12(n, p->spr_a12[(cy - 257) >> 3]);
            }
            if (cy == 256) inc_y(p);
            if (cy == 257) {
                load_bg(p);
                p->v = (p->v & ~0x041F) | (p->t & 0x041F);
                if (!pre) eval_sprites(n, sl);
                else { p->spr_count = 0; p->spr0_possible = 0; }
            }
            if (cy == 338 || cy == 340) p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF));
            if (pre && cy >= 280 && cy < 305) p->v = (p->v & ~0x7BE0) | (p->t & 0x7BE0);
        }
        if (!pre && cy >= 1 && cy <= 256) {
            if (rend) draw_pixel(n);
            else p->fb[sl * 256 + cy - 1] = (u16)((p->pal[0] & 0x3F) | ((p->mask & 0xE0) << 1));
        }
    }

    if (sl == 241 && cy == 1) {
        if (!p->suppress_vbl) p->status |= 0x80;
        p->suppress_vbl = 0;
    }

    /* Cuadro impar con el fondo activo en el punto 339: la pre-render pierde el punto 340 */
    if (sl == 261 && cy == 339 && p->odd && (p->mask & 8)) p->dot = 340;
    if (++p->dot > 340) {
        p->dot = 0;
        if (++p->scanline > 261) {
            p->scanline = 0;
            p->frame++; p->frame_done = 1;
            p->odd ^= 1;
        }
    }
}
