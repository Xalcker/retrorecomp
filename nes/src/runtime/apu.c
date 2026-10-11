/*
 * APU 2A03: dos pulsos, triángulo, ruido y DMC, con el frame counter
 * (secuencia de 4 y 5 pasos, IRQ), contadores de longitud, envolventes y barrido.
 * apu_run avanza ciclos de CPU de uno en uno; la mezcla usa las curvas no lineales
 * del hardware y los filtros (paso alto 90 y 440 Hz, paso bajo 14 kHz).
 */
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846   /* -std=c99 estricto en MinGW no lo define */
#endif
#include <string.h>
#include "nes.h"

#define CPU_HZ 1789773.0

static const u8 len_tab[32] = { 10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
                                12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30 };
static const u8 duty_tab[4][8] = { {0,1,0,0,0,0,0,0}, {0,1,1,0,0,0,0,0},
                                   {0,1,1,1,1,0,0,0}, {1,0,0,1,1,1,1,1} };
static const u16 noise_tab[16] = { 4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068 };
static const u16 dmc_tab[16] = { 428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54 };
static const u8 tri_tab[32] = { 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
                                0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

static float pulse_mix[31], tnd_mix[203];
/* Sunsoft 5B: 32 niveles de 1.5 dB (el registro fijo de 4 bits usa los impares); 1.0 es el máximo */
static float s5b_lvl[32];
/* Niveles del audio de expansión, calibrados como Mesen2 (la referencia más usada): en su escala, un pulso del 2A03 a
 * volumen 15 vale ~746.8, un paso de volumen del VRC6 vale 75 (15 x 5), el máximo de un canal del 5B 178 x 15 y una
 * unidad de (muestra - 8) x volumen del Namco 163, ya promediada entre sus canales, 20. Aquí se expresan en
 * pulse_mix[15]. Con estos niveles un pulso del VRC6 a 15 suena ~1.5 veces un pulso del 2A03 y un canal del 5B al
 * máximo ~3.6 veces. */
#define MESEN_PULSE15 746.8f
#define VRC6_UNIT (pulse_mix[15] * 75.0f / MESEN_PULSE15)
#define S5B_UNIT (pulse_mix[15] * 178.0f * 15.0f / MESEN_PULSE15)
#define N163_UNIT (pulse_mix[15] * 20.0f / MESEN_PULSE15)
static int tabs_ready;

static void init_tabs(void)
{
    for (int i = 0; i < 31; i++) pulse_mix[i] = i ? 95.88f / (8128.0f / i + 100) : 0;
    for (int i = 0; i < 203; i++) tnd_mix[i] = i ? 163.67f / (24329.0f / i + 100) : 0;
    tabs_ready = 1;
}

void apu_reset(NES *n)
{
    Apu *a = &n->apu;
    if (!tabs_ready) init_tabs();
    u8 keep_mute = a->mute;
    memset(a, 0, sizeof *a);
    a->mute = keep_mute;
    a->nz.shift = 1;
    a->m5 = CART_MMC5(&n->cart);
    a->vrc6 = CART_VRC6(&n->cart);
    memset(&n->vrc6, 0, sizeof n->vrc6);
    n->vrc6.step[0] = n->vrc6.step[1] = 15;
    a->s5b = MAPPER(&n->cart) == 69;
    memset(&n->s5b, 0, sizeof n->s5b);
    { S5b *s = &n->s5b; for (int i = 0; i < 3; i++) s->tcnt[i] = 16; s->ncnt = 32; s->ecnt = 16; s->lfsr = 1; s->ehold = 1; }
    a->n163 = MAPPER(&n->cart) == 19;
    memset(&n->n163, 0, sizeof n->n163);
    n->n163.ch = 7; n->n163.cnt = 15;
    s5b_lvl[31] = 1.0f;                                /* -1.5 dB por paso: 10^(-1.5/20) (sin libm: no todas las herramientas la enlazan) */
    for (int i = 30; i >= 1; i--) s5b_lvl[i] = s5b_lvl[i + 1] * 0.8413951416f;
    a->d.rate = dmc_tab[0]; a->d.timer = dmc_tab[0]; a->d.buf_empty = 1; a->d.silence = 1; a->d.bits = 8;
    a->cps = CPU_HZ / APU_RATE;
    a->rate_mul = 1.0;
    a->next = a->cps;
    a->delay = 0;                       /* como si $4017 = 0 se hubiera escrito ~10 ciclos antes de la primera instrucción */
}

/* ---- Envolvente, barrido y contadores ---- */

static void env_clock(u8 *start, u8 *div, u8 *decay, u8 vol, u8 loop)
{
    if (*start) { *start = 0; *decay = 15; *div = vol; return; }
    if (*div == 0) {
        *div = vol;
        if (*decay) (*decay)--;
        else if (loop) *decay = 15;
    } else (*div)--;
}

static int sweep_target(const Pulse *p, int ch)
{
    int d = p->period >> p->sw_shift;
    if (p->sw_neg) return p->period - d - (ch == 0);
    return p->period + d;
}

static void sweep_clock(Pulse *p, int ch)
{
    int tgt = sweep_target(p, ch);
    if (p->sw_div == 0 && p->sw_en && p->sw_shift && p->period >= 8 && tgt <= 0x7FF)
        p->period = (u16)tgt;
    if (p->sw_div == 0 || p->sw_reload) { p->sw_div = p->sw_per; p->sw_reload = 0; }
    else p->sw_div--;
}

static void quarter(Apu *a)
{
    for (int i = 0; i < 2; i++)
        env_clock(&a->p[i].env_start, &a->p[i].env_div, &a->p[i].env_decay, a->p[i].vol, a->p[i].halt);
    env_clock(&a->nz.env_start, &a->nz.env_div, &a->nz.env_decay, a->nz.vol, a->nz.halt);
    if (a->t.lin_reload) a->t.lin = a->t.lin_val;
    else if (a->t.lin) a->t.lin--;
    if (!a->t.ctrl) a->t.lin_reload = 0;
}

static void half(Apu *a)
{
    for (int i = 0; i < 2; i++) {
        if (!a->p[i].lhalt && a->p[i].len) a->p[i].len--;
        sweep_clock(&a->p[i], i);
    }
    if (!a->t.lhalt && a->t.len) a->t.len--;
    if (!a->nz.lhalt && a->nz.len) a->nz.len--;
}

/* ---- VRC6 ---- */

/* Cada canal tiene un divisor de 12 bits (más corto con $9003: x16 o x256) que avanza un paso al vencer. Los pulsos
 * dan su volumen mientras el paso (de 15 a 0) no pasa del ancho de pulso, o siempre en modo constante; el diente de
 * sierra suma 'rate' cada dos pasos y vuelve a 0 a los 14. */
static u32 vrc6_period(const Vrc6 *v, int i)
{
    return v->period[i] >> ((v->ctrl & 4) ? 8 : (v->ctrl & 2) ? 4 : 0);
}

static int vrc6_out(const Vrc6 *v)
{
    int o = 0;
    for (int i = 0; i < 2; i++)
        if (v->en[i] && (v->mode[i] || v->step[i] <= v->duty[i])) o += v->vol[i];
    if (v->en[2]) o += v->acc >> 3;
    return o;
}

static void vrc6_clock(Vrc6 *v)
{
    if (v->ctrl & 1) return;
    for (int i = 0; i < 3; i++) {
        if (!v->en[i]) continue;
        if (v->timer[i]) { v->timer[i]--; continue; }
        v->timer[i] = (u16)vrc6_period(v, i);
        if (i < 2) v->step[i] = (v->step[i] - 1) & 15;
        else if (++v->sstep == 14) { v->sstep = 0; v->acc = 0; }
        else if (!(v->sstep & 1)) v->acc = (u8)(v->acc + v->rate);
    }
}

static u64 vrc6_skip(const NES *n)                 /* ciclos que se pueden saltar sin que venza un divisor */
{
    const Vrc6 *v = &n->vrc6;
    u64 k = ~0ull;
    if (v->ctrl & 1) return k;
    for (int i = 0; i < 3; i++) if (v->en[i] && v->timer[i] < k) k = v->timer[i];
    return k;
}

void apu_vrc6_write(NES *n, u16 reg, u8 x)
{
    apu_sync(n);
    Vrc6 *v = &n->vrc6;
    int i = ((reg >> 12) & 0xF) - 9;                   /* 0, 1 = pulsos ($9000, $A000), 2 = sierra ($B000) */
    switch (reg & 3) {
    case 0:
        if (i < 2) { v->mode[i] = x >> 7; v->duty[i] = (x >> 4) & 7; v->vol[i] = x & 15; }
        else v->rate = x & 0x3F;
        break;
    case 1: v->period[i] = (u16)((v->period[i] & 0xF00) | x); break;
    case 2:
        v->period[i] = (u16)((v->period[i] & 0xFF) | ((x & 15) << 8));
        v->en[i] = x >> 7;
        if (!v->en[i]) { if (i < 2) v->step[i] = 15; else { v->acc = 0; v->sstep = 0; } }
        break;
    case 3: if (i == 0) v->ctrl = x & 7; break;
    }
}

/* ---- Sunsoft 5B ---- */

static u32 s5b_tone_reload(const S5b *s, int i)
{
    u32 p = s->reg[2 * i] | ((s->reg[2 * i + 1] & 15) << 8);
    return 16 * (p ? p : 1);
}
static u32 s5b_noise_reload(const S5b *s) { u32 p = s->reg[6] & 31; return 32 * (p ? p : 1); }
static u32 s5b_env_reload(const S5b *s) { u32 p = s->reg[11] | (s->reg[12] << 8); return 16 * (p ? p : 1); }

static int s5b_env_level(const S5b *s) { return s->eatt ? s->estep : 31 - s->estep; }

static float s5b_out(const S5b *s)
{
    float o = 0;
    for (int i = 0; i < 3; i++) {
        int on = (s->tout[i] | ((s->reg[7] >> i) & 1)) & ((s->lfsr & 1) | ((s->reg[7] >> (i + 3)) & 1));
        if (!on) continue;
        u8 v = s->reg[8 + i];
        o += s5b_lvl[(v & 0x10) ? s5b_env_level(s) : (v & 15) ? 2 * (v & 15) + 1 : 0];
    }
    return o;
}

static void s5b_env_step(S5b *s)
{
    if (s->ehold) return;
    if (++s->estep < 32) return;
    u8 sh = s->reg[13];                                /* fin de un ciclo de 32 pasos: CONT, ATT, ALT, HOLD */
    if (!(sh & 8)) { s->ehold = 1; s->estep = 31; s->eatt = 0; return; }     /* sin CONT: queda en 0 */
    if (sh & 1) { s->ehold = 1; s->estep = 31; if (sh & 2) s->eatt ^= 1; return; }
    if (sh & 2) s->eatt ^= 1;
    s->estep = 0;
}

static void s5b_clock(S5b *s)
{
    for (int i = 0; i < 3; i++) if (--s->tcnt[i] == 0) { s->tcnt[i] = (u16)s5b_tone_reload(s, i); s->tout[i] ^= 1; }
    if (--s->ncnt == 0) {
        s->ncnt = (u16)s5b_noise_reload(s);
        s->lfsr = (s->lfsr >> 1) | (((s->lfsr ^ (s->lfsr >> 3)) & 1) << 16);
    }
    if (--s->ecnt == 0) { s->ecnt = s5b_env_reload(s); s5b_env_step(s); }
}

static u64 s5b_skip(const NES *n)                  /* ciclos que se pueden saltar sin llegar a un evento */
{
    const S5b *s = &n->s5b;
    u64 k = s->ncnt - 1;
    for (int i = 0; i < 3; i++) if ((u64)s->tcnt[i] - 1 < k) k = s->tcnt[i] - 1;
    if (!s->ehold && (u64)s->ecnt - 1 < k) k = s->ecnt - 1;
    return k;
}

void apu_5b_write(NES *n, int data, u8 v)
{
    S5b *s = &n->s5b;
    if (!data) { s->sel = v & 15; return; }
    apu_sync(n);
    s->reg[s->sel] = v;
    if (s->sel == 13) { s->estep = 0; s->ehold = 0; s->eatt = (v >> 2) & 1; s->ecnt = s5b_env_reload(s); }
}

/* ---- Namco 163 ---- */

static int n163_channels(const N163 *s) { return ((s->ram[0x7F] >> 4) & 7) + 1; }

static void n163_update(N163 *s)                    /* actualiza el canal que toca y pasa al siguiente */
{
    u8 *r = &s->ram[0x40 + 8 * s->ch];
    u32 freq = r[0] | (r[2] << 8) | ((r[4] & 3) << 16);
    u32 phase = r[1] | (r[3] << 8) | (r[5] << 16);
    u32 len = (256 - (r[4] & 0xFC)) << 16;
    phase = (phase + freq) % len;
    r[1] = (u8)phase; r[3] = (u8)(phase >> 8); r[5] = (u8)(phase >> 16);
    u8 idx = (u8)((phase >> 16) + r[6]);
    u8 b = s->ram[idx >> 1];
    int smp = (idx & 1) ? b >> 4 : b & 15;
    s->out[s->ch] = (smp - 8) * (r[7] & 15);
    s->ch = s->ch <= 8 - n163_channels(s) ? 7 : s->ch - 1;
}

static void n163_clock(N163 *s) { if (!s->off && --s->cnt == 0) { s->cnt = 15; n163_update(s); } }

static float n163_out(const N163 *s)                /* el chip muestra los canales de a uno: en promedio, la media */
{
    int n = n163_channels(s), o = 0;
    for (int c = 8 - n; c < 8; c++) o += s->out[c];
    return (float)o / n;
}

void apu_n163_addr(NES *n, u8 v) { n->n163.addr = v; }

void apu_n163_off(NES *n, int off) { if (n->n163.off != off) { apu_sync(n); n->n163.off = (u8)off; } }

u8 apu_n163_data(NES *n, int write, u8 v)
{
    N163 *s = &n->n163;
    if (write) { apu_sync(n); s->ram[s->addr & 0x7F] = v; }
    else v = s->ram[s->addr & 0x7F];
    if (s->addr & 0x80) s->addr = (u8)(0x80 | ((s->addr + 1) & 0x7F));
    return v;
}

/* ---- DMC ---- */

/* El DMC pide un byte: el CPU se detiene en su próxima lectura y cumple el DMA (ver dma_run en nes.c) */
__attribute__((noinline, cold)) static void dmc_fetch(NES *n, int boundary)
{
    Dmc *d = &n->apu.d;
    if (!d->buf_empty || (n->stall & 1) || n->dmc_left || n->apu.cycle - 1 == n->dmc_get_cyc) return;
    if (boundary && n->apu.dmc_implicit) {      /* solo en un límite de byte, no en la DMA de carga */
        n->apu.dmc_implicit = 0;
        n->dmc_implicit_cyc = n->apu.cycle;          /* el ciclo siguiente al de este reloj (a->cycle ya avanzó) */
    } else if (!d->remaining) return;
    n->stall |= 1;
}

u16 apu_dmc_addr(const NES *n) { return n->apu.d.cur; }

/* Al principio de cada ciclo de una DMA del DMC: si $4015 ya apagó el DMC, la DMA se aborta (aborto explícito) */
int apu_dmc_dma_live(NES *n)
{
    apu_sync(n);
    return n->apu.d.on;
}

void apu_dmc_dma_done(NES *n, u8 v)
{
    Dmc *d = &n->apu.d;
    if (!d->remaining || !d->buf_empty) return;          /* el DMA se canceló mientras esperaba */
    d->buf = v;
    d->buf_empty = 0;
    d->cur = (u16)((d->cur + 1) | 0x8000);
    if (--d->remaining == 0) {
        if (d->loop) { d->cur = d->addr; d->remaining = d->length; }
        else { d->on = 0; if (d->irq_en) n->irq_line |= 4; }
    }
}

/* Lo que $4015 dejó pendiente: la DMA de carga (sale en el segundo ciclo put) y el bit del DMC, que se aplica con
 * retraso. Fuera de apu_cycle para no engordar la ruta de cada ciclo. */
__attribute__((noinline, cold)) static void dmc_pending(NES *n)
{
    Apu *a = &n->apu;
    if (a->d.load_delay && !(a->cycle & 1) && --a->d.load_delay == 0) dmc_fetch(n, 0);   /* ciclo put (a->cycle ya avanzó) */
    if (a->dmc_delay && --a->dmc_delay == 0) {
        a->d.on = a->dmc_next;
        if (!a->d.on) a->d.remaining = 0;
        else if (n->dmc_left) { n->stall |= 1 | ((n->dmc_left & 2) << 1); n->dmc_left = 0; }   /* la DMA abortada sigue */
    }
}

static void dmc_clock(NES *n)
{
    Dmc *d = &n->apu.d;
    if (d->timer) { d->timer--; return; }
    d->timer = d->rate - 1;
    if (!d->silence) {
        if (d->shift & 1) { if (d->out <= 125) d->out += 2; }
        else if (d->out >= 2) d->out -= 2;
    }
    d->shift >>= 1;
    if (--d->bits == 0) {
        d->bits = 8;
        if (d->buf_empty) d->silence = 1;
        else { d->silence = 0; d->shift = d->buf; d->buf_empty = 1; }
        dmc_fetch(n, 1);                             /* en cada límite de byte, si el búfer quedó vacío */
    }
}

/* ---- Un ciclo de CPU ---- */

static int pulse_out(const Pulse *p, int ch)
{
    if (!p->on || !p->len || p->period < 8) return 0;
    if (ch < 2 && !p->sw_neg && sweep_target(p, ch) > 0x7FF) return 0;       /* los pulsos del MMC5 no tienen barrido */
    if (!duty_tab[p->duty][p->step]) return 0;
    return p->cst ? p->vol : p->env_decay;
}

static void len_apply(u8 *len, u8 *lhalt, u8 hnew, u8 *rel, u8 prev)
{
    if (*rel) { if (*len == prev) *len = *rel; *rel = 0; }
    *lhalt = hnew;
}

static void apu_cycle(NES *n)
{
    Apu *a = &n->apu;
    a->cycle++;
    if (a->m5) {                                    /* envolvente y longitud de los pulsos del MMC5: 240 Hz */
        a->m5_acc += 10;
        if (a->m5_acc >= 74574) {
            a->m5_acc -= 74574;
            for (int i = 2; i < 4; i++) {
                Pulse *q = &a->p[i];
                env_clock(&q->env_start, &q->env_div, &q->env_decay, q->vol, q->halt);
                if (!q->lhalt && q->len) q->len--;
            }
        }
    }

    /* frame counter */
    if (a->delay && --a->delay == 0) {
        a->fcc = 0; a->mode5 = (a->pending >> 7) & 1;
        if (a->mode5) { quarter(a); half(a); }
    } else {
        a->fcc++;
        if (!a->mode5) {
            switch (a->fcc) {
            case 7457: quarter(a); break;
            case 14913: quarter(a); half(a); break;
            case 22371: quarter(a); break;
            case 29828: if (!a->inhibit) { a->frame_irq = 1; n->irq_line |= 2; } break;
            case 29829: quarter(a); half(a); if (!a->inhibit) { a->frame_irq = 1; n->irq_line |= 2; } break;
            case 29830: if (!a->inhibit) { a->frame_irq = 1; n->irq_line |= 2; } a->fcc = 0; break;
            }
        } else {
            switch (a->fcc) {
            case 7457: quarter(a); break;
            case 14913: quarter(a); half(a); break;
            case 22371: quarter(a); break;
            case 37281: quarter(a); half(a); break;
            case 37282: a->fcc = 0; break;
            }
        }
    }

    /* triángulo: un paso por ciclo de CPU */
    Triangle *t = &a->t;
    if (t->timer) t->timer--;
    else {
        t->timer = t->period;
        if (t->len && t->lin && t->period >= 2) t->step = (t->step + 1) & 31;
    }

    /* pulsos: cada 2 ciclos de CPU */
    if (a->cycle & 1)
        for (int i = 0; i < (a->m5 ? 4 : 2); i++) {
            Pulse *p = &a->p[i];
            if (p->timer) p->timer--;
            else { p->timer = p->period; p->step = (p->step - 1) & 7; }
        }

    /* ruido */
    Noise *z = &a->nz;
    if (z->timer) z->timer--;
    else {
        z->timer = noise_tab[z->period] - 1;
        int fb = (z->shift & 1) ^ ((z->shift >> (z->mode ? 6 : 1)) & 1);
        z->shift = (z->shift >> 1) | (fb << 14);
    }

    dmc_clock(n);
    if (__builtin_expect(a->d.load_delay | a->dmc_delay, 0)) dmc_pending(n);
    if (a->vrc6) vrc6_clock(&n->vrc6);
    if (a->s5b) s5b_clock(&n->s5b);
    if (a->n163) n163_clock(&n->n163);

    /* Cargas del contador de longitud y cambios del halt escritos en este ciclo: se aplican después del
     * paso del frame counter. Una carga se ignora si el reloj de longitud tocó el contador en este ciclo. */
    len_apply(&a->p[0].len, &a->p[0].lhalt, a->p[0].hnew, &a->p[0].rel, a->p[0].prev);
    len_apply(&a->p[1].len, &a->p[1].lhalt, a->p[1].hnew, &a->p[1].rel, a->p[1].prev);
    if (a->m5) {
        len_apply(&a->p[2].len, &a->p[2].lhalt, a->p[2].hnew, &a->p[2].rel, a->p[2].prev);
        len_apply(&a->p[3].len, &a->p[3].lhalt, a->p[3].hnew, &a->p[3].rel, a->p[3].prev);
    }
    len_apply(&a->t.len, &a->t.lhalt, a->t.hnew, &a->t.rel, a->t.prev);
    len_apply(&a->nz.len, &a->nz.lhalt, a->nz.hnew, &a->nz.rel, a->nz.prev);

    /* mezcla */
    int p = ((a->mute & 1) ? 0 : pulse_out(&a->p[0], 0)) + ((a->mute & 2) ? 0 : pulse_out(&a->p[1], 1));
    int tri = (a->mute & 4) ? 0 : tri_tab[t->step];
    int nz = (!z->on || !z->len || (z->shift & 1) || (a->mute & 8)) ? 0 : (z->cst ? z->vol : z->env_decay);
    int dm = (a->mute & 16) ? 0 : a->d.out;
    float mix = pulse_mix[p] + tnd_mix[3 * tri + 2 * nz + dm];
    if (a->m5) mix += pulse_mix[pulse_out(&a->p[2], 2) + pulse_out(&a->p[3], 3)] + tnd_mix[a->m5_pcm >> 1];
    if (a->vrc6) mix += vrc6_out(&n->vrc6) * VRC6_UNIT;
    if (a->s5b) mix += s5b_out(&n->s5b) * S5B_UNIT;
    if (a->n163) mix += n163_out(&n->n163) * N163_UNIT;
    a->acc += mix; a->acc_n += 1;
    if (--a->next <= 0) {
        float x = (float)(a->acc / a->acc_n);
        a->acc = 0; a->acc_n = 0;
        a->next += a->cps / a->rate_mul;
        /* filtros: paso alto 90 y 440 Hz, paso bajo 14 kHz */
        static double k1, k2, kl;
        static int filters_ready;
        if (!filters_ready) {
            const double dt = 1.0 / APU_RATE;
            k1 = (1 / (2 * M_PI * 90)) / ((1 / (2 * M_PI * 90)) + dt);
            k2 = (1 / (2 * M_PI * 440)) / ((1 / (2 * M_PI * 440)) + dt);
            kl = dt / ((1 / (2 * M_PI * 14000)) + dt);
            filters_ready = 1;
        }
        a->hp1 = k1 * (a->hp1 + x - a->in1); a->in1 = x;
        a->hp2 = k2 * (a->hp2 + a->hp1 - a->in2); a->in2 = a->hp1;
        a->lp += kl * (a->hp2 - a->lp);
        double o = a->lp * 60000.0;
        if (o > 32767) o = 32767; if (o < -32768) o = -32768;
        if (a->nbuf < APU_BUF) a->buf[a->nbuf++] = (int16_t)o;
    }
}

/* ---- Ejecución por eventos ----
 * El APU no avanza ciclo a ciclo: entre dos eventos (un temporizador que vence, un paso del frame
 * counter, una muestra de salida) nada cambia, así que esos ciclos se saltan de golpe. El resultado
 * es idéntico bit a bit al de ejecutar cada ciclo. Solo se ejecuta "al día" cuando hace falta:
 * al acceder a sus registros, al leer el audio y cuando un evento puede verse desde fuera
 * (IRQ del frame counter, lectura de muestras del DMC). */

/* Un contador que en cada tick baja de 'timer' y, al llegar a 0, se recarga con 'period' y "vence": cuántas veces
 * vence en T ticks y cómo queda. Vence una vez cada period + 1 ticks. */
static u64 adv_timer(u32 *timer, u32 period, u64 T)
{
    if (T <= *timer) { *timer -= (u32)T; return 0; }
    u64 r = T - *timer - 1, per = (u64)period + 1;
    *timer = (u32)(period - (u32)(r % per));
    return 1 + r / per;
}

/* ¿Es inaudible el canal (su salida es 0 pase lo que pase con su secuenciador)? Sus eventos solo cambian estado
 * interno y se pueden adelantar de golpe. */
static int pulse_inert(const Apu *a, int i)
{
    const Pulse *p = &a->p[i];
    if (i < 2 && ((a->mute >> i) & 1)) return 1;
    if (!p->on || !p->len || p->period < 8) return 1;
    if (i < 2 && !p->sw_neg && sweep_target(p, i) > 0x7FF) return 1;
    return (p->cst ? p->vol : p->env_decay) == 0;
}
static int tri_inert(const Apu *a)
{
    return a->t.period < 2 || !a->t.len || !a->t.lin;       /* el paso no avanza */
}
static int noise_inert(const Apu *a)
{
    const Noise *z = &a->nz;
    return (a->mute & 8) || !z->on || !z->len || (z->cst ? z->vol : z->env_decay) == 0;
}

/* Cuántos ciclos se pueden saltar sin que ocurra ningún evento */
static u64 safe_skip(const Apu *a)
{
    u64 k = ~0ull;
    if (a->p[0].rel | a->p[1].rel | a->t.rel | a->nz.rel | (a->p[0].lhalt ^ a->p[0].hnew) | (a->p[1].lhalt ^ a->p[1].hnew) |
        (a->t.lhalt ^ a->t.hnew) | (a->nz.lhalt ^ a->nz.hnew) |
        (a->m5 && (a->p[2].rel | a->p[3].rel | (a->p[2].lhalt ^ a->p[2].hnew) | (a->p[3].lhalt ^ a->p[3].hnew)))) return 0;     /* hay un cambio por aplicar en el próximo ciclo */
    #define LIM(v) do { u64 v_ = (v); if (v_ < k) k = v_; } while (0)
    if (!tri_inert(a)) LIM(a->t.timer);
    if (!noise_inert(a)) LIM(a->nz.timer);
    LIM(a->d.timer);
    if (a->d.load_delay | a->dmc_delay) return 0;    /* DMA de carga o $4015 pendiente: ciclo a ciclo */
    if (a->m5) LIM(((74574 - (u64)a->m5_acc + 9) / 10) - 1);       /* reloj de 240 Hz del MMC5 */
    for (int i = 0; i < (a->m5 ? 4 : 2); i++) {
        if (pulse_inert(a, i)) continue;
        /* los pulsos avanzan en los ciclos impares: cuántos ciclos caben con timer pasos */
        u64 c0 = a->cycle;
        LIM(2 * (u64)a->p[i].timer + 2 * ((c0 + 1) >> 1) - c0);
    }
    /* frame counter: el evento ocurre en el ciclo que lleva fcc a E */
    static const int ev4[] = { 7457, 14913, 22371, 29828, 29829, 29830 };
    static const int ev5[] = { 7457, 14913, 22371, 37281, 37282 };
    const int *ev = a->mode5 ? ev5 : ev4;
    int nev = a->mode5 ? 5 : 6;
    for (int i = 0; i < nev; i++)
        if (ev[i] > a->fcc) { LIM((u64)(ev[i] - a->fcc) - 1); break; }
    if (a->delay) LIM((u64)a->delay - 1);
    /* la muestra de salida: se emite en el ciclo en que 'next' llega a 0 */
    if (a->next > 1) LIM((u64)ceil(a->next) - 1); else k = 0;
    #undef LIM
    return k;
}

/* Avanza k ciclos sin eventos (salvo los de canales inaudibles, que solo cambian estado interno) */
static void bulk(NES *n, u64 k)
{
    Apu *a = &n->apu;
    u64 c0 = a->cycle;
    u64 pulse_ticks = ((c0 + k + 1) >> 1) - ((c0 + 1) >> 1);
    a->cycle += k;
    a->fcc += (int)k;
    if (a->delay) a->delay -= (int)k;
    {   /* triángulo: si es inaudible el paso no avanza; solo se recarga el temporizador */
        u32 t = a->t.timer;
        adv_timer(&t, a->t.period, k);
        a->t.timer = (u16)t;
    }
    {   /* ruido: cada vencimiento mueve el registro de desplazamiento */
        u32 t = a->nz.timer;
        u64 f = adv_timer(&t, noise_tab[a->nz.period] - 1, k);
        a->nz.timer = (u16)t;
        u16 sh = a->nz.shift;
        int bit = a->nz.mode ? 6 : 1;
        for (u64 i = 0; i < f; i++) sh = (u16)((sh >> 1) | ((((sh & 1) ^ ((sh >> bit) & 1))) << 14));
        a->nz.shift = sh;
    }
    if (a->m5) a->m5_acc += (u32)(10 * k);
    for (int i = 0; i < (a->m5 ? 4 : 2); i++) {
        u32 t = a->p[i].timer;
        u64 f = adv_timer(&t, a->p[i].period, pulse_ticks);
        a->p[i].timer = (u16)t;
        a->p[i].step = (u8)((a->p[i].step - (u32)(f & 7)) & 7);
    }
    {   u32 t = a->d.timer;
        adv_timer(&t, a->d.rate - 1, k);                 /* (safe_skip no deja pasar un vencimiento del DMC) */
        a->d.timer = (u16)t;
    }
    /* la mezcla no cambia en estos ciclos */
    int p = ((a->mute & 1) ? 0 : pulse_out(&a->p[0], 0)) + ((a->mute & 2) ? 0 : pulse_out(&a->p[1], 1));
    int tri = (a->mute & 4) ? 0 : tri_tab[a->t.step];
    int nz = (!a->nz.on || !a->nz.len || (a->nz.shift & 1) || (a->mute & 8)) ? 0 :
             (a->nz.cst ? a->nz.vol : a->nz.env_decay);
    int dm = (a->mute & 16) ? 0 : a->d.out;
    float mix = pulse_mix[p] + tnd_mix[3 * tri + 2 * nz + dm];
    if (a->m5) mix += pulse_mix[pulse_out(&a->p[2], 2) + pulse_out(&a->p[3], 3)] + tnd_mix[a->m5_pcm >> 1];
    if (a->vrc6) {                                  /* (advance no deja pasar un vencimiento del VRC6) */
        Vrc6 *v = &n->vrc6;
        if (!(v->ctrl & 1)) for (int i = 0; i < 3; i++) if (v->en[i]) v->timer[i] = (u16)(v->timer[i] - k);
        mix += vrc6_out(v) * VRC6_UNIT;
    }
    if (a->s5b) {                                   /* (advance no deja llegar a un evento del 5B) */
        S5b *s = &n->s5b;
        for (int i = 0; i < 3; i++) s->tcnt[i] = (u16)(s->tcnt[i] - k);
        s->ncnt = (u16)(s->ncnt - k);
        if (!s->ehold) s->ecnt -= (u32)k;
        mix += s5b_out(s) * S5B_UNIT;
    }
    /* k sumas de 'mix' (un float) en un double: exactas en ambos casos (menos de 50 bits), así que
     * una multiplicación da el mismo resultado que sumar ciclo a ciclo */
    a->acc += (double)mix * (double)k;
    if (a->n163) {                                  /* el Namco 163 cambia cada 15 ciclos: se suma por tramos sin partir el salto */
        N163 *s = &n->n163;
        double sum = 0;
        u64 rem = k;
        while (rem) {
            u64 run = s->off ? rem : (u64)s->cnt - 1 < rem ? (u64)s->cnt - 1 : rem;   /* ciclos sin actualización */
            sum += n163_out(s) * (double)run; rem -= run; s->cnt = (u8)(s->cnt - run);
            if (rem) { s->cnt = 15; n163_update(s); sum += n163_out(s); rem--; }   /* el ciclo de la actualización */
        }
        a->acc += sum * N163_UNIT;
    }
    a->acc_n += (double)k;
    a->next -= (double)k;
}

/* Próximo ciclo (cuenta absoluta) en que puede pasar algo visible desde fuera */
static void set_deadline(Apu *a)
{
    u64 d = ~0ull;
    if (!a->mode5 && !a->inhibit) {                 /* IRQ del frame counter */
        u64 e = a->delay ? a->cycle + (u64)a->delay : a->cycle + (u64)(a->fcc < 29828 ? 29828 - a->fcc : 1);
        if (e < d) d = e;
    }
    if (a->d.remaining) {                          /* el DMC puede pedir un byte (DMA) o levantar su IRQ */
        u64 e = a->cycle + (u64)a->d.timer + 1;
        if (e < d) d = e;
    }
    if (a->d.load_delay) {                         /* la DMA de carga sale en el load_delay-ésimo ciclo put (impar) */
        u64 e = (a->cycle | 1) + 2 * (u64)(a->d.load_delay - 1) + 1;
        if (e < d) d = e;
    }
    if (a->dmc_delay && a->cycle + a->dmc_delay < d) d = a->cycle + a->dmc_delay;   /* $4015 puede reanudar una DMA abortada */
    a->deadline = d;
}

static void advance(NES *n, u64 target)
{
    Apu *a = &n->apu;
    while (a->cycle < target) {
        u64 left = target - a->cycle;
        u64 k = safe_skip(a);
        if (a->vrc6) { u64 kv = vrc6_skip(n); if (kv < k) k = kv; }
        if (a->s5b) { u64 ks = s5b_skip(n); if (ks < k) k = ks; }
        if (k > left) k = left;
        if (k) bulk(n, k);
        if (a->cycle < target) apu_cycle(n);
    }
    set_deadline(a);
}

void apu_sync(NES *n)
{
    if (n->apu.cycle < n->cyc) advance(n, n->cyc);
}

/* Al final de cada ciclo de CPU (n->cyc cuenta los ciclos ya terminados menos este): solo si algo
 * visible puede haber pasado en el ciclo que termina */
void apu_check(NES *n)
{
    if (n->cyc + 1 >= n->apu.deadline) advance(n, n->cyc + 1);
}

int apu_take(NES *n, int16_t *dst, int max)
{
    apu_sync(n);
    Apu *a = &n->apu;
    int k = a->nbuf < max ? a->nbuf : max;
    memcpy(dst, a->buf, k * sizeof *dst);
    memmove(a->buf, a->buf + k, (a->nbuf - k) * sizeof *dst);
    a->nbuf -= k;
    return k;
}

/* ---- Registros ---- */

u8 apu_read(NES *n, u16 addr)
{
    apu_sync(n);
    Apu *a = &n->apu;
    (void)addr;
    u8 r = (a->p[0].len ? 1 : 0) | (a->p[1].len ? 2 : 0) | (a->t.len ? 4 : 0) | (a->nz.len ? 8 : 0)
         | (a->d.remaining ? 0x10 : 0) | (a->frame_irq ? 0x40 : 0) | ((n->irq_line & 4) ? 0x80 : 0);
    a->frame_irq = 0; n->irq_line &= ~2;
    return r;
}

static void pulse_write(Pulse *p, int reg, u8 v)
{
    switch (reg) {
    case 0: p->duty = v >> 6; p->halt = p->hnew = (v >> 5) & 1; p->cst = (v >> 4) & 1; p->vol = v & 15; break;
    case 1: p->sw_en = v >> 7; p->sw_per = (v >> 4) & 7; p->sw_neg = (v >> 3) & 1;
            p->sw_shift = v & 7; p->sw_reload = 1; break;
    case 2: p->period = (p->period & 0x700) | v; break;
    case 3: p->period = (p->period & 0xFF) | ((v & 7) << 8);
            if (p->on) { p->rel = len_tab[v >> 3]; p->prev = p->len; }   /* la carga se decide al final del ciclo */
            p->step = 0; p->env_start = 1; break;
    }
}

void apu_write(NES *n, u16 addr, u8 v)
{
    apu_sync(n);
    Apu *a = &n->apu;
    switch (addr) {
    case 0x4000: case 0x4001: case 0x4002: case 0x4003: pulse_write(&a->p[0], addr & 3, v); break;
    case 0x4004: case 0x4005: case 0x4006: case 0x4007: pulse_write(&a->p[1], addr & 3, v); break;
    case 0x4008: a->t.ctrl = a->t.hnew = v >> 7; a->t.lin_val = v & 0x7F; break;
    case 0x400A: a->t.period = (a->t.period & 0x700) | v; break;
    case 0x400B:
        a->t.period = (a->t.period & 0xFF) | ((v & 7) << 8);
        if (a->t.on) { a->t.rel = len_tab[v >> 3]; a->t.prev = a->t.len; }
        a->t.lin_reload = 1; break;
    case 0x400C: a->nz.halt = a->nz.hnew = (v >> 5) & 1; a->nz.cst = (v >> 4) & 1; a->nz.vol = v & 15; break;
    case 0x400E: a->nz.mode = v >> 7; a->nz.period = v & 15; break;
    case 0x400F: if (a->nz.on) { a->nz.rel = len_tab[v >> 3]; a->nz.prev = a->nz.len; } a->nz.env_start = 1; break;
    case 0x4010:
        a->d.irq_en = v >> 7; a->d.loop = (v >> 6) & 1; a->d.rate = dmc_tab[v & 15];
        if (!a->d.irq_en) n->irq_line &= ~4;
        break;
    case 0x4011: a->d.out = v & 0x7F; break;
    case 0x4012: a->d.addr = 0xC000 | (v << 6); break;
    case 0x4013: a->d.length = (v << 4) | 1; break;
    case 0x4015:
        a->p[0].on = v & 1; a->p[1].on = (v >> 1) & 1; a->t.on = (v >> 2) & 1; a->nz.on = (v >> 3) & 1;
        if (!a->p[0].on) a->p[0].len = a->p[0].rel = 0;
        if (!a->p[1].on) a->p[1].len = a->p[1].rel = 0;
        if (!a->t.on) a->t.len = a->t.rel = 0;
        if (!a->nz.on) a->nz.len = a->nz.rel = 0;
        n->irq_line &= ~4;
        {
            /* El bit del DMC se aplica después: impar = put: 3 ciclos; par = get: 4 (cuentan este mismo ciclo). Al
             * apagar, si la DMA del DMC sale en este ciclo o salió en el anterior, espera a que termine: 5 o 6. Al
             * encender justo antes de un reloj del DMC, en ese límite de byte sale una DMA de un ciclo (aborto
             * implícito). Mismo modelo que TriCNES. */
            int put = a->cycle & 1, on = (v >> 4) & 1;
            int firing = !on && (put ? a->d.timer == a->d.rate - 1 : a->d.timer == 0);
            a->dmc_delay = (u8)(firing ? (put ? 5 : 6) : (put ? 3 : 4));
            a->dmc_next = (u8)on;
            if (on && a->d.timer == (put ? 7 : 8)) a->dmc_implicit = 1;
            if (on && !a->d.remaining) {
                a->d.cur = a->d.addr; a->d.remaining = a->d.length;
                if (a->d.buf_empty) a->d.load_delay = 2;    /* DMA de carga: tras dos ciclos put */
            }
        }
        break;
    case 0x4017:
        a->inhibit = (v >> 6) & 1;
        if (a->inhibit) { a->frame_irq = 0; n->irq_line &= ~2; }
        a->pending = v;
        a->delay = (a->cycle & 1) ? 3 : 4;               /* impar = put: 3; par = get: 4 */
        break;
    }
    set_deadline(a);
}

/* Audio del MMC5: dos pulsos como los del 2A03 pero sin barrido ($5000-$5007; $5015 los habilita) */
void apu_mmc5_write(NES *n, u16 addr, u8 v)
{
    Apu *a = &n->apu;
    apu_sync(n);
    if (addr >= 0x5000 && addr <= 0x5007) {
        int ch = 2 + ((addr - 0x5000) >> 2), reg = addr & 3;
        if (reg != 1) pulse_write(&a->p[ch], reg, v);
    } else if (addr == 0x5010) {
        a->m5_pcm_read = v & 1;
    } else if (addr == 0x5011) {
        if (!a->m5_pcm_read && v) a->m5_pcm = v;
    } else if (addr == 0x5015) {
        a->p[2].on = v & 1; a->p[3].on = (v >> 1) & 1;
        if (!a->p[2].on) a->p[2].len = a->p[2].rel = 0;
        if (!a->p[3].on) a->p[3].len = a->p[3].rel = 0;
    }
    set_deadline(a);
}

u8 apu_mmc5_status(NES *n)
{
    apu_sync(n);
    return (n->apu.p[2].len ? 1 : 0) | (n->apu.p[3].len ? 2 : 0);
}

void apu_mmc5_pcm(NES *n, u8 v)
{
    apu_sync(n);
    if (v) n->apu.m5_pcm = v;                          /* en modo lectura, un byte de $8000-$BFFF fija la salida (0 se ignora) */
}
