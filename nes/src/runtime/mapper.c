/*
 * Mappers: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color Dreams, 34 BNROM/NINA-001,
 * 66 GxROM, 71 Camerica, 79 NINA-03/06, 87, 140, 180 (UNROM al revés), 206 (DxROM), 9 (MMC2), 10 (MMC4),
 * 69 (FME-7, sin el audio 5B) 118 (TxSROM) y 119 (TQROM). 9, 10, 69 y 118 se probaron solo con cartuchos sintéticos o un juego.
 * Cada mapper guarda su estado en cart.reg[] y puede cambiar cart.mirroring.
 * Agregar uno nuevo = un caso en cada función (y en mapper_supported).
 */
#include <string.h>
#include "nes.h"

int mapper_supported(int id)
{
    switch (id) {
    case 0: case 1: case 2: case 3: case 4: case 7: case 11: case 34: case 66: case 71: case 79:
    case 87: case 140: case 180: case 206: case 9: case 10: case 69: case 118: case 119: case 5: return 1;
    }
    return 0;
}

/* MMC1: reg[0]=desplazamiento, [1]=cuenta, [2]=control, [3]=chr0, [4]=chr1, [5]=prg, [6]=último ciclo */
void mapper_reset(NES *n)
{
    Cart *c = &n->cart;
    for (int i = 0; i < 8; i++) c->reg[i] = 0;
    c->a12_low = 0;
    c->chr_mask = (c->chr_size & (c->chr_size - 1)) == 0 ? (u32)(c->chr_size - 1) : 0;
    if (c->mapper == 1) { c->reg[2] = 0x0C; c->reg[6] = 0xFFFFFFFFu; }
    c->irq_cycle = ~0ull; c->irq_t0 = 0;
    c->mmc5 = c->mapper == 5;
    if (c->mmc5) {
        memset(c->m5, 0, sizeof c->m5);
        c->m5[0x00] = 3; c->m5[0x14] = 0x7F; c->m5[0x15] = 0x7F; c->m5[0x16] = 0x7F; c->m5[0x17] = 0xFF;   /* PRG modo 3; últimos bancos */
        c->m5[0x01] = 3;
        c->m5_spr16 = c->m5_inframe = c->m5_pending = c->m5_counter = 0;
        c->exgfx = 0;
        c->mirroring = MIRROR_CUSTOM;
        for (int i = 0; i < 4; i++) c->nt_map[i] = i & 1;
    }
    c->mmc3 = c->mapper == 4 || c->mapper == 118 || c->mapper == 119;
    c->chr_hook = c->mapper == 9 || c->mapper == 10;
    if (c->chr_hook) { c->reg[5] = c->reg[6] = 1; }                /* latches en FE */
    if (c->mapper == 69) { c->reg[0] = 0; c->reg[16] = 0; }
    c->low_regs = c->mapper == 34 || c->mapper == 79 || c->mapper == 87 || c->mapper == 140 || c->mapper == 5;
    if (c->mapper == 4 || c->mapper == 206 || c->mapper == 118 || c->mapper == 119) {
        /* reg[0]=selección, [1..8]=R0..R7, [9]=latch IRQ, [10]=contador, [11]=recarga,
         * [12]=IRQ activo, [13]=nivel de A12, [14]=protección de RAM */
        c->reg[8] = 1; c->reg[14] = 0x80;          /* R7 = 1, RAM activa */
        if (c->mapper == 4) c->mirroring = c->mirroring == MIRROR_FOUR ? MIRROR_FOUR : MIRROR_VERTICAL;
        if (c->mapper == 118) c->mirroring = MIRROR_CUSTOM;
    }
    mapper_remap(c);
}

/* ---- MMC3 ---- */
static inline size_t banks8(const Cart *c) { return c->prg_size >> 13; }

static void mmc3_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    int odd = a & 1;
    if (c->mapper == 206) {                      /* DxROM: solo $8000/$8001, sin inversión, espejado ni IRQ */
        if (a >= 0xA000) return;
        if (!odd) c->reg[0] = v & 7;
        else { int r = c->reg[0] & 7; c->reg[1 + r] = r >= 6 ? v & 0x0F : v & 0x3F; }
        return;
    }
    switch ((a >> 13) & 3) {
    case 0:
        if (!odd) c->reg[0] = v;
        else c->reg[1 + (c->reg[0] & 7)] = v;
        break;
    case 1:
        if (!odd) { if (c->mapper == 4 && c->mirroring != MIRROR_FOUR) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; }
        else c->reg[14] = v;
        break;
    case 2:
        if (!odd) c->reg[9] = v;
        else { c->reg[10] = 0; c->reg[11] = 1; }
        break;
    case 3:
        if (!odd) { c->reg[12] = 0; n->irq_line &= ~1; }
        else c->reg[12] = 1;
        break;
    }
}

/* Flanco de subida de A12 tras un tiempo en bajo: cuenta una línea */
void mapper_a12(NES *n, int bit)
{
    Cart *c = &n->cart;
    if (!c->mmc3) return;
    if (!bit) {
        if (c->reg[13]) { c->reg[13] = 0; c->a12_low = n->ppu.clock; }
        return;
    }
    if (c->reg[13]) return;
    c->reg[13] = 1;
    if (n->ppu.clock - c->a12_low < 9) return;   /* filtro: menos de ~3 ciclos de CPU en bajo */
    if (c->reg[10] == 0 || c->reg[11]) c->reg[10] = c->reg[9];
    else c->reg[10]--;
    c->reg[11] = 0;
    if (c->reg[10] == 0 && c->reg[12]) n->irq_line |= 1;
}

static inline size_t banks16(const Cart *c) { return c->prg_size >> 14; }

static size_t prg_at(const Cart *c, size_t bank16, u16 off)
{
    return ((bank16 % banks16(c)) << 14) | (off & 0x3FFF);
}

/* ---- MMC5 ---- */

static u8 *mmc5_ram(Cart *c, unsigned bank)
{
    bank &= 7;
    return bank == 0 ? c->prg_ram : c->wram_hi + (size_t)(bank - 1) * 0x2000;
}

/* Cada ventana de 8 KB de $8000-$FFFF: ROM o RAM según el modo ($5100) y los registros $5114-$5117 */
static void mmc5_remap_prg(Cart *c)
{
    int mode = c->m5[0x00] & 3;
    size_t nb = c->prg_size >> 13;
    for (int s = 0; s < 4; s++) {
        unsigned v, span = 1, sub = 0;
        int rom_only = 0;
        switch (mode) {
        case 0:  v = c->m5[0x17]; rom_only = 1; span = 4; sub = s; break;
        case 1:  if (s < 2) { v = c->m5[0x15]; span = 2; sub = s; }
                 else { v = c->m5[0x17]; rom_only = 1; span = 2; sub = s - 2; } break;
        case 2:  if (s < 2) { v = c->m5[0x15]; span = 2; sub = s; }
                 else if (s == 2) v = c->m5[0x16];
                 else { v = c->m5[0x17]; rom_only = 1; } break;
        default: v = c->m5[0x14 + s]; if (s == 3) rom_only = 1; break;
        }
        if (rom_only || (v & 0x80)) {
            unsigned bank = ((v & 0x7F) & ~(span - 1)) + sub;
            c->prg_map[s] = (size_t)(bank % nb) << 13;
            c->prg_ptr[s] = c->prg + c->prg_map[s];
        } else {
            c->prg_map[s] = (size_t)-1;                    /* RAM: no hay desplazamiento en la ROM */
            c->prg_ptr[s] = mmc5_ram(c, ((v & 7) & ~(span - 1)) + sub);
        }
    }
}

/* Número de banco de 1 KB de CHR para la página 'pg' (0-7) con el juego A (sprites) o B (fondo) */
static unsigned mmc5_chr_bank(const Cart *c, int pg, int setB)
{
    int mode = c->m5[0x01] & 3;
    unsigned up = (unsigned)(c->m5[0x30] & 3) << 8;
    const u8 *r = c->m5 + (setB ? 0x28 : 0x20);        /* A: $5120-$5127; B: $5128-$512B */
    if (!setB) {
        switch (mode) {
        case 0: return ((up | r[7]) << 3) + pg;
        case 1: return ((up | r[pg < 4 ? 3 : 7]) << 2) + (pg & 3);
        case 2: return ((up | r[(pg | 1)]) << 1) + (pg & 1);
        default: return up | r[pg];
        }
    }
    switch (mode) {
    case 0: return ((up | r[3]) << 3) + pg;
    case 1: return ((up | r[3]) << 2) + (pg & 3);
    case 2: return ((up | r[(pg & 2) ? 3 : 1]) << 1) + (pg & 1);
    default: return up | r[pg & 3];
    }
}

static void mmc5_remap_chr(Cart *c)
{
    for (int pg = 0; pg < 8; pg++) {
        size_t a = (size_t)mmc5_chr_bank(c, pg, 0) << 10, b = (size_t)mmc5_chr_bank(c, pg, 1) << 10;
        a = c->chr_mask ? (a & c->chr_mask) : a % c->chr_size;
        b = c->chr_mask ? (b & c->chr_mask) : b % c->chr_size;
        c->chr_page_spr[pg] = c->chr + a;
        c->chr_page[pg] = c->chr + (c->m5_spr16 ? b : a);      /* sprites 8x16: el fondo usa el juego B */
        c->chr_wr[pg] = 0;
    }
}

/* $5105: de dónde sale cada nametable (0 y 1: CIRAM; 2: ExRAM; 3: relleno) */
static void mmc5_nametables(Cart *c)
{
    for (int i = 0; i < 4; i++) c->nt_map[i] = (c->m5[0x05] >> (i * 2)) & 3;
}

void mapper_ppu_ctrl(NES *n, u8 v)
{
    Cart *c = &n->cart;
    u8 spr16 = (v >> 5) & 1;
    if (spr16 != c->m5_spr16) { c->m5_spr16 = spr16; mmc5_remap_chr(c); }
}

/* La PPU empieza la línea 'line' con el dibujo activo (la línea 240 termina el cuadro) */
void mapper_scanline(NES *n, int line)
{
    Cart *c = &n->cart;
    if (line >= 240) { c->m5_inframe = 0; return; }
    if (!c->m5_inframe) { c->m5_inframe = 1; c->m5_counter = 0; c->m5_pending = 0; n->irq_line &= ~1; }
    else c->m5_counter++;
    if (c->m5_counter == c->m5[0x53] && c->m5[0x53]) c->m5_pending = 1;     /* $5203 = 0 no genera IRQ */
    if (c->m5_pending && (c->m5[0x54] & 0x80)) n->irq_line |= 1;
}

static u8 mmc5_read(NES *n, u16 a)
{
    Cart *c = &n->cart;
    switch (a) {
    case 0x5204: {
        u8 v = (c->m5_pending ? 0x80 : 0) | (c->m5_inframe ? 0x40 : 0);
        c->m5_pending = 0; n->irq_line &= ~1;
        return v;
    }
    case 0x5015: return apu_mmc5_status(n);
    case 0x5205: return (u8)c->m5_prod;
    case 0x5206: return (u8)(c->m5_prod >> 8);
    }
    if (a >= 0x5C00) return c->exram[a & 0x3FF];
    return 0;
}

static void mmc5_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    if (a >= 0x5C00) { if ((c->m5[0x04] & 3) != 3) c->exram[a & 0x3FF] = v; return; }
    if (a >= 0x5100 && a <= 0x5130) {
        c->m5[a - 0x5100] = v;
        if (a == 0x5105) mmc5_nametables(c);
        if (a == 0x5104) c->exgfx = (v & 3) == 1;
        if (a == 0x5100 || (a >= 0x5113 && a <= 0x5117)) mmc5_remap_prg(c);
        if (a == 0x5101 || (a >= 0x5120 && a <= 0x512B) || a == 0x5130) mmc5_remap_chr(c);
        return;
    }
    if (a >= 0x5200 && a <= 0x5206) {
        c->m5[0x50 + (a - 0x5200)] = v;
        if (a == 0x5205 || a == 0x5206) c->m5_prod = (u16)(c->m5[0x55] * c->m5[0x56]);
        if (a == 0x5204 && c->m5_pending && (v & 0x80)) n->irq_line |= 1;
        if (a == 0x5204 && !(v & 0x80)) n->irq_line &= ~1;
        return;
    }
    if (a <= 0x5007 || a == 0x5010 || a == 0x5011 || a == 0x5015) apu_mmc5_write(n, a, v);    /* audio: dos pulsos y PCM */
}

static size_t prg32(const Cart *c, size_t bank, u16 a)
{
    size_t nb = c->prg_size >> 15;
    if (!nb) nb = 1;
    return (((bank % nb) << 15) | (a & 0x7FFF)) % c->prg_size;
}

static size_t mmc3_prg(const Cart *c, u16 a)
{
    int slot = (a >> 13) & 3;                    /* 0:$8000 1:$A000 2:$C000 3:$E000 */
    size_t nb = banks8(c), bank;
    int swap = c->reg[0] & 0x40;
    switch (slot) {
    case 0: bank = swap ? nb - 2 : c->reg[7]; break;
    case 1: bank = c->reg[8]; break;
    case 2: bank = swap ? c->reg[7] : nb - 2; break;
    default: bank = nb - 1; break;
    }
    return ((bank % nb) << 13) | (a & 0x1FFF);
}

/* Desplazamiento en la ROM de PRG que ve el CPU en 'a', calculado desde los registros del mapper */
static long prg_offset_slow(const Cart *c, u16 a)
{
    if (a < 0x8000) return -1;
    switch (c->mapper) {
    case 1: {
        /* SUROM (512 KB): el bit 4 del registro de CHR 0 elige el bloque de 256 KB en el que se mueven los bancos */
        u32 mode = (c->reg[2] >> 2) & 3, bank = c->reg[5] & 0x0F;
        u32 outer = c->prg_size > 0x40000 ? ((c->reg[3] >> 4) & 1) * 16 : 0;
        u32 first = outer, last = outer + 15;
        if (c->prg_size <= 0x40000) last = (u32)banks16(c) - 1;
        if (mode < 2) return (long)prg_at(c, outer + (bank & ~1u) + (a >= 0xC000), a);
        if (mode == 2) return (long)(a < 0xC000 ? prg_at(c, first, a) : prg_at(c, outer + bank, a));
        return (long)(a < 0xC000 ? prg_at(c, outer + bank, a) : prg_at(c, last, a));
    }
    case 2:
        return (long)(a < 0xC000 ? prg_at(c, c->reg[0], a) : prg_at(c, banks16(c) - 1, a));
    case 4: case 206: case 118: case 119: return (long)mmc3_prg(c, a);
    case 9: {
        size_t nb = banks8(c);
        int slot = (a >> 13) & 3;
        size_t bank = slot == 0 ? c->reg[0] & 15 : nb - 4 + slot;
        return (long)(((bank % nb) << 13) | (a & 0x1FFF));
    }
    case 10: return (long)(a < 0xC000 ? prg_at(c, c->reg[0] & 15, a) : prg_at(c, banks16(c) - 1, a));
    case 69: {
        size_t nb = banks8(c);
        int slot = (a >> 13) & 3;
        size_t bank = slot == 3 ? nb - 1 : c->reg[9 + slot];
        return (long)(((bank % nb) << 13) | (a & 0x1FFF));
    }
    case 11: case 34: case 66: case 79: case 140: return (long)prg32(c, c->reg[0], a);
    case 71: return (long)(a < 0xC000 ? prg_at(c, c->reg[0], a) : prg_at(c, banks16(c) - 1, a));
    case 180: return (long)(a < 0xC000 ? prg_at(c, 0, a) : prg_at(c, c->reg[0], a));
    case 7: return (long)prg_at(c, (c->reg[0] & 7) * 2 + (a >= 0xC000), a);
    default:                                     /* 0 y 3: NROM-128 se refleja; -256 usa los 2 bancos */
        return (long)prg_at(c, a >> 14 & 1, a);
    }
}

static size_t chr_off(const Cart *c, u16 a);

/* Número de banco de 1 KB de CHR que ve la PPU en 'a' (MMC3 y derivados) */
static size_t mmc3_chr_bank(const Cart *c, u16 a)
{
    int slot = (a >> 10) & 7;
    if (c->reg[0] & 0x80) slot ^= 4;
    if (slot < 4) return (c->reg[1 + (slot >> 1)] & 0xFE) | (slot & 1);
    return c->reg[1 + 2 + (slot - 4)];
}

void mapper_remap(Cart *c)
{
    if (c->mmc5) { mmc5_remap_prg(c); mmc5_remap_chr(c); return; }
    for (int i = 0; i < 4; i++) {
        c->prg_map[i] = (size_t)prg_offset_slow(c, (u16)(0x8000 + i * 0x2000));
        c->prg_ptr[i] = c->prg + c->prg_map[i];
    }
    for (int i = 0; i < 8; i++) c->chr_page_spr[i] = NULL;
    for (int i = 0; i < 8; i++) {
        c->chr_page[i] = c->chr + chr_off(c, (u16)(i * 0x400));
        c->chr_wr[i] = c->chr_is_ram;
        if (c->mapper == 119 && (mmc3_chr_bank(c, (u16)(i * 0x400)) & 0x40)) {    /* TQROM: el bit 6 elige la RAM */
            c->chr_page[i] = c->chr_ram + ((mmc3_chr_bank(c, (u16)(i * 0x400)) & 7) << 10);
            c->chr_wr[i] = 1;
        }
    }
}

long mapper_prg_offset(const Cart *c, u16 a)
{
    if (a < 0x8000) return -1;
    size_t m = c->prg_map[(a >> 13) & 3];
    return m == (size_t)-1 ? -1 : (long)(m + (a & 0x1FFF));                    /* -1: RAM (MMC5) */
}

u8 mapper_cpu_read(NES *n, u16 a)
{
    Cart *c = &n->cart;
    if (c->mmc5) {
        if (a >= 0x5000 && a < 0x6000) return mmc5_read(n, a);
        if (a >= 0x6000 && a < 0x8000) return mmc5_ram(c, c->m5[0x13])[a & 0x1FFF];
        if (a >= 0x8000) {
            u8 v = c->prg_ptr[(a >> 13) & 3][a & 0x1FFF];
            if (a < 0xC000 && n->apu.m5_pcm_read) apu_mmc5_pcm(n, v);          /* PCM en modo lectura */
            return v;
        }
    }
    if (a < 0x6000) return 0;                    /* bus abierto (aprox.) */
    if (a < 0x8000) {
        if (c->mmc3 && !(c->reg[14] & 0x80)) return 0;
        if (c->mapper == 69 && !(c->reg[13] & 0x40)) {           /* FME-7: ROM en $6000 (bit 6 = 0) */
            size_t nb = banks8(c);
            return c->prg[((c->reg[13] & 0x3F) % nb) << 13 | (a & 0x1FFF)];
        }
        if (c->mapper == 69 && !(c->reg[13] & 0x80)) return 0;    /* RAM desactivada */
        return c->prg_ram[a & 0x1FFF];
    }
    return c->prg_ptr[(a >> 13) & 3][a & 0x1FFF];
}

static void mmc1_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    u32 now = (u32)n->cyc;
    int ignore = (now - c->reg[6] == 1);         /* escritura en el ciclo siguiente (leer-modificar-escribir) */
    c->reg[6] = now;
    if (ignore) return;
    if (v & 0x80) { c->reg[0] = 0; c->reg[1] = 0; c->reg[2] |= 0x0C; return; }
    c->reg[0] |= (u32)(v & 1) << c->reg[1];
    if (++c->reg[1] < 5) return;
    u32 val = c->reg[0] & 0x1F;
    c->reg[0] = 0; c->reg[1] = 0;
    switch ((a >> 13) & 3) {
    case 0:
        c->reg[2] = val;
        c->mirroring = (val & 3) == 0 ? MIRROR_SINGLE0 : (val & 3) == 1 ? MIRROR_SINGLE1 :
                       (val & 3) == 2 ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
        break;
    case 1: c->reg[3] = val; break;
    case 2: c->reg[4] = val; break;
    case 3: c->reg[5] = val; break;
    }
}

/* TxSROM: el espejado sale del bit 7 del banco de CHR de cada nametable */
static void mmc118_mirror(Cart *c)
{
    for (int i = 0; i < 4; i++) {
        int reg = (c->reg[0] & 0x80) ? 3 + i : (i < 2 ? 1 : 2);
        c->nt_map[i] = (c->reg[reg] >> 7) & 1;
    }
}

/* MMC2 y MMC4: $A000 PRG, $B000-$E000 CHR (cuatro registros según el latch) y $F000 espejado */
static void mmc2_write(Cart *c, u16 a, u8 v)
{
    switch (a >> 12) {
    case 0xA: c->reg[0] = v & 15; break;
    case 0xB: c->reg[1] = v & 31; break;
    case 0xC: c->reg[2] = v & 31; break;
    case 0xD: c->reg[3] = v & 31; break;
    case 0xE: c->reg[4] = v & 31; break;
    case 0xF: if (c->mirroring != MIRROR_FOUR) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; break;
    }
}

/* Tras leer un patrón: $0FD8/$0FE8 (MMC2) o $0FD8-$0FDF/$0FE8-$0FEF (MMC4) y $1FD8-$1FDF/$1FE8-$1FEF
 * cambian el latch de cada mitad (FD = 0, FE = 1) y, con él, el banco de CHR de esa mitad */
void mapper_chr_latch(Cart *c, u16 a)
{
    a &= 0x1FFF;
    int l = -1, side = a >= 0x1000;
    u16 lo = a & 0xFFF;
    if (side || c->mapper == 10) {
        if (lo >= 0xFD8 && lo <= 0xFDF) l = 0;
        else if (lo >= 0xFE8 && lo <= 0xFEF) l = 1;
    } else {
        if (lo == 0xFD8) l = 0;
        else if (lo == 0xFE8) l = 1;
    }
    if (l < 0 || c->reg[5 + side] == (u32)l) return;
    c->reg[5 + side] = (u32)l;
    for (int i = 0; i < 4; i++) {
        int pg = side * 4 + i;
        c->chr_page[pg] = c->chr + chr_off(c, (u16)(pg * 0x400));
    }
}

/* FME-7: registro de comando en $8000 y parámetro en $A000. Comandos 0-7 CHR, 8 $6000, 9-B PRG,
 * C espejado, D-F IRQ de 16 bits que baja un paso por ciclo de CPU y se levanta al pasar de 0 a $FFFF */
static void fme7_irq_update(Cart *c, u64 now)
{
    c->irq_cycle = ((c->reg[15] & 0x81) == 0x81) ? now + c->reg[16] + 1 : ~0ull;
}

static void fme7_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    if (a < 0xA000) { if (a < 0xC000) c->reg[0] = v & 15; return; }      /* $8000: comando */
    if (a >= 0xC000) return;                                           /* (audio 5B en $C000/$E000: no) */
    u64 now = n->cyc;
    /* valor actual del contador, antes de cambiar nada */
    u32 cur = (c->reg[15] & 0x80) ? (u16)(c->reg[16] - (u16)(now - c->irq_t0)) : c->reg[16];
    int cmd = c->reg[0] & 15;
    if (cmd < 8) c->reg[1 + cmd] = v;
    else switch (cmd) {
    case 8: c->reg[13] = v; break;
    case 9: case 10: case 11: c->reg[cmd] = v & 0x3F; break;
    case 12: c->mirroring = (v & 3) == 0 ? MIRROR_VERTICAL : (v & 3) == 1 ? MIRROR_HORIZONTAL :
                            (v & 3) == 2 ? MIRROR_SINGLE0 : MIRROR_SINGLE1; break;
    case 13: c->reg[15] = v; n->irq_line &= ~1; break;                 /* control; escribirlo reconoce el IRQ */
    case 14: cur = (cur & 0xFF00) | v; break;
    case 15: cur = (cur & 0x00FF) | ((u32)v << 8); break;
    }
    c->reg[16] = cur & 0xFFFF;
    c->irq_t0 = now;
    fme7_irq_update(c, now);
}

/* Registros por debajo de $8000 (NINA-03/06, 87, 140, NINA-001) */
static int low_write(Cart *c, u16 a, u8 v)
{
    switch (c->mapper) {
    case 79:  if (a >= 0x4100 && a < 0x6000 && (a & 0xE100) == 0x4100) { c->reg[0] = (v >> 3) & 1; c->reg[1] = v & 7; return 1; } break;
    case 87:  if (a >= 0x6000 && a < 0x8000) { c->reg[1] = ((v & 1) << 1) | ((v >> 1) & 1); return 1; } break;
    case 140: if (a >= 0x6000 && a < 0x8000) { c->reg[0] = (v >> 4) & 3; c->reg[1] = v & 15; return 1; } break;
    case 34:
        if (!c->chr_is_ram && a >= 0x7FFD && a <= 0x7FFF) {      /* NINA-001 */
            if (a == 0x7FFD) c->reg[0] = v & 1;
            else if (a == 0x7FFE) c->reg[1] = v & 15;
            else c->reg[2] = v & 15;
            c->prg_ram[a & 0x1FFF] = v;
            return 1;
        }
        break;
    }
    return 0;
}

void mapper_cpu_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    if (a < 0x4020) return;
    if (c->mmc5) {
        int ram_ok = (c->m5[0x02] & 3) == 2 && (c->m5[0x03] & 3) == 1;           /* protección de PRG-RAM */
        if (a >= 0x5000 && a < 0x6000) { mmc5_write(n, a, v); c->map_ver++; return; }
        if (a >= 0x6000 && a < 0x8000) { if (ram_ok) mmc5_ram(c, c->m5[0x13])[a & 0x1FFF] = v; return; }
        if (a >= 0x8000) {                                                        /* RAM mapeada en $8000-$DFFF */
            int s = (a >> 13) & 3;
            if (c->prg_map[s] == (size_t)-1 && ram_ok) c->prg_ptr[s][a & 0x1FFF] = v;
        }
        return;
    }
    if (c->low_regs && a < 0x8000 && low_write(c, a, v)) { c->map_ver++; mapper_remap(c); return; }
    if (a < 0x6000) return;
    if (a >= 0x8000) c->map_ver++;
    if (a < 0x8000) {
        if (c->mmc3 && (c->reg[14] & 0xC0) != 0x80) return;
        if (c->mapper == 69 && (!(c->reg[13] & 0x40) || !(c->reg[13] & 0x80))) return;      /* solo con RAM elegida y activa */
        c->prg_ram[a & 0x1FFF] = v; return;
    }
    switch (c->mapper) {
    case 4: case 206: case 119: mmc3_write(n, a, v); break;
    case 118: mmc3_write(n, a, v); mmc118_mirror(c); break;
    case 9: case 10: mmc2_write(c, a, v); break;
    case 69: fme7_write(n, a, v); break;
    case 1: mmc1_write(n, a, v); break;
    case 11: c->reg[0] = v & 3; c->reg[1] = v >> 4; break;
    case 66: c->reg[0] = (v >> 4) & 3; c->reg[1] = v & 3; break;
    case 34: if (c->chr_is_ram) c->reg[0] = v; break;                     /* BNROM (NINA-001 usa $7FFD-$7FFF) */
    case 71:
        if (a >= 0xC000) c->reg[0] = v & 15;
        else if (a >= 0x9000 && a < 0xA000) c->mirroring = (v & 0x10) ? MIRROR_SINGLE1 : MIRROR_SINGLE0;
        break;
    case 180: c->reg[0] = v & 7; break;
    case 2: c->reg[0] = v; break;
    case 3: c->reg[0] = v; break;
    case 7:
        c->reg[0] = v;
        c->mirroring = (v & 0x10) ? MIRROR_SINGLE1 : MIRROR_SINGLE0;
        break;
    }
    mapper_remap(c);
}

#define WRAP(c, x) ((c)->chr_mask ? ((x) & (c)->chr_mask) : ((x) % (c)->chr_size))

static size_t chr_off(const Cart *c, u16 a)
{
    a &= 0x1FFF;
    switch (c->mapper) {
    case 1:
        if (c->reg[2] & 0x10) {                  /* 2 bancos de 4 KB */
            u32 b = a < 0x1000 ? c->reg[3] : c->reg[4];
            size_t x = (size_t)b << 12 | (a & 0xFFF); return WRAP(c, x);
        }
        { size_t x = (size_t)(c->reg[3] & ~1u) << 12 | a; return WRAP(c, x); }
    case 3:
        { size_t x = (size_t)c->reg[0] << 13 | a; return WRAP(c, x); }
    case 11: case 66: case 79: case 87: case 140:
        { size_t x = (size_t)c->reg[1] << 13 | a; return WRAP(c, x); }
    case 34:
        if (c->chr_is_ram) { size_t x = a; return WRAP(c, x); }
        { size_t x = (size_t)(a < 0x1000 ? c->reg[1] : c->reg[2]) << 12 | (a & 0xFFF); return WRAP(c, x); }
    case 9: case 10: {
        u32 b = a < 0x1000 ? (c->reg[5] == 0 ? c->reg[1] : c->reg[2]) : (c->reg[6] == 0 ? c->reg[3] : c->reg[4]);
        size_t x = (size_t)b << 12 | (a & 0xFFF); return WRAP(c, x);
    }
    case 69: {
        size_t x = ((size_t)c->reg[1 + ((a >> 10) & 7)] << 10) | (a & 0x3FF); return WRAP(c, x);
    }
    case 4: case 206: case 118: case 119: {
        size_t x = (mmc3_chr_bank(c, a) << 10) | (a & 0x3FF); return WRAP(c, x);
    }
    default:
        { size_t x = a; return WRAP(c, x); }
    }
}

u8 mapper_ppu_read(NES *n, u16 a)
{
    return n->cart.chr_page[(a >> 10) & 7][a & 0x3FF];
}

void mapper_ppu_write(NES *n, u16 a, u8 v)
{
    if (n->cart.chr_wr[(a >> 10) & 7]) n->cart.chr_page[(a >> 10) & 7][a & 0x3FF] = v;
}

/* ---- Consultas para el recompilador (con el mapeo del reset) ---- */

/* Ventana de PRG que contiene 'addr' con el mapeo que tiene la consola al arrancar */
int mapper_prg_window(const Cart *c, u16 addr, PrgWin *w)
{
    if (addr < 0x8000) return 0;
    size_t ps = c->prg_size;
    switch (c->mapper) {
    case 0: case 3: case 87:
        if (ps >= 0x8000) { *w = (PrgWin){ 0x8000, 0x8000, 0, 1 }; return 1; }
        *w = (PrgWin){ 0xC000, 0x4000, 0, 1 }; return 1;       /* addr debe venir canónica */
    case 2: case 1:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 0 };
        else               *w = (PrgWin){ 0xC000, 0x4000, (ps > 0x40000 && c->mapper == 1 ? 0x40000 : ps) - 0x4000,
                                         !(ps > 0x40000 && c->mapper == 1) };      /* SUROM: la mitad fija depende del bloque */
        return 1;
    case 11: case 34: case 66: case 79: case 140:
        *w = (PrgWin){ 0x8000, 0x8000, 0, 0 }; return 1;
    case 71:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 0 };
        else               *w = (PrgWin){ 0xC000, 0x4000, ps - 0x4000, 1 };
        return 1;
    case 180:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 1 };
        else               *w = (PrgWin){ 0xC000, 0x4000, 0, 0 };
        return 1;
    case 10:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 0 };
        else               *w = (PrgWin){ 0xC000, 0x4000, ps - 0x4000, 1 };
        return 1;
    case 9:
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, ps - 0x6000, 1 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, ps - 0x4000, 1 }; break;
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 1 }; break;
        }
        return 1;
    case 69: case 5:
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, 0x2000, 0 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, 0x4000, 0 }; break;
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 1 }; break;
        }
        return 1;
    case 4: case 206: case 118: case 119:
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, 0x2000, 0 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, ps - 0x4000, 1 }; break;
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 1 }; break;
        }
        return 1;
    case 7: *w = (PrgWin){ 0x8000, 0x8000, 0, 0 }; return 1;
    }
    return 0;
}

/* Las ROMs de 16 KB (NROM, CNROM) se ven en $8000 y en $C000: se toma $C000 como canónica */
u16 mapper_prg_canon(const Cart *c, u16 addr)
{
    if ((c->mapper == 0 || c->mapper == 3 || c->mapper == 87) && c->prg_size < 0x8000 && addr >= 0x8000 && addr < 0xC000)
        return addr | 0x4000;
    return addr;
}

/* Tamaño mínimo de ventana: las funciones recompiladas no cruzan múltiplos de esto */
size_t mapper_prg_granule(const Cart *c)
{
    switch (c->mapper) {
    case 4: case 206: case 118: case 119: case 9: case 69: case 5: return 0x2000;
    case 1: case 2: case 71: case 180: case 10: return 0x4000;
    case 7: case 11: case 34: case 66: case 79: case 140: return 0x8000;
    default: return c->prg_size >= 0x8000 ? 0x8000 : 0x4000;
    }
}
