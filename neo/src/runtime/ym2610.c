/*
 * YM2610 (OPNB): 4 canales FM de 4 operadores, SSG (3 tonos, ruido y envolvente), 6 canales de
 * ADPCM-A y uno de ADPCM-B, dos temporizadores y la IRQ (la INT del Z80).
 *
 * Puertos del Z80: 4 elige el registro de la primera mitad y 5 lo escribe (SSG $00-$0D, ADPCM-B
 * $10-$1B, banderas $1C, temporizadores y LFO $22-$28, FM 1-2 $30-$B6); 6 y 7 lo mismo con la segunda
 * (ADPCM-A $00-$2D, FM 3-4 $30-$B6). Al leer: 4 es el estado (bit 7 ocupado, bit 1 temporizador B,
 * bit 0 temporizador A), 5 el registro elegido del SSG y 6 las banderas de fin de los ADPCM (bits 0-5
 * A, bit 7 B).
 *
 * El reloj del YM2610 es un tercio del maestro (8 MHz): una muestra del FM cada 144 ciclos suyos (432
 * maestros, 55 555 Hz). En cada una se calculan el FM, 9 pasos del SSG (va a 500 kHz: 8 MHz / 16), el
 * ADPCM-B (avanza Delta-N / 65 536 muestras) y, cada 3, el ADPCM-A (18 518 Hz). Todo sale a la mezcla
 * (audio.c) como un tramo de 432 ciclos maestros.
 *
 * - FM: el YM2612 de GENRecomp (fase, envolvente con SSG-EG, LFO, los 8 algoritmos, canal 3 especial),
 *   sin DAC. Del OPNA solo existen los canales 2, 3, 5 y 6 (índices 1, 2, 4 y 5).
 * - SSG (como el YM2149; wiki de NeoGeoDev, "SSG"): el tono cambia cada TP pasos; el ruido (LFSR de 17
 *   bits) avanza cada 2 × NP; la envolvente da 32 pasos, uno cada EP, con las formas de $0D (seguir,
 *   subir, alternar, quedarse). Volumen fijo de 4 bits o el de la envolvente, de 5, en pasos de 1,5 dB.
 * - ADPCM-A (wiki, "ADPCM"): 4 bits por muestra, el nibble alto primero; tabla de 49 pasos y
 *   acumulador de 12 bits. Atenuación total = (63 - volumen maestro) + (31 - volumen del canal), en
 *   pasos de 0,75 dB. Al pasar la dirección de fin se calla y levanta su bandera.
 * - ADPCM-B: 4 bits por muestra, acumulador de 16 bits y paso adaptativo (127-24 576); entre una
 *   muestra y la siguiente se interpola. Volumen lineal ($1B), repetición ($10, bit 4).
 * - Temporizadores (registros $24-$27): el A cuenta de N (10 bits) a 1024 de a una muestra del FM y el B
 *   de N (8 bits) a 256 de a 16 muestras. No se llevan muestra a muestra: se guarda cuándo vence cada uno.
 *
 * Los niveles relativos de FM, SSG y ADPCM se ajustarán con los oráculos (fase 9).
 */
#include <math.h>
#include <string.h>

#include "neo.h"

#define PI 3.14159265358979323846
#define BUSY_MCLK (32 * 6 * 3)          /* 32 ciclos internos después de escribir un dato */
#define FM_GAIN 16.0f                   /* salida de 9 bits por canal a la escala de 16 bits */
#define SSG_GAIN 3072.0f                /* un canal del SSG al máximo */
#define ADPCMA_GAIN 4.0f                /* 12 bits por canal */
#define ADPCMB_GAIN 0.5f                /* 16 bits */

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

static u16 sin_tab[256];                /* -log2(seno) en 1/256 (cuarto de onda) */
static u16 exp_tab[256];                /* 2^(-x/256), 11 bits */
static float ssg_amp[32];               /* nivel de 5 bits del SSG */
static float pa_amp[128];               /* atenuación del ADPCM-A en pasos de 0,75 dB */
static int tables_ready;

/* Detune por código de tecla (DT 1-3; 5-7 son los mismos con signo) */
static const u8 dt_tab[4][32] = {
    { 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8 },
    { 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16 },
    { 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22 },
};

/* Incremento de la envolvente: fila según la tasa (0-3 para tasas < 48 según tasa & 3; 4-15 para
 * 48-63), columna según el paso del contador */
static const u8 eg_inc[19][8] = {
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 },
    { 2, 2, 2, 2, 2, 2, 2, 2 }, { 2, 2, 2, 4, 2, 2, 2, 4 }, { 2, 4, 2, 4, 2, 4, 2, 4 }, { 2, 4, 4, 4, 2, 4, 4, 4 },
    { 4, 4, 4, 4, 4, 4, 4, 4 }, { 4, 4, 4, 8, 4, 4, 4, 8 }, { 4, 8, 4, 8, 4, 8, 4, 8 }, { 4, 8, 8, 8, 4, 8, 8, 8 },
    { 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8 },
};

/* Muestras entre pasos del LFO, según su frecuencia ($22) */
static const u8 lfo_period[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
/* AM: corrimiento de la onda del LFO (0-126) según AMS */
static const u8 ams_shift[4] = { 8, 3, 1, 0 };
/* PM: desvío máximo en centésimas de semitono según PMS */
static const float pms_cents[8] = { 0, 3.4f, 6.7f, 10, 14, 20, 40, 80 };
static float pm_mul[8][32];             /* factor de fnum por PMS y paso del LFO */

/* ADPCM-A: los 49 tamaños de paso y cómo cambia el índice con cada nibble */
static const u16 pa_steps[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
    173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552,
};
static const s8 pa_index[8] = { -1, -1, -1, -1, 2, 5, 7, 9 };
/* ADPCM-B: factor del paso (en 64 avos) con cada nibble */
static const u8 pb_scale[8] = { 57, 57, 57, 57, 77, 102, 128, 153 };

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        double s = sin((2 * i + 1) * PI / 1024.0);
        sin_tab[i] = (u16)lround(-log2(s) * 256.0);
        exp_tab[i] = (u16)lround(pow(2.0, (255 - i) / 256.0) * 1024.0);
    }
    for (int p = 0; p < 8; p++)
        for (int st = 0; st < 32; st++) {
            /* onda del PM: triangular de 32 pasos entre -1 y 1 */
            double tri = st < 8 ? st / 8.0 : st < 24 ? (16 - st) / 8.0 : (st - 32) / 8.0;
            pm_mul[p][st] = (float)pow(2.0, pms_cents[p] * tri / 1200.0);
        }
    for (int l = 0; l < 32; l++) ssg_amp[l] = l ? (float)pow(2.0, (l - 31) / 4.0) : 0.0f;     /* 1,5 dB por paso */
    for (int a = 0; a < 128; a++) pa_amp[a] = (float)pow(10.0, -a * 0.75 / 20.0);
    tables_ready = 1;
}

void ym_reset(Ym2610 *y)
{
    if (!tables_ready) init_tables();
    memset(y, 0, sizeof *y);
    y->ta_at = y->tb_at = NEVER;
    for (int c = 0; c < 6; c++) {
        y->ch[c].pan = 0xC0;
        for (int o = 0; o < 4; o++) { y->ch[c].op[o].env = 0x3FF; y->ch[c].op[o].state = EG_RELEASE; }
    }
    y->ssg.lfsr = 1;
    y->pb.step = 127;
    y->dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Temporizadores                                                      */
/* ------------------------------------------------------------------ */

static u64 period_a(const Ym2610 *y) { return (u64)(1024 - ((y->reg[0][0x24] << 2) | (y->reg[0][0x25] & 3))) * MCLK_PER_FM; }
static u64 period_b(const Ym2610 *y) { return (u64)(256 - y->reg[0][0x26]) * 16 * MCLK_PER_FM; }

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

/* ------------------------------------------------------------------ */
/* FM (el YM2612 de GENRecomp)                                         */
/* ------------------------------------------------------------------ */

/* Código de tecla: bloque y los 4 bits altos de fnum */
static inline int keycode(u16 fnum, u8 block)
{
    int f11 = fnum >> 10 & 1, f10 = fnum >> 9 & 1, f9 = fnum >> 8 & 1, f8 = fnum >> 7 & 1;
    int n3 = (f11 & (f10 | f9 | f8)) | ((f11 ^ 1) & f10 & f9 & f8);
    return block << 2 | f11 << 1 | n3;
}

static inline int eg_rate(int r, int kc, int ks)
{
    if (r == 0) return 0;
    int rate = 2 * r + (kc >> (3 - ks));
    return rate > 63 ? 63 : rate;
}

static void key_on(YmOp *o, int kc)
{
    if (o->key) return;
    o->key = 1;
    o->phase = 0;
    o->ssg_inv = 0;
    if (eg_rate(o->ar, kc, o->ks) >= 62) { o->env = 0; o->state = EG_DECAY; }
    else o->state = EG_ATTACK;
}

/* SSG-EG: ¿la atenuación suena invertida ahora? */
static inline int ssg_inverted(const YmOp *o)
{
    return (o->ssg & 8) && o->state != EG_RELEASE && ((o->ssg_inv ^ (o->ssg >> 2)) & 1);
}

static void key_off(YmOp *o)
{
    if (!o->key) return;
    /* Si suelta la tecla con la envolvente invertida, se queda con el valor que sonaba */
    if (ssg_inverted(o)) o->env = (0x200 - o->env) & 0x3FF;
    o->key = 0;
    o->state = EG_RELEASE;
}

/* SSG-EG al llegar al final del ciclo (atenuación $200 o más) */
static void ssg_update(YmOp *o, int kc)
{
    if (!(o->ssg & 8) || o->env < 0x200 || o->state == EG_RELEASE) return;
    if (o->ssg & 1) {                                   /* mantener */
        if (o->ssg & 2) o->ssg_inv = 1;
        if (o->state != EG_ATTACK && !((o->ssg_inv ^ (o->ssg >> 2)) & 1)) o->env = 0x3FF;
        return;
    }
    if (o->ssg & 2) o->ssg_inv ^= 1;                    /* alternar */
    else o->phase = 0;                                  /* repetir: la fase vuelve a empezar */
    if (o->state == EG_ATTACK) return;
    if (eg_rate(o->ar, kc, o->ks) < 62) o->state = EG_ATTACK;
    else { o->env = 0; o->state = o->sl ? EG_DECAY : EG_SUSTAIN; }
}

static void eg_step(YmOp *o, int kc, u32 cnt)
{
    int r;
    switch (o->state) {
    case EG_ATTACK: r = o->ar; break;
    case EG_DECAY: r = o->dr; break;
    case EG_SUSTAIN: r = o->sr; break;
    default: r = o->rr * 2 + 1; break;
    }
    int rate = eg_rate(r, kc, o->ks);
    if (rate == 0) return;
    int shift = rate < 48 ? 11 - (rate >> 2) : 0;
    if (cnt & ((1u << shift) - 1)) return;
    int row = rate < 48 ? (rate & 3) : rate - 44;
    if (row > 18) row = 18;
    int inc = eg_inc[row][cnt >> shift & 7];
    if ((o->ssg & 8) && o->state != EG_ATTACK) {       /* SSG-EG: 4 veces más rápido, hasta $200 */
        if (o->env >= 0x200) return;
        inc *= 4;
    }
    switch (o->state) {
    case EG_ATTACK:
        if (rate >= 62) o->env = 0;
        else o->env += (~o->env * inc) >> 4;
        if (o->env <= 0) { o->env = 0; o->state = EG_DECAY; }
        break;
    case EG_DECAY:
        o->env += inc;
        if (o->env >= (o->sl == 15 ? 0x3E0 : o->sl << 5)) o->state = EG_SUSTAIN;
        break;
    default:
        o->env += inc;
        break;
    }
    if (o->env > 0x3FF) o->env = 0x3FF;
}

/* Salida de un operador (14 bits con signo): fase de 10 bits + modulación, atenuación de 10 bits */
static inline int op_out(u32 phase10, int att)
{
    int i = (int)(phase10 & 0x3FF);
    int q = i & 0xFF;
    if (i & 0x100) q ^= 0xFF;
    int level = sin_tab[q] + (att << 2);
    if (level >= 13 * 256) return 0;
    int v = (exp_tab[level & 0xFF] << 2) >> (level >> 8);
    return (i & 0x200) ? -v : v;
}

/* Incremento de fase de un operador: fnum y bloque, desafinación y multiplicador */
static inline u32 op_inc(const YmOp *op, u16 fnum, u8 block, int kc)
{
    u32 inc = ((u32)fnum << block) >> 1;
    int dt = dt_tab[op->dt & 3][kc];
    inc = (op->dt & 4) ? inc - (u32)dt : inc + (u32)dt;
    inc &= 0x1FFFF;
    return op->mul ? inc * op->mul : inc >> 1;
}

/* fnum y bloque de un operador (el canal 3 en modo especial tiene uno por operador) */
static inline void op_freq(const Ym2610 *y, int c, int o, u16 *fnum, u8 *block)
{
    *fnum = y->ch[c].fnum;
    *block = y->ch[c].block;
    if (c == 2 && (y->reg[0][0x27] & 0xC0) && o < 3) { *fnum = y->fnum3[o]; *block = y->block3[o]; }
}

/* Tras escribir registros: código de tecla e incremento de cada operador */
static void refresh(Ym2610 *y)
{
    for (int c = 0; c < 6; c++)
        for (int o = 0; o < 4; o++) {
            YmOp *op = &y->ch[c].op[o];
            u16 fnum; u8 block;
            op_freq(y, c, o, &fnum, &block);
            op->kc = (u8)keycode(fnum, block);
            op->inc = op_inc(op, fnum, block, op->kc);
        }
    y->dirty = 0;
}

/* Los canales FM que existen en el YM2610 (los 2, 3, 5 y 6 del OPNA) y su número en el menú (FM 1-4) */
static const u8 fm_chan[4] = { 1, 2, 4, 5 };

static void fm_sample(Ym2610 *y, u8 mute, int *L, int *R)
{
    if (y->dirty) refresh(y);
    /* Envolventes: cada 3 muestras */
    int eg_now = 0;
    if (++y->eg_sub == 3) { y->eg_sub = 0; y->eg_cnt++; eg_now = 1; }
    /* LFO */
    int lfo_am = 0, lfo_pm = 0;
    if (y->lfo_on) {
        if (++y->lfo_cnt >= lfo_period[y->lfo_freq]) { y->lfo_cnt = 0; y->lfo_step = (y->lfo_step + 1) & 127; }
        lfo_am = y->lfo_step < 64 ? y->lfo_step * 2 : (127 - y->lfo_step) * 2;
        lfo_pm = y->lfo_step >> 2;
    }
    for (int k = 0; k < 4; k++) {
        int c = fm_chan[k];
        YmCh *ch = &y->ch[c];
        /* Canal callado: las cuatro teclas sueltas, la envolvente al mínimo y la realimentación en 0.
         * Da 0 y nada cambia (la fase no importa: al apretar la tecla vuelve a 0). */
        if (!(ch->op[0].key | ch->op[1].key | ch->op[2].key | ch->op[3].key) && !(ch->fb_out[0] | ch->fb_out[1]) &&
            ch->op[0].env >= 0x3FF && ch->op[1].env >= 0x3FF && ch->op[2].env >= 0x3FF && ch->op[3].env >= 0x3FF) {
            ch->out = 0;
            continue;
        }
        int out[4];
        for (int o = 0; o < 4; o++) {
            YmOp *op = &ch->op[o];
            int kc = op->kc;
            if (eg_now) { eg_step(op, kc, y->eg_cnt); ssg_update(op, kc); }
            u32 inc = op->inc;
            if (ch->pms && y->lfo_on) {             /* LFO de tono: fnum cambia en cada muestra */
                u16 fnum; u8 block;
                op_freq(y, c, o, &fnum, &block);
                inc = op_inc(op, (u16)(fnum * pm_mul[ch->pms][lfo_pm]) & 0x7FF, block, kc);
            }
            op->phase = (op->phase + inc) & 0xFFFFF;
            int att = (ssg_inverted(op) ? (0x200 - op->env) & 0x3FF : op->env) + (op->tl << 3);
            if (op->am && ch->ams) att += lfo_am >> ams_shift[ch->ams];
            out[o] = att > 0x3FF ? 0x3FF : att;     /* aquí: la atenuación; abajo se reemplaza por la salida */
        }
        /* Salidas en el orden S1 S2 S3 S4 según el algoritmo */
        u32 ph[4];
        for (int o = 0; o < 4; o++) ph[o] = ch->op[o].phase >> 10;
        int s1, s2, s3, s4, acc;
        int fbm = ch->fb ? (ch->fb_out[0] + ch->fb_out[1]) >> (10 - ch->fb) : 0;
        s1 = op_out(ph[0] + (u32)fbm, out[0]);
        ch->fb_out[1] = ch->fb_out[0];
        ch->fb_out[0] = s1;
        switch (ch->alg) {
        case 0: s2 = op_out(ph[1] + (u32)(s1 >> 1), out[1]); s3 = op_out(ph[2] + (u32)(s2 >> 1), out[2]);
                s4 = op_out(ph[3] + (u32)(s3 >> 1), out[3]); acc = s4; break;
        case 1: s2 = op_out(ph[1], out[1]); s3 = op_out(ph[2] + (u32)((s1 + s2) >> 1), out[2]);
                s4 = op_out(ph[3] + (u32)(s3 >> 1), out[3]); acc = s4; break;
        case 2: s2 = op_out(ph[1], out[1]); s3 = op_out(ph[2] + (u32)(s2 >> 1), out[2]);
                s4 = op_out(ph[3] + (u32)((s1 + s3) >> 1), out[3]); acc = s4; break;
        case 3: s2 = op_out(ph[1] + (u32)(s1 >> 1), out[1]); s3 = op_out(ph[2], out[2]);
                s4 = op_out(ph[3] + (u32)((s2 + s3) >> 1), out[3]); acc = s4; break;
        case 4: s2 = op_out(ph[1] + (u32)(s1 >> 1), out[1]); s3 = op_out(ph[2], out[2]);
                s4 = op_out(ph[3] + (u32)(s3 >> 1), out[3]); acc = s2 + s4; break;
        case 5: s2 = op_out(ph[1] + (u32)(s1 >> 1), out[1]); s3 = op_out(ph[2] + (u32)(s1 >> 1), out[2]);
                s4 = op_out(ph[3] + (u32)(s1 >> 1), out[3]); acc = s2 + s3 + s4; break;
        case 6: s2 = op_out(ph[1] + (u32)(s1 >> 1), out[1]); s3 = op_out(ph[2], out[2]);
                s4 = op_out(ph[3], out[3]); acc = s2 + s3 + s4; break;
        default: s2 = op_out(ph[1], out[1]); s3 = op_out(ph[2], out[2]);
                s4 = op_out(ph[3], out[3]); acc = s1 + s2 + s3 + s4; break;
        }
        if (acc > 8191) acc = 8191;
        if (acc < -8192) acc = -8192;
        ch->out = acc;
        if (mute & (1 << k)) continue;
        int v = acc >> 5;                           /* 9 bits por canal, como en GENRecomp */
        if (ch->pan & 0x80) *L += v;
        if (ch->pan & 0x40) *R += v;
    }
}

/* ------------------------------------------------------------------ */
/* SSG                                                                 */
/* ------------------------------------------------------------------ */

/* Un paso de la envolvente del SSG (32 por periodo) según la forma de $0D */
static void ssg_env_step(Ym2610 *y)
{
    Ssg *s = &y->ssg;
    if (s->ehold) return;
    if (++s->estep < 32) return;
    u8 shape = y->reg[0][0x0D];
    if (!(shape & 8) || (shape & 1)) {                  /* sin seguir, o quedarse */
        s->ehold = 1;
        s->estep = 31;
        s->eatt = (shape & 8) ? (u8)(s->eatt ^ ((shape >> 1) & 1)) : 0;
        return;
    }
    if (shape & 2) s->eatt ^= 1;                        /* alternar */
    s->estep = 0;
}

static inline int ssg_env_level(const Ssg *s) { return s->eatt ? s->estep : 31 - s->estep; }

/* Un contador que sube en cada paso y vuelve a 0 al llegar al periodo, tras `steps` pasos de una vez:
 * devuelve cuántas veces volvió */
static inline int counter_steps(u16 *cnt, u16 period, int steps)
{
    u32 c = *cnt;
    if (c + (u32)steps < period) { *cnt = (u16)(c + (u32)steps); return 0; }
    u32 d = c + 1 >= period ? 1 : period - c;           /* el paso en que vuelve la primera vez */
    u32 rest = (u32)steps - d;
    *cnt = (u16)(rest % period);
    return 1 + (int)(rest / period);
}

/* 9 pasos del SSG (una muestra del FM): la suma promedio de los tres canales */
static float ssg_sample(Ym2610 *y)
{
    Ssg *s = &y->ssg;
    const u8 *r = y->reg[0];
    u16 tp[3];
    for (int c = 0; c < 3; c++) { tp[c] = (u16)(r[c * 2] | (r[c * 2 + 1] & 0x0F) << 8); if (!tp[c]) tp[c] = 1; }
    u16 np = (u16)((r[6] & 0x1F) ? (r[6] & 0x1F) : 1) * 2;
    u16 ep = (u16)(r[0x0B] | r[0x0C] << 8);
    if (!ep) ep = 1;
    u8 mix = r[7];
    float sum = 0;
    if (!((r[8] | r[9] | r[10]) & 0x1F)) {
        /* Los tres en volumen fijo 0: suenan 0 (ssg_amp[0]); los contadores avanzan los 9 pasos de una vez */
        for (int c = 0; c < 3; c++) if (counter_steps(&s->tcnt[c], tp[c], 9) & 1) s->tout[c] ^= 1;
        for (int k = counter_steps(&s->ncnt, np, 9); k > 0; k--) {
            u32 bit = (s->lfsr ^ (s->lfsr >> 3)) & 1;
            s->lfsr = (s->lfsr >> 1) | bit << 16;
        }
        for (int k = counter_steps(&s->ecnt, ep, 9); k > 0; k--) ssg_env_step(y);
        return 0.0f;
    }
    for (int step = 0; step < 9; step++) {
        for (int c = 0; c < 3; c++)
            if (++s->tcnt[c] >= tp[c]) { s->tcnt[c] = 0; s->tout[c] ^= 1; }
        if (++s->ncnt >= np) {
            s->ncnt = 0;
            u32 bit = (s->lfsr ^ (s->lfsr >> 3)) & 1;
            s->lfsr = (s->lfsr >> 1) | bit << 16;
        }
        if (++s->ecnt >= ep) { s->ecnt = 0; ssg_env_step(y); }
        int noise = s->lfsr & 1;
        for (int c = 0; c < 3; c++) {
            int on = (s->tout[c] | (mix >> c & 1)) & (noise | (mix >> (c + 3) & 1));
            if (!on) continue;
            u8 vol = r[8 + c];
            int level = (vol & 0x10) ? ssg_env_level(s) : (vol & 15) ? (vol & 15) * 2 + 1 : 0;
            sum += ssg_amp[level];
        }
    }
    return sum / 9.0f;
}

/* ------------------------------------------------------------------ */
/* ADPCM                                                               */
/* ------------------------------------------------------------------ */

static inline u8 vrom_nibble(const Neo *g, int bus_b, u32 nib)
{
    const Cart *c = &g->cart;
    int r = bus_b && c->len[REG_VB] ? REG_VB : REG_VA;
    size_t n = c->len[r];
    if (!n) return 0;
    u8 b = c->data[c->off[r] + (nib >> 1) % n];
    return (nib & 1) ? b & 15 : b >> 4;
}

static void pa_key_on(Ym2610 *y, int ch)
{
    const u8 *r = y->reg[1];
    AdpcmA *a = &y->pa[ch];
    a->on = 1;
    a->addr = (u32)(r[0x10 + ch] | r[0x18 + ch] << 8) << 9;
    a->end = ((u32)(r[0x20 + ch] | r[0x28 + ch] << 8) << 9) | 0x1FF;
    a->acc = 0;
    a->step = 0;
}

/* Una muestra de los 6 canales de ADPCM-A (cada 3 muestras del FM) */
static void pa_sample(Neo *g, float *L, float *R)
{
    Ym2610 *y = &g->ym;
    const u8 *r = y->reg[1];
    for (int ch = 0; ch < 6; ch++) {
        AdpcmA *a = &y->pa[ch];
        if (!a->on) continue;
        if (a->addr > a->end) {
            a->on = 0;
            if (!(y->end_mask & (1 << ch))) y->end_flags |= (u8)(1 << ch);
            continue;
        }
        int n = vrom_nibble(g, 0, a->addr++);
        int step = pa_steps[a->step];
        int d = ((2 * (n & 7) + 1) * step) >> 3;
        a->acc += (n & 8) ? -d : d;
        a->acc = ((a->acc + 2048) & 0xFFF) - 2048;     /* 12 bits con signo, da la vuelta */
        a->step += pa_index[n & 7];
        if (a->step < 0) a->step = 0;
        if (a->step > 48) a->step = 48;
        if (g->audio.mute & 0x20) continue;
        u8 cv = r[0x08 + ch];
        int att = (63 - (r[0x01] & 0x3F)) + (31 - (cv & 0x1F));
        float v = (float)a->acc * pa_amp[att] * ADPCMA_GAIN;
        if (cv & 0x80) *L += v;
        if (cv & 0x40) *R += v;
    }
}

static void pb_start(Ym2610 *y)
{
    const u8 *r = y->reg[0];
    AdpcmB *b = &y->pb;
    b->on = 1;
    b->addr = (u32)(r[0x12] | r[0x13] << 8) << 9;
    b->pos = 0;
    b->acc = b->prev = 0;
    b->step = 127;
}

/* El ADPCM-B en una muestra del FM: avanza Delta-N / 65 536 muestras y se interpola */
static void pb_sample(Neo *g, float *L, float *R)
{
    Ym2610 *y = &g->ym;
    AdpcmB *b = &y->pb;
    const u8 *r = y->reg[0];
    if (!b->on) return;
    b->pos += (u32)(r[0x19] | r[0x1A] << 8);
    while (b->pos >= 0x10000) {
        b->pos -= 0x10000;
        u32 end = ((u32)(r[0x14] | r[0x15] << 8) << 9) | 0x1FF;
        if (b->addr > end) {
            if (r[0x10] & 0x10) { u32 pos = b->pos; pb_start(y); b->pos = pos; }      /* repetir */
            else {
                b->on = 0;
                if (!(y->end_mask & 0x80)) y->end_flags |= 0x80;
                return;
            }
        }
        int n = vrom_nibble(g, 1, b->addr++);
        b->prev = b->acc;
        int d = ((2 * (n & 7) + 1) * b->step) >> 3;
        b->acc += (n & 8) ? -d : d;
        if (b->acc > 32767) b->acc = 32767;
        if (b->acc < -32768) b->acc = -32768;
        b->step = (b->step * pb_scale[n & 7]) >> 6;
        if (b->step < 127) b->step = 127;
        if (b->step > 24576) b->step = 24576;
    }
    if (g->audio.mute & 0x40) return;
    float v = ((float)b->prev + (float)(b->acc - b->prev) * (float)b->pos / 65536.0f) * (float)r[0x1B] / 256.0f * ADPCMB_GAIN;
    if (r[0x11] & 0x80) *L += v;
    if (r[0x11] & 0x40) *R += v;
}

/* ------------------------------------------------------------------ */
/* Una muestra y el registro                                           */
/* ------------------------------------------------------------------ */

static void sample(Neo *g)
{
    Ym2610 *y = &g->ym;
    int fl = 0, fr = 0;
    fm_sample(y, g->audio.mute, &fl, &fr);
    float L = (float)fl * FM_GAIN, R = (float)fr * FM_GAIN;
    if (!(g->audio.mute & 0x10)) { float s = ssg_sample(y) * SSG_GAIN; L += s; R += s; }
    else ssg_sample(y);
    if (++y->pa_sub == 3) { y->pa_sub = 0; pa_sample(g, &L, &R); }
    pb_sample(g, &L, &R);
    audio_add(g, y->mclk, y->mclk + MCLK_PER_FM, L, R);
}

void ym_run(Neo *g, u64 mclk)
{
    Ym2610 *y = &g->ym;
    while (y->mclk + MCLK_PER_FM <= mclk) {
        sample(g);
        y->mclk += MCLK_PER_FM;
    }
}

u8 ym_read(Neo *g, int port, u64 mclk)
{
    Ym2610 *y = &g->ym;
    ym_update(y, mclk);
    switch (port & 3) {
    case 0: return (u8)((mclk < y->busy_until ? 0x80 : 0) | y->flags);
    case 1: return y->addr[0] < 0x10 ? y->reg[0][y->addr[0]] : 0;
    case 2: ym_run(g, mclk); return y->end_flags;
    default: return 0;
    }
}

/* Registro de un operador: el orden de los registros es S1 S3 S2 S4 */
static YmOp *slot(Ym2610 *y, int c, int r) { static const u8 map[4] = { 0, 2, 1, 3 }; return &y->ch[c].op[map[r >> 2 & 3]]; }

static void fm_write(Ym2610 *y, int half, u8 r, u8 v)
{
    y->dirty = 1;
    if (half == 0 && r == 0x22) {
        y->lfo_on = v >> 3 & 1;
        y->lfo_freq = v & 7;
        if (!y->lfo_on) { y->lfo_step = 0; y->lfo_cnt = 0; }
        return;
    }
    if (half == 0 && r == 0x28) {
        int c = v & 3;
        if (c == 3) return;
        if (v & 4) c += 3;
        if (c == 0 || c == 3) return;                   /* no existen en el YM2610 */
        YmCh *ch = &y->ch[c];
        int kc = keycode(ch->fnum, ch->block);
        for (int o = 0; o < 4; o++) {
            static const u8 bitmap[4] = { 0x10, 0x20, 0x40, 0x80 };   /* S1 S2 S3 S4 */
            if (v & bitmap[o]) key_on(&ch->op[o], kc); else key_off(&ch->op[o]);
        }
        return;
    }
    if (r < 0x30) return;
    int c = r & 3;
    if (c == 3) return;
    c += half * 3;
    YmCh *ch = &y->ch[c];
    if (r < 0xA0) {
        YmOp *o = slot(y, c, r);
        switch (r & 0xF0) {
        case 0x30: o->dt = v >> 4 & 7; o->mul = v & 15; break;
        case 0x40: o->tl = v & 0x7F; break;
        case 0x50: o->ks = v >> 6; o->ar = v & 0x1F; break;
        case 0x60: o->am = v >> 7; o->dr = v & 0x1F; break;
        case 0x70: o->sr = v & 0x1F; break;
        case 0x80: o->sl = v >> 4; o->rr = v & 15; break;
        default: o->ssg = v & 15; break;
        }
        return;
    }
    switch (r & 0xFC) {
    case 0xA0: ch->fnum = (u16)((ch->latch & 7) << 8 | v); ch->block = ch->latch >> 3 & 7; break;
    case 0xA4: ch->latch = v & 0x3F; break;
    case 0xA8:
        if (half == 0) {                        /* canal 3 en modo especial: $A9 S1, $AA S2, $A8 S3 */
            static const u8 m[3] = { 2, 0, 1 };
            int k = m[r & 3];
            y->fnum3[k] = (u16)((y->latch3 & 7) << 8 | v);
            y->block3[k] = y->latch3 >> 3 & 7;
        }
        break;
    case 0xAC: if (half == 0) y->latch3 = v & 0x3F; break;
    case 0xB0: ch->fb = v >> 3 & 7; ch->alg = v & 7; break;
    case 0xB4: ch->pan = v & 0xC0; ch->ams = v >> 4 & 3; ch->pms = v & 7; break;
    default: break;
    }
}

void ym_write(Neo *g, int port, u8 v, u64 mclk)
{
    Ym2610 *y = &g->ym;
    int half = (port >> 1) & 1;
    if (!(port & 1)) { y->addr[half] = v; return; }
    ym_run(g, mclk);
    ym_update(y, mclk);
    u8 r = y->addr[half];
    u8 old = y->reg[half][r];
    y->reg[half][r] = v;
    y->busy_until = mclk + BUSY_MCLK;
    if (half == 0) {
        if (r == 0x27) {
            /* Echar a andar un temporizador lo carga; pararlo lo deja quieto */
            if ((v & 1) && !(old & 1)) y->ta_at = mclk + period_a(y);
            else if (!(v & 1)) y->ta_at = NEVER;
            if ((v & 2) && !(old & 2)) y->tb_at = mclk + period_b(y);
            else if (!(v & 2)) y->tb_at = NEVER;
            y->flags &= (u8)~((v >> 4) & 3);
            y->dirty = 1;                                /* el modo del canal 3 */
            return;
        }
        if (r == 0x0D) { y->ssg.estep = 0; y->ssg.ehold = 0; y->ssg.eatt = (v >> 2) & 1; return; }
        if (r == 0x10) {                                 /* ADPCM-B: empezar, repetir, reset */
            if (v & 1) { y->pb.on = 0; y->reg[0][0x10] = 0; }
            else if ((v & 0x80) && !(old & 0x80)) pb_start(y);
            else if (!(v & 0x80)) y->pb.on = 0;
            return;
        }
        if (r == 0x1C) {                                 /* banderas: 1 las borra y las enmascara; 0 las desenmascara */
            y->end_flags &= (u8)~v;
            y->end_mask = v & 0xBF;
            return;
        }
        if (r >= 0x20) fm_write(y, 0, r, v);
        return;
    }
    if (r == 0x00) {                                     /* ADPCM-A: el bit 7 suelta, si no aprieta */
        for (int ch = 0; ch < 6; ch++)
            if (v & (1 << ch)) {
                if (v & 0x80) y->pa[ch].on = 0;
                else pa_key_on(y, ch);
            }
        return;
    }
    if (r >= 0x30) fm_write(y, 1, r, v);
}
