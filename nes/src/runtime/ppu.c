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
static void set_event(NES *n)
{
    Ppu *p = &n->ppu;
    static const int ev[4] = { 241 * 341 + 1, 261 * 341 + 1, 261 * 341 + 339, 261 * 341 + 340 };
    int pos = p->scanline * 341 + p->dot, best = 1 << 30;
    for (int i = 0; i < 4; i++) {
        int d = ev[i] - pos;
        if (d < 0) d += 262 * 341;
        if (d < best) best = d;
    }
    /* MMC3: su IRQ se levanta con las lecturas de la PPU a partir del punto 256 de las líneas con dibujo (y en
     * toda la pre-render). Ahí la consola tiene que ir al día en cada ciclo; antes no hace falta. */
    if (n->cart.mmc3 && (p->mask & 0x18)) {
        int sl = p->scanline;
        if (sl == 261 || (sl < 240 && p->dot >= 256)) { p->event_dot = 0; return; }
        int w = sl < 239 ? (p->dot < 256 ? sl * 341 + 256 : (sl + 1) * 341 + 256) : sl < 240 ? 261 * 341 : 261 * 341;
        if (sl < 240 && p->dot < 256) w = sl * 341 + 256;
        else if (sl < 239) w = (sl + 1) * 341 + 256;
        else w = 261 * 341;
        int d = w - pos;
        if (d < best) best = d;
    }
    if (n->cart.mmc5 && (p->mask & 0x18)) {         /* MMC5: la línea empieza ahí (cuenta de scanlines) */
        int sl = p->scanline;
        if (sl <= 240 && p->dot <= 8) { p->event_dot = 0; return; }
        if (sl < 240) { int d = (sl + 1) * 341 - pos; if (d < best) best = d; }
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
    const int mmc3 = n->cart.mmc3;
    do {
        u64 left = target - p->clock;
        int sl = p->scanline, d = p->dot;
        {
            if (sl < 240) {
                if (p->mask & 0x18) {
                    /* con el MMC3, una subida de A12 que cuenta no puede quedar dentro de una baldosa: solo si A12
                     * ya está alto o el fondo usa la tabla $0000 (no hay subida) */
                    if (d >= 1 && d <= 249 && ((d - 1) & 7) == 0 && left >= 8 &&
                        (!p->ovf_dot || p->ovf_dot < d || p->ovf_dot >= d + 8) && !n->cart.exgfx &&
                        (!mmc3 || n->cart.reg[13] || !(p->ctrl & 0x10)) && (!n->cart.mmc5 || d > 8)) {
                        if (p->spr_count == 0 && (p->mask & 8) && (d > 8 || (p->mask & 2))) ppu_tile_bg(n);
                        else ppu_tile(n);
                        continue;
                    }
                    if (!mmc3 && d == 258 && left >= 63) { p->dot = 321; p->clock += 63; continue; }
                    if (!mmc3 && !n->cart.exgfx && (d == 321 || d == 329) && left >= 8) { ppu_prefetch_tile(n); continue; }
                }
            } else if (sl < 261) {
                /* línea 240 y vblank: lo único que pasa es la bandera en (241, 1) */
                int pos = sl * 341 + d, ev = 241 * 341 + 1, end = 261 * 341;
                int room = pos < ev ? ev - pos : pos == ev ? 0 : end - pos;
                if (n->cart.mmc5 && pos == 240 * 341) room = 0;          /* el MMC5 mira el punto 0 de la línea 240 */
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
    set_event(n);
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
    case MIRROR_CUSTOM:     return n->cart.nt_map[t];
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
    if (a < 0x2000) {
        u8 v = n->cart.chr_page[a >> 10][a & 0x3FF];
        if (__builtin_expect(n->cart.chr_hook, 0)) mapper_chr_latch(&n->cart, a);   /* MMC2/MMC4: cambia el banco después de leer */
        return v;
    }
    if (a < 0x3F00) {
        int pg = nt_index(n, a);
        if (__builtin_expect(n->cart.mirroring == MIRROR_CUSTOM && n->cart.mmc5, 0)) {     /* MMC5: ExRAM o relleno */
            if (pg == 2) return n->cart.exram[a & 0x3FF];
            if (pg == 3) return (a & 0x3FF) < 0x3C0 ? n->cart.m5[0x06] : (u8)((n->cart.m5[0x07] & 3) * 0x55);
        }
        return n->ppu.vram[pg * 0x400 + (a & 0x3FF)];
    }
    return n->ppu.pal[pal_idx(a)];
}

/* Lectura de patrones de un sprite: con el MMC5 y sprites 8x16 usa su propio juego de bancos */
static u8 vrd_s(NES *n, u16 a)
{
    if (__builtin_expect(n->cart.mmc5, 0) && a < 0x2000) return n->cart.chr_page_spr[(a >> 10) & 7][a & 0x3FF];
    return vrd(n, a);
}

static void vwr(NES *n, u16 a, u8 v)
{
    a &= 0x3FFF;
    if (a < 0x2000) mapper_ppu_write(n, a, v);
    else if (a < 0x3F00) {
        int pg = nt_index(n, a);
        if (n->cart.mirroring == MIRROR_CUSTOM && n->cart.mmc5) {
            if (pg == 2) n->cart.exram[a & 0x3FF] = v;
            if (pg >= 2) return;
        }
        n->ppu.vram[pg * 0x400 + (a & 0x3FF)] = v;
    }
    else n->ppu.pal[pal_idx(a)] = v & 0x3F;
}

static inline void a12(NES *n, int bit) { if (n->cart.mmc3) mapper_a12(n, bit); }

static void inc_x(Ppu *p);
static void inc_y(Ppu *p);

/* Tras leer o escribir $2007: v sube 1 o 32; con el dibujo activo en una línea de dibujo, sube a la vez como
 * el incremento de X grueso y el de Y (la PPU comparte los circuitos) */
/* MMC5 ExGrafix: el patrón del fondo sale de un banco de CHR de 4 KB que elige el ExRAM de cada mosaico */
static u8 exgfx_read(NES *n, int plane)
{
    Ppu *p = &n->ppu;
    Cart *c = &n->cart;
    size_t bank = ((size_t)(p->ex_byte & 0x3F) | ((size_t)(c->m5[0x30] & 3) << 6)) << 12;
    bank = c->chr_mask ? (bank & c->chr_mask) : bank % c->chr_size;
    return c->chr[bank + ((size_t)p->nt_id << 4) + ((p->v >> 12) & 7) + (size_t)plane];
}

static void data_inc(Ppu *p)
{
    if ((p->mask & 0x18) && (p->scanline < 240 || p->scanline == 261)) { inc_x(p); inc_y(p); }
    else p->v += (p->ctrl & 4) ? 32 : 1;
}

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
            if (p->mask & 1) r &= 0x30;                    /* escala de grises: solo los dos bits altos del color */
            r = (r & 0x3F) | (p->latch & 0xC0);            /* la paleta tiene 6 bits: los otros dos son el bus abierto de la PPU */
            p->read_buf = vrd(n, p->v - 0x1000);
        } else {
            r = p->read_buf;
            p->read_buf = vrd(n, p->v);
        }
        data_inc(p);
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
        if (n->cart.mmc5) mapper_ppu_ctrl(n, v);
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
        data_inc(p);
        a12(n, (p->v >> 12) & 1);
        break;
    }
    if (n->cart.mmc3 || n->cart.mmc5) set_event(n);  /* $2001 puede encender el dibujo: cambia la ventana del IRQ */
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
    const int h = (p->ctrl & 0x20) ? 16 : 8;
    const u8 *oam = p->oam;
    u8 *sec = p->sec;
    memset(sec, 0xFF, 32);                      /* puntos 1-64: la OAM secundaria se llena de $FF */

    /* Puntos 65-256, tal como lo hace el hardware (con el defecto de la comprobación de desbordamiento): en los
     * puntos impares se lee un byte de la OAM; en los pares se copia a la secundaria o, si ya hay ocho
     * sprites, se busca el desbordamiento incrementando a la vez el número de sprite y el de byte. */
    /* La dirección de OAM es un contador de 8 bits. Con la OAM alineada todo esto equivale a "cuatro bytes por
     * sprite"; desalineada (OAMADDR no múltiplo de 4) se nota: el sprite se lee con la dirección sumando 1 por
     * byte (queda desalineado), un Y fuera de rango suma 4 y borra los dos bits bajos, y la comprobación de
     * rango se repite con el byte X (si no está en rango, suma 1 y borra los dos bits bajos). */
    int a = p->oam_snap, idx = 0, sec_addr = 0, done = 0, bug = 0, sprite0 = 0, ov_range = 0;
    u8 buf = 0xFF;
    #define IN_RANGE(v) (line >= (v) && line < (v) + h)
    for (int cyc = 65; cyc <= 256; cyc++) {
        if (cyc & 1) {                                  /* lectura de la OAM; el byte de atributos no guarda los bits 2-4 */
            buf = oam[a & 0xFF];
            if ((a & 3) == 2) buf &= 0xE3;
            continue;
        }
        if (done) { a = (a + 4) & 0xFF; if (sec_addr >= 0x20) buf = sec[sec_addr & 0x1F]; continue; }
        if (sec_addr < 0x20) {
            sec[sec_addr] = buf;
            if (idx == 0) {
                if (IN_RANGE(buf)) {
                    sec_addr++; a = (a + 1) & 0xFF; idx = 1;
                    if (cyc == 66) sprite0 = 1;         /* el primer sprite procesado hace de "sprite 0" */
                } else {
                    int na = (a & 0xFC) + 4;
                    if (na > 255) { done = 1; na &= 0xFF; }
                    a = na;
                }
            } else {
                sec_addr++;
                if (idx < 3) { a = (a + 1) & 0xFF; idx++; }
                else {                                  /* byte X: se vuelve a comprobar el rango */
                    int na = a + 1;
                    if (!IN_RANGE(buf)) na &= 0xFC;
                    if (na > 255) { done = 1; na &= 0xFF; }
                    a = na; idx = 0;
                }
            }
        } else {                                        /* ocho sprites ya: se busca el desbordamiento (con el defecto) */
            int H = a >> 2, L = a & 3;
            buf = sec[sec_addr & 0x1F];
            if (!ov_range && IN_RANGE(oam[a & 0xFF])) ov_range = 1;
            if (ov_range) {
                if (!p->ovf_dot) p->ovf_dot = (u16)cyc;       /* el flag sube en este punto de la línea */
                L++;
                if (L == 4) { H = (H + 1) & 0x3F; L = 0; }
                if (bug == 0) bug = 3;
                else if (--bug == 0) { done = 1; L = 0; }
            } else {                                    /* el defecto: suben a la vez H y L */
                H = (H + 1) & 0x3F;
                L = (L + 1) & 3;
                if (H == 0) done = 1;
            }
            a = (L & 3) | (H << 2);
        }
    }
    #undef IN_RANGE

    p->sec_count = sec_addr >> 2;
    p->sec_sprite0 = sprite0;
}

/* Puntos 257-320: busca los patrones de los sprites de la OAM secundaria */
static void load_sprites(NES *n, int line)
{
    Ppu *p = &n->ppu;
    const int cnt = p->sec_count, h = (p->ctrl & 0x20) ? 16 : 8;
    const u8 *sec = p->sec;
    p->spr0_possible = p->sec_sprite0;
    for (int i = 0; i < 8; i++) p->spr_a12[i] = (h == 8) ? (p->ctrl >> 3) & 1 : 1;
    for (int i = 0; i < cnt; i++) {
        int y = sec[i * 4], diff = line - y;
        u8 tile = sec[i * 4 + 1], attr = sec[i * 4 + 2];
        int row = (attr & 0x80) ? h - 1 - diff : diff;
        u16 addr;
        if (h == 8) addr = ((p->ctrl & 8) << 9) | (tile << 4) | (row & 7);
        else addr = ((tile & 1) << 12) | ((tile & 0xFE) << 4) | ((row & 8) << 1) | (row & 7);
        u8 lo = vrd_s(n, addr), hi = vrd_s(n, addr + 8);
        if (attr & 0x40) { lo = rev8(lo); hi = rev8(hi); }
        p->spr[i].x = sec[i * 4 + 3];
        p->spr[i].attr = attr;
        p->spr[i].lo = lo; p->spr[i].hi = hi;
        if (h == 16) p->spr_a12[i] = tile & 1;
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
    for (int k = 0; k < 8; k++) {
        { p->bg_lo = (u16)((p->bg_lo << 1) | 1); p->bg_hi = (u16)((p->bg_hi << 1) | 1); p->at_lo = (u16)((p->at_lo << 1) | 1); p->at_hi = (u16)((p->at_hi << 1) | 1); }   /* con solo sprites también se desplazan; entra un 1 por la derecha */
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

/* Evento de A12 de un punto dentro de una baldosa: 'clock' es el valor del reloj en ese punto */
static inline void a12_at(NES *n, u64 clock, int bit)
{
    if (n->cart.mmc3) {
        u64 save = n->ppu.clock;
        n->ppu.clock = clock;
        mapper_a12(n, bit);
        n->ppu.clock = save;
    }
}

/* Baldosa de 8 puntos en una línea sin sprites y con el fondo activo (y sin recorte a la izquierda en
 * estas columnas). Mismo resultado que ppu_tile, pero sin desplazar los registros punto a punto: tras
 * cargar el mosaico en el primer punto, el pixel k sale del bit (15 - fine_x - k) de cada registro, y al final
 * los registros quedan desplazados 7 lugares más. */
static void ppu_tile_bg(NES *n)
{
    Ppu *p = &n->ppu;
    const int c = p->dot, y = p->scanline;
    if (c == 65) { p->oam_snap = p->oam_addr; eval_sprites(n, p->scanline); }
    const u8 mask = p->mask;
    u32 lo = p->bg_lo, hi = p->bg_hi, alo = p->at_lo, ahi = p->at_hi;
    if (c >= 2) { lo = ((lo << 1) | 1) & 0xFFFF; hi = ((hi << 1) | 1) & 0xFFFF; alo = ((alo << 1) | 1) & 0xFFFF; ahi = ((ahi << 1) | 1) & 0xFFFF; }
    /* punto 0: carga el mosaico que se buscó en la baldosa anterior y empieza la siguiente búsqueda */
    lo = (lo & 0xFF00) | p->nt_lo;
    hi = (hi & 0xFF00) | p->nt_hi;
    alo = (alo & 0xFF00) | ((p->nt_attr & 1) ? 0xFF : 0);
    ahi = (ahi & 0xFF00) | ((p->nt_attr & 2) ? 0xFF : 0);
    const u64 clock0 = p->clock;
    const int pbit = (p->ctrl >> 4) & 1;
    a12_at(n, clock0 + 1, 0);
    p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF));
    a12_at(n, clock0 + 3, 0);
    {   /* punto 2: atributo */
        u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
        if (p->v & 0x40) a >>= 4;
        if (p->v & 2) a >>= 2;
        p->nt_attr = a & 3;
    }
    a12_at(n, clock0 + 5, pbit);
    p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7));
    a12_at(n, clock0 + 7, pbit);
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
    p->bg_lo = (u16)((lo << 7) | 0x7F); p->bg_hi = (u16)((hi << 7) | 0x7F);
    p->at_lo = (u16)((alo << 7) | 0x7F); p->at_hi = (u16)((ahi << 7) | 0x7F);
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
    if (c == 65) { p->oam_snap = p->oam_addr; eval_sprites(n, p->scanline); }
    const u64 clock0 = p->clock;
    const int pbit = (p->ctrl >> 4) & 1;
    for (int k = 0; k < 8; k++) {
        const int cy = c + k;
        if (cy >= 2) {
            { p->bg_lo = (u16)((p->bg_lo << 1) | 1); p->bg_hi = (u16)((p->bg_hi << 1) | 1); p->at_lo = (u16)((p->at_lo << 1) | 1); p->at_hi = (u16)((p->at_hi << 1) | 1); }   /* con solo sprites también se desplazan; entra un 1 por la derecha */
            if (1)
                for (int i = 0; i < p->spr_count; i++) {
                    if (p->spr[i].x) p->spr[i].x--;
                    else { p->spr[i].lo <<= 1; p->spr[i].hi <<= 1; }
                }
        }
        switch (k) {
        case 0: a12_at(n, clock0 + 1, 0); load_bg(p); p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF)); break;
        case 2: {
            a12_at(n, clock0 + 3, 0);
            u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
            if (p->v & 0x40) a >>= 4;
            if (p->v & 2) a >>= 2;
            p->nt_attr = a & 3; break; }
        case 4: a12_at(n, clock0 + 5, pbit); p->nt_lo = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7)); break;
        case 6: a12_at(n, clock0 + 7, pbit); p->nt_hi = vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8); break;
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
        if (cy == 65 && rend) { p->oam_snap = p->oam_addr; if (!pre) eval_sprites(n, sl); }
        if (p->ovf_dot && cy == p->ovf_dot) { p->status |= 0x20; p->ovf_dot = 0; }
        if (pre && cy == 1) {
            p->status &= ~0xE0;
            p->spr_count = 0; p->spr0_possible = 0;
        }
        if (rend) {
            if ((cy >= 2 && cy < 258) || (cy >= 321 && cy < 338)) {
                { p->bg_lo = (u16)((p->bg_lo << 1) | 1); p->bg_hi = (u16)((p->bg_hi << 1) | 1); p->at_lo = (u16)((p->at_lo << 1) | 1); p->at_hi = (u16)((p->at_hi << 1) | 1); }
                if (cy < 258)
                    for (int i = 0; i < p->spr_count; i++) {
                        if (p->spr[i].x) p->spr[i].x--;
                        else { p->spr[i].lo <<= 1; p->spr[i].hi <<= 1; }
                    }
                switch ((cy - 1) & 7) {
                case 0: a12(n, 0); load_bg(p); p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF));
                        if (n->cart.exgfx) p->ex_byte = n->cart.exram[p->v & 0x3FF]; break;
                case 2: {
                    a12(n, 0);
                    u8 a = vrd(n, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 7));
                    if (p->v & 0x40) a >>= 4;
                    if (p->v & 2) a >>= 2;
                    p->nt_attr = n->cart.exgfx ? (p->ex_byte >> 6) & 3 : a & 3; break; }
                case 4: a12(n, (p->ctrl >> 4) & 1);
                        p->nt_lo = n->cart.exgfx ? exgfx_read(n, 0) : vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7)); break;
                case 6: a12(n, (p->ctrl >> 4) & 1);
                        p->nt_hi = n->cart.exgfx ? exgfx_read(n, 8) : vrd(n, ((p->ctrl & 0x10) << 8) + (p->nt_id << 4) + ((p->v >> 12) & 7) + 8); break;
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
                if (!pre) load_sprites(n, sl);
                else { p->spr_count = 0; p->spr0_possible = 0; }
                p->ovf_dot = 0;
                p->oam_addr = 0;                     /* puntos 257-320: la OAM se lee desde 0 */
            }
            if (cy == 338 || cy == 340) p->nt_id = vrd(n, 0x2000 | (p->v & 0x0FFF));
            if (pre && cy >= 280 && cy < 305) p->v = (p->v & ~0x7BE0) | (p->t & 0x7BE0);
        }
        if (n->cart.mmc5 && rend && cy == 4 && !pre) mapper_scanline(n, sl);       /* MMC5: cuenta de líneas */
        if (!pre && cy >= 1 && cy <= 256) {
            if (rend) draw_pixel(n);
            else p->fb[sl * 256 + cy - 1] = (u16)((p->pal[0] & 0x3F) | ((p->mask & 0xE0) << 1));
        }
    }

    if (n->cart.mmc5 && sl == 240 && cy == 0) mapper_scanline(n, 240);              /* fin del cuadro visible */
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
