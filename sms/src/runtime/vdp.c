/*
 * VDP (315-5246 del SMS2 y el del Game Gear): puertos, memorias, contadores e interrupciones, escrito desde
 * la documentación de Charles MacDonald (msvdp-20021112.txt). El dibujo llega en la fase 3.
 *
 * Puertos ($80-$BF; $40-$7F para los contadores):
 *   control  un comando de dos bytes: el primero pone los 8 bits bajos de la dirección; el segundo, los 6
 *            altos y el código (0 leer VRAM: carga el búfer y sube la dirección; 1 escribir VRAM; 2 escribir
 *            el registro de los bits 3-0 con el primer byte; 3 escribir CRAM). Leerlo devuelve el estado (INT,
 *            OVR, COL en los bits 7-5) y lo borra, borra la interrupción de línea pendiente y el primer byte.
 *   datos    escribir va a la VRAM o a la CRAM según el código y carga el búfer; leer devuelve el búfer y lo
 *            vuelve a llenar con la VRAM. Los dos suben la dirección (14 bits) y borran el primer byte.
 *            CRAM: en el SMS (y en el GG en su modo SMS), 32 bytes --BBGGRR; en el GG, 32 palabras de 12 bits,
 *            la dirección da la vuelta en $3F y el byte par queda en un latch que se escribe con el impar.
 *   $7E/$7F  contador V de la línea y contador H congelado (cambia solo cuando cambia TH, io.c).
 *
 * Temporización (NTSC, 262 líneas de 228 ciclos del Z80): al empezar cada línea sube el contador V; el de la
 * interrupción de línea baja en las líneas 0 a 192 (o 224, 240) y se recarga con el registro 10 en las demás;
 * al pasar de 0 a $FF se recarga y queda pendiente la interrupción de línea. La bandera de cuadro (bit 7 del
 * estado) sube en la línea $C1 ($E1 con 224 líneas, $F1 con 240). La línea IRQ del Z80 es (cuadro y registro
 * 1, bit 5) o (línea y registro 0, bit 4). El alto del cuadro se fija al empezarlo. msvdp no sabe en qué
 * punto de la línea pasa cada cosa: aquí, al empezarla, con el contador H en 0 (el primer pixel de la
 * imagen); se calibra con el VDPTEST de FluBBa (EXTRA=1 make testroms).
 */
#include <string.h>

#include "sms.h"

#define LINE_CYC  (MCLK_PER_LINE / MCLK_PER_Z80)        /* 228 */

/* Registros tras la BIOS 1.3 (bios13.asm): R0 $36, R1 $A0, R2-R5 $FF, R6 $FB, R7-R9 0, R10 $FF */
static const u8 bios_regs[11] = { 0x36, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF };

/* Alto de la imagen según el modo: con el modo 4 (R0 bit 2) y M2 (R0 bit 1), M1 (R1 bit 4) da 224 líneas y
 * M3 (R1 bit 3) 240 (solo el SMS2 y el GG) */
static u16 active_height(const Vdp *v)
{
    if ((v->reg[0] & 0x06) == 0x06) {
        if ((v->reg[1] & 0x18) == 0x10) return 224;
        if ((v->reg[1] & 0x18) == 0x08) return 240;
    }
    return 192;
}

static void update_irq(Sms *g)
{
    const Vdp *v = &g->vdp;
    g->z80.irq = (u8)(((v->status & 0x80) && (v->reg[1] & 0x20)) || (v->line_irq && (v->reg[0] & 0x10)));
}

/* Lo que pasa al empezar la línea v->line */
static void line_begin(Sms *g)
{
    Vdp *v = &g->vdp;
    if (v->line == 0) v->height = active_height(v);
    if (v->line <= v->height) {
        if (v->line_counter-- == 0) { v->line_counter = v->reg[10]; v->line_irq = 1; }
    } else v->line_counter = v->reg[10];
    if (v->line == v->height + 1) v->status |= 0x80;
    update_irq(g);
}

void vdp_reset(Sms *g)
{
    Vdp *v = &g->vdp;
    u64 frame = v->frame;
    memset(v, 0, sizeof *v);
    memcpy(v->reg, bios_regs, sizeof bios_regs);
    v->frame = frame;
    v->height = active_height(v);
    v->line_start = g->cyc;
    v->line_counter = v->reg[10];
    line_begin(g);
}

void vdp_sync(Sms *g, u64 cyc)
{
    Vdp *v = &g->vdp;
    while (cyc >= v->line_start + LINE_CYC) {
        v->line_start += LINE_CYC;
        if (++v->line == LINES_NTSC) { v->line = 0; v->frame++; v->frame_done = 1; }
        line_begin(g);
    }
}

/* Contador V (NTSC): lineal hasta $DA (192 líneas) o $EA (224), y desde ahí salta a $D5 o $E5 para
 * terminar en $FF en la línea 261. Con 240 líneas (que en NTSC no se ve bien), $00-$FF y $00-$05 */
u8 vdp_vcount(const Vdp *v)
{
    int l = v->line;
    switch (v->height) {
    case 224: return (u8)(l <= 0xEA ? l : l - 6);
    case 240: return (u8)l;
    default: return (u8)(l <= 0xDA ? l : l - 6);
    }
}

/* Contador H: 342 pixeles por línea (1,5 por ciclo del Z80); el contador interno de 9 bits va de 0 a $127 y
 * salta a $1D2 hasta $1FF; se leen sus 8 bits altos ($00-$93, $E9-$FF) */
static u8 hcount_at(const Vdp *v, u64 cyc)
{
    if (cyc < v->line_start) cyc = v->line_start;
    u32 px = (u32)((cyc - v->line_start) * 3 / 2);
    if (px > 341) px = 341;
    u32 h = px < 0x128 ? px : px - 0x128 + 0x1D2;
    return (u8)(h >> 1);
}

void vdp_latch_h(Sms *g, u64 cyc)
{
    vdp_sync(g, cyc);
    g->vdp.hcount = hcount_at(&g->vdp, cyc);
}

static void data_write(Sms *g, u8 val)
{
    Vdp *v = &g->vdp;
    v->pending = 0;
    if (v->code == 3) {
        if (g->model == MODEL_GG && !g->sms_mode) {
            u8 a = (u8)(v->addr & 0x3F);
            if (!(a & 1)) v->cram_latch = val;
            else { v->cram[a & 0x3E] = v->cram_latch; v->cram[a] = (u8)(val & 0x0F); }
        } else v->cram[v->addr & 0x1F] = (u8)(val & 0x3F);
    } else v->vram[v->addr & 0x3FFF] = val;
    v->buffer = val;
    v->addr = (u16)((v->addr + 1) & 0x3FFF);
}

static void control_write(Sms *g, u8 val)
{
    Vdp *v = &g->vdp;
    if (!v->pending) {
        v->first = val;
        v->addr = (u16)((v->addr & 0x3F00) | val);
        v->pending = 1;
        return;
    }
    v->pending = 0;
    v->addr = (u16)((val & 0x3F) << 8 | v->first);
    v->code = val >> 6;
    if (v->code == 0) {
        v->buffer = v->vram[v->addr];
        v->addr = (u16)((v->addr + 1) & 0x3FFF);
    } else if (v->code == 2) {
        if ((val & 0x0F) <= 10) v->reg[val & 0x0F] = v->first;
        update_irq(g);
    }
}

void vdp_write(Sms *g, u8 port, u8 val)
{
    vdp_sync(g, g->io_cyc);
    if (port & 1) control_write(g, val);
    else data_write(g, val);
}

u8 vdp_read(Sms *g, u8 port)
{
    Vdp *v = &g->vdp;
    vdp_sync(g, g->io_cyc);
    if (port < 0x80) return (port & 1) ? v->hcount : vdp_vcount(v);
    v->pending = 0;
    if (!(port & 1)) {
        u8 r = v->buffer;
        v->buffer = v->vram[v->addr];
        v->addr = (u16)((v->addr + 1) & 0x3FFF);
        return r;
    }
    /* Estado: los bits 4-0 no los pone el VDP (msvdp: basura en el SMS y el SMS2); aquí 0 */
    u8 r = v->status;
    v->status = 0;
    v->line_irq = 0;
    update_irq(g);
    return r;
}
