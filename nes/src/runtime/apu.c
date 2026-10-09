/*
 * APU 2A03: dos pulsos, triángulo, ruido y DMC, con el frame counter
 * (secuencia de 4 y 5 pasos, IRQ), contadores de longitud, envolventes y barrido.
 * apu_run avanza ciclos de CPU de uno en uno; la mezcla usa las curvas no lineales
 * del hardware y los filtros (paso alto 90 y 440 Hz, paso bajo 14 kHz).
 */
#include <math.h>
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
    a->d.rate = dmc_tab[0]; a->d.timer = dmc_tab[0]; a->d.buf_empty = 1; a->d.silence = 1; a->d.bits = 8;
    a->cps = CPU_HZ / APU_RATE;
    a->rate_mul = 1.0;
    a->next = a->cps;
    a->delay = 3;                       /* $4017 = 0 al arrancar: reinicio con retraso */
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
        if (!a->p[i].halt && a->p[i].len) a->p[i].len--;
        sweep_clock(&a->p[i], i);
    }
    if (!a->t.ctrl && a->t.len) a->t.len--;
    if (!a->nz.halt && a->nz.len) a->nz.len--;
}

/* ---- DMC ---- */

static void dmc_fetch(NES *n)
{
    Dmc *d = &n->apu.d;
    if (!d->buf_empty || !d->remaining) return;
    d->buf = nes_read(n, d->cur);
    d->buf_empty = 0;
    n->stall += 4;
    d->cur = (u16)((d->cur + 1) | 0x8000);
    if (--d->remaining == 0) {
        if (d->loop) { d->cur = d->addr; d->remaining = d->length; }
        else if (d->irq_en) { n->irq_line |= 4; }
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
        else { d->silence = 0; d->shift = d->buf; d->buf_empty = 1; dmc_fetch(n); }
    }
}

/* ---- Un ciclo de CPU ---- */

static int pulse_out(const Pulse *p, int ch)
{
    if (!p->on || !p->len || p->period < 8) return 0;
    if (!p->sw_neg && sweep_target(p, ch) > 0x7FF) return 0;
    if (!duty_tab[p->duty][p->step]) return 0;
    return p->cst ? p->vol : p->env_decay;
}

static void apu_cycle(NES *n)
{
    Apu *a = &n->apu;
    a->cycle++;

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
        for (int i = 0; i < 2; i++) {
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

    /* mezcla */
    int p = ((a->mute & 1) ? 0 : pulse_out(&a->p[0], 0)) + ((a->mute & 2) ? 0 : pulse_out(&a->p[1], 1));
    int tri = (a->mute & 4) ? 0 : tri_tab[t->step];
    int nz = (!z->on || !z->len || (z->shift & 1) || (a->mute & 8)) ? 0 : (z->cst ? z->vol : z->env_decay);
    int dm = (a->mute & 16) ? 0 : a->d.out;
    float mix = pulse_mix[p] + tnd_mix[3 * tri + 2 * nz + dm];
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

/* Cuántos ciclos se pueden saltar sin que ocurra ningún evento */
static u64 safe_skip(const Apu *a)
{
    u64 k = ~0ull;
    #define LIM(v) do { u64 v_ = (v); if (v_ < k) k = v_; } while (0)
    LIM(a->t.timer);
    LIM(a->nz.timer);
    LIM(a->d.timer);
    for (int i = 0; i < 2; i++) {
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

/* Avanza k ciclos sin eventos */
static void bulk(NES *n, u64 k)
{
    Apu *a = &n->apu;
    u64 c0 = a->cycle;
    u64 pulse_ticks = ((c0 + k + 1) >> 1) - ((c0 + 1) >> 1);
    a->cycle += k;
    a->fcc += (int)k;
    if (a->delay) a->delay -= (int)k;
    a->t.timer -= (u16)k;
    a->nz.timer -= (u16)k;
    a->d.timer -= (u16)k;
    a->p[0].timer -= (u16)pulse_ticks;
    a->p[1].timer -= (u16)pulse_ticks;
    /* la mezcla no cambia en estos ciclos */
    int p = ((a->mute & 1) ? 0 : pulse_out(&a->p[0], 0)) + ((a->mute & 2) ? 0 : pulse_out(&a->p[1], 1));
    int tri = (a->mute & 4) ? 0 : tri_tab[a->t.step];
    int nz = (!a->nz.on || !a->nz.len || (a->nz.shift & 1) || (a->mute & 8)) ? 0 :
             (a->nz.cst ? a->nz.vol : a->nz.env_decay);
    int dm = (a->mute & 16) ? 0 : a->d.out;
    float mix = pulse_mix[p] + tnd_mix[3 * tri + 2 * nz + dm];
    /* k sumas de 'mix' (un float) en un double: exactas en ambos casos (menos de 50 bits), así que
     * una multiplicación da el mismo resultado que sumar ciclo a ciclo */
    a->acc += (double)mix * (double)k;
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
    a->deadline = d;
}

static void advance(NES *n, u64 target)
{
    Apu *a = &n->apu;
    while (a->cycle < target) {
        u64 left = target - a->cycle;
        u64 k = safe_skip(a);
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
    case 0: p->duty = v >> 6; p->halt = (v >> 5) & 1; p->cst = (v >> 4) & 1; p->vol = v & 15; break;
    case 1: p->sw_en = v >> 7; p->sw_per = (v >> 4) & 7; p->sw_neg = (v >> 3) & 1;
            p->sw_shift = v & 7; p->sw_reload = 1; break;
    case 2: p->period = (p->period & 0x700) | v; break;
    case 3: p->period = (p->period & 0xFF) | ((v & 7) << 8);
            if (p->on) p->len = len_tab[v >> 3];
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
    case 0x4008: a->t.ctrl = v >> 7; a->t.lin_val = v & 0x7F; break;
    case 0x400A: a->t.period = (a->t.period & 0x700) | v; break;
    case 0x400B:
        a->t.period = (a->t.period & 0xFF) | ((v & 7) << 8);
        if (a->t.on) a->t.len = len_tab[v >> 3];
        a->t.lin_reload = 1; break;
    case 0x400C: a->nz.halt = (v >> 5) & 1; a->nz.cst = (v >> 4) & 1; a->nz.vol = v & 15; break;
    case 0x400E: a->nz.mode = v >> 7; a->nz.period = v & 15; break;
    case 0x400F: if (a->nz.on) a->nz.len = len_tab[v >> 3]; a->nz.env_start = 1; break;
    case 0x4010:
        a->d.irq_en = v >> 7; a->d.loop = (v >> 6) & 1; a->d.rate = dmc_tab[v & 15];
        if (!a->d.irq_en) n->irq_line &= ~4;
        break;
    case 0x4011: a->d.out = v & 0x7F; break;
    case 0x4012: a->d.addr = 0xC000 | (v << 6); break;
    case 0x4013: a->d.length = (v << 4) | 1; break;
    case 0x4015:
        a->p[0].on = v & 1; a->p[1].on = (v >> 1) & 1; a->t.on = (v >> 2) & 1; a->nz.on = (v >> 3) & 1;
        if (!a->p[0].on) a->p[0].len = 0;
        if (!a->p[1].on) a->p[1].len = 0;
        if (!a->t.on) a->t.len = 0;
        if (!a->nz.on) a->nz.len = 0;
        a->d.on = (v >> 4) & 1;
        n->irq_line &= ~4;
        if (!a->d.on) a->d.remaining = 0;
        else if (!a->d.remaining) { a->d.cur = a->d.addr; a->d.remaining = a->d.length; dmc_fetch(n); }
        break;
    case 0x4017:
        a->inhibit = (v >> 6) & 1;
        if (a->inhibit) { a->frame_irq = 0; n->irq_line &= ~2; }
        a->pending = v;
        a->delay = (a->cycle & 1) ? 4 : 3;
        break;
    }
    set_deadline(a);
}
