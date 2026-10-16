/*
 * El gancho Z80_IO_AT de z80_host.h. Va solo en su archivo: z80test pone su propio bus (z80_read...) y, si
 * esto estuviera en bus.c, el enlazador traería también el de la consola.
 */
#include "sms.h"

void sms_io_at(Sms *g, int t)
{
    if (g) g->io_cyc = g->cyc + (u64)t;     /* z80test corre el Z80 sin consola (g = NULL) */
}
