/*
 * YM2612 (OPN2): 6 canales FM de 4 operadores, DAC en el canal 6, LFO y dos temporizadores.
 *
 * Una muestra cada 144 ciclos del 68000 (1008 ciclos maestros: 53 267 Hz). Por muestra y operador:
 *
 * - Fase: contador de 20 bits; el incremento sale de fnum (11 bits) y el bloque, más el detune (tabla
 *   por código de tecla), por el múltiplo (0 = mitad). El LFO desvía fnum (PM).
 * - Envolvente: atenuación de 10 bits (0 = volumen máximo). Un contador global avanza cada 3
 *   muestras; según la tasa efectiva (2·R + escala por tecla) se actualiza cada 2^n pasos con el
 *   incremento de la tabla del chip. El ataque baja la atenuación de forma exponencial; los demás la
 *   suben de forma lineal. Más el nivel total (TL << 3) y la AM del LFO.
 * - Salida: seno en logaritmo (cuarto de onda de 256 entradas) más la atenuación, y una exponencial
 *   (256 entradas): 14 bits con signo. La entrada de modulación suma salidas y la corre un bit; la
 *   realimentación de S1 usa sus dos salidas anteriores corridas 10 - FB.
 * - Canal: suma de los portadores según el algoritmo (0-7), recortada a 14 bits; el DAC usa 9.
 *
 * Temporizadores: A (10 bits) cuenta en cada muestra, B (8 bits) cada 16; al desbordar levantan su
 * bandera si están habilitados ($27). Leer cualquier puerto da el estado: bit 7 ocupado (tras escribir
 * un dato), bits 0-1 las banderas.
 *
 * SSG-EG ($90, bit 3): la envolvente baja 4 veces más rápido mientras la atenuación es menor que $200
 * y al llegar ahí, según los bits 0-2 (mantener, alternar, invertir), vuelve a empezar (y la fase a 0),
 * se invierte o se queda quieta. Invertida, la atenuación que suena es $200 menos la de la envolvente.
 *
 * Sin emular todavía: el modo CSM, los retardos internos entre operadores y la distorsión del DAC del
 * YM2612 (la "escalera").
 */
#include <math.h>
#include <string.h>

#include "gen.h"

#define PI 3.14159265358979323846
#define MCLK_PER_SAMPLE 1008
#define BUSY_MCLK (32 * 6 * 7)          /* 32 ciclos internos después de escribir un dato */
#define FM_GAIN 16.0f                   /* salida de 9 bits por canal a la escala de 16 bits */

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

static u16 sin_tab[256];                /* -log2(seno) en 1/256 (cuarto de onda) */
static u16 exp_tab[256];                /* 2^(-x/256), 11 bits */
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
    tables_ready = 1;
}

void ym_reset(Ym2612 *y)
{
    if (!tables_ready) init_tables();
    memset(y, 0, sizeof *y);
    for (int c = 0; c < 6; c++) {
        y->ch[c].pan = 0xC0;
        for (int o = 0; o < 4; o++) { y->ch[c].op[o].env = 0x3FF; y->ch[c].op[o].state = EG_RELEASE; }
    }
}

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

static void sample(Gen *g)
{
    Ym2612 *y = &g->ym;
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
    /* Temporizadores */
    if (y->timer_ctl & 1) {
        if (++y->ta_cnt >= 1024) { y->ta_cnt = y->ta; if (y->timer_ctl & 4) y->status |= 1; }
    }
    if (++y->tb_sub >= 16) {
        y->tb_sub = 0;
        if (y->timer_ctl & 2) {
            if (++y->tb_cnt == 0) { y->tb_cnt = y->tb; if (y->timer_ctl & 8) y->status |= 2; }
        }
    }

    int L = 0, R = 0;
    for (int c = 0; c < 6; c++) {
        YmCh *ch = &y->ch[c];
        int out[4];
        for (int o = 0; o < 4; o++) {
            YmOp *op = &ch->op[o];
            u16 fnum = ch->fnum;
            u8 block = ch->block;
            if (c == 2 && (y->timer_ctl & 0xC0) && o < 3) { fnum = y->fnum3[o]; block = y->block3[o]; }
            int kc = keycode(fnum, block);
            if (eg_now) { eg_step(op, kc, y->eg_cnt); ssg_update(op, kc); }
            if (ch->pms && y->lfo_on) fnum = (u16)(fnum * pm_mul[ch->pms][lfo_pm]) & 0x7FF;
            u32 inc = ((u32)fnum << block) >> 1;
            int dt = dt_tab[op->dt & 3][kc];
            inc = (op->dt & 4) ? inc - (u32)dt : inc + (u32)dt;
            inc &= 0x1FFFF;
            inc = op->mul ? inc * op->mul : inc >> 1;
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
        if (c == 5 && y->dac_on) acc = ((int)y->dac - 128) << 6;
        ch->out = acc;
        if (g->audio.mute & (1 << c)) continue;
        int v = acc >> 5;                           /* el DAC usa 9 bits */
        if (ch->pan & 0x80) L += v;
        if (ch->pan & 0x40) R += v;
    }
    audio_add(g, y->mclk, y->mclk + MCLK_PER_SAMPLE, (float)L * FM_GAIN, (float)R * FM_GAIN);
}

void ym_run(Gen *g, u64 mclk)
{
    Ym2612 *y = &g->ym;
    while (y->mclk + MCLK_PER_SAMPLE <= mclk) {
        sample(g);
        y->mclk += MCLK_PER_SAMPLE;
    }
}

u8 ym_read(Gen *g, u64 mclk)
{
    ym_run(g, mclk);
    return (u8)((mclk < g->ym.busy_until ? 0x80 : 0) | g->ym.status);
}

/* Registro de un operador: el orden de los registros es S1 S3 S2 S4 */
static YmOp *slot(Ym2612 *y, int c, int r) { static const u8 map[4] = { 0, 2, 1, 3 }; return &y->ch[c].op[map[r >> 2 & 3]]; }

static void write_reg(Gen *g, int port, u8 r, u8 v)
{
    Ym2612 *y = &g->ym;
    if (port == 0 && r < 0x30) {
        switch (r) {
        case 0x22: y->lfo_on = v >> 3 & 1; y->lfo_freq = v & 7; if (!y->lfo_on) { y->lfo_step = 0; y->lfo_cnt = 0; } break;
        case 0x24: y->ta = (u16)((y->ta & 3) | v << 2); break;
        case 0x25: y->ta = (u16)((y->ta & 0x3FC) | (v & 3)); break;
        case 0x26: y->tb = v; break;
        case 0x27:
            if ((v & 1) && !(y->timer_ctl & 1)) y->ta_cnt = y->ta;
            if ((v & 2) && !(y->timer_ctl & 2)) { y->tb_cnt = y->tb; y->tb_sub = 0; }
            if (v & 0x10) y->status &= (u8)~1;
            if (v & 0x20) y->status &= (u8)~2;
            y->timer_ctl = v & 0xCF;
            break;
        case 0x28: {
            int c = v & 3;
            if (c == 3) break;
            if (v & 4) c += 3;
            YmCh *ch = &y->ch[c];
            int kc = keycode(ch->fnum, ch->block);
            for (int o = 0; o < 4; o++) {
                static const u8 bitmap[4] = { 0x10, 0x20, 0x40, 0x80 };   /* S1 S2 S3 S4 */
                if (v & bitmap[o]) key_on(&ch->op[o], kc); else key_off(&ch->op[o]);
            }
            break;
        }
        case 0x2A: y->dac = v; break;
        case 0x2B: y->dac_on = v >> 7; break;
        default: break;
        }
        return;
    }
    if (r < 0x30) return;
    int c = r & 3;
    if (c == 3) return;
    c += port * 3;
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
        if (port == 0) {                        /* canal 3 en modo especial: $A9 S1, $AA S2, $A8 S3 */
            static const u8 m[3] = { 2, 0, 1 };
            int k = m[r & 3];
            y->fnum3[k] = (u16)((y->latch3 & 7) << 8 | v);
            y->block3[k] = y->latch3 >> 3 & 7;
        }
        break;
    case 0xAC: if (port == 0) y->latch3 = v & 0x3F; break;
    case 0xB0: ch->fb = v >> 3 & 7; ch->alg = v & 7; break;
    case 0xB4: ch->pan = v & 0xC0; ch->ams = v >> 4 & 3; ch->pms = v & 7; break;
    default: break;
    }
}

void ym_write(Gen *g, int a, u8 v, u64 mclk)
{
    Ym2612 *y = &g->ym;
    ym_run(g, mclk);
    int port = a >> 1 & 1;
    if (!(a & 1)) { y->addr[port] = v; return; }
    write_reg(g, port, y->addr[port], v);
    y->busy_until = mclk + BUSY_MCLK;
}
