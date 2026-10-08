/*
 * Controladores de memoria del cartucho (MBC). Se encargan de qué banco de
 * ROM aparece en $0000-$3FFF y $4000-$7FFF, y de la RAM del cartucho en
 * $A000-$BFFF. Cada cambio de banco actualiza el mapa de lectura rápida.
 */
#include "gb.h"

#include <stdlib.h>
#include <string.h>

static void update_banks(GB *g)
{
    Mbc *m = &g->mbc;
    u32 banks = g->rom_banks;
    u32 ram_banks = g->sram_size / 0x2000;

    switch (m->type) {
    case MBC_NONE:
        m->bank0 = 0;
        m->bankx = 1 % banks;
        m->sram_bank = g->sram_size ? 0 : -1;
        break;
    case MBC_1: {
        u32 low = m->rom_bank & 0x1F;
        if (low == 0) low = 1;
        int shift = m->multicart ? 4 : 5;
        if (m->multicart) low &= 0x0F;  /* el bit 4 del registro no está conectado */
        u32 high = (u32)(m->ram_bank & 3) << shift;
        m->bankx = (high | low) % banks;
        m->bank0 = m->mode ? high % banks : 0;
        if (!m->ram_enable || !g->sram_size) m->sram_bank = -1;
        else m->sram_bank = (m->mode && ram_banks > 1) ? (m->ram_bank & 3) % ram_banks : 0;
        break;
    }
    case MBC_2: {
        u32 b = m->rom_bank & 0x0F;
        if (b == 0) b = 1;
        m->bank0 = 0;
        m->bankx = b % banks;
        m->sram_bank = m->ram_enable ? 0 : -1;
        break;
    }
    case MBC_3: {
        /* MBC30 (ROM de más de 2 MB): registro de banco de 8 bits y 8 bancos de RAM */
        int mbc30 = g->rom_banks > 128;
        u32 b = m->rom_bank & (mbc30 ? 0xFF : 0x7F);
        if (b == 0) b = 1;
        m->bank0 = 0;
        m->bankx = b % banks;
        m->rtc_select = -1;
        if (!m->ram_enable) {
            m->sram_bank = -1;
        } else if (m->ram_bank >= 0x08 && m->ram_bank <= 0x0C && m->has_rtc) {
            m->sram_bank = -1;
            m->rtc_select = m->ram_bank - 0x08;
        } else if (g->sram_size && ram_banks) {
            m->sram_bank = (m->ram_bank & (mbc30 ? 7 : 3)) % ram_banks;
        } else {
            m->sram_bank = -1;
        }
        break;
    }
    case MBC_5:
        m->bank0 = 0;
        m->bankx = (m->rom_bank & 0x1FF) % banks;
        if (!m->ram_enable || !g->sram_size || !ram_banks) m->sram_bank = -1;
        else m->sram_bank = (m->ram_bank & 0x0F) % ram_banks;
        break;
    }
    gb_remap(g);
}

void mbc_init(GB *g)
{
    Mbc *m = &g->mbc;
    u8 type = g->rom[0x147];
    memset(m, 0, sizeof *m);
    m->rtc_select = -1;
    m->rom_bank = 1;

    switch (type) {
    case 0x00: m->type = MBC_NONE; break;
    case 0x08: m->type = MBC_NONE; m->has_ram = 1; break;
    case 0x09: m->type = MBC_NONE; m->has_ram = 1; m->has_battery = 1; break;
    case 0x01: m->type = MBC_1; break;
    case 0x02: m->type = MBC_1; m->has_ram = 1; break;
    case 0x03: m->type = MBC_1; m->has_ram = 1; m->has_battery = 1; break;
    case 0x05: m->type = MBC_2; m->has_ram = 1; break;
    case 0x06: m->type = MBC_2; m->has_ram = 1; m->has_battery = 1; break;
    case 0x0F: m->type = MBC_3; m->has_rtc = 1; m->has_battery = 1; break;
    case 0x10: m->type = MBC_3; m->has_rtc = 1; m->has_ram = 1; m->has_battery = 1; break;
    case 0x11: m->type = MBC_3; break;
    case 0x12: m->type = MBC_3; m->has_ram = 1; break;
    case 0x13: m->type = MBC_3; m->has_ram = 1; m->has_battery = 1; break;
    case 0x19: case 0x1C: m->type = MBC_5; break;
    case 0x1A: case 0x1D: m->type = MBC_5; m->has_ram = 1; break;
    case 0x1B: case 0x1E: m->type = MBC_5; m->has_ram = 1; m->has_battery = 1; break;
    default: m->type = MBC_5; break;   /* desconocido: lo más permisivo */
    }

    static const u32 ram_sizes[6] = { 0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000 };
    u8 rs = g->rom[0x149];
    u32 size = 0;
    if (m->type == MBC_2) size = 0x200;
    else if (m->has_ram) size = rs < 6 ? ram_sizes[rs] : 0;
    /* Un banco parcial de 2 KB se maneja como uno de 8 KB */
    if (size && size < 0x2000 && m->type != MBC_2) size = 0x2000;

    g->sram_size = size;
    g->sram = size ? calloc(1, size < 0x2000 ? 0x2000 : size) : NULL;
    if (g->sram) memset(g->sram, 0xFF, size < 0x2000 ? 0x2000 : size);

    /* MBC1M (cartuchos de varios juegos): 1 MB y la cabecera de otro juego,
     * con el logo de Nintendo, al inicio del banco $10 */
    if (m->type == MBC_1 && g->rom_banks == 64 &&
        memcmp(g->rom + 0x104, g->rom + 0x10 * 0x4000 + 0x104, 48) == 0)
        m->multicart = 1;
    update_banks(g);
}

/* --- RTC del MBC3 --- */

static const u8 rtc_mask[5] = { 0x3F, 0x3F, 0x1F, 0xFF, 0xC1 };

/* Suma segundos a los registros, con acarreo hasta el contador de días de 9
 * bits; al desbordarse los días se enciende el bit 7 de DH. */
void mbc_rtc_advance(GB *g, u64 seconds)
{
    Mbc *m = &g->mbc;
    if (!m->has_rtc || (m->rtc[4] & 0x40)) return;     /* detenido */
    while (seconds > 0) {
        /* Avance rápido de días completos cuando el resto está en cero */
        if (seconds >= 86400 && m->rtc[0] == 0 && m->rtc[1] == 0 && m->rtc[2] == 0) {
            u64 days = seconds / 86400;
            u32 d = (u32)m->rtc[3] | (u32)(m->rtc[4] & 1) << 8;
            u64 total = d + days;
            if (total > 511) m->rtc[4] |= 0x80;
            total &= 511;
            m->rtc[3] = (u8)total;
            m->rtc[4] = (u8)((m->rtc[4] & 0xFE) | (total >> 8));
            seconds -= days * 86400;
            continue;
        }
        seconds--;
        /* Los registros pueden tener valores fuera de rango (escritos por el
         * juego); como en el hardware, solo acarrean al llegar justo a 60/24 */
        m->rtc[0] = (u8)((m->rtc[0] + 1) & 0x3F);
        if (m->rtc[0] != 60) continue;
        m->rtc[0] = 0;
        m->rtc[1] = (u8)((m->rtc[1] + 1) & 0x3F);
        if (m->rtc[1] != 60) continue;
        m->rtc[1] = 0;
        m->rtc[2] = (u8)((m->rtc[2] + 1) & 0x1F);
        if (m->rtc[2] != 24) continue;
        m->rtc[2] = 0;
        u32 d = ((u32)m->rtc[3] | (u32)(m->rtc[4] & 1) << 8) + 1;
        if (d > 511) { d = 0; m->rtc[4] |= 0x80; }
        m->rtc[3] = (u8)d;
        m->rtc[4] = (u8)((m->rtc[4] & 0xFE) | (d >> 8));
    }
}

/* Cuenta el tiempo emulado transcurrido desde la última vez */
static void rtc_update(GB *g)
{
    Mbc *m = &g->mbc;
    u64 delta = g->cyc - m->rtc_last_cyc;
    m->rtc_last_cyc = g->cyc;
    if (m->rtc[4] & 0x40) return;
    m->rtc_sub += delta;
    if (m->rtc_sub >= GB_CPU_HZ) {
        mbc_rtc_advance(g, m->rtc_sub / GB_CPU_HZ);
        m->rtc_sub %= GB_CPU_HZ;
    }
}

void mbc_write(GB *g, u16 addr, u8 v)
{
    Mbc *m = &g->mbc;
    switch (m->type) {
    case MBC_NONE:
        return;
    case MBC_1:
        if (addr < 0x2000) m->ram_enable = (v & 0x0F) == 0x0A;
        else if (addr < 0x4000) m->rom_bank = v & 0x1F;
        else if (addr < 0x6000) m->ram_bank = v & 3;
        else m->mode = v & 1;
        break;
    case MBC_2:
        if (addr >= 0x4000) return;
        if (addr & 0x100) m->rom_bank = v & 0x0F;
        else m->ram_enable = (v & 0x0F) == 0x0A;
        break;
    case MBC_3:
        if (addr < 0x2000) m->ram_enable = (v & 0x0F) == 0x0A;
        else if (addr < 0x4000) m->rom_bank = v;
        else if (addr < 0x6000) m->ram_bank = v;
        else {
            if (m->rtc_latch_prev == 0 && v == 1) {
                rtc_update(g);
                memcpy(m->rtc_latched, m->rtc, sizeof m->rtc);
            }
            m->rtc_latch_prev = v;
            return;
        }
        break;
    case MBC_5:
        if (addr < 0x2000) m->ram_enable = v == 0x0A;
        else if (addr < 0x3000) m->rom_bank = (u16)((m->rom_bank & 0x100) | v);
        else if (addr < 0x4000) m->rom_bank = (u16)((m->rom_bank & 0xFF) | (v & 1) << 8);
        else if (addr < 0x6000) m->ram_bank = v & 0x0F;
        else return;
        break;
    }
    update_banks(g);
}

u8 mbc_sram_read(GB *g, u16 addr)
{
    Mbc *m = &g->mbc;
    if (m->type == MBC_3 && m->rtc_select >= 0)
        return m->rtc_latched[m->rtc_select];   /* los bits sin uso se leen en 0 */
    if (m->sram_bank < 0) return 0xFF;
    if (m->type == MBC_2)
        return (u8)(0xF0 | g->sram[addr & 0x1FF]);
    u32 off = (u32)m->sram_bank * 0x2000 + (addr - 0xA000);
    if (off >= g->sram_size) off %= g->sram_size;
    return g->sram[off];
}

void mbc_sram_write(GB *g, u16 addr, u8 v)
{
    Mbc *m = &g->mbc;
    if (m->type == MBC_3 && m->rtc_select >= 0) {
        rtc_update(g);
        m->rtc[m->rtc_select] = v & rtc_mask[m->rtc_select];
        if (m->rtc_select == 0) m->rtc_sub = 0;   /* escribir segundos reinicia el divisor */
        g->sram_dirty = 1;
        return;
    }
    if (m->sram_bank < 0) return;
    if (m->type == MBC_2) {
        g->sram[addr & 0x1FF] = v & 0x0F;
    } else {
        u32 off = (u32)m->sram_bank * 0x2000 + (addr - 0xA000);
        if (off >= g->sram_size) off %= g->sram_size;
        g->sram[off] = v;
    }
    g->sram_dirty = 1;
}
