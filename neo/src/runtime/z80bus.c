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
 * El Z80 va a 1/6 del reloj maestro (1/3 del 68000) y corre detrás del 68000: se pone al día antes de
 * que el 68000 le mande un comando o lea la respuesta, y al final de cada cuadro (audio_end_frame).
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

/*
 * Bucles de espera: el programa de sonido suele esperar en un bucle corto que solo lee la RAM y
 * compara (ld a,(hl) / and a / jp z,bucle; o ei / ld a,(x) / ld b,a / ld a,(y) / cp b / jr z,bucle).
 * Si el bucle es puro (sin puertos, y cada registro que lee lo escribe antes en la misma vuelta o no
 * lo toca), cada vuelta deja el Z80 igual salvo el tiempo y R: se saltan de una vez todas las vueltas
 * enteras que caben antes del destino y del próximo evento del YM2610, como el HALT. Se admite una
 * escritura: un contador en la RAM (ld a,(nn) / inc a o dec a / ld (nn),a, como la semilla de azar de
 * KOF '98) que nada más del bucle lee; lo que deja en A y en las banderas se tiene que pisar antes de
 * usarse, y con un contador el bucle no puede leer por (HL), (BC) o (DE). Al saltar k vueltas el
 * contador suma k (o resta). La duración de una vuelta (ciclos y R) se mide entre dos llegadas
 * seguidas al comienzo; si entre medio el PC salió del bucle (una interrupción), se vuelve a medir. Con
 * una interrupción o una NMI esperando no se salta nada.
 */
enum { RA = 1, RF = 2, RB = 4, RC = 8, RD = 16, RE = 32, RH = 64, RL = 128 };
static const u8 reg_bit[8] = { RB, RC, RD, RE, RH, RL, RH | RL, RA };    /* (HL) lee H y L */

static int loop_pure(Neo *g, Z80Loop *lp, u16 start, u16 jump_at)
{
    struct { u8 op, r, w, indirect, role; u16 nn; } in[16];  /* role: 1-3 = ld a,(nn) / inc a / ld (nn),a del contador */
    int n = 0;
    u16 pc = start;
    lp->counters = 0;
    for (;;) {
        if (n == 16) return 0;
        u8 op = z80_read(g, pc), r = 0, w = 0, ind = 0;
        u16 nn = 0;
        int len = 1, closes = 0;
        if (op == 0x00 || op == 0xFB) {                                     /* NOP, EI */
        } else if (op >= 0x40 && op < 0x80 && op != 0x76) {                 /* LD r,r' / LD r,(HL) */
            if ((op >> 3 & 7) == 6) return 0;                               /* LD (HL),r escribe */
            r = reg_bit[op & 7]; w = reg_bit[op >> 3 & 7]; ind = (op & 7) == 6;
        } else if (op == 0x0A || op == 0x1A) { r = op == 0x0A ? RB | RC : RD | RE; w = RA; ind = 1;   /* LD A,(BC/DE) */
        } else if (op == 0x3A || op == 0x32) {                              /* LD A,(nn) / LD (nn),A */
            nn = (u16)(z80_read(g, (u16)(pc + 1)) | z80_read(g, (u16)(pc + 2)) << 8);
            len = 3;
            if (op == 0x3A) w = RA;
            else {
                /* solo el contador: ld a,(nn) / inc a o dec a / ld (nn),a */
                if (n < 2 || in[n - 2].op != 0x3A || in[n - 2].nn != nn || (in[n - 1].op != 0x3C && in[n - 1].op != 0x3D) ||
                    lp->counters == 2 || nn < 0xF800) return 0;
                in[n - 2].role = 1; in[n - 1].role = 2;
                lp->counter[lp->counters] = nn;
                lp->delta[lp->counters++] = in[n - 1].op == 0x3C ? 1 : 0xFF;
                r = RA;
            }
        } else if (op == 0x3C || op == 0x3D) { r = RA; w = RA | RF;          /* INC A / DEC A */
        } else if (op >= 0xA0 && op < 0xC0) {                               /* AND/XOR/OR/CP r */
            r = RA | reg_bit[op & 7]; w = RF | (op >= 0xB8 ? 0 : RA); ind = (op & 7) == 6;
        } else if (op == 0xE6 || op == 0xEE || op == 0xF6 || op == 0xFE) {  /* AND/XOR/OR/CP n */
            r = RA; w = RF | (op == 0xFE ? 0 : RA); len = 2;
        } else if (op == 0xCB) {                                            /* BIT b,r (no (HL): lee WZ) */
            u8 o2 = z80_read(g, (u16)(pc + 1));
            if (o2 < 0x40 || o2 >= 0x80 || (o2 & 7) == 6) return 0;
            r = reg_bit[o2 & 7] | RF; w = RF; len = 2;
        } else if ((op & 0xC7) == 0xC2 || op == 0xC3 || op == 0x18 || ((op & 0xE7) == 0x20)) {
            int jr = op == 0x18 || (op & 0xE7) == 0x20;
            u16 t = jr ? (u16)(pc + 2 + (int8_t)z80_read(g, (u16)(pc + 1)))
                       : (u16)(z80_read(g, (u16)(pc + 1)) | z80_read(g, (u16)(pc + 2)) << 8);
            len = jr ? 2 : 3;
            r = op == 0xC3 || op == 0x18 ? 0 : RF;
            if (t == start) closes = 1;
            else if (r == 0 || (t >= start && t <= jump_at)) return 0;      /* fuera del bucle solo una salida no tomada */
        } else return 0;
        in[n].op = op; in[n].r = r; in[n].w = w; in[n].indirect = ind; in[n].nn = nn; in[n].role = op == 0x32 ? 3 : 0;
        n++;
        if (closes) { if (pc != jump_at) return 0; break; }
        pc = (u16)(pc + len);
        if (pc > jump_at || pc < start) return 0;
    }
    u8 all = 0, done = 0, taint = 0;
    for (int i = 0; i < n; i++) all |= in[i].w;
    for (int i = 0; i < n; i++) {
        if (lp->counters) {
            if (in[i].indirect) return 0;
            for (int c = 0; c < lp->counters; c++)
                if (in[i].op == 0x3A && in[i].nn == lp->counter[c] && in[i].role != 1) return 0;   /* otro lee el contador */
        }
        if (!in[i].role && (in[i].r & taint)) return 0;     /* usa lo que salió del contador */
        if (in[i].r & all & ~done) return 0;                /* lee lo que dejó la vuelta anterior */
        done |= in[i].w;
        if (in[i].role == 1) taint |= RA;
        else if (in[i].role == 2) taint |= RA | RF;
        else if (in[i].role == 0) taint &= (u8)~in[i].w;
    }
    return !taint;
}

static u32 banks_key(const Neo *g) { return (u32)g->z80_bank[0] | g->z80_bank[1] << 8 | g->z80_bank[2] << 16 | (u32)g->z80_bank[3] << 24; }

/* Tras una instrucción que empezó en pc0: ¿se cerró una vuelta de un bucle de espera? */
static void idle_loop(Neo *g, u16 pc0, u64 target)
{
    Z80 *z = &g->z80;
    Z80Loop *lp = &g->z80_loop;
    u16 pc = z->pc;
    if (lp->armed && (pc0 < lp->pc || pc0 > lp->end)) lp->armed = 0;      /* salió del bucle */
    if (pc >= pc0 || pc0 - pc > 64) return;                                 /* no es un salto corto hacia atrás */
    u32 key = pc >= 0x8000 ? banks_key(g) : 0;
    if (pc != lp->pc || pc0 != lp->end || key != lp->banks) {
        lp->pc = pc; lp->end = pc0; lp->banks = key; lp->armed = 0;
        lp->ok = (u8)loop_pure(g, lp, pc, pc0);
    }
    if (!lp->ok) return;
    if (lp->armed && !z->nmi && !(z->irq && z->iff1)) {
        u64 len = g->z80_mclk - lp->at, lim = target, ev = ym_next_event(&g->ym);
        if (ev < lim) lim = ev;
        if (len && lim > g->z80_mclk) {
            u64 k = (lim - g->z80_mclk) / len;
            g->z80_mclk += k * len;
            z->r = (u8)((z->r & 0x80) | ((z->r + k * (u8)(z->r - lp->r)) & 0x7F));
            for (int c = 0; c < lp->counters; c++) {
                u8 *m = &g->zram[lp->counter[c] - 0xF800];
                *m = (u8)(*m + k * lp->delta[c]);
            }
        }
    }
    lp->at = g->z80_mclk;
    lp->r = z->r;
    lp->armed = 1;
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
        u16 pc0 = z->pc;
        g->z80_mclk += (u64)z80_step(g, z) * MCLK_PER_Z80;
        idle_loop(g, pc0, target);
    }
}
