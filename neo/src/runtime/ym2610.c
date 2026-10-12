/*
 * YM2610. Por ahora lo que el programa del Z80 necesita para andar: los registros, los dos
 * temporizadores, sus banderas y la IRQ (la INT del Z80). El sonido (FM, SSG, ADPCM-A y ADPCM-B) llega
 * en la fase 4.
 *
 * Puertos del Z80: 4 elige el registro de la primera mitad y 5 lo escribe (SSG, ADPCM-B,
 * temporizadores, FM 1-2); 6 y 7 lo mismo con la segunda (ADPCM-A, FM 3-4). Al leer: 4 es el estado
 * (bit 7 ocupado, bit 1 temporizador B, bit 0 temporizador A), 5 el registro elegido del SSG y 6 las
 * banderas de fin de los ADPCM.
 *
 * Temporizadores (registros $24-$27): el A cuenta de N (10 bits) a 1024 de a una muestra del FM (144
 * ciclos del YM2610, 432 maestros) y el B de N (8 bits) a 256 de a 16 muestras. $27: bits 0/1 los
 * echan a andar (cargando N), 2/3 dejan que pongan su bandera al vencer, 4/5 borran la bandera. Con
 * una bandera puesta, la IRQ está pedida. No se llevan ciclo a ciclo: se guarda cuándo vence cada uno.
 */
#include "neo.h"

#include <string.h>

static u64 period_a(const Ym2610 *y) { return (u64)(1024 - ((y->reg[0][0x24] << 2) | (y->reg[0][0x25] & 3))) * MCLK_PER_FM; }
static u64 period_b(const Ym2610 *y) { return (u64)(256 - y->reg[0][0x26]) * 16 * MCLK_PER_FM; }

void ym_reset(Ym2610 *y)
{
    memset(y, 0, sizeof *y);
    y->ta_at = y->tb_at = NEVER;
}

void ym_update(Ym2610 *y, u64 mclk)
{
    while (y->ta_at <= mclk) {
        if (y->reg[0][0x27] & 4) y->flags |= 1;
        y->ta_at += period_a(y);
    }
    while (y->tb_at <= mclk) {
        if (y->reg[0][0x27] & 8) y->flags |= 2;
        y->tb_at += period_b(y);
    }
}

u8 ym_read(Ym2610 *y, int port, u64 mclk)
{
    ym_update(y, mclk);
    switch (port & 3) {
    case 0: return y->flags;
    case 1: return y->addr[0] < 0x10 ? y->reg[0][y->addr[0]] : 0;
    case 2: return 0;                               /* fin de los ADPCM: fase 4 */
    default: return 0;
    }
}

void ym_write(Ym2610 *y, int port, u8 v, u64 mclk)
{
    int half = (port >> 1) & 1;
    if (!(port & 1)) { y->addr[half] = v; return; }
    ym_update(y, mclk);
    u8 r = y->addr[half];
    u8 old = y->reg[half][r];
    y->reg[half][r] = v;
    if (half || r != 0x27) return;
    /* Echar a andar un temporizador lo carga; pararlo lo deja quieto */
    if ((v & 1) && !(old & 1)) y->ta_at = mclk + period_a(y);
    else if (!(v & 1)) y->ta_at = NEVER;
    if ((v & 2) && !(old & 2)) y->tb_at = mclk + period_b(y);
    else if (!(v & 2)) y->tb_at = NEVER;
    y->flags &= (u8)~((v >> 4) & 3);
}
