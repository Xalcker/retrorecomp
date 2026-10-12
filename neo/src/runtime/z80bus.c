/*
 * El Z80 del Neo Geo: su mapa, sus puertos y cuándo corre.
 *
 *   $0000-$7FFF  los primeros 32 KB de la M1 ROM del cartucho (fijos)
 *   $8000-$BFFF  ventana de 16 KB de la M1 (banco del puerto $0B)
 *   $C000-$DFFF  ventana de 8 KB ($0A);  $E000-$EFFF de 4 KB ($09);  $F000-$F7FF de 2 KB ($08)
 *   $F800-$FFFF  RAM de 2 KB
 * Un banco se elige leyendo el puerto con el número de banco en la mitad alta de la dirección (IN
 * A,(C) con B = banco): la ventana muestra la M1 desde banco × su tamaño. Al encender quedan como si
 * la M1 fuera lineal (MAME hace lo mismo; la wiki dice que no están definidos).
 *
 * Puertos (la mitad baja de la dirección):
 *   $00  leer: el comando del 68000 (y se da por atendida la NMI); escribir: lo mismo, sin dato
 *   $04-$07  YM2610 (ym2610.c)
 *   $08-$0B  leer: banco de una ventana; escribir $08: habilita la NMI, $18: la deshabilita
 *   $0C  escribir: la respuesta para el 68000 (la lee en $320000)
 * Cada escritura del 68000 en $320000 deja el comando y pide la NMI; la NMI llega si está habilitada
 * (por flanco: la línea es "pedida y habilitada"). La INT viene de los temporizadores del YM2610.
 *
 * El Z80 va a 1/6 del reloj maestro (1/3 del 68000) y corre detrás del 68000: se pone al día tras
 * cada instrucción del 68000 y antes de que el 68000 le mande un comando o lea la respuesta.
 */
#include "neo.h"

static const u16 window_base[4] = { 0xF000, 0xE000, 0xC000, 0x8000 };
static const u32 window_size[4] = { 0x800, 0x1000, 0x2000, 0x4000 };

static u8 m1_read(const Neo *g, u32 off)
{
    size_t n = g->cart.len[REG_M];
    return n ? g->cart.data[g->cart.off[REG_M] + off % n] : 0xFF;
}

uint8_t z80_read(struct Neo *g, uint16_t a)
{
    if (a < 0x8000) return m1_read(g, a);
    if (a >= 0xF800) return g->zram[a & 0x7FF];
    int w = a >= 0xF000 ? 0 : a >= 0xE000 ? 1 : a >= 0xC000 ? 2 : 3;
    return m1_read(g, (u32)g->z80_bank[w] * window_size[w] + (u32)(a - window_base[w]));
}

void z80_write(struct Neo *g, uint16_t a, uint8_t v)
{
    if (a >= 0xF800) g->zram[a & 0x7FF] = v;
}

static void nmi_update(Neo *g)
{
    u8 line = g->nmi_pending && g->nmi_enabled;
    if (line && !g->nmi_line) g->z80.nmi = 1;
    g->nmi_line = line;
}

uint8_t z80_in(struct Neo *g, uint16_t port)
{
    u8 p = (u8)port;
    switch (p & 0x0C) {
    case 0x00:
        g->nmi_pending = 0;
        nmi_update(g);
        return g->sound_cmd;
    case 0x04: return ym_read(g, p & 3, g->z80_mclk);
    case 0x08:
        g->z80_bank[p & 3] = (u8)(port >> 8);
        return 0;
    default: return 0;
    }
}

void z80_out(struct Neo *g, uint16_t port, uint8_t v)
{
    u8 p = (u8)port;
    switch (p & 0x0C) {
    case 0x00: g->nmi_pending = 0; nmi_update(g); break;
    case 0x04: ym_write(g, p & 3, v, g->z80_mclk); break;
    case 0x08: g->nmi_enabled = !(p & 0x10); nmi_update(g); break;
    default: g->sound_reply = v; break;
    }
}

void sound_command(Neo *g, u8 v)
{
    z80_sync(g);
    g->sound_cmd = v;
    g->sound_cmds++;
    g->nmi_pending = 1;             /* si la NMI ya estaba pedida, no hay flanco nuevo (como en la consola) */
    nmi_update(g);
}

void z80_run_until(Neo *g, u64 target)
{
    Z80 *z = &g->z80;
    Ym2610 *y = &g->ym;
    while (g->z80_mclk < target) {
        if (g->z80_mclk >= ym_next_event(y)) ym_update(y, g->z80_mclk);
        z->irq = y->flags != 0;
        if (z->halted && !z->nmi && !(z->irq && z->iff1)) {
            /* HALT sin nada que atender: cada paso son 4 ciclos T y sube R. Se saltan de una vez hasta
             * el destino o hasta que venza un temporizador */
            u64 stop = target, ev = ym_next_event(y);
            if (ev > g->z80_mclk && ev < stop) stop = ev;
            u64 n = (stop - g->z80_mclk + 4 * MCLK_PER_Z80 - 1) / (4 * MCLK_PER_Z80);
            g->z80_mclk += n * 4 * MCLK_PER_Z80;
            z->r = (u8)((z->r & 0x80) | ((z->r + n) & 0x7F));
            z->q = 0;
            z->p = 0;
            continue;
        }
        g->z80_mclk += (u64)z80_step(g, z) * MCLK_PER_Z80;
    }
}
