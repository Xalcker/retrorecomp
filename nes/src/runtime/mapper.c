/*
 * Mappers: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color Dreams, 34 BNROM/NINA-001,
 * 66 GxROM, 71 Camerica, 79 NINA-03/06, 87, 140, 180 (UNROM al revés), 206 (DxROM), 9 (MMC2), 10 (MMC4),
 * 69 (FME-7, con el audio 5B) 118 (TxSROM) y 119 (TQROM). 9, 10, 69 y 118 se probaron solo con cartuchos sintéticos o un juego.
 * 13 (CPROM), 75 (VRC1), 95 (Namcot 3425), 115 (Kasheng), 184 (Sunsoft 1), 210 (Namco 175/340), 18 (Jaleco SS88006),
 * 21, 22, 23 y 25 (VRC2/VRC4, con el IRQ del VRC4), 24 y 26 (VRC6, con su audio), 85 (VRC7, con su audio FM) y 19 (Namco 163, con su audio; sin CHR ROM como nametable).
 * Cada mapper guarda su estado en cart.reg[] y puede cambiar cart.mirroring.
 * Agregar uno nuevo = un caso en cada función (y en mapper_supported).
 */
#include <string.h>
#include <stdlib.h>
#include "nes.h"

int mapper_supported(int id)
{
    switch (id) {
    case 0: case 1: case 2: case 3: case 4: case 7: case 11: case 34: case 66: case 71: case 79:
    case 87: case 140: case 180: case 206: case 9: case 10: case 69: case 118: case 119: case 5:
    case 13: case 75: case 95: case 115: case 184: case 210: case 18: case 23: case 19:
    case 21: case 22: case 25: case 24: case 26: case 85: return 1;
    }
    return 0;
}

/* Mappers con nametables propios (MIRROR_CUSTOM): se parte del espejado de la cabecera */
static void nt_from_header(Cart *c)
{
    if (c->mirroring != MIRROR_CUSTOM) {
        int v = c->mirroring == MIRROR_VERTICAL || c->mirroring == MIRROR_FOUR;
        for (int i = 0; i < 4; i++) c->nt_map[i] = v ? i & 1 : i >> 1;
    }
    c->mirroring = MIRROR_CUSTOM;
}

/* MMC1: reg[0]=desplazamiento, [1]=cuenta, [2]=control, [3]=chr0, [4]=chr1, [5]=prg, [6]=último ciclo */
void mapper_reset(NES *n)
{
    Cart *c = &n->cart;
    for (int i = 0; i < 24; i++) c->reg[i] = 0;
    c->a12_low = 0;
    c->chr_mask = (c->chr_size & (c->chr_size - 1)) == 0 ? (u32)(c->chr_size - 1) : 0;
    if (MAPPER(c) == 1) { c->reg[2] = 0x0C; c->reg[6] = 0xFFFFFFFFu; }
    c->irq_cycle = ~0ull; c->irq_t0 = 0;
    c->mmc5 = MAPPER(c) == 5;
    if (CART_MMC5(c)) {
        memset(c->m5, 0, sizeof c->m5);
        c->m5[0x00] = 3; c->m5[0x14] = 0x7F; c->m5[0x15] = 0x7F; c->m5[0x16] = 0x7F; c->m5[0x17] = 0xFF;   /* PRG modo 3; últimos bancos */
        c->m5[0x01] = 3;
        c->m5_spr16 = c->m5_inframe = c->m5_pending = c->m5_counter = 0;
        c->exgfx = 0;
        c->mirroring = MIRROR_CUSTOM;
        for (int i = 0; i < 4; i++) c->nt_map[i] = i & 1;
    }
    { const char *alt = getenv("NES_MMC3_ALT"); c->mmc3_alt = alt && *alt == '1'; }
    c->mmc3 = MAPPER(c) == 4 || MAPPER(c) == 118 || MAPPER(c) == 119 || MAPPER(c) == 115;
    c->chr_hook = MAPPER(c) == 9 || MAPPER(c) == 10;
    if (CART_CHRHOOK(c)) { c->reg[5] = c->reg[6] = 1; }                /* latches en FE */
    if (MAPPER(c) == 69) { c->reg[0] = 0; c->reg[16] = 0; }
    c->low_regs = MAPPER(c) == 34 || MAPPER(c) == 79 || MAPPER(c) == 87 || MAPPER(c) == 140 || MAPPER(c) == 5 ||
                  MAPPER(c) == 19 || MAPPER(c) == 184 || MAPPER(c) == 115;
    if (MAPPER(c) == 19 || MAPPER(c) == 95) nt_from_header(c);
    if (MAPPER(c) == 75 || MAPPER(c) == 18 || MAPPER(c) == 19) { c->reg[1] = 1; c->reg[2] = 2; }
    if (MAPPER(c) == 210) { c->reg[9] = 1; c->reg[10] = 2; }
    if (CART_VRC(c)) { c->reg[1] = 1; c->reg[14] = 341; }
    if (CART_VRC6(c) || CART_VRC7(c)) c->reg[14] = 341;
    if (MAPPER(c) == 4 || MAPPER(c) == 206 || MAPPER(c) == 118 || MAPPER(c) == 119 || MAPPER(c) == 95 || MAPPER(c) == 115) {
        /* reg[0]=selección, [1..8]=R0..R7, [9]=latch IRQ, [10]=contador, [11]=recarga,
         * [12]=IRQ activo, [13]=nivel de A12, [14]=protección de RAM */
        c->reg[8] = 1; c->reg[14] = 0x80;          /* R7 = 1, RAM activa */
        if (MAPPER(c) == 4 || MAPPER(c) == 115) c->mirroring = c->mirroring == MIRROR_FOUR ? MIRROR_FOUR : MIRROR_VERTICAL;
        if (MAPPER(c) == 118) c->mirroring = MIRROR_CUSTOM;
    }
    mapper_remap(c);
}

/* ---- MMC3 ---- */
static inline size_t banks8(const Cart *c) { return c->prg_size >> 13; }

static void mmc3_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    int odd = a & 1;
    if (MAPPER(c) == 206 || MAPPER(c) == 95) {   /* DxROM: solo $8000/$8001, sin inversión, espejado ni IRQ */
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
        if (!odd) { if ((MAPPER(c) == 4 || MAPPER(c) == 119 || MAPPER(c) == 115) && c->mirroring != MIRROR_FOUR) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; }
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
    if (!CART_MMC3(c)) return;
    if (!bit) {
        if (c->reg[13]) { c->reg[13] = 0; c->a12_low = n->ppu.clock; }
        return;
    }
    if (c->reg[13]) return;
    c->reg[13] = 1;
    if (n->ppu.clock - c->a12_low < 10) return;   /* filtro: menos de ~3 ciclos de CPU en bajo */
    int reload = c->reg[10] == 0 || c->reg[11], forced = c->reg[11];
    if (reload) c->reg[10] = c->reg[9];
    else c->reg[10]--;
    c->reg[11] = 0;
    /* Revisión nueva (la de casi todos los juegos): hay IRQ siempre que el contador queda en 0. Revisión vieja
     * (MMC3 rev. A, MMC6; NES_MMC3_ALT=1): solo si baja a 0 contando o por una recarga forzada con $C001. */
    if (c->reg[10] == 0 && c->reg[12] && (!c->mmc3_alt || !reload || forced)) n->irq_line |= 1;
}

static inline size_t banks16(const Cart *c) { return c->prg_size >> 14; }

static size_t prg_at(const Cart *c, size_t bank16, u16 off)
{
    return ((bank16 % banks16(c)) << 14) | (off & 0x3FFF);
}

static size_t prg8(const Cart *c, size_t bank, u16 a)
{
    size_t nb = banks8(c);
    return ((bank % nb) << 13) | (a & 0x1FFF);
}

/* Namcot 3425 (95): el bit 5 de los bancos de CHR R0 y R1 elige la página de CIRAM de cada mitad */
static void mmc95_mirror(Cart *c)
{
    for (int i = 0; i < 4; i++) c->nt_map[i] = (c->reg[i < 2 ? 1 : 2] >> 5) & 1;
}

/* Namco 163 (19): $C000-$D800 eligen la nametable (la página de CIRAM es el bit 0; CHR ROM como nametable no se hace) */
static void n163_nt(Cart *c)
{
    for (int i = 0; i < 4; i++) c->nt_map[i] = c->reg[11 + i] & 1;
}

/* Namco 163: contador de 15 bits que sube un paso por ciclo de CPU y levanta el IRQ al llegar a $7FFF.
 * reg[15] = valor en irq_t0, reg[16] = habilitado */
static u32 n163_count(const Cart *c, u64 now)
{
    u64 v = c->reg[15];
    if (c->reg[16]) v += now - c->irq_t0;
    return v > 0x7FFF ? 0x7FFF : (u32)v;
}

static void n163_irq_write(NES *n, int high, u8 v)
{
    Cart *c = &n->cart;
    u32 cur = n163_count(c, n->cyc);
    if (high) { cur = (cur & 0xFF) | ((u32)(v & 0x7F) << 8); c->reg[16] = v >> 7; }
    else cur = (cur & 0x7F00) | v;
    c->reg[15] = cur; c->irq_t0 = n->cyc;
    c->irq_cycle = (c->reg[16] && cur < 0x7FFF) ? n->cyc + (0x7FFF - cur) : ~0ull;
    n->irq_line &= ~1;
}

/* Jaleco SS88006 (18): contador de 16 bits que baja un paso por ciclo de CPU, pero solo se mueven los 4, 8, 12 o 16 bits
 * bajos (según $F001); el IRQ sale al desbordarse esa parte. reg[11] = recarga, reg[12] = control, reg[13] = contador en irq_t0 */
static u32 jaleco_mask(const Cart *c)
{
    u32 m = c->reg[12];
    return (m & 8) ? 0xF : (m & 4) ? 0xFF : (m & 2) ? 0xFFF : 0xFFFF;
}

static void jaleco_sync(Cart *c, u64 now)            /* pasa lo transcurrido al valor guardado */
{
    if (c->reg[12] & 1) {
        u32 m = jaleco_mask(c), low = c->reg[13] & m;
        u32 el = (u32)((now - c->irq_t0) % ((u64)m + 1));
        c->reg[13] = (c->reg[13] & ~m) | ((low - el) & m);
    }
    c->irq_t0 = now;
}

static void jaleco_arm(Cart *c, u64 now)
{
    c->irq_t0 = now;
    c->irq_cycle = (c->reg[12] & 1) ? now + (c->reg[13] & jaleco_mask(c)) + 1 : ~0ull;
}

/* VRC4: contador de 8 bits que sube con cada reloj; al pasar de $FF se recarga con el latch y levanta el IRQ. En modo
 * ciclos el reloj es cada ciclo de CPU; en modo línea, un prescaler baja 3 por ciclo y da un reloj (y suma 341) al llegar
 * a 0 o menos: 3 relojes cada 341 ciclos, uno por línea de PPU. reg[11] = latch, reg[12] = control (bit 0 = habilitar al
 * reconocer, bit 1 = habilitado, bit 2 = modo ciclos), reg[13] = contador y reg[14] = prescaler en irq_t0. */
static u64 vrc_clocks(Cart *c, u64 k)              /* relojes en k ciclos desde irq_t0; deja el prescaler al día */
{
    if (c->reg[12] & 4) return k;
    u64 n = 3 * (k / 341);                          /* cada 341 ciclos el prescaler vuelve al mismo valor */
    int p = (int)c->reg[14];
    for (u64 i = k % 341; i > 0; i--) { p -= 3; if (p <= 0) { p += 341; n++; } }
    c->reg[14] = (u32)p;
    return n;
}

static void vrc_sync(Cart *c, u64 now)
{
    if (c->reg[12] & 2) {
        u64 n = vrc_clocks(c, now - c->irq_t0);
        u32 cnt = c->reg[13], to_wrap = 0x100 - cnt;
        if (n < to_wrap) cnt += (u32)n;
        else cnt = c->reg[11] + (u32)((n - to_wrap) % (0x100 - c->reg[11]));
        c->reg[13] = cnt;
    }
    c->irq_t0 = now;
}

static void vrc_arm(Cart *c, u64 now)              /* irq_cycle: el ciclo siguiente al del reloj que pasa de $FF */
{
    c->irq_t0 = now;
    if (!(c->reg[12] & 2)) { c->irq_cycle = ~0ull; return; }
    u32 need = 0x100 - c->reg[13];
    u64 cyc;
    if (c->reg[12] & 4) cyc = need;
    else {                                          /* hasta el reloj número need (a lo más 256 vueltas) */
        int p = (int)c->reg[14];
        cyc = 0;
        for (u32 i = 0; i < need; i++) { u64 d = (u64)(p + 2) / 3; cyc += d; p = p - 3 * (int)d + 341; }
    }
    c->irq_cycle = now + cyc;
}

static void vrc_irq_write(NES *n, int sel, u8 v)
{
    Cart *c = &n->cart;
    u64 t = n->cyc + 1;                             /* el reloj del ciclo de la escritura va antes de ella */
    vrc_sync(c, t);
    switch (sel) {
    case 0: c->reg[11] = (c->reg[11] & 0xF0) | (v & 15); break;
    case 1: c->reg[11] = (c->reg[11] & 0x0F) | ((u32)(v & 15) << 4); break;
    case 2:
        c->reg[12] = v & 7;
        if (v & 2) { c->reg[13] = c->reg[11]; c->reg[14] = 341; }
        n->irq_line &= ~1;
        break;
    case 3: c->reg[12] = (c->reg[12] & ~2u) | ((c->reg[12] & 1) << 1); n->irq_line &= ~1; break;
    }
    vrc_arm(c, t);
}

/* VRC6: el mismo IRQ, pero el latch se escribe entero en $F000, el control en $F001 y el reconocimiento en $F002 */
static void vrc6_irq_write(NES *n, int sel, u8 v)
{
    Cart *c = &n->cart;
    if (sel == 0) { vrc_sync(c, n->cyc + 1); c->reg[11] = v; vrc_arm(c, n->cyc + 1); }
    else if (sel < 3) vrc_irq_write(n, sel + 1, v);
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
    switch (MAPPER(c)) {
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
    case 4: case 206: case 118: case 119: case 95: return (long)mmc3_prg(c, a);
    case 115:
        if (c->reg[15] & 0x80) {                 /* modo NROM: bancos de 16 o 32 KB */
            int slot = (a >> 13) & 3;
            size_t base = (c->reg[15] & 0x40) ? (size_t)(c->reg[15] & 0x0E) << 1 : (size_t)(c->reg[15] & 0x0F) << 1;
            return (long)prg8(c, (c->reg[15] & 0x40) ? base + slot : base + (slot & 1), a);
        }
        return (long)mmc3_prg(c, a);
    case 75: case 210: case 18: case 19: {
        int slot = (a >> 13) & 3;
        size_t bank = slot == 3 ? banks8(c) - 1
                    : MAPPER(c) == 75 ? c->reg[slot] & 0x1F
                    : MAPPER(c) == 210 ? c->reg[8 + slot] & 0x3F
                    : MAPPER(c) == 18 ? c->reg[slot] : c->reg[slot] & 0x3F;
        return (long)prg8(c, bank, a);
    }
    case 85: {                                   /* VRC7: tres bancos de 8 KB y el último fijo */
        int slot = (a >> 13) & 3;
        return (long)prg8(c, slot == 3 ? banks8(c) - 1 : c->reg[slot], a);
    }
    case 24: case 26: {                          /* VRC6: 16 KB en $8000, 8 KB en $C000 y el último fijo */
        int slot = (a >> 13) & 3;
        size_t bank = slot == 3 ? banks8(c) - 1 : slot == 2 ? c->reg[1] : c->reg[0] * 2 + slot;
        return (long)prg8(c, bank, a);
    }
    case 21: case 22: case 23: case 25: {        /* VRC2/4: el bit 1 de $9002 intercambia $8000 y $C000 */
        int slot = (a >> 13) & 3, swap = c->reg[2] & 1;
        size_t nb = banks8(c);
        size_t bank = slot == 3 ? nb - 1 : slot == 1 ? c->reg[1] & 0x1F
                    : (slot == 0) != swap ? c->reg[0] & 0x1F : nb - 2;
        return (long)prg8(c, bank, a);
    }
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
    if (CART_MMC5(c)) { mmc5_remap_prg(c); mmc5_remap_chr(c); return; }
    for (int i = 0; i < 4; i++) {
        c->prg_map[i] = (size_t)prg_offset_slow(c, (u16)(0x8000 + i * 0x2000));
        c->prg_ptr[i] = c->prg + c->prg_map[i];
    }
    for (int i = 0; i < 8; i++) c->chr_page_spr[i] = NULL;
    for (int i = 0; i < 8; i++) {
        c->chr_page[i] = c->chr + chr_off(c, (u16)(i * 0x400));
        c->chr_wr[i] = c->chr_is_ram;
        if (MAPPER(c) == 119 && (mmc3_chr_bank(c, (u16)(i * 0x400)) & 0x40)) {    /* TQROM: el bit 6 elige la RAM */
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
    if (CART_MMC5(c)) {
        if (a >= 0x5000 && a < 0x6000) return mmc5_read(n, a);
        if (a >= 0x6000 && a < 0x8000) return mmc5_ram(c, c->m5[0x13])[a & 0x1FFF];
        if (a >= 0x8000) {
            u8 v = c->prg_ptr[(a >> 13) & 3][a & 0x1FFF];
            if (a < 0xC000 && n->apu.m5_pcm_read) apu_mmc5_pcm(n, v);          /* PCM en modo lectura */
            return v;
        }
    }
    if (MAPPER(c) == 19 && a >= 0x4800 && a < 0x5000) return apu_n163_data(n, 0, 0);     /* Namco 163: RAM del audio */
    if (MAPPER(c) == 19 && a >= 0x5000 && a < 0x6000) {         /* Namco 163: contador del IRQ */
        u32 cur = n163_count(c, n->cyc);
        return a < 0x5800 ? (u8)cur : (u8)((cur >> 8) | (c->reg[16] << 7));
    }
    if (a < 0x6000) return 0;                    /* bus abierto (aprox.) */
    if (a < 0x8000) {
        if (CART_MMC3(c) && !(c->reg[14] & 0x80)) return 0;
        if (MAPPER(c) == 69 && !(c->reg[13] & 0x40)) {           /* FME-7: ROM en $6000 (bit 6 = 0) */
            size_t nb = banks8(c);
            return c->prg[((c->reg[13] & 0x3F) % nb) << 13 | (a & 0x1FFF)];
        }
        if (MAPPER(c) == 69 && !(c->reg[13] & 0x80)) return 0;    /* RAM desactivada */
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
    if (side || MAPPER(c) == 10) {
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
    if (a >= 0xC000) { apu_5b_write(n, a >= 0xE000, v); return; }     /* audio del 5B: $C000 elige registro, $E000 lo escribe */
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
static int low_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    switch (MAPPER(c)) {
    case 184: if (a >= 0x6000) { c->reg[0] = v; return 1; } break;
    case 115: if (a >= 0x6000) { c->reg[15 + (a & 1)] = v; return 1; } break;
    case 19:
        if (a >= 0x5000 && a < 0x6000) { n163_irq_write(n, a >= 0x5800, v); return 1; }
        if (a >= 0x4800 && a < 0x5000) { apu_n163_data(n, 1, v); return 1; }
        break;
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
    if (CART_MMC5(c)) {
        int ram_ok = (c->m5[0x02] & 3) == 2 && (c->m5[0x03] & 3) == 1;           /* protección de PRG-RAM */
        if (a >= 0x5000 && a < 0x6000) { mmc5_write(n, a, v); c->map_ver++; return; }
        if (a >= 0x6000 && a < 0x8000) { if (ram_ok) mmc5_ram(c, c->m5[0x13])[a & 0x1FFF] = v; return; }
        if (a >= 0x8000) {                                                        /* RAM mapeada en $8000-$DFFF */
            int s = (a >> 13) & 3;
            if (c->prg_map[s] == (size_t)-1 && ram_ok) c->prg_ptr[s][a & 0x1FFF] = v;
        }
        return;
    }
    if (CART_LOWREGS(c) && a < 0x8000 && low_write(n, a, v)) { c->map_ver++; mapper_remap(c); return; }
    if (a < 0x6000) return;
    if (a >= 0x8000) c->map_ver++;
    if (a < 0x8000) {
        if (CART_MMC3(c) && (c->reg[14] & 0xC0) != 0x80) return;
        if (MAPPER(c) == 69 && (!(c->reg[13] & 0x40) || !(c->reg[13] & 0x80))) return;      /* solo con RAM elegida y activa */
        c->prg_ram[a & 0x1FFF] = v; return;
    }
    switch (MAPPER(c)) {
    case 4: case 206: case 119: case 115: mmc3_write(n, a, v); break;
    case 118: mmc3_write(n, a, v); mmc118_mirror(c); break;
    case 95: mmc3_write(n, a, v); mmc95_mirror(c); break;
    case 13: c->reg[0] = v & 3; break;
    case 75:
        switch ((a >> 12) & 7) {
        case 0: c->reg[0] = v & 0x1F; break;
        case 1: c->reg[5] = v; if (c->mirroring != MIRROR_FOUR) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; break;
        case 2: c->reg[1] = v & 0x1F; break;
        case 4: c->reg[2] = v & 0x1F; break;
        case 6: c->reg[3] = v & 15; break;
        case 7: c->reg[4] = v & 15; break;
        }
        break;
    case 210:
        switch (a & 0xF800) {
        case 0x8000: case 0x8800: case 0x9000: case 0x9800: case 0xA000: case 0xA800: case 0xB000: case 0xB800:
            c->reg[(a >> 11) & 7] = v; break;
        case 0xE000:
            c->reg[8] = v & 0x3F;                /* Namco 340: los bits 6-7 eligen el espejado */
            { static const u8 m[4] = { MIRROR_SINGLE0, MIRROR_VERTICAL, MIRROR_SINGLE1, MIRROR_HORIZONTAL };
              if (c->mirroring != MIRROR_FOUR) c->mirroring = m[v >> 6]; }
            break;
        case 0xE800: c->reg[9] = v & 0x3F; break;
        case 0xF000: c->reg[10] = v & 0x3F; break;
        }
        break;
    case 18: {
        int sub = a & 3, sh = sub & 1 ? 4 : 0, grp = (a >> 12) & 7;
        switch (grp) {
        case 0: case 1: {                        /* PRG: $8000/$8002/$9000 = nibble bajo, +1 = alto */
            if (grp == 1 && sub >= 2) break;
            int r = grp * 2 + (sub >> 1);
            c->reg[r] = (c->reg[r] & ~(0xFu << sh)) | ((u32)(v & 15) << sh);
            break;
        }
        case 2: case 3: case 4: case 5: {        /* CHR: dos bancos por registro, cada uno en dos nibbles */
            int r = 3 + (grp - 2) * 2 + (sub >> 1);
            c->reg[r] = (c->reg[r] & ~(0xFu << sh)) | ((u32)(v & 15) << sh);
            break;
        }
        case 6:                                  /* $E000-$E003: recarga del IRQ, un nibble por dirección */
            c->reg[11] = (c->reg[11] & ~(0xFu << (sub * 4))) | ((u32)(v & 15) << (sub * 4));
            break;
        case 7:
            if (sub == 0) { c->reg[13] = c->reg[11]; jaleco_arm(c, n->cyc); n->irq_line &= ~1; }
            else if (sub == 1) { jaleco_sync(c, n->cyc); c->reg[12] = v; jaleco_arm(c, n->cyc); n->irq_line &= ~1; }
            else if (sub == 2) c->mirroring = (v & 3) == 0 ? MIRROR_HORIZONTAL : (v & 3) == 1 ? MIRROR_VERTICAL :
                                              (v & 3) == 2 ? MIRROR_SINGLE0 : MIRROR_SINGLE1;
            break;
        }
        break;
    }
    case 21: case 22: case 23: case 25: {
        /* VRC2/VRC4: las dos líneas de registro llegan por distintas patas según el cartucho (bit 0, bit 1):
         * 21 = A1,A2 (VRC4a, submapper 1) o A6,A7 (VRC4c, 2); 22 = A1,A0 (VRC2a); 23 = A0,A1 (VRC4f, 1; VRC2b, 3) o
         * A2,A3 (VRC4e, 2); 25 = A1,A0 (VRC4b, 1; VRC2c, 3) o A3,A2 (VRC4d, 2). Sin submapper se juntan las dos con un
         * OR (una escritura que solo es de una variante cae también en otro registro de la otra). */
        int s = c->submapper;
        int sel = MAPPER(c) == 21 ? ((s == 2 ? 0 : a >> 1) | (s == 1 ? 0 : a >> 6)) & 3
                : MAPPER(c) == 23 ? ((s == 2 ? 0 : a) | (s == 1 || s == 3 ? 0 : a >> 2)) & 3
                : MAPPER(c) == 22 ? ((a >> 1) & 1) | ((a & 1) << 1)
                : ((s == 2 ? 0 : (a >> 1) & 1) | (s == 2 ? 0 : (a & 1) << 1))
                  | ((s == 1 || s == 3 ? 0 : (a >> 3) & 1) | (s == 1 || s == 3 ? 0 : ((a >> 2) & 1) << 1));
        switch (a & 0xF000) {
        case 0x8000: c->reg[0] = v & 0x1F; break;
        case 0xA000: c->reg[1] = v & 0x1F; break;
        case 0x9000:
            if (sel < 2) { if (c->mirroring != MIRROR_FOUR) c->mirroring = (v & 3) == 0 ? MIRROR_VERTICAL : (v & 3) == 1 ? MIRROR_HORIZONTAL :
                                                                           (v & 3) == 2 ? MIRROR_SINGLE0 : MIRROR_SINGLE1; }
            else c->reg[2] = (v >> 1) & 1;
            break;
        case 0xB000: case 0xC000: case 0xD000: case 0xE000: {
            int r = 3 + (((a >> 12) - 0xB) * 2 + (sel >> 1));
            if (sel & 1) c->reg[r] = (c->reg[r] & 0x0F) | ((u32)(v & 0x1F) << 4);
            else c->reg[r] = (c->reg[r] & 0x1F0) | (v & 0x0F);
            break;
        }
        case 0xF000: if (MAPPER(c) != 22 && c->submapper != 3) vrc_irq_write(n, sel, v); break;   /* el VRC2 no tiene IRQ */
        }
        break;
    }
    case 19:
        switch (a & 0xF800) {
        case 0x8000: case 0x8800: case 0x9000: case 0x9800: case 0xA000: case 0xA800: case 0xB000: case 0xB800:
            c->reg[3 + ((a >> 11) & 7)] = v; break;
        case 0xC000: case 0xC800: case 0xD000: case 0xD800: c->reg[11 + ((a >> 11) & 3)] = v; n163_nt(c); break;
        case 0xE000: c->reg[0] = v & 0x3F; apu_n163_off(n, (v >> 6) & 1); break;
        case 0xE800: c->reg[1] = v & 0x3F; break;
        case 0xF000: c->reg[2] = v & 0x3F; break;
        case 0xF800: apu_n163_addr(n, v); break;
        }
        break;
    case 85: {
        /* VRC7: el segundo registro de cada par está en A3 (VRC7b, submapper 1) o en A4 (VRC7a, submapper 2); sin
         * submapper, cualquiera de las dos. El audio: $9010 elige el registro del OPLL y $9030 lo escribe. */
        int s = c->submapper;
        int hi = ((s == 2 ? 0 : a >> 3) | (s == 1 ? 0 : a >> 4)) & 1;
        switch (a & 0xF000) {
        case 0x8000: c->reg[hi] = v; break;
        case 0x9000:
            if (a & 0x10) apu_vrc7_write(n, (a >> 5) & 1, v);
            else c->reg[2] = v;
            break;
        case 0xA000: case 0xB000: case 0xC000: case 0xD000: c->reg[3 + ((a >> 12) - 0xA) * 2 + hi] = v; break;
        case 0xE000:
            if (!hi) {
                static const u8 m[4] = { MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE0, MIRROR_SINGLE1 };
                if (c->mirroring != MIRROR_FOUR) c->mirroring = m[v & 3];
                apu_vrc7_off(n, (v >> 6) & 1);
            } else vrc6_irq_write(n, 0, v);                       /* latch */
            break;
        case 0xF000: vrc6_irq_write(n, hi ? 2 : 1, v); break;     /* control / reconocimiento */
        }
        break;
    }
    case 24: case 26: {                          /* VRC6: el 26 cruza A0 y A1 */
        int sel = MAPPER(c) == 24 ? a & 3 : ((a >> 1) & 1) | ((a & 1) << 1);
        switch (a & 0xF000) {
        case 0x8000: c->reg[0] = v; break;
        case 0x9000: case 0xA000: case 0xB000:
            if ((a & 0xF000) == 0xB000 && sel == 3) {   /* $B003: espejado (modo de bancos de CHR 0, el habitual) */
                static const u8 m[4] = { MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE0, MIRROR_SINGLE1 };
                c->reg[2] = v;
                if (c->mirroring != MIRROR_FOUR) c->mirroring = m[(v >> 2) & 3];
            } else apu_vrc6_write(n, (u16)((a & 0xF000) | sel), v);
            break;
        case 0xC000: c->reg[1] = v; break;
        case 0xD000: c->reg[3 + sel] = v; break;
        case 0xE000: c->reg[7 + sel] = v; break;
        case 0xF000: vrc6_irq_write(n, sel, v); break;
        }
        break;
    }
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
    switch (MAPPER(c)) {
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
    case 4: case 206: case 118: case 119: case 95: {
        size_t x = (mmc3_chr_bank(c, a) << 10) | (a & 0x3FF); return WRAP(c, x);
    }
    case 115: {
        size_t x = ((mmc3_chr_bank(c, a) | ((size_t)(c->reg[16] & 1) << 8)) << 10) | (a & 0x3FF); return WRAP(c, x);
    }
    case 13: {                                   /* CPROM: 4 KB fijos en $0000 y uno de 4 en $1000 (16 KB de RAM) */
        size_t x = a < 0x1000 ? a : ((size_t)(c->reg[0] & 3) << 12) | (a & 0xFFF); return WRAP(c, x);
    }
    case 75: {
        u32 b = a < 0x1000 ? (((c->reg[5] >> 1) & 1) << 4) | (c->reg[3] & 15) : (((c->reg[5] >> 2) & 1) << 4) | (c->reg[4] & 15);
        size_t x = (size_t)b << 12 | (a & 0xFFF); return WRAP(c, x);
    }
    case 184: {
        u32 b = a < 0x1000 ? c->reg[0] & 7 : ((c->reg[0] >> 4) & 7) | 4;
        size_t x = (size_t)b << 12 | (a & 0xFFF); return WRAP(c, x);
    }
    case 210: case 18: case 23: case 19: case 21: case 25: case 24: case 26: case 85: {
        int base = MAPPER(c) == 210 ? 0 : 3;
        size_t x = ((size_t)c->reg[base + ((a >> 10) & 7)] << 10) | (a & 0x3FF); return WRAP(c, x);
    }
    case 22: {                                   /* VRC2a: el bit 0 del banco de CHR no se conecta */
        size_t x = ((size_t)(c->reg[3 + ((a >> 10) & 7)] >> 1) << 10) | (a & 0x3FF); return WRAP(c, x);
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
    switch (MAPPER(c)) {
    case 0: case 3: case 87: case 13: case 184:
        if (ps >= 0x8000) { *w = (PrgWin){ 0x8000, 0x8000, 0, 1 }; return 1; }
        *w = (PrgWin){ 0xC000, 0x4000, 0, 1 }; return 1;       /* addr debe venir canónica */
    case 2: case 1:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 0 };
        else               *w = (PrgWin){ 0xC000, 0x4000, (ps > 0x40000 && MAPPER(c) == 1 ? 0x40000 : ps) - 0x4000,
                                         !(ps > 0x40000 && MAPPER(c) == 1) };      /* SUROM: la mitad fija depende del bloque */
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
    case 21: case 22: case 23: case 25:
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, 0x2000, 0 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, ps - 0x4000, 0 }; break;      /* el bit 1 de $9002 lo intercambia con $8000 */
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 1 }; break;
        }
        return 1;
    case 115:                                    /* el modo NROM de $6000 puede cambiar todas las ventanas */
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, 0x2000, 0 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, ps - 0x4000, 0 }; break;
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 0 }; break;
        }
        return 1;
    case 69: case 5: case 75: case 210: case 18: case 19: case 24: case 26: case 85:
        switch ((addr >> 13) & 3) {
        case 0: *w = (PrgWin){ 0x8000, 0x2000, 0, 0 }; break;
        case 1: *w = (PrgWin){ 0xA000, 0x2000, 0x2000, 0 }; break;
        case 2: *w = (PrgWin){ 0xC000, 0x2000, 0x4000, 0 }; break;
        default: *w = (PrgWin){ 0xE000, 0x2000, ps - 0x2000, 1 }; break;
        }
        return 1;
    case 4: case 206: case 118: case 119: case 95:
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
    if ((MAPPER(c) == 0 || MAPPER(c) == 3 || MAPPER(c) == 87) && c->prg_size < 0x8000 && addr >= 0x8000 && addr < 0xC000)
        return addr | 0x4000;
    return addr;
}

/* Tamaño mínimo de ventana: las funciones recompiladas no cruzan múltiplos de esto */
size_t mapper_prg_granule(const Cart *c)
{
    switch (MAPPER(c)) {
    case 4: case 206: case 118: case 119: case 9: case 69: case 5:
    case 95: case 115: case 75: case 210: case 18: case 23: case 19: case 21: case 22: case 25: case 24: case 26: case 85: return 0x2000;
    case 1: case 2: case 71: case 180: case 10: return 0x4000;
    case 7: case 11: case 34: case 66: case 79: case 140: return 0x8000;
    default: return c->prg_size >= 0x8000 ? 0x8000 : 0x4000;
    }
}
