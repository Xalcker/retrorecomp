/*
 * E/S que no es del VDP (smstech de Charles MacDonald; SMS Power!, PeripheralPorts, Port3E, StartButton):
 *
 *   $3E (escritura en $00-$3F pares)  control de memoria: qué ranuras están habilitadas (bits en 0). Se
 *        guarda; solo el bit 2 (la E/S apagada: $C0-$FF leen $FF) cambia algo sin BIOS.
 *   $3F (escritura en $00-$3F impares)  control de E/S: dirección (bits 0-3: TR y TH de A, TR y TH de B; 1 =
 *        entrada) y nivel de salida (bits 4-7) de TR y TH de los dos puertos. Una consola de exportación lee
 *        en $DD el nivel que se escribió en TH (así detectan los juegos que no es japonesa); cuando TH pasa de 0 a
 *        1, el contador H se congela (es lo que usa la pistola).
 *   $DC (lectura en $C0-$FF pares)  arriba, abajo, izquierda, derecha, TL (botón 1) y TR (botón 2) del
 *        mando A, y arriba y abajo del B. En 0 lo apretado.
 *   $DD (lectura en $C0-$FF impares)  izquierda, derecha, TL y TR del B, el botón de reset (no hay en el SMS2
 *        ni en el GG: 1), un 1, y TH de A y de B.
 *   Game Gear en modo GG, $00-$06: $00 tiene Start (bit 7, en 0 apretado) y la región (bit 6: 1 fuera de
 *        Japón; bit 5: 0 en NTSC); $01-$05 son el puerto de enlace (sin cable: se guarda lo escrito) y $06 el
 *        estéreo del PSG. Al encender: C0 7F FF 00 FF 00 FF. En el GG solo responden $C0/$DC y $C1/$DD.
 *
 * En el Master System, START aprieta el botón 1 (los juegos piden «PUSH START BUTTON» con el botón 1).
 */
#include <string.h>

#include "sms.h"

static const u8 gg_init[7] = { 0xC0, 0x7F, 0xFF, 0x00, 0xFF, 0x00, 0xFF };

void io_reset(Sms *g)
{
    Io *io = &g->io;
    u8 prev = io->pause_prev;
    memset(io, 0, sizeof *io);
    io->pause_prev = prev;
    io->mem_ctrl = g->model == MODEL_GG ? 0xA8 : 0xAB;      /* lo que deja la BIOS (y guarda en $C000) */
    io->io_ctrl = 0xFF;                                     /* TR y TH como entradas */
    memcpy(io->gg, gg_init, sizeof io->gg);
}

/* Nivel de TH de un puerto (0 = A, 1 = B): como salida, el que se escribió (consola de exportación); como
 * entrada, en alto (no hay nada que lo baje) */
static int th_level(const Io *io, int port)
{
    u8 dir = port ? 0x08 : 0x02, lvl = port ? 0x80 : 0x20;
    return (io->io_ctrl & dir) ? 1 : (io->io_ctrl & lvl) != 0;
}

/* Los bits de los botones de un mando, en el orden de $DC (arriba, abajo, izquierda, derecha, TL, TR), en 1
 * lo apretado */
static u8 pad_bits(const Sms *g, int n)
{
    u8 p = g->pad[n], b = (u8)(p & 0x0F);
    if (p & SMS_A) b |= 0x10;
    if (p & SMS_B) b |= 0x20;
    if (n == 0 && g->model == MODEL_SMS && (p & SMS_START)) b |= 0x10;    /* START es el botón 1 */
    return b;
}

u8 io_read(Sms *g, u8 port)
{
    Io *io = &g->io;
    if (g->model == MODEL_GG && !g->sms_mode && port <= 0x06) {
        if (port == 0) return (u8)((io->gg[0] & 0x7F) | ((g->pad[0] & SMS_START) ? 0x00 : 0x80));
        return io->gg[port];
    }
    if (io->mem_ctrl & 0x04) return 0xFF;                   /* la E/S apagada */
    if (g->model == MODEL_GG && port != 0xC0 && port != 0xC1 && port != 0xDC && port != 0xDD) return 0xFF;
    u8 a = pad_bits(g, 0), b = g->model == MODEL_GG ? 0 : pad_bits(g, 1);
    if (!(port & 1)) {                                      /* $DC */
        u8 v = (u8)~(a | (b & 0x03) << 6);
        if (!(io->io_ctrl & 0x01)) v = (u8)((v & ~0x20) | ((io->io_ctrl & 0x10) ? 0x20 : 0));   /* TR de A como salida */
        return v;
    }
    u8 v = (u8)~((b >> 2) & 0x0F);                          /* $DD: izquierda, derecha, TL y TR de B */
    if (!(io->io_ctrl & 0x04)) v = (u8)((v & ~0x08) | ((io->io_ctrl & 0x40) ? 0x08 : 0));       /* TR de B como salida */
    v = (u8)((v & 0x0F) | 0x30 | th_level(io, 0) << 6 | th_level(io, 1) << 7);
    return v;
}

void io_write(Sms *g, u8 port, u8 v)
{
    Io *io = &g->io;
    if (g->model == MODEL_GG && !g->sms_mode && port <= 0x06) {
        if (port == 6) psg_run(g, g->io_cyc * MCLK_PER_Z80);   /* el estéreo cambia desde aquí */
        if (port) io->gg[port] = v;                         /* $00 es de solo lectura */
        return;
    }
    if (!(port & 1)) { io->mem_ctrl = v; return; }
    int th0 = th_level(io, 0), th1 = th_level(io, 1);
    io->io_ctrl = v;
    /* El contador H se congela cuando TH de algún puerto pasa de 0 a 1 (el VDPTEST lo comprueba: de 1 a 0 no) */
    if ((!th0 && th_level(io, 0)) || (!th1 && th_level(io, 1))) vdp_latch_h(g, g->io_cyc);
}
