/*
 * Mappers: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM.
 * Cada mapper guarda su estado en cart.reg[] y puede cambiar cart.mirroring.
 * Agregar uno nuevo = un caso en cada función (y en mapper_supported).
 */
#include "nes.h"

int mapper_supported(int id)
{
    return id == 0 || id == 1 || id == 2 || id == 3 || id == 4 || id == 7;
}

/* MMC1: reg[0]=desplazamiento, [1]=cuenta, [2]=control, [3]=chr0, [4]=chr1, [5]=prg, [6]=último ciclo */
void mapper_reset(NES *n)
{
    Cart *c = &n->cart;
    for (int i = 0; i < 8; i++) c->reg[i] = 0;
    c->a12_low = 0;
    c->chr_mask = (c->chr_size & (c->chr_size - 1)) == 0 ? (u32)(c->chr_size - 1) : 0;
    if (c->mapper == 1) { c->reg[2] = 0x0C; c->reg[6] = 0xFFFFFFFFu; }
    if (c->mapper == 4) {
        /* reg[0]=selección, [1..8]=R0..R7, [9]=latch IRQ, [10]=contador, [11]=recarga,
         * [12]=IRQ activo, [13]=nivel de A12, [14]=protección de RAM */
        c->reg[8] = 1; c->reg[14] = 0x80;          /* R7 = 1, RAM activa */
        c->mirroring = c->mirroring == MIRROR_FOUR ? MIRROR_FOUR : MIRROR_VERTICAL;
    }
    mapper_remap(c);
}

/* ---- MMC3 ---- */
static inline size_t banks8(const Cart *c) { return c->prg_size >> 13; }

static void mmc3_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    int odd = a & 1;
    switch ((a >> 13) & 3) {
    case 0:
        if (!odd) c->reg[0] = v;
        else c->reg[1 + (c->reg[0] & 7)] = v;
        break;
    case 1:
        if (!odd) { if (c->mirroring != MIRROR_FOUR) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; }
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
    if (c->mapper != 4) return;
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
        u32 mode = (c->reg[2] >> 2) & 3, bank = c->reg[5] & 0x0F;
        if (mode < 2) return (long)prg_at(c, (bank & ~1u) + (a >= 0xC000), a);
        if (mode == 2) return (long)(a < 0xC000 ? prg_at(c, 0, a) : prg_at(c, bank, a));
        return (long)(a < 0xC000 ? prg_at(c, bank, a) : prg_at(c, banks16(c) - 1, a));
    }
    case 2:
        return (long)(a < 0xC000 ? prg_at(c, c->reg[0], a) : prg_at(c, banks16(c) - 1, a));
    case 4: return (long)mmc3_prg(c, a);
    case 7: return (long)prg_at(c, (c->reg[0] & 7) * 2 + (a >= 0xC000), a);
    default:                                     /* 0 y 3: NROM-128 se refleja; -256 usa los 2 bancos */
        return (long)prg_at(c, a >> 14 & 1, a);
    }
}

static size_t chr_off(const Cart *c, u16 a);

void mapper_remap(Cart *c)
{
    for (int i = 0; i < 4; i++) c->prg_map[i] = (size_t)prg_offset_slow(c, (u16)(0x8000 + i * 0x2000));
    for (int i = 0; i < 8; i++) c->chr_page[i] = c->chr + chr_off(c, (u16)(i * 0x400));
}

long mapper_prg_offset(const Cart *c, u16 a)
{
    if (a < 0x8000) return -1;
    return (long)(c->prg_map[(a >> 13) & 3] + (a & 0x1FFF));
}

u8 mapper_cpu_read(NES *n, u16 a)
{
    Cart *c = &n->cart;
    if (a < 0x6000) return 0;                    /* bus abierto (aprox.) */
    if (a < 0x8000) return (c->mapper == 4 && !(c->reg[14] & 0x80)) ? 0 : c->prg_ram[a & 0x1FFF];
    return c->prg[mapper_prg_offset(c, a)];
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

void mapper_cpu_write(NES *n, u16 a, u8 v)
{
    Cart *c = &n->cart;
    if (a < 0x6000) return;
    if (a >= 0x8000) c->map_ver++;
    if (a < 0x8000) {
        if (c->mapper == 4 && (c->reg[14] & 0xC0) != 0x80) return;
        c->prg_ram[a & 0x1FFF] = v; return;
    }
    switch (c->mapper) {
    case 4: mmc3_write(n, a, v); break;
    case 1: mmc1_write(n, a, v); break;
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
    case 4: {
        int slot = (a >> 10) & 7;
        if (c->reg[0] & 0x80) slot ^= 4;
        size_t b;
        if (slot < 4) b = (c->reg[1 + (slot >> 1)] & 0xFE) | (slot & 1);
        else b = c->reg[1 + 2 + (slot - 4)];
        { size_t x = (b << 10) | (a & 0x3FF); return WRAP(c, x); }
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
    if (n->cart.chr_is_ram) n->cart.chr_page[(a >> 10) & 7][a & 0x3FF] = v;
}

/* ---- Consultas para el recompilador (con el mapeo del reset) ---- */

/* Ventana de PRG que contiene 'addr' con el mapeo que tiene la consola al arrancar */
int mapper_prg_window(const Cart *c, u16 addr, PrgWin *w)
{
    if (addr < 0x8000) return 0;
    size_t ps = c->prg_size;
    switch (c->mapper) {
    case 0: case 3:
        if (ps >= 0x8000) { *w = (PrgWin){ 0x8000, 0x8000, 0, 1 }; return 1; }
        *w = (PrgWin){ 0xC000, 0x4000, 0, 1 }; return 1;       /* addr debe venir canónica */
    case 2: case 1:
        if (addr < 0xC000) *w = (PrgWin){ 0x8000, 0x4000, 0, 0 };
        else               *w = (PrgWin){ 0xC000, 0x4000, ps - 0x4000, 1 };
        return 1;
    case 4:
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
    if ((c->mapper == 0 || c->mapper == 3) && c->prg_size < 0x8000 && addr >= 0x8000 && addr < 0xC000)
        return addr | 0x4000;
    return addr;
}

/* Tamaño mínimo de ventana: las funciones recompiladas no cruzan múltiplos de esto */
size_t mapper_prg_granule(const Cart *c)
{
    switch (c->mapper) {
    case 4: return 0x2000;
    case 1: case 2: return 0x4000;
    case 7: return 0x8000;
    default: return c->prg_size >= 0x8000 ? 0x8000 : 0x4000;
    }
}
