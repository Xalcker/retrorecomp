/*
 * Audio del VRC7 (mapper 85): un OPLL (derivado del YM2413) con 6 canales FM de dos operadores (modulador y
 * portador), 15 instrumentos de fábrica y uno programable ($00-$07). Genera una muestra cada 36 ciclos de CPU, como
 * el chip (3.58 MHz / 72 = 49 716 Hz).
 *
 * Modelo (YM2413 application manual y notas de andete en SMS Power):
 * - Fase: F-num x 2^bloque / 2^19 vueltas por muestra, por el multiplicador del operador (MULT: 1/2, 1..15).
 * - Envolvente en dB de atenuación (0 = máximo, 48 = silencio): ataque, decaimiento hasta SL (3 dB por paso),
 *   sostenido o, en instrumentos de percusión, caída con RR; al soltar la tecla, RR (o 5 con SUS).
 *   Velocidad efectiva = 4 x tasa + escala de teclado (KSR).
 * - Atenuación total = envolvente + TL (modulador, 0.75 dB) o volumen del canal (portador, 3 dB) + KSL + trémolo.
 * - El modulador desplaza la fase del portador; también se realimenta a sí mismo (FB). Las ondas pueden ser seno o
 *   media onda (DM, DC).
 * - Vibrato y trémolo: osciladores de 6.4 Hz y 3.7 Hz.
 * Los instrumentos de fábrica son el volcado del chip de Nuke.YKT.
 */
#include <string.h>
#include "nes.h"

static const u8 vrc7_patch[16][8] = {
    { 0 },
    { 0x03, 0x21, 0x05, 0x06, 0xE8, 0x81, 0x42, 0x27 },
    { 0x13, 0x41, 0x14, 0x0D, 0xD8, 0xF6, 0x23, 0x12 },
    { 0x11, 0x11, 0x08, 0x08, 0xFA, 0xB2, 0x20, 0x12 },
    { 0x31, 0x61, 0x0C, 0x07, 0xA8, 0x64, 0x61, 0x27 },
    { 0x32, 0x21, 0x1E, 0x06, 0xE1, 0x76, 0x01, 0x28 },
    { 0x02, 0x01, 0x06, 0x00, 0xA3, 0xE2, 0xF4, 0xF4 },
    { 0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x11, 0x07 },
    { 0x23, 0x21, 0x22, 0x17, 0xA2, 0x72, 0x01, 0x17 },
    { 0x35, 0x11, 0x25, 0x00, 0x40, 0x73, 0x72, 0x01 },
    { 0xB5, 0x01, 0x0F, 0x0F, 0xA8, 0xA5, 0x51, 0x02 },
    { 0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12 },
    { 0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x18, 0x16 },
    { 0x01, 0x02, 0xD3, 0x05, 0xC9, 0x95, 0x03, 0x02 },
    { 0x61, 0x63, 0x0C, 0x00, 0x94, 0xC0, 0x33, 0xF6 },
    { 0x21, 0x72, 0x0D, 0x00, 0xC1, 0xD5, 0x56, 0x06 },
};

/* Constantes del modelo que la documentación deja abiertas, ajustadas contra emu2413 (la referencia que usa Mesen2) con
 * los 15 instrumentos en tres notas: mismos tiempos de liberación, ataques cercanos y armónicos a ~2-3 dB en promedio */
#ifndef VRC7_MOD_DEPTH
#define VRC7_MOD_DEPTH 2.0f          /* vueltas de fase del portador por unidad de salida del modulador (±4π) */
#endif
#ifndef VRC7_EG_SCALE
#define VRC7_EG_SCALE 0.5f           /* factor sobre las velocidades de la envolvente (ajustado: tiempos de liberación iguales) */
#endif
#ifndef VRC7_PM_DEPTH
#define VRC7_PM_DEPTH 0.0040f        /* desviación relativa máxima del vibrato (~7 cents) */
#endif
#ifndef VRC7_ATTACK
#define VRC7_ATTACK 24.0f            /* el ataque, cuántas veces más rápido que el decaimiento a la misma tasa (ajustado) */
#endif
#ifndef VRC7_FB_SCALE
#define VRC7_FB_SCALE 0.5f           /* factor sobre la realimentación (FB n: π/16 x 2^(n-1)); ajustado, el OPLL usa la mitad */
#endif
#define VRC7_AM_DEPTH 4.8f           /* dB del trémolo */
#define VRC7_SILENT 48.0f            /* dB: por debajo de esto, el operador no suena */
#define VRC7_RATE (1789773.0 / 36.0) /* muestras por segundo */

static const float mult_tab[16] = { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15 };
/* KSL: atenuación en dB para el bloque 7 según los 4 bits altos de F-num (6 dB menos por bloque más bajo) */
static const float ksl_tab[16] = { 0, 9, 12, 13.875f, 15, 16.125f, 16.875f, 17.625f, 18, 18.75f, 19.125f, 19.5f,
                                    19.875f, 20.25f, 20.625f, 21 };

static float sin_tab[1024], amp_tab[490];     /* seno de una vuelta; 10^(-dB/20) en pasos de 0.1 dB */
static int tabs_ok;

static void vrc7_tables(void)
{
    if (tabs_ok) return;
    /* Seno por polinomio (sin libm): Bhaskara mejorado, error < 0.1 % */
    for (int i = 0; i < 1024; i++) {
        double x = (i & 511) / 512.0 * 3.14159265358979;                   /* 0..π */
        double y = 16 * x * (3.14159265358979 - x) / (49.3480220054468 - 4 * x * (3.14159265358979 - x));
        sin_tab[i] = (float)(i < 512 ? y : -y);
    }
    amp_tab[0] = 1;
    for (int i = 1; i < 490; i++) amp_tab[i] = amp_tab[i - 1] * 0.98855309f;   /* 10^(-0.1/20) */
    tabs_ok = 1;
}

static float amp_of(float db) { int i = (int)(db * 10.0f + 0.5f); return i >= 480 ? 0.0f : i <= 0 ? 1.0f : amp_tab[i]; }

/* Tiempo (en muestras) para recorrer 48 dB a una velocidad efectiva 0-63 (decaimiento y liberación): se duplica la
 * velocidad cada 4 pasos. Con 60-63 es casi instantáneo. */
static float eg_step(int r)
{
    if (r < 4) return 0;
    if (r > 63) r = 63;
    /* r = 4: ~10.4 s para 48 dB (decaimiento más lento del OPL); cada +4 lo hace el doble de rápido */
    float t = 10.4f / (float)(1u << ((r - 4) >> 2)) / (1.0f + 0.25f * (r & 3));
    return 48.0f / (t * (float)VRC7_RATE) * VRC7_EG_SCALE;
}

void vrc7_reset(Vrc7 *v)
{
    vrc7_tables();
    memset(v, 0, sizeof *v);
    v->cnt = 36;
    for (int i = 0; i < 12; i++) { v->op[i].eg = VRC7_SILENT; v->op[i].state = 3; }
}

void vrc7_write(Vrc7 *v, u8 reg, u8 x)
{
    if (reg >= 0x40) return;
    if (reg >= 0x20 && reg < 0x26) {                     /* tecla: al encenderla, ataque desde donde esté */
        int ch = reg - 0x20, was = v->reg[reg] & 0x10, on = x & 0x10;
        if (on && !was) for (int k = 0; k < 2; k++) { v->op[ch * 2 + k].state = 0; v->op[ch * 2 + k].phase = 0; }
        if (!on && was) for (int k = 0; k < 2; k++) v->op[ch * 2 + k].state = 3;
    }
    v->reg[reg] = x;
}

/* Una muestra de los 6 canales, cada uno en -1..1 */
float vrc7_sample(Vrc7 *v)
{
    v->am_phase += 1; v->pm_phase += 1;
    float am = VRC7_AM_DEPTH * 0.5f * (1.0f - sin_tab[(v->am_phase * 1024u / 13432u) & 1023]);   /* 3.7 Hz */
    float pm = VRC7_PM_DEPTH * sin_tab[(v->pm_phase * 1024u / 7768u) & 1023];                   /* 6.4 Hz */
    if (v->am_phase >= 13432u * 64) v->am_phase -= 13432u * 64;
    if (v->pm_phase >= 7768u * 64) v->pm_phase -= 7768u * 64;
    float out = 0;
    for (int ch = 0; ch < 6; ch++) {
        u8 r3 = v->reg[0x30 + ch], r2 = v->reg[0x20 + ch];
        const u8 *p = (r3 >> 4) ? vrc7_patch[r3 >> 4] : v->reg;
        int fnum = v->reg[0x10 + ch] | ((r2 & 1) << 8), block = (r2 >> 1) & 7, sus = (r2 >> 5) & 1;
        float mod = 0, chout = 0;
        for (int k = 0; k < 2; k++) {
            Vrc7Op *o = &v->op[ch * 2 + k];
            u8 flags = p[k];
            /* fase */
            float inc = (float)fnum * (float)(1 << block) / 524288.0f * mult_tab[flags & 15];
            if (flags & 0x40) inc *= 1.0f + pm;
            o->phase += inc;
            o->phase -= (float)(int)o->phase;
            /* envolvente */
            int ks = (flags & 0x10) ? (block << 1) | (fnum >> 8) : block >> 1;
            int ar = p[4 + k] >> 4, dr = p[4 + k] & 15, sl = p[6 + k] >> 4, rr = p[6 + k] & 15;
            float sl_db = sl == 15 ? VRC7_SILENT : sl * 3.0f;
            switch (o->state) {
            case 0:                                              /* ataque: exponencial hacia 0 dB */
                if (ar == 15) o->eg = 0;
                else if (ar) {
                    float s = eg_step(ar * 4 + ks) * VRC7_ATTACK;
                    o->eg -= s * (o->eg + 4.0f) / (VRC7_SILENT + 4.0f) + s * 0.02f;
                }
                if (o->eg <= 0) { o->eg = 0; o->state = 1; }
                break;
            case 1:                                              /* decaimiento hasta SL */
                o->eg += dr ? eg_step(dr * 4 + ks) : 0;
                if (o->eg >= sl_db) { o->eg = sl_db; o->state = 2; }
                break;
            case 2:                                              /* sostenido, o caída con RR si es de percusión */
                if (!(flags & 0x20)) o->eg += eg_step(rr * 4 + ks);
                break;
            default:                                             /* liberación */
                o->eg += eg_step((sus ? 5 : (flags & 0x20) ? rr : 7) * 4 + ks);
                break;
            }
            if (o->eg > VRC7_SILENT) o->eg = VRC7_SILENT;
            /* atenuación y salida */
            int ksl = (p[2 + k] >> 6);
            float kdb = ksl ? ksl_tab[fnum >> 5] - 6.0f * (7 - block) : 0;
            if (kdb < 0) kdb = 0;
            if (ksl == 1) kdb *= 0.25f; else if (ksl == 2) kdb *= 0.5f;
            float db = o->eg + kdb + ((flags & 0x80) ? am : 0) + (k == 0 ? (p[2] & 63) * 0.75f : (r3 & 15) * 3.0f);
            float ph = o->phase;
            if (k == 0) {                                        /* modulador: realimentación */
                int fb = p[3] & 7;
                if (fb) ph += (o->fb[0] + o->fb[1]) * 0.5f * (float)(1 << (fb - 1)) / 32.0f * VRC7_FB_SCALE;
            } else ph += mod * VRC7_MOD_DEPTH;
            float s = sin_tab[(int)(ph * 1024.0f) & 1023];
            if (s < 0 && (p[3] & (k == 0 ? 0x08 : 0x10))) s = 0;     /* media onda (DM / DC) */
            float y = s * amp_of(db);
            if (k == 0) { o->fb[1] = o->fb[0]; o->fb[0] = y; mod = y; }
            else chout = y;
        }
        out += chout;
    }
    return out;
}
