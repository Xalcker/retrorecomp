/*
 * Prueba sintética del sonido (make test): programa el PSG directamente y mide la salida a 44 100 Hz, como el
 * audiotest de GENRecomp: la afinación (cruces por cero de un La de 440 Hz), el silencio con el volumen 15, los
 * pasos de 2 dB, el ruido, el silencio de un canal (el menú Audio), el estéreo del Game Gear en modo GG (el
 * puerto $06, también por un OUT) y que en el Master System los dos lados suenen igual.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static Sms g;
static u64 now;                                     /* ciclo maestro */
static int16_t buf[AUDIO_RATE * 2 * 2];
static int nbuf;

static void fresh(const char *name)
{
    static u8 rom[0x8000];
    const char *err;
    sms_free(&g);
    if (sms_load(&g, rom, sizeof rom, name, MODEL_AUTO, &err)) { printf("no carga: %s\n", err); exit(1); }
    now = 0;
    nbuf = 0;
}

static void psg(u8 v) { psg_write(&g, v, now); }

/* Corre el PSG `sec` segundos y guarda la salida */
static void run(double sec)
{
    u64 end = now + (u64)(sec * 53693175.0);
    while (now < end) {
        now += 53693175 / 60;
        g.cyc = now / MCLK_PER_Z80;
        audio_end_frame(&g);
        nbuf += sms_audio_take(&g, buf + nbuf * 2, (int)(sizeof buf / 4) - nbuf);
    }
}

/* Frecuencia por cruces por cero del canal ch (0 L, 1 R) en las muestras [from, nbuf) */
static double freq(int ch, int from)
{
    int cross = 0, first = -1, last = -1;
    for (int i = from + 1; i < nbuf; i++)
        if ((buf[(i - 1) * 2 + ch] < 0) != (buf[i * 2 + ch] < 0)) {
            cross++;
            if (first < 0) first = i;
            last = i;
        }
    if (cross < 3) return 0;
    return (cross - 1) / 2.0 / ((last - first) / (double)AUDIO_RATE);
}

static double rms(int ch, int from, int to)
{
    double s = 0;
    for (int i = from; i < to; i++) s += (double)buf[i * 2 + ch] * buf[i * 2 + ch];
    return to > from ? sqrt(s / (to - from)) : 0;
}

static void tone(void)
{
    fresh("a.sms");
    psg(0x80 | (254 & 15));                         /* tono 0: periodo 254 -> 3 579 545 / (32 x 254) = 440.4 Hz */
    psg((u8)(254 >> 4));
    psg(0x90);                                      /* volumen 0 (máximo) */
    run(0.5);
    double f = freq(0, nbuf / 4);
    CHECK(fabs(f - 440.4) < 2.0, "La del PSG: %.1f Hz (esperado 440.4)", f);
    double loud = rms(0, nbuf / 4, nbuf);
    CHECK(loud > 2000, "el La del PSG suena muy bajo (%.0f)", loud);
    CHECK(rms(0, nbuf / 4, nbuf) == rms(1, nbuf / 4, nbuf), "en el SMS los dos lados no suenan igual");
    /* 2 dB por paso: el volumen 3 queda 6 dB abajo (la mitad) */
    int from = nbuf;
    psg(0x93);
    run(0.3);
    double half = rms(0, from + (nbuf - from) / 3, nbuf);
    CHECK(fabs(half / loud - 0.501) < 0.05, "volumen 3: %.3f del máximo (esperado ~0,50)", half / loud);
    /* El silencio del menú Audio (bit 0 de audio.mute) */
    from = nbuf;
    g.audio.mute = 0x01;
    run(0.3);
    CHECK(rms(0, from + (nbuf - from) / 2, nbuf) < 50, "el canal 0 silenciado suena (%.0f)", rms(0, from + (nbuf - from) / 2, nbuf));
    g.audio.mute = 0;
    from = nbuf;
    psg(0x9F);                                      /* volumen 15: silencio */
    run(0.3);
    CHECK(rms(0, from + (nbuf - from) / 2, nbuf) < 50, "el volumen 15 no calla el canal");
    /* Ruido blanco, periodo 16 */
    from = nbuf;
    psg(0xE4);
    psg(0xF0);
    run(0.3);
    double nr = rms(0, from + (nbuf - from) / 3, nbuf);
    double nf = freq(0, from + (nbuf - from) / 3);
    CHECK(nr > 1000 && nf > 1000, "el ruido no suena como ruido (nivel %.0f, %.0f cruces por segundo / 2)", nr, nf);
}

static void stereo(void)
{
    /* Game Gear en modo GG: el tono 0 solo a la izquierda (bit 4 de $06) */
    fresh("a.gg");
    psg(0x80 | (254 & 15));
    psg((u8)(254 >> 4));
    psg(0x90);
    g.io_cyc = now / MCLK_PER_Z80;
    g.cyc = g.io_cyc;
    z80_out(&g, 0x06, 0x10);
    run(0.5);
    double l = rms(0, nbuf / 4, nbuf), r = rms(1, nbuf / 4, nbuf);
    CHECK(l > 2000 && r < 50, "GG, $06 = $10: izquierda %.0f, derecha %.0f", l, r);
    int from = nbuf;
    g.io_cyc = g.cyc = now / MCLK_PER_Z80;
    z80_out(&g, 0x06, 0x01);                        /* solo a la derecha */
    run(0.5);
    l = rms(0, from + (nbuf - from) / 4, nbuf); r = rms(1, from + (nbuf - from) / 4, nbuf);
    CHECK(r > 2000 && l < 50, "GG, $06 = $01: izquierda %.0f, derecha %.0f", l, r);
    /* Un juego de SMS en el GG: sin estéreo */
    fresh("a.sms");
    sms_free(&g);
    {
        static u8 rom[0x8000];
        const char *err;
        sms_load(&g, rom, sizeof rom, "a.sms", MODEL_GG, &err);
    }
    now = 0; nbuf = 0;
    psg(0x80 | (254 & 15));
    psg((u8)(254 >> 4));
    psg(0x90);
    g.io.gg[6] = 0x10;                              /* no existe en el modo SMS: no cuenta */
    run(0.5);
    CHECK(rms(0, nbuf / 4, nbuf) == rms(1, nbuf / 4, nbuf), "GG en modo SMS con estéreo");
}

int main(void)
{
    console_utf8();
    tone();
    stereo();
    sms_free(&g);
    if (fallas) { printf("audiotest: %d fallas\n", fallas); return 1; }
    printf("audiotest: afinación, volumen, ruido, silencio por canal y estéreo del Game Gear bien\n");
    return 0;
}
