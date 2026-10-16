/*
 * Prueba sintética del sonido (make test): programa el PSG y el YM2612 directamente y mide la salida a
 * 44 100 Hz: la afinación (cruces por cero de un La de 440 Hz en cada chip), el panorama, la DAC del
 * canal 6, la relajación al soltar la tecla, el temporizador A (cada cuántas muestras desborda), la
 * bandera de ocupado y los modos de SSG-EG del YM2612.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static Gen *g;
static u64 now;                                     /* ciclo maestro */
static int16_t buf[AUDIO_RATE * 2 * 2];
static int nbuf;

static void fresh(void)
{
    static u8 rom[0x20000];
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0x100, "SEGA MEGA DRIVE ", 16);
    rom[1] = 0xFF; rom[2] = 0xFE; rom[6] = 0x02; rom[0x200] = 0x60; rom[0x201] = 0xFE;
    if (!g) g = malloc(sizeof *g);
    else gen_free(g);
    const char *err;
    if (gen_load(g, rom, sizeof rom, &err)) { printf("no carga: %s\n", err); exit(1); }
    now = 0;
    nbuf = 0;
}

static void ym(int port, u8 r, u8 v) { ym_write(g, port * 2, r, now); ym_write(g, port * 2 + 1, v, now); now += 2000; }

/* Corre los chips `sec` segundos y guarda la salida */
static void run(double sec)
{
    u64 end = now + (u64)(sec * 53693175.0);
    while (now < end) {
        now += 53693175 / 60;
        g->cyc = now / MCLK_PER_M68K;
        audio_end_frame(g);
        nbuf += gen_audio_take(g, buf + nbuf * 2, (int)(sizeof buf / 4) - nbuf);
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

static void test_psg(void)
{
    fresh();
    psg_write(g, 0x80 | (254 & 15), now);          /* tono 0: periodo 254 -> 3 579 545 / (32 x 254) = 440.4 Hz */
    psg_write(g, (u8)(254 >> 4), now);
    psg_write(g, 0x90, now);                        /* volumen 0 (máximo) */
    run(1.0);
    double f = freq(0, 0);
    CHECK(fabs(f - 440.4) < 1.0, "PSG: tono de 440.4 Hz (%.2f)", f);
    CHECK(rms(0, 0, nbuf) > 1000 && fabs(rms(0, 0, nbuf) - rms(1, 0, nbuf)) < 1, "PSG: suena igual en los dos lados");
    psg_write(g, 0x9F, now);                        /* volumen 15: silencio */
    int from = nbuf;
    run(0.2);
    CHECK(rms(0, from + 100, nbuf) < 1, "PSG: volumen 15 en silencio");
    psg_write(g, 0xE4, now);                        /* ruido blanco, periodo 16 */
    psg_write(g, 0xF0, now);
    from = nbuf;
    run(0.2);
    CHECK(rms(0, from, nbuf) > 500, "PSG: ruido blanco");
}

/* Canal 0 con un solo operador (algoritmo 7, S1) */
static void ym_tone(int pan)
{
    ym(0, 0xB0, 0x07);                              /* algoritmo 7, sin realimentación */
    ym(0, 0xB4, (u8)pan);
    static const u8 tl[4] = { 0x00, 0x7F, 0x7F, 0x7F };    /* registros $40, $44, $48, $4C: S1 S3 S2 S4 */
    for (int k = 0; k < 4; k++) {
        ym(0, (u8)(0x30 + k * 4), 0x01);            /* DT 0, MUL 1 */
        ym(0, (u8)(0x40 + k * 4), tl[k]);
        ym(0, (u8)(0x50 + k * 4), 0x1F);            /* AR 31 */
        ym(0, (u8)(0x60 + k * 4), 0x00);
        ym(0, (u8)(0x70 + k * 4), 0x00);
        ym(0, (u8)(0x80 + k * 4), 0x0F);            /* SL 0, RR 15 */
    }
    ym(0, 0xA4, (4 << 3) | (1083 >> 8));            /* bloque 4, fnum 1083: 440 Hz */
    ym(0, 0xA0, 1083 & 0xFF);
    ym(0, 0x28, 0x10);                              /* S1 de canal 0 */
}

static void test_ym(void)
{
    fresh();
    ym_tone(0x80);                                  /* solo a la izquierda */
    run(1.0);
    double f = freq(0, 0);
    CHECK(fabs(f - 440.0) < 1.5, "YM2612: La de 440 Hz con fnum 1083 y bloque 4 (%.2f)", f);
    CHECK(rms(0, 0, nbuf) > 2000 && rms(1, 0, nbuf) < 1, "YM2612: panorama a la izquierda (L %.0f, R %.0f)", rms(0, 0, nbuf), rms(1, 0, nbuf));
    ym(0, 0x28, 0x00);                              /* soltar */
    int from = nbuf;
    run(0.5);
    CHECK(rms(0, nbuf - 2000, nbuf) < 1, "YM2612: la relajación llega al silencio");
    CHECK(rms(0, from, from + 200) > 10, "YM2612: la relajación no es instantánea con RR 15");

    fresh();
    ym(0, 0x2B, 0x80);                              /* DAC */
    ym(0, 0x2A, 0x80);                              /* el centro: sin salida */
    run(0.2);
    from = nbuf;
    ym(0, 0x2A, 0xFF);                              /* escalón: el pasa-altos de la salida lo deja pasar al principio */
    run(0.1);
    CHECK(buf[(from + 20) * 2] > 1000 && buf[(from + 20) * 2 + 1] > 1000, "YM2612: DAC del canal 6 (%d)", buf[(from + 20) * 2]);
    CHECK(abs(buf[(nbuf - 1) * 2]) < 100, "YM2612: la DAC quieta no deja componente continua (%d)", buf[(nbuf - 1) * 2]);

    fresh();
    ym(0, 0x24, (u8)((1024 - 100) >> 2));           /* temporizador A: 100 muestras */
    ym(0, 0x25, (u8)((1024 - 100) & 3));
    u64 t0 = now;                                   /* el temporizador arranca al escribir $27 */
    ym(0, 0x27, 0x05);                              /* carga y bandera de A */
    while (!(ym_read(g, now) & 1)) now += 100;
    double samples = (double)(now - t0) / 1008.0;
    CHECK(samples > 98 && samples < 102, "YM2612: el temporizador A desborda a las 100 muestras (%.1f)", samples);
    ym(0, 0x27, 0x15);                              /* reiniciar la bandera */
    CHECK(!(ym_read(g, now) & 1), "YM2612: la bandera del temporizador A se borra con $27 bit 4");
    ym_write(g, 0, 0x30, now);
    ym_write(g, 1, 0x00, now);
    CHECK(ym_read(g, now) & 0x80, "YM2612: ocupado justo después de escribir un dato");
    CHECK(!(ym_read(g, now + 2000) & 0x80), "YM2612: ya no ocupado después");
}

/* SSG-EG en S1 del canal 0, con decaimiento rápido: nivel (RMS) del último tramo y el menor de
 * los tramos de 50 ms entre 0.1 s y 1 s */
static void ssg_case(u8 mode, double *last, double *min_win)
{
    fresh();
    ym_tone(0xC0);
    ym(0, 0x60, 20);                                /* DR 20 */
    ym(0, 0x80, 0xFF);                              /* SL 15, RR 15 */
    ym(0, 0x90, mode);
    ym(0, 0x28, 0x00);
    ym(0, 0x28, 0x10);                              /* tecla de nuevo, con todo puesto */
    run(1.0);
    *last = rms(0, nbuf - 4410, nbuf);
    *min_win = 1e9;
    for (int i = 4410; i + 2205 <= nbuf; i += 2205) {
        double r = rms(0, i, i + 2205);
        if (r < *min_win) *min_win = r;
    }
}

static void test_ssg(void)
{
    double last, minw;
    ssg_case(0x00, &last, &minw);
    CHECK(last < 50, "sin SSG-EG la nota decae (%.0f)", last);
    ssg_case(0x08, &last, &minw);
    CHECK(minw > 300, "SSG-EG $08 (repetir): sigue sonando (el tramo más bajo %.0f)", minw);
    ssg_case(0x09, &last, &minw);
    CHECK(last < 1, "SSG-EG $09 (una vez): queda en silencio (%.0f)", last);
    ssg_case(0x0B, &last, &minw);
    CHECK(last > 2000, "SSG-EG $0B (una vez, invertida): queda sonando (%.0f)", last);
    ssg_case(0x0A, &last, &minw);
    CHECK(minw > 300, "SSG-EG $0A (alternar): sigue sonando (el tramo más bajo %.0f)", minw);
}

int main(void)
{
    console_utf8();
    test_psg();
    test_ym();
    test_ssg();
    gen_free(g);
    free(g);
    if (!fallas) printf("audiotest: todo bien\n");
    return fallas != 0;
}
