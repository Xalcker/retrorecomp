/*
 * Bus del 68000: el mapa de memoria del Genesis.
 *
 *   $000000-$3FFFFF  cartucho: ROM (con espejos; bancos de 512 KB con el mapper de SSF2), SRAM o
 *                    EEPROM serie (eeprom.c)
 *   $400000-$9FFFFF  expansión (Mega CD, 32X): nada
 *   $A00000-$A0FFFF  bus del Z80 (con el bus pedido): su RAM, el YM2612, el registro de banco
 *   $A10000-$A1001F  E/S: versión, mandos, serie (io.c)
 *   $A11100 / $A11200  pedido del bus del Z80 / reset del Z80
 *   $A130F1-$A130FF  control de la SRAM y bancos de SSF2;  $A14000 TMSS (con gen.tmss, el VDP solo
 *                    responde después de escribir "SEGA" ahí; si no, el 68000 se cuelga como en la consola)
 *   $C00000-$DFFFFF  VDP (vdp.c), con sus espejos;  $E00000-$FFFFFF RAM de 64 KB, con espejos
 *
 * Cada acceso son 4 ciclos del 68000. Lo que no responde devuelve el último valor del bus de datos
 * (`open_bus`, de los accesos de datos: no depende del prefetch, así que el intérprete y el código
 * generado leen lo mismo).
 */
#include "gen.h"

/* --- Cartucho --- */

static inline int sram_window(const Cart *c, u32 a)
{
    if (!c->sram_data) return 0;
    u32 base = c->sram_start & ~1u;
    if (a < base || a - base >= c->sram_size) return 0;
    return (c->a130f1 & 1) || c->rom_size <= base;     /* encima de la ROM, solo con $A130F1 */
}

/* La SRAM de 8 bits solo responde en los bytes pares o en los impares */
static inline int sram_lane(const Cart *c, u32 a)
{
    int t = c->sram_type >> 3 & 3;
    return t == 2 ? !(a & 1) : t == 3 ? (a & 1) : 1;
}

static inline u8 rom8(const Cart *c, u32 a)
{
    u32 o = cart_rom_offset(c, a);
    if (c->mapper == MAPPER_NONE) o &= c->rom_mask;
    return o < c->rom_size ? c->rom[o] : 0xFF;
}

static inline u8 cart_rd8(Gen *g, u32 a)
{
    const Cart *c = &g->cart;
    if (c->has_eeprom && eeprom_owns(c, a)) return eeprom_read(g, a);
    if (sram_window(c, a))
        return sram_lane(c, a) ? c->sram_data[a - (c->sram_start & ~1u)] : (u8)(g->open_bus >> ((a & 1) ? 0 : 8));
    return rom8(c, a);
}

/* Una palabra del cartucho (a par): de la ROM de una vez si ahí no hay SRAM ni EEPROM */
static inline u16 cart_rd16(Gen *g, u32 a)
{
    const Cart *c = &g->cart;
    if (!c->has_eeprom && !sram_window(c, a)) {
        u32 o = cart_rom_offset(c, a);
        if (c->mapper == MAPPER_NONE) o &= c->rom_mask;
        if (o + 1 < c->rom_size) return rd16(c->rom + o);
    }
    return (u16)(cart_rd8(g, a) << 8 | cart_rd8(g, a + 1));
}

static inline void cart_wr8(Gen *g, u32 a, u8 v)
{
    Cart *c = &g->cart;
    if (c->has_eeprom && eeprom_owns(c, a)) { eeprom_write(g, a, v); return; }
    if (sram_window(c, a) && !(c->a130f1 & 2) && sram_lane(c, a))
        c->sram_data[a - (c->sram_start & ~1u)] = v;
}

/* $A130F1-$A130FF (bytes impares) */
static void mapper_write(Gen *g, u32 a, u8 v)
{
    Cart *c = &g->cart;
    int r = (a & 0xF) >> 1;
    if (!(a & 1)) return;
    if (r == 0) { c->a130f1 = v & 3; c->map_ver++; return; }
    if (c->mapper == MAPPER_SSF2) {
        c->bank[r] = (u32)(v & 0x3F) * 0x80000;
        c->map_ver++;
    }
}

/* --- Bus del Z80 visto desde el 68000 (8 bits) --- */

static inline int z80_bus_free(const Gen *g) { return g->z80_busreq || g->z80_reset; }

static u8 z80area_rd8(Gen *g, u32 a)
{
    z80_sync(g);
    if (!z80_bus_free(g)) return 0xFF;
    a &= 0x7FFF;
    if (a < 0x4000) return g->zram[a & 0x1FFF];
    if (a < 0x6000) return ym_read(g, g->cyc * MCLK_PER_M68K);
    return 0xFF;
}

static void z80area_wr8(Gen *g, u32 a, u8 v)
{
    z80_sync(g);
    if (!z80_bus_free(g)) return;
    a &= 0x7FFF;
    if (a < 0x4000) g->zram[a & 0x1FFF] = v;
    else if (a < 0x6000) ym_write(g, (int)(a & 3), v, g->cyc * MCLK_PER_M68K);
    else if (a < 0x6100) g->z80_bank = (u16)(((g->z80_bank >> 1) | ((v & 1u) << 8)) & 0x1FF);
    else if (a >= 0x7F10 && a < 0x7F18 && (a & 1)) psg_write(g, v, g->cyc * MCLK_PER_M68K);
}

/* --- Lectura y escritura --- */

/* Una dirección del VDP que responde. Con TMSS y sin "SEGA" en $A14000 el VDP no contesta y el
 * 68000 espera para siempre: se detiene el CPU. */
static inline int vdp_addr(Gen *g, u32 a)
{
    if ((a & 0xE700E0) != 0xC00000) return 0;
    if (g->tmss && g->tmss_reg != 0x53454741u) { g->tmss_locked = 1; g->cpu.halted = 1; return 0; }
    return 1;
}

static u16 read16(Gen *g, u32 a)
{
    switch (a >> 20) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        return cart_rd16(g, a);
    case 0xE: case 0xF:
        return rd16(g->ram + (a & 0xFFFF));
    case 0xC: case 0xD:
        return vdp_addr(g, a) ? vdp_read(g, a & 0x1E) : g->open_bus;
    case 0xA:
        if (a < 0xA10000) { u8 v = z80area_rd8(g, a); return (u16)(v << 8 | v); }
        if (a < 0xA10020) { u8 v = io_read(g, a >> 1 & 0xF); return (u16)(v << 8 | v); }
        if ((a & 0xFFFF00) == 0xA11100) {       /* bit 8: 0 = el 68000 tiene el bus del Z80 */
            z80_sync(g);
            return (u16)((g->open_bus & 0xFEFF) | ((g->z80_busreq && !g->z80_reset) ? 0 : 0x100));
        }
        return g->open_bus;
    default:
        return g->open_bus;
    }
}

static u8 read8(Gen *g, u32 a)
{
    switch (a >> 20) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        return cart_rd8(g, a);
    case 0xE: case 0xF:
        return g->ram[a & 0xFFFF];
    case 0xA:
        if (a < 0xA10000) return z80area_rd8(g, a);
        if (a < 0xA10020) return io_read(g, a >> 1 & 0xF);
        /* fallthrough */
    default: {
        u16 w = read16(g, a & ~1u);
        return (a & 1) ? (u8)w : (u8)(w >> 8);
    }
    }
}

static void write16(Gen *g, u32 a, u16 v)
{
    switch (a >> 20) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        cart_wr8(g, a, (u8)(v >> 8)); cart_wr8(g, a + 1, (u8)v);
        return;
    case 0xE: case 0xF:
        g->ram[a & 0xFFFF] = (u8)(v >> 8); g->ram[(a & 0xFFFF) + 1] = (u8)v;
        return;
    case 0xC: case 0xD:
        if (!vdp_addr(g, a)) return;
        if ((a & 0x1F) >= 0x10) { if ((a & 0x1E) == 0x10) psg_write(g, (u8)v, g->cyc * MCLK_PER_M68K); return; }
        vdp_write(g, a & 0x1E, v);
        return;
    case 0xA:
        if (a < 0xA10000) { z80area_wr8(g, a, (u8)(v >> 8)); return; }
        if (a < 0xA10020) { io_write(g, a >> 1 & 0xF, (u8)v); return; }
        if ((a & 0xFFFF00) == 0xA11100) { z80_set_busreq(g, v >> 8 & 1); return; }
        if ((a & 0xFFFF00) == 0xA11200) { z80_set_reset(g, !(v >> 8 & 1)); return; }
        if ((a & 0xFFFFF0) == 0xA130F0) { mapper_write(g, a + 1, (u8)v); return; }
        if ((a & 0xFFFFFC) == 0xA14000) {
            if (a & 2) g->tmss_reg = (g->tmss_reg & 0xFFFF0000u) | v;
            else g->tmss_reg = (g->tmss_reg & 0xFFFFu) | (u32)v << 16;
        }
        return;
    default:
        return;
    }
}

static void write8(Gen *g, u32 a, u8 v)
{
    switch (a >> 20) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        cart_wr8(g, a, v);
        return;
    case 0xE: case 0xF:
        g->ram[a & 0xFFFF] = v;
        return;
    case 0xC: case 0xD:
        if (!vdp_addr(g, a)) return;
        if ((a & 0x1F) >= 0x10) {                           /* $C00011-$C00017: PSG */
            if ((a & 0x19) == 0x11) psg_write(g, v, g->cyc * MCLK_PER_M68K);
            return;
        }
        vdp_write(g, a & 0x1E, (u16)(v << 8 | v));          /* el byte va en las dos mitades */
        return;
    case 0xA:
        if (a < 0xA10000) { z80area_wr8(g, a, v); return; }
        if (a < 0xA10020) { if (a & 1) io_write(g, a >> 1 & 0xF, v); return; }
        if (a == 0xA11100) { z80_set_busreq(g, v & 1); return; }
        if (a == 0xA11200) { z80_set_reset(g, !(v & 1)); return; }
        if ((a & 0xFFFFF0) == 0xA130F0) { mapper_write(g, a, v); return; }
        if ((a & 0xFFFFFC) == 0xA14000) {
            int sh = (3 - (int)(a & 3)) * 8;
            g->tmss_reg = (g->tmss_reg & ~(0xFFu << sh)) | (u32)v << sh;
        }
        return;
    default:
        return;
    }
}

/* Para el Z80 (su ventana al bus del 68000) y otros accesos sin ciclos del 68000 */
u8 bus_read8(Gen *g, u32 addr) { return read8(g, addr & 0xFFFFFF); }
void bus_write8(Gen *g, u32 addr, u8 v) { write8(g, addr & 0xFFFFFF, v); }

/* --- Lo que pide el 68000 (m68k.h) --- */

uint16_t m68k_fetch(Gen *g, uint32_t a)
{
    u16 v = (a >= 0xE00000) ? rd16(g->ram + (a & 0xFFFF)) : read16(g, a);
    g->cyc += 4;
    return v;
}

uint16_t m68k_rd16(Gen *g, uint32_t a)
{
    u16 v = read16(g, a);
    g->open_bus = v;
    g->cyc += 4;
    return v;
}

uint8_t m68k_rd8(Gen *g, uint32_t a)
{
    u8 v = read8(g, a);
    g->open_bus = (u16)(v << 8 | v);
    g->cyc += 4;
    return v;
}

void m68k_wr16(Gen *g, uint32_t a, uint16_t v)
{
    write16(g, a, v);
    g->open_bus = v;
    g->cyc += 4;
}

void m68k_wr8(Gen *g, uint32_t a, uint8_t v)
{
    write8(g, a, v);
    g->open_bus = (u16)(v << 8 | v);
    g->cyc += 4;
}


/* Sin efectos: solo ROM, SRAM y RAM (lo que puede ser código) */
uint16_t m68k_peek16(Gen *g, uint32_t a)
{
    if (a < 0x400000) return (u16)(cart_rd8(g, a) << 8 | cart_rd8(g, a + 1));
    if (a >= 0xE00000) return rd16(g->ram + (a & 0xFFFF));
    return 0xFFFF;
}

/* Desplazamiento en la ROM de la instrucción en `pc`, si se lee de la ROM (no de la SRAM, la EEPROM
 * ni la RAM); -1 si no. Lo usa el despachador del código generado. */
long gen_code_offset(const Gen *g, u32 pc)
{
    const Cart *c = &g->cart;
    if (pc >= 0x400000 || sram_window(c, pc) || (c->has_eeprom && (eeprom_owns(c, pc) || eeprom_owns(c, pc + 1))))
        return -1;
    u32 o = cart_rom_offset(c, pc);
    if (c->mapper == MAPPER_NONE) o &= c->rom_mask;
    return o < c->rom_size ? (long)o : -1;
}

/* RESET del 68000: en el Genesis no llega a los periféricos que se emulan */
void m68k_reset_line(Gen *g) { (void)g; }

void m68k_int_ack(Gen *g, int level) { vdp_int_ack(g, level); }
