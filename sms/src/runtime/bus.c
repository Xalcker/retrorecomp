/*
 * Bus del Z80: el mapa de memoria, los mappers y el reparto de los puertos.
 *
 * Memoria (SMS Power!, Mappers; smstech de Charles MacDonald):
 *   $0000-$BFFF  tres ranuras de 16 KB del cartucho. Con el mapper Sega (315-5235), $FFFD-$FFFF eligen la
 *                página de cada ranura (al encender, 0, 1 y 2) y el primer KB ($0000-$03FF) es siempre la
 *                página 0 (los vectores); $FFFC pone la RAM del cartucho en la ranura 2 (bit 3, con el banco
 *                de 16 KB en el bit 2) o en $C000-$FFFF (bit 4). Los registros son de solo escritura, pero la
 *                escritura también llega a la RAM: se leen en $DFFC-$DFFF. Con el de Codemasters, una
 *                escritura en la ranura (en $0000, $4000 u $8000) elige su página (al encender, 0, 1 y 0), sin
 *                el KB fijo; el bit 7 de lo escrito en $4000 pone 8 KB de RAM en $A000-$BFFF (Ernie Els Golf).
 *                Sin mapper, las páginas 0, 1 y 2.
 *   $C000-$FFFF  la RAM de trabajo de 8 KB, con su espejo en $E000.
 * Una página que pasa del tamaño de la ROM da la vuelta (la imagen se arma en páginas enteras).
 *
 * Puertos: deciden A7, A6 y A0 ($00-$3F control de memoria y de E/S; $40-$7F contadores y PSG; $80-$BF el
 * VDP; $C0-$FF los mandos). En el Game Gear en modo GG, $00-$06 son sus registros. Los tiempos del VDP salen
 * del ciclo del acceso dentro de la instrucción (io_cyc, por Z80_IO_AT).
 */
#include <string.h>

#include "sms.h"

static u32 page_off(const Sms *g, u32 page)
{
    return (page % g->cart.pages) * PAGE_SIZE;
}

/* Las ranuras según los registros del mapper */
static void remap(Sms *g)
{
    Mapper *m = &g->map;
    switch (g->cart.mapper) {
    case MAPPER_SEGA:
        for (int i = 0; i < 3; i++) m->slot[i] = page_off(g, m->reg[1 + i]);
        m->ram_slot2 = (m->reg[0] & 0x08) != 0;
        m->ram_high = (m->reg[0] & 0x10) != 0;
        if (m->ram_slot2 || m->ram_high) g->cart.ram_used = 1;
        break;
    case MAPPER_CODEMASTERS:
        m->slot[0] = page_off(g, m->reg[0]);
        m->slot[1] = page_off(g, m->reg[1] & 0x7F);
        m->slot[2] = page_off(g, m->reg[2]);
        m->ram_a000 = (m->reg[1] & 0x80) != 0;
        if (m->ram_a000) g->cart.ram_used = 1;
        break;
    default:
        for (int i = 0; i < 3; i++) m->slot[i] = page_off(g, (u32)i);
        break;
    }
    m->map_ver++;
}

void mapper_reset(Sms *g)
{
    Mapper *m = &g->map;
    u32 ver = m->map_ver;
    memset(m, 0, sizeof *m);
    m->map_ver = ver;
    if (g->cart.mapper == MAPPER_CODEMASTERS) { m->reg[0] = 0; m->reg[1] = 1; m->reg[2] = 0; }
    else { m->reg[0] = 0; m->reg[1] = 0; m->reg[2] = 1; m->reg[3] = 2; }
    remap(g);
}

/* La RAM del cartucho del mapper Sega en una ranura: el banco de 16 KB del bit 2 de $FFFC */
static u8 *sega_ram(Sms *g, u16 addr)
{
    return g->cart.ram + ((g->map.reg[0] & 0x04) ? 0x4000 : 0) + (addr & 0x3FFF);
}

static inline const u8 *rom_at(const Sms *g, u16 addr)
{
    if (addr < 0x400 && g->cart.mapper != MAPPER_CODEMASTERS) return g->cart.image + addr;
    return g->cart.image + g->map.slot[addr >> 14] + (addr & 0x3FFF);
}

u8 bus_peek(const Sms *g, u16 addr)
{
    const Mapper *m = &g->map;
    if (addr >= 0xC000) {
        if (m->ram_high) return g->cart.ram[addr & 0x3FFF];
        return g->ram[addr & 0x1FFF];
    }
    if (addr >= 0x8000) {
        if (m->ram_slot2) return g->cart.ram[((m->reg[0] & 0x04) ? 0x4000 : 0) + (addr & 0x3FFF)];
        if (m->ram_a000 && addr >= 0xA000) return g->cart.ram[addr & 0x1FFF];
    }
    return *rom_at(g, addr);
}

long bus_rom_offset(const Sms *g, u16 addr)
{
    const Mapper *m = &g->map;
    if (addr >= 0xC000 || (addr >= 0x8000 && (m->ram_slot2 || (m->ram_a000 && addr >= 0xA000)))) return -1;
    return (long)((size_t)(rom_at(g, addr) - g->cart.image) % g->cart.rom_size);
}

uint8_t z80_read(Sms *g, uint16_t addr)
{
    return bus_peek(g, addr);
}

void z80_write(Sms *g, uint16_t addr, uint8_t v)
{
    Mapper *m = &g->map;
    if (addr >= 0xC000) {
        if (m->ram_high) g->cart.ram[addr & 0x3FFF] = v;
        else g->ram[addr & 0x1FFF] = v;
        /* Los registros del mapper Sega ($FFFC-$FFFF; la escritura llegó también a la RAM). $FFF8-$FFFB son
         * las gafas 3-D, que no se emulan: queda solo lo escrito en la RAM */
        if (addr >= 0xFFFC && g->cart.mapper == MAPPER_SEGA) {
            m->reg[addr - 0xFFFC] = v;
            remap(g);
        }
        return;
    }
    if (g->cart.mapper == MAPPER_CODEMASTERS) {
        if (addr >= 0xA000 && m->ram_a000) { g->cart.ram[addr & 0x1FFF] = v; return; }
        /* La escritura en cualquier parte de una ranura elige su página (los juegos escriben en $0000,
         * $4000 y $8000) */
        m->reg[addr >> 14] = v;
        remap(g);
        return;
    }
    if (addr >= 0x8000 && m->ram_slot2) *sega_ram(g, addr) = v;
    /* La ROM no se escribe */
}

/* --- Puertos --- */

uint8_t z80_in(Sms *g, uint16_t port)
{
    u8 p = (u8)port;
    if (p >= 0x40 && p < 0xC0) return vdp_read(g, p);
    if (p >= 0xC0 || (g->model == MODEL_GG && !g->sms_mode && p <= 0x06)) return io_read(g, p);
    return 0xFF;                        /* $00-$3F: el SMS2 y el GG leen $FF */
}

void z80_out(Sms *g, uint16_t port, uint8_t v)
{
    u8 p = (u8)port;
    if (p < 0x40) io_write(g, p, v);
    else if (p < 0x80) psg_write(g, v, g->io_cyc * MCLK_PER_Z80);
    else if (p < 0xC0) vdp_write(g, p, v);
    else if (p == 0xFD && g->sdsc_out) g->sdsc_out(g, v);   /* la consola de depuración SDSC (solo emuladores) */
}
