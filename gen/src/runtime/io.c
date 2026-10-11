/*
 * E/S de la Mega Drive ($A10000-$A1001F): registro de versión, los tres puertos (datos y
 * dirección de los pines) y sus registros serie. En los puertos 1 y 2 hay un mando de 3 o de 6
 * botones; en el 3, nada.
 *
 * Mando de 3 botones (activo en bajo): con TH = 1 lee C B Der Izq Abajo Arriba; con TH = 0,
 * Start A 0 0 Abajo Arriba. El de 6 botones cuenta las bajadas de TH seguidas (se reinicia si pasa
 * ~1.5 ms sin una): en la tercera, con TH = 0 los bits 3-0 leen 0 (así se reconoce) y con TH = 1
 * leen Mode X Y Z; en la cuarta, con TH = 0 leen 1111.
 */
#include <string.h>

#include "gen.h"

#define TH_TIMEOUT 11500        /* ~1.5 ms en ciclos del 68000 */

void io_reset(Gen *g)
{
    Io *io = &g->io;
    memset(io, 0, sizeof *io);
    for (int i = 0; i < 3; i++) { io->data[i] = 0x7F; io->txdata[i] = 0xFF; }
}

/* TH tal como lo ve el mando: el que pone la consola si es salida; si no, la resistencia lo sube */
static inline int th_level(const Io *io, int p) { return (io->ctrl[p] & 0x40) ? (io->data[p] & 0x40) != 0 : 1; }

/* Los pines 0-6 que pone el mando del puerto p */
static u8 pad_pins(Gen *g, int p)
{
    Io *io = &g->io;
    u16 b = g->pad[p];
    int th = th_level(io, p);
    int n = 0;
    if (g->pad6 >> p & 1) {
        if (g->cyc - io->th_time[p] > TH_TIMEOUT) io->th_edges[p] = 0;
        n = io->th_edges[p];
    }
    u8 sa = (u8)((b & GEN_START ? 0 : 0x20) | (b & GEN_A ? 0 : 0x10));
    if (th) {
        if (n == 3) return (u8)(0x40 | (~b & 0x30) | (~(b >> 8) & 0x0F));      /* C B Mode X Y Z */
        return (u8)(0x40 | (~b & 0x3F));                                        /* C B R L D U */
    }
    if (n == 3) return sa;                                                      /* Start A 0 0 0 0 */
    if (n == 4) return (u8)(sa | 0x0F);                                         /* Start A 1 1 1 1 */
    return (u8)(sa | (~b & 0x03));                                              /* Start A 0 0 D U */
}

u8 io_read(Gen *g, int reg)
{
    Io *io = &g->io;
    switch (reg) {
    case 0x0: return g->version;
    case 0x1: case 0x2: case 0x3: {
        int p = reg - 1;
        u8 pins = p < 2 ? pad_pins(g, p) : 0x7F;
        return (u8)((io->data[p] & (io->ctrl[p] | 0x80)) | (pins & ~io->ctrl[p] & 0x7F));
    }
    case 0x4: case 0x5: case 0x6: return io->ctrl[reg - 4];
    case 0x7: case 0xA: case 0xD: return io->txdata[(reg - 7) / 3];
    case 0x8: case 0xB: case 0xE: return 0x00;                      /* RxData */
    case 0x9: case 0xC: case 0xF: return io->sctrl[(reg - 9) / 3];
    default: return 0xFF;
    }
}

/* Una bajada de TH en el puerto p cuenta para el mando de 6 botones */
static void th_changed(Gen *g, int p, int before)
{
    Io *io = &g->io;
    if (p > 1 || !before || th_level(io, p)) return;
    if (g->cyc - io->th_time[p] > TH_TIMEOUT) io->th_edges[p] = 0;
    if (io->th_edges[p] < 255) io->th_edges[p]++;
    io->th_time[p] = g->cyc;
}

void io_write(Gen *g, int reg, u8 v)
{
    Io *io = &g->io;
    switch (reg) {
    case 0x1: case 0x2: case 0x3: {
        int p = reg - 1, before = th_level(io, p);
        io->data[p] = v;
        th_changed(g, p, before);
        return;
    }
    case 0x4: case 0x5: case 0x6: {
        int p = reg - 4, before = th_level(io, p);
        io->ctrl[p] = v;
        th_changed(g, p, before);
        return;
    }
    case 0x7: case 0xA: case 0xD: io->txdata[(reg - 7) / 3] = v; return;
    case 0x9: case 0xC: case 0xF: io->sctrl[(reg - 9) / 3] = v & 0xF8; return;
    default: return;
    }
}
