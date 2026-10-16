/*
 * Bus del 68000: el mapa de memoria del Neo Geo AES.
 *
 *   $000000-$0FFFFF  P1: el primer MB del programa (con espejos si es más chico). $000000-$00007F
 *                    muestra los vectores de la BIOS hasta que se escribe REG_SWPROM
 *   $100000-$1FFFFF  RAM de 64 KB, con espejos
 *   $200000-$2FFFFF  P2: el banco de 1 MB que eligió la última escritura en $2FFFF0-$2FFFFF (si P
 *                    mide más de 2 MB; si mide 2 MB o menos, fijo)
 *   $300000  REG_P1CNT (mando 1; al escribir $300001, el perro guardián, que el AES no tiene)
 *   $320000  REG_SOUND: escribir manda un comando al Z80; leer, su respuesta
 *   $340000  REG_P2CNT;  $380000 REG_STATUS_B (Start, Select, tarjeta);  $380001 salidas del mando,
 *            $380011 banco de la tarjeta
 *   $3A0001-$3A001F  latch del sistema (bytes impares; el dato es el bit 4 de la dirección)
 *   $3C0000-$3DFFFF  LSPC (lspc.c)
 *   $400000-$7FFFFF  paleta: 8 KB del banco elegido, con espejos
 *   $800000-$BFFFFF  tarjeta de memoria (bytes impares; 2 ciclos de espera)
 *   $C00000-$CFFFFF  BIOS de 128 KB, con espejos;  $D00000-$FFFFFF nada (el AES no tiene RAM de respaldo)
 *
 * Cada acceso son 4 ciclos del 68000. Lo que no responde devuelve el último valor del bus de datos
 * (`open_bus`, de los accesos de datos: no depende del prefetch, así que el intérprete y el código
 * generado leen lo mismo). La ROM y la BIOS guardan las palabras con los bytes invertidos (neo.h).
 * Los registros de 16 bits reciben un byte escrito en las dos mitades (el 68000 lo pone en las dos).
 */
#include "neo.h"

#define CARD_WAIT 2

/* --- Programa y BIOS --- */

static inline const u8 *p1_ptr(const Neo *g, u32 a)
{
    const Cart *c = &g->cart;
    size_t n = c->len[REG_P] < P_BANK_SIZE ? c->len[REG_P] : P_BANK_SIZE;
    return c->data + c->off[REG_P] + (a & 0xFFFFF) % n;
}

static inline const u8 *p2_ptr(const Neo *g, u32 a)
{
    const Cart *c = &g->cart;
    if (!c->p_banks) return NULL;
    size_t base = P_BANK_SIZE * (1 + (size_t)(g->p2_bank % c->p_banks));
    size_t n = c->len[REG_P] - base < P_BANK_SIZE ? c->len[REG_P] - base : P_BANK_SIZE;
    return c->data + c->off[REG_P] + base + (a & 0xFFFFF) % n;
}

static inline int bios_vectors(const Neo *g, u32 a) { return a < 0x80 && !(g->latch & LATCH_SWPROM); }
static inline const u8 *bios_ptr(const Neo *g, u32 a) { return g->bios.rom + (a & (BIOS_SIZE - 1)); }

/* Escritura en la ventana P2: el latch de bancos (en $2FFFF0-$2FFFFF, como en MAME) */
static void p2_write(Neo *g, u32 a, u16 v)
{
    if (g->cart.p_banks <= 1 || (a & 0xFFFF0) != 0xFFFF0) return;
    u8 b = (u8)(v & 7);
    if (b != g->p2_bank) { g->p2_bank = b; g->map_ver++; }
}

/* --- Tarjeta de memoria --- */

static inline int card_index(const Neo *g, u32 a, u32 *i)
{
    if (!g->card.data || !(a & 1)) return 0;
    *i = (((a & 0x3FFFFF) >> 1) | (u32)g->crdbank << 21) & (g->card.size - 1);
    return 1;
}

static inline int card_writable(const Neo *g)
{
    return !g->card.protect && !(g->latch & LATCH_CRDLOCK1) && (g->latch & LATCH_CRDUNLOCK2);
}

/* --- E/S --- */

static u8 io_rd8(Neo *g, u32 a)
{
    switch ((a >> 17) & 7) {
    case 0:                                         /* $300000: mando 1; $300001/$300081: sin DIP en el AES */
        return (a & 1) ? 0xFF : (u8)~g->pad[0];
    case 1:                                         /* $320000: respuesta del Z80; $320001: sin monedas ni reloj */
        if (a & 1) return 0xFF;
        z80_sync(g);
        return g->sound_reply;
    case 2: return (a & 1) ? 0xFF : (u8)~g->pad[1];
    case 4:                                         /* $380000: REG_STATUS_B */
        if (a & 1) return (u8)(g->open_bus >> 0);
        return (u8)((g->card.data ? 0 : 0x30) | (g->card.protect ? 0x40 : 0)
                    | (~(((g->pad[0] >> 8) & 3) | ((g->pad[1] >> 8) & 3) << 2) & 0xF));
    case 6: {                                       /* $3C0000: LSPC */
        u16 v = lspc_read(g, a);
        return (u8)((a & 1) ? v : v >> 8);
    }
    default: return (u8)((a & 1) ? g->open_bus : g->open_bus >> 8);
    }
}

static u16 io_rd16(Neo *g, u32 a)
{
    if (((a >> 17) & 7) == 6) return lspc_read(g, a);
    return (u16)(io_rd8(g, a) << 8 | io_rd8(g, a | 1));
}

static void io_wr8(Neo *g, u32 a, u8 v)
{
    switch ((a >> 17) & 7) {
    case 1: if (!(a & 1)) sound_command(g, v); break;
    case 4:
        if (!(a & 1)) break;
        if ((a & 0x7F) == 0x01) g->poutput = v;
        else if ((a & 0x7F) == 0x11) g->crdbank = v & 7;
        break;
    case 5:                                         /* $3A0001-$3A001F: latch del sistema */
        if (a & 1) {
            u8 bit = (u8)(1u << ((a >> 1) & 7));
            if (a & 0x10) g->latch |= bit; else g->latch &= (u8)~bit;
        }
        break;
    case 6: lspc_write(g, a, (u16)(v << 8 | v)); break;
    default: break;                                 /* $300001 (perro guardián del MVS) y lo demás */
    }
}

static void io_wr16(Neo *g, u32 a, u16 v)
{
    if (((a >> 17) & 7) == 6) { lspc_write(g, a, v); return; }
    io_wr8(g, a, (u8)(v >> 8));
    io_wr8(g, a | 1, (u8)v);
}

/* --- Paleta --- */

static inline u16 *pal_ptr(Neo *g, u32 a) { return &g->pal[(g->latch & LATCH_PALBANK1) ? 1 : 0][(a & 0x1FFF) >> 1]; }

static inline void pal_write(Neo *g, u32 a, u16 v)
{
    int b = (g->latch & LATCH_PALBANK1) ? 1 : 0, i = (int)((a & 0x1FFF) >> 1);
    g->pal[b][i] = v;
    g->pal_argb[b][i] = neo_color(v);
}

/* --- Lo que pide el 68000 --- */

uint16_t m68k_rd16(Neo *g, uint32_t a)
{
    g->cyc += 4;
    u16 v;
    switch (a >> 20) {
    case 0x0: v = rdw_le(bios_vectors(g, a) ? bios_ptr(g, a) : p1_ptr(g, a)); break;
    case 0x1: v = (u16)(g->ram[a & 0xFFFF] << 8 | g->ram[(a + 1) & 0xFFFF]); break;
    case 0x2: { const u8 *p = p2_ptr(g, a); v = p ? rdw_le(p) : g->open_bus; break; }
    case 0x3: v = io_rd16(g, a); break;
    case 0x4: case 0x5: case 0x6: case 0x7: v = *pal_ptr(g, a); break;
    case 0x8: case 0x9: case 0xA: case 0xB: {
        u32 i;
        g->cyc += CARD_WAIT;
        v = (u16)(0xFF00 | (card_index(g, a | 1, &i) ? g->card.data[i] : 0xFF));
        break;
    }
    case 0xC: v = rdw_le(bios_ptr(g, a)); break;
    default: v = g->open_bus; break;
    }
    g->open_bus = v;
    return v;
}

uint8_t m68k_rd8(Neo *g, uint32_t a)
{
    g->cyc += 4;
    u8 v;
    switch (a >> 20) {
    case 0x0: v = (bios_vectors(g, a) ? bios_ptr(g, a) : p1_ptr(g, a & ~1u))[(a & 1) ^ 1]; break;
    case 0x1: v = g->ram[a & 0xFFFF]; break;
    case 0x2: { const u8 *p = p2_ptr(g, a & ~1u); v = p ? p[(a & 1) ^ 1] : (u8)((a & 1) ? g->open_bus : g->open_bus >> 8); break; }
    case 0x3: v = io_rd8(g, a); break;
    case 0x4: case 0x5: case 0x6: case 0x7: { u16 w = *pal_ptr(g, a); v = (u8)((a & 1) ? w : w >> 8); break; }
    case 0x8: case 0x9: case 0xA: case 0xB: {
        u32 i;
        g->cyc += CARD_WAIT;
        v = card_index(g, a, &i) ? g->card.data[i] : 0xFF;
        break;
    }
    case 0xC: v = bios_ptr(g, a & ~1u)[(a & 1) ^ 1]; break;
    default: v = (u8)((a & 1) ? g->open_bus : g->open_bus >> 8); break;
    }
    g->open_bus = (u16)((a & 1) ? (g->open_bus & 0xFF00) | v : (g->open_bus & 0x00FF) | v << 8);
    return v;
}

/* Lectura de programa: como una de datos, sin tocar el bus abierto */
uint16_t m68k_fetch(Neo *g, uint32_t a)
{
    u16 keep = g->open_bus;
    u16 v = m68k_rd16(g, a);
    g->open_bus = keep;
    return v;
}

void m68k_wr16(Neo *g, uint32_t a, uint16_t v)
{
    g->cyc += 4;
    g->open_bus = v;
    switch (a >> 20) {
    case 0x1: g->ram[a & 0xFFFF] = (u8)(v >> 8); g->ram[(a + 1) & 0xFFFF] = (u8)v; break;
    case 0x2: p2_write(g, a, v); break;
    case 0x3: io_wr16(g, a, v); break;
    case 0x4: case 0x5: case 0x6: case 0x7: pal_write(g, a, v); break;
    case 0x8: case 0x9: case 0xA: case 0xB: {
        u32 i;
        g->cyc += CARD_WAIT;
        if (card_writable(g) && card_index(g, a | 1, &i)) { g->card.data[i] = (u8)v; g->card.dirty = 1; }
        break;
    }
    default: break;
    }
}

void m68k_wr8(Neo *g, uint32_t a, uint8_t v)
{
    g->cyc += 4;
    g->open_bus = (u16)(v << 8 | v);
    switch (a >> 20) {
    case 0x1: g->ram[a & 0xFFFF] = v; break;       /* TAS también escribe (en el Genesis no) */
    case 0x2: p2_write(g, a, (u16)(v << 8 | v)); break;
    case 0x3: io_wr8(g, a, v); break;
    case 0x4: case 0x5: case 0x6: case 0x7: pal_write(g, a, (u16)(v << 8 | v)); break;
    case 0x8: case 0x9: case 0xA: case 0xB: {
        u32 i;
        g->cyc += CARD_WAIT;
        if (card_writable(g) && card_index(g, a, &i)) { g->card.data[i] = v; g->card.dirty = 1; }
        break;
    }
    default: break;
    }
}

/* Sin efectos ni ciclos: la cola de prefetch al salir del código generado (y los depuradores) */
uint16_t m68k_peek16(Neo *g, uint32_t a)
{
    switch (a >> 20) {
    case 0x0: return rdw_le(bios_vectors(g, a) ? bios_ptr(g, a) : p1_ptr(g, a));
    case 0x1: return (u16)(g->ram[a & 0xFFFF] << 8 | g->ram[(a + 1) & 0xFFFF]);
    case 0x2: { const u8 *p = p2_ptr(g, a); return p ? rdw_le(p) : 0xFFFF; }
    case 0xC: return rdw_le(bios_ptr(g, a));
    default: return 0xFFFF;
    }
}

/* La instrucción RESET reinicia los periféricos; en el Neo Geo, nada que el programa vea todavía */
void m68k_reset_line(Neo *g) { (void)g; }

/* El 68000 atiende una interrupción: en el Neo Geo no se borra sola (se borra con IRQACK) */
void m68k_int_ack(Neo *g, int level) { (void)g; (void)level; }
