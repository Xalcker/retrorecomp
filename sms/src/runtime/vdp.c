/*
 * VDP (315-5246 del SMS2 y el del Game Gear): puertos, memorias, contadores e interrupciones, escrito desde
 * la documentación de Charles MacDonald (msvdp-20021112.txt). El dibujo está en vdp_render.c.
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
 * Temporización (NTSC, 262 líneas de 228 ciclos del Z80). msvdp no dice en qué punto de la línea pasa cada
 * cosa; lo dicen la guía y la tabla del VDPTEST de FluBBa (medido en un SMS). Con c = ciclos desde que cambia el
 * contador V (cuando el contador H vale $F4):
 *   c = 0  (H $F4)  cambia el contador V y se dibuja la línea (vdp_render.c). El contador de la interrupción de
 *                   línea baja en las líneas 0 a 192 (o al alto de la imagen) y se recarga con el registro 10 en
 *                   las demás; al pasar de 0 a $FF se recarga y queda pendiente la interrupción de línea. En la
 *                   línea $C1 ($E1 con 224 líneas, $F1 con 240) queda pendiente la de cuadro y su bandera (bit 7
 *                   del estado) ya se ve, igual que la de desborde de sprites (bit 6) de la línea (VDPTEST: «VINT
 *                   flag HCount» la lee apagada un ciclo antes y encendida en este).
 *   c = 2 y 3       sube la línea IRQ del Z80 por la de cuadro (con el registro 1, bit 5) y por la de línea (con
 *                   el registro 0, bit 4); ver VINT_DELAY.
 *   la colisión de sprites (bit 5) se ve en el ciclo del pixel donde ocurre.
 * El contador H (los 8 bits altos de uno de 9 que cuenta 342 pixeles) avanza 3/4 por ciclo: $00-$93 y luego
 * $E9-$FF, como la tabla del VDPTEST. Solo se lee congelado: cuando TH pasa de 0 a 1 (io.c). El alto del cuadro
 * y el scroll vertical se fijan al empezar el cuadro.
 */
#include <string.h>

#include "sms.h"

#define LINE_CYC  (MCLK_PER_LINE / MCLK_PER_Z80)        /* 228 */
#define VCHANGE_K 212                                   /* el lugar de la tabla del contador H donde cambia el V ($F4) */
/* Ciclos después del cambio del V en que sube la línea INT del Z80 por la interrupción de cuadro y por la de
 * línea. En la consola son H $F3 (HClock -18 y -17, un poco antes del cambio), pero el Z80 mira la línea en el
 * último ciclo de cada instrucción y en el HALT, y aquí se mira al empezar la siguiente: medido con las pruebas
 * de «Frame IRQ HCount» y «Line IRQ HCount» del VDPTEST (las dos variantes de cada una, que cambian de ranura de
 * 4 ciclos del HALT con un ciclo de diferencia), equivale a +2 y +3 */
#define VINT_DELAY 2
#define LINT_DELAY 3

/* Registros tras la BIOS 1.3 (bios13.asm): R0 $36, R1 $A0, R2-R5 $FF, R6 $FB, R7-R9 0, R10 $FF */
static const u8 bios_regs[11] = { 0x36, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF };

/* Alto de la imagen según el modo: con el modo 4 (R0 bit 2) y M2 (R0 bit 1), M1 (R1 bit 4) da 224 líneas y
 * M3 (R1 bit 3) 240 (solo el SMS2 y el GG). El Game Gear en modo GG se queda en 192 (msvdp: su resolución no
 * cambia, aunque la tabla de nombres sí; Micro Machines lo usa) */
static u16 active_height(const Sms *g)
{
    const Vdp *v = &g->vdp;
    if (vdp_extended(v) && !(g->model == MODEL_GG && !g->sms_mode)) return (v->reg[1] & 0x18) == 0x10 ? 224 : 240;
    return 192;
}

static void update_irq(Sms *g)
{
    const Vdp *v = &g->vdp;
    g->z80.irq = (u8)((v->vint && (v->reg[1] & 0x20)) || (v->line_irq && (v->reg[0] & 0x10)));
}

/* El próximo ciclo en que el VDP tiene algo que hacer (sms_after_step lo mira tras cada instrucción) */
static void next_event(Vdp *v)
{
    u64 n = v->line_start + LINE_CYC;
    if (v->vint_at && v->vint_at < n) n = v->vint_at;
    if (v->lint_at && v->lint_at < n) n = v->lint_at;
    if (v->col_at && v->col_at < n) n = v->col_at;
    v->next_event = n;
}

/* El contador de línea y las interrupciones de la línea l, que empieza (su línea INT sube unos ciclos después) */
static void line_irqs(Sms *g, int l)
{
    Vdp *v = &g->vdp;
    if (l <= v->height) {
        if (v->line_counter-- == 0) { v->line_counter = v->reg[10]; v->lint_at = v->line_start + LINT_DELAY; }
    } else v->line_counter = v->reg[10];
    if (l == v->height + 1) { v->vint_at = v->line_start + VINT_DELAY; v->status |= 0x80; }
}

/* c = 0: empieza la línea v->line */
static void line_begin(Sms *g)
{
    Vdp *v = &g->vdp;
    if (v->line == 0) { v->height = active_height(g); v->vscroll = v->reg[9]; }
    if (v->line < v->height) vdp_render_line(g, v->line);
    else vdp_border_sprites(g, vdp_vcount(v));
}

void vdp_reset(Sms *g)
{
    Vdp *v = &g->vdp;
    u64 frame = v->frame;
    memset(v, 0, sizeof *v);
    memcpy(v->reg, bios_regs, sizeof bios_regs);
    v->frame = frame;
    v->height = active_height(g);
    v->line_start = g->cyc;
    v->line_counter = v->reg[10];
    line_begin(g);
    line_irqs(g, 0);
    next_event(v);
}

void vdp_sync(Sms *g, u64 cyc)
{
    Vdp *v = &g->vdp;
    for (;;) {
        if (v->col_at && cyc >= v->col_at) { v->status |= 0x20; v->col_at = 0; }
        if (v->vint_at && cyc >= v->vint_at) { v->vint = 1; v->vint_at = 0; update_irq(g); }
        if (v->lint_at && cyc >= v->lint_at) { v->line_irq = 1; v->lint_at = 0; update_irq(g); }
        u64 next = v->line_start + LINE_CYC;
        if (cyc < next) break;
        v->line_start = next;
        if (++v->line == LINES_NTSC) { v->line = 0; v->frame++; v->frame_done = 1; }
        line_begin(g);
        line_irqs(g, v->line);
    }
    next_event(v);
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

/* El contador H en el lugar k (0-227) de la tabla del VDPTEST: $00 al empezar la imagen, sube 3/4 por ciclo
 * hasta $93 y salta a $E9 para terminar en $FF */
static u8 hcount_k(int k)
{
    return (u8)(k <= 196 ? (3 * k + 2) >> 2 : 0xE9 + ((3 * (k - 197) + 1) >> 2));
}

/* El ciclo de la línea (desde que cambia el V) en que el contador H llega al pixel x de la imagen: para la
 * colisión de sprites */
u32 vdp_pixel_cycle(int x)
{
    int h = x >> 1, k = (4 * h + 1) / 3;
    while (k > 0 && hcount_k(k - 1) >= h) k--;
    while (hcount_k(k) < h) k++;
    return (u32)((k - VCHANGE_K + LINE_CYC) % LINE_CYC);
}

static u8 hcount_at(const Vdp *v, u64 cyc)
{
    u64 c = cyc < v->line_start ? 0 : cyc - v->line_start;
    return hcount_k((int)((c + VCHANGE_K) % LINE_CYC));
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
    v->vint = 0;
    v->line_irq = 0;
    v->vint_at = v->lint_at = 0;    /* y las que estaban por subir */
    update_irq(g);
    next_event(v);
    return r;
}
