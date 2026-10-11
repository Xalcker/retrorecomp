/*
 * EEPROM serie (I2C) de algunos cartuchos: el juego mueve SCL y SDA a mano escribiendo en una
 * dirección del cartucho y lee SDA en otra. La cabecera no dice dónde ni qué chip, así que sale de
 * una base de datos por número de serie.
 *
 * Protocolo: START (SDA baja con SCL alto), bytes de 8 bits con el más alto primero (se leen al
 * subir SCL) y un noveno pulso de ACK (quien recibe baja SDA), STOP (SDA sube con SCL alto).
 *   - X24C01 (addr_bits = 7): el primer byte es la dirección << 1 | leer.
 *   - 24C01-24C16 (8): un byte de dispositivo 1010xxx + leer; al escribir, después la dirección
 *     (en los de más de 256 bytes, los bits altos van en el de dispositivo).
 *   - 24C32 o más (16): dispositivo y dos bytes de dirección.
 * Escribir avanza dentro de la página (page_mask); leer avanza por toda la memoria. El contenido es
 * cart.sram_data, así que se guarda como la SRAM.
 */
#include <string.h>

#include "gen.h"

enum { E_IDLE, E_DEV, E_ADDR7, E_ADDR_HI, E_ADDR_LO, E_WRITE, E_READ };

static const struct { const char *serial; EepromCfg cfg; } db[] = {
    /* Acclaim, mapeo viejo: SDA y SCL en los bits 0 y 1 de $200001; SDA se lee en el bit 1 */
    { "T-081326", { 0x200001, 0x200001, 0x200001, 0, 1, 1, 8, 0xFF, 0x03 } },     /* NBA Jam (USA, Europa) */
    { "T-81033",  { 0x200001, 0x200001, 0x200001, 0, 1, 1, 8, 0xFF, 0x03 } },     /* NBA Jam (Japón) */
    /* Acclaim, mapeo nuevo: SDA en el bit 0 de $200001 (escribir y leer), SCL en el bit 0 de $200000 */
    { "T-81406",  { 0x200001, 0x200000, 0x200001, 0, 0, 0, 8, 0xFF, 0x03 } },     /* NBA Jam Tournament Edition (X24C02) */
};

int eeprom_detect(Cart *c)
{
    for (size_t i = 0; i < sizeof db / sizeof db[0]; i++)
        if (strstr(c->hdr.serial, db[i].serial)) { c->eeprom = db[i].cfg; return 1; }
    return 0;
}

void eeprom_reset(Gen *g)
{
    Eeprom *e = &g->eeprom;
    memset(e, 0, sizeof *e);
    e->scl = e->sda = e->out = 1;
}

int eeprom_owns(const Cart *c, u32 a)
{
    const EepromCfg *k = &c->eeprom;
    return c->has_eeprom && (a == k->sda_in_addr || a == k->scl_addr || a == k->sda_out_addr);
}

u8 eeprom_read(Gen *g, u32 a)
{
    const EepromCfg *k = &g->cart.eeprom;
    const Eeprom *e = &g->eeprom;
    if (a != k->sda_out_addr) return 0;
    return (u8)((e->sda & e->out) << k->sda_out_bit);
}

static u8 *cell(Gen *g) { return &g->cart.sram_data[g->eeprom.addr & g->cart.eeprom.size_mask]; }

/* Un byte recibido: qué sigue y si se responde con ACK */
static int received(Gen *g, u8 b)
{
    Eeprom *e = &g->eeprom;
    const EepromCfg *k = &g->cart.eeprom;
    switch (e->state) {
    case E_DEV: {
        /* Los bits 1-3 del byte de dispositivo son dirección en los de más de 256 bytes (24C04-24C16); el
         * resto son patas de selección de chip, conectadas a tierra en los cartuchos: otro valor, sin ACK */
        u32 sel = b >> 1 & 7, blocks = k->addr_bits == 8 ? ((u32)k->size_mask + 1) >> 8 : 0;
        if ((b & 0xF0) != 0xA0 || sel >= (blocks ? blocks : 1)) { e->state = E_IDLE; return 0; }
        if (k->addr_bits == 8) e->addr = (u16)((e->addr & 0xFF) | (sel << 8));
        e->state = (b & 1) ? E_READ : k->addr_bits == 16 ? E_ADDR_HI : E_ADDR_LO;
        return 1;
    }
    case E_ADDR7:
        e->addr = b >> 1;
        e->state = (b & 1) ? E_READ : E_WRITE;
        return 1;
    case E_ADDR_HI:
        e->addr = (u16)((b << 8) | (e->addr & 0xFF));
        e->state = E_ADDR_LO;
        return 1;
    case E_ADDR_LO:
        e->addr = (u16)((e->addr & 0xFF00) | b);
        e->state = E_WRITE;
        return 1;
    case E_WRITE:
        *cell(g) = b;
        e->writes++;
        e->addr = (u16)((e->addr & ~k->page_mask) | ((e->addr + 1) & k->page_mask));
        return 1;
    default:
        return 0;
    }
}

static void lines(Gen *g, u8 scl, u8 sda)
{
    Eeprom *e = &g->eeprom;
    const EepromCfg *k = &g->cart.eeprom;
    if (e->scl && scl) {                                    /* SCL alto: START o STOP */
        if (e->sda && !sda) { e->state = k->addr_bits == 7 ? E_ADDR7 : E_DEV; e->bit = 0; e->shift = 0; e->out = 1; e->sending = 0; }
        else if (!e->sda && sda) { e->state = E_IDLE; e->out = 1; e->sending = 0; }
    } else if (!e->scl && scl && e->state != E_IDLE) {      /* subida: se lee un bit */
        if (e->sending) {
            if (e->bit < 8) e->bit++;                       /* el juego lee el bit que puso la EEPROM */
            else if (sda) { e->state = E_IDLE; e->sending = 0; }   /* sin ACK: terminó de leer */
            else { e->addr = (u16)((e->addr + 1) & k->size_mask); e->bit = 9; }
        } else if (e->bit < 8) { e->shift = (u8)(e->shift << 1 | sda); e->bit++; }
        else e->bit = 9;                                    /* el juego lee el ACK de la EEPROM */
    } else if (e->scl && !scl && e->state != E_IDLE) {      /* bajada: la EEPROM pone su bit */
        if (e->sending && e->bit < 9) e->out = e->bit < 8 ? (e->shift >> (7 - e->bit)) & 1 : 1;
        else if (e->bit == 8) e->out = received(g, e->shift) ? 0 : 1;
        else if (e->bit == 9) {
            e->bit = 0;
            e->sending = e->state == E_READ;
            e->shift = e->sending ? *cell(g) : 0;
            e->out = e->sending ? e->shift >> 7 : 1;
        }
    }
    e->scl = scl;
    e->sda = sda;
}

void eeprom_write(Gen *g, u32 a, u8 v)
{
    const EepromCfg *k = &g->cart.eeprom;
    const Eeprom *e = &g->eeprom;
    u8 scl = a == k->scl_addr ? v >> k->scl_bit & 1 : e->scl;
    u8 sda = a == k->sda_in_addr ? v >> k->sda_in_bit & 1 : e->sda;
    lines(g, scl, sda);
}
