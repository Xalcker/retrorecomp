/*
 * PSG: el SN76489 de Sega (dentro del VDP del Genesis, del Master System y del Game Gear). Tres canales de tono
 * y uno de ruido.
 *
 * - Escritura: un byte con el bit 7 elige registro (bits 6-5 canal, bit 4 volumen) y pone sus 4
 *   bits bajos; un byte sin el bit 7 pone los 6 bits altos del tono elegido (o los bajos del volumen
 *   o del ruido). Escribir el ruido reinicia el LFSR.
 * - Cada 16 ciclos del reloj del Z80 (240 ciclos maestros) baja cada contador; al llegar a 0 se
 *   recarga con el periodo y la salida se invierte. Un periodo de 0 o 1 deja la salida fija en 1 (los
 *   juegos lo usan para sacar muestras con el volumen).
 * - Ruido: periodo 16, 32 o 64 o el del tono 3; LFSR de 16 bits que avanza en cada subida de su
 *   biestable: con ruido blanco realimenta los bits 0 y 3, si no, solo el 0.
 * - Volumen: 15 pasos de 2 dB y el 15 apaga el canal.
 * - Estéreo (el Game Gear, PSG_STEREO): cada canal va a la izquierda, a la derecha, a los dos o a ninguno.
 *
 * Común a GENRecomp y SMSRecomp (retrorecomp/shared): lo propio de cada consola viene de su psg_host.h (psg.h).
 */
#include <math.h>
#include <string.h>

#include "psg.h"
#include PSG_HOST_HEADER

#define MCLK_PER_TICK 240       /* 15 (Z80) x 16 */

#ifndef PROF_ENTER              /* la consola no usa prof.h (--profile): no se marca la zona */
#define PROF_ENTER(z) ((void)0)
#define PROF_LEAVE() ((void)0)
#endif

static float level[16];
static int level_ready;

void psg_reset(Psg *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; i++) p->vol[i] = 15;
    p->lfsr = 0x8000;
    if (!level_ready) {
        for (int i = 0; i < 15; i++) level[i] = PSG_LEVEL * powf(10.0f, -0.1f * (float)i);
        level[15] = 0;
        level_ready = 1;
    }
}

/* La salida de los cuatro canales, a cada lado */
static void output(PSG_HOST *g, float *l, float *r)
{
    const Psg *p = PSG_OF(g);
    unsigned st = PSG_STEREO(g);
    float sl = 0, sr = 0;
    for (int c = 0; c < 4; c++) {
        if (PSG_MUTED(g, c)) continue;
        int on = c < 3 ? p->out[c] || p->period[c] <= 1 : p->lfsr & 1;
        float s = on ? level[p->vol[c]] : -level[p->vol[c]];
        if (st & (0x10u << c)) sl += s;
        if (st & (0x01u << c)) sr += s;
    }
    *l = sl;
    *r = sr;
}

void psg_run(PSG_HOST *g, uint64_t mclk)
{
    Psg *p = PSG_OF(g);
    PROF_ENTER(PROF_SONIDO);
    while (p->mclk + MCLK_PER_TICK <= mclk) {
        float l, r;
        output(g, &l, &r);
        audio_add(g, p->mclk, p->mclk + MCLK_PER_TICK, l, r);
        p->mclk += MCLK_PER_TICK;
        for (int c = 0; c < 3; c++)
            if (p->counter[c] == 0 || --p->counter[c] == 0) {
                p->counter[c] = p->period[c];
                p->out[c] ^= 1;
            }
        if (p->counter[3] == 0 || --p->counter[3] == 0) {
            int r = p->period[3] & 3;
            p->counter[3] = r == 3 ? p->period[2] : (uint16_t)(0x10 << r);
            p->noise_ff ^= 1;
            if (p->noise_ff) {
                uint16_t fb = (p->period[3] & 4) ? ((p->lfsr ^ (p->lfsr >> 3)) & 1) : (p->lfsr & 1);
                p->lfsr = (uint16_t)(p->lfsr >> 1 | fb << 15);
            }
        }
    }
    PROF_LEAVE();
}

void psg_write(PSG_HOST *g, uint8_t v, uint64_t mclk)
{
    Psg *p = PSG_OF(g);
    psg_run(g, mclk);
    if (v & 0x80) p->latch = v >> 4 & 7;
    int c = p->latch >> 1;
    if (p->latch & 1) { p->vol[c] = v & 15; return; }
    if (c == 3) {
        p->period[3] = v & 7;
        p->lfsr = 0x8000;
        return;
    }
    if (v & 0x80) p->period[c] = (uint16_t)((p->period[c] & 0x3F0) | (v & 0x0F));
    else p->period[c] = (uint16_t)((p->period[c] & 0x00F) | (v & 0x3F) << 4);
}
