/*
 * El Z80 dentro del Genesis: su mapa de memoria y cuándo corre.
 *
 *   $0000-$1FFF  RAM de 8 KB (espejo en $2000-$3FFF)
 *   $4000-$5FFF  YM2612 ($4000-$4003, con espejos)
 *   $6000-$60FF  registro de banco: cada escritura mete el bit 0 en un registro de 9 bits
 *   $7F11-$7F17  PSG (impares)
 *   $8000-$FFFF  ventana de 32 KB al bus del 68000, en el banco elegido
 *
 * Va a 1/15 del reloj maestro. Corre atrasado respecto del 68000: cada instrucción del 68000 solo anota
 * hasta dónde le toca (z80_due) y se pone al día donde se tocan: antes de que el 68000 toque su bus, el
 * pedido o el reset (hasta el mismo ciclo maestro, z80_sync), y antes de que el 68000 escriba el PSG o el
 * mapper, de que el VDP cambie la ventana de su INT y del audio al final del cuadro (hasta z80_due,
 * gen_z80_catchup en gen.h). Todo pasa en el mismo orden que si corriera tras cada instrucción del 68000:
 * tests/golden.sh no cambia un bit. Lo único que no queda igual es que toque la RAM, el VDP o la E/S del
 * 68000 por la ventana de $8000 (gen_z80_window_ext lo cuenta; genbench lo dice). No corre con el reset
 * puesto ($A11200) ni con el bus pedido por el 68000 ($A11100). Su INT dura una línea desde el
 * VBLANK (vdp.c fija la ventana z80_irq_from/until).
 *
 * Cada acceso del Z80 al bus del 68000 (la ventana de $8000) le cuesta unos 3 ciclos más: espera a
 * que el 68000 le ceda el bus. Sin emular todavía: lo que pierde el 68000 en esa cesión.
 */
#include "gen.h"

#define BANK_WAIT (3 * MCLK_PER_Z80)        /* espera del Z80 al usar el bus del 68000 */

unsigned long long gen_z80_window_ext;
uint8_t z80_read(struct Gen *g, uint16_t a)
{
    if (a < 0x4000) return g->zram[a & 0x1FFF];
    if (a < 0x6000) return ym_read(g, g->z80_mclk);
    if (a < 0x8000) return 0xFF;
    g->z80_mclk += BANK_WAIT;
    u32 a68 = (u32)g->z80_bank << 15 | (a & 0x7FFFu);
    if (a68 >= 0x400000 || (g->cart.sram_data && a68 >= (g->cart.sram_start & ~1u))) gen_z80_window_ext++;
    return bus_read8(g, a68);
}

void z80_write(struct Gen *g, uint16_t a, uint8_t v)
{
    if (a < 0x4000) { g->zram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { ym_write(g, a & 3, v, g->z80_mclk); return; }
    if (a < 0x6100) { g->z80_bank = (u16)(((g->z80_bank >> 1) | ((v & 1u) << 8)) & 0x1FF); return; }
    if (a >= 0x7F10 && a < 0x7F18 && (a & 1)) { psg_write(g, v, g->z80_mclk); return; }
    if (a >= 0x8000) {
        g->z80_mclk += BANK_WAIT;
        gen_z80_window_ext++;
        bus_write8(g, (u32)g->z80_bank << 15 | (a & 0x7FFFu), v);
    }
}

/* El Genesis no usa los puertos del Z80 */
uint8_t z80_in(struct Gen *g, uint16_t port) { (void)g; (void)port; return 0xFF; }
void z80_out(struct Gen *g, uint16_t port, uint8_t v) { (void)g; (void)port; (void)v; }

void z80_run_until(Gen *g, u64 target)
{
    Z80 *z = &g->z80;
    PROF_ENTER(PROF_Z80);
    while (g->z80_mclk < target) {
        if (g->z80_reset || g->z80_busreq) { g->z80_mclk = target; break; }
        z->irq = g->z80_mclk >= g->z80_irq_from && g->z80_mclk < g->z80_irq_until;
        if (z->halted && !z->ei && !(z->irq && z->iff1)) {
            /* HALT sin interrupción que atender: cada paso son 4 ciclos T y sube R. Se saltan de una
             * vez los pasos hasta el destino o hasta el que vea empezar la interrupción. */
            u64 stop = target;
            if (z->iff1 && g->z80_irq_from > g->z80_mclk && g->z80_irq_from < stop) stop = g->z80_irq_from;
            u64 n = (stop - g->z80_mclk + 4 * MCLK_PER_Z80 - 1) / (4 * MCLK_PER_Z80);
            g->z80_mclk += n * 4 * MCLK_PER_Z80;
            Z80_STAT(g, skipped, n * 4);
            z->r = (u8)((z->r & 0x80) | ((z->r + n) & 0x7F));
            z->q = 0;
            z->p = 0;
            u64 last = g->z80_mclk - 4 * MCLK_PER_Z80;     /* lo que vio el último paso */
            z->irq = last >= g->z80_irq_from && last < g->z80_irq_until;
            continue;
        }
#ifdef GEN_RECOMP
        /* El driver en la RAM del Z80 en código nativo (gen/zram.c), si no hay una INT que atender */
        if (z->pc < 0x2000 && !(z->irq && z->iff1 && !z->ei) && !z->halted && !gen_force_interp &&
            (const void *)g != z80_interp_only && zram_run(g, target)) continue;
#endif
        if (gen_z80_probe) gen_z80_probe(g, z->pc);
        int t = z80_step(g, z);
        g->z80_mclk += (u64)t * MCLK_PER_Z80;
        Z80_STAT(g, interp, (u64)t);
    }
    PROF_LEAVE();
}

void (*gen_z80_probe)(Gen *g, u16 pc);

void z80_sync(Gen *g)
{
    z80_run_until(g, g->cyc * MCLK_PER_M68K);
}

void z80_set_reset(Gen *g, int on)
{
    z80_sync(g);
    if (on && !g->z80_reset) {
        z80_reset(&g->z80);
        ym_run(g, g->cyc * MCLK_PER_M68K);
        u64 t = g->ym.mclk;
        ym_reset(&g->ym);
        g->ym.mclk = t;
    }
    g->z80_reset = (u8)on;
}

void z80_set_busreq(Gen *g, int on)
{
    z80_sync(g);
    g->z80_busreq = (u8)on;
}
