/*
 * Prueba del YM2610 y de la mezcla (make test), manejando el chip como lo haría el Z80 (ym_write),
 * con un juego sintético cuya V ROM tiene muestras hechas aquí:
 *   - FM: un La de 440 Hz (el reloj del AES lo sube un 0,7 %), el panorama y silenciar un canal; los
 *     canales 1 y 4 del OPNA no existen.
 *   - SSG: un La (TP = 568), el ruido, la envolvente (sube y se queda) y el volumen.
 *   - ADPCM-A: los primeros valores de una muestra (calculados a mano con la tabla de pasos), el fin
 *     con su bandera, la máscara de $1C y soltar la tecla.
 *   - ADPCM-B: cuánto tarda una muestra según Delta-N, la repetición y su bandera.
 *   - Temporizadores A y B, su IRQ y la bandera de ocupado.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static Neo n;
static u64 now;                                 /* ciclo maestro de la prueba */

static void w(int half, u8 r, u8 v)
{
    ym_write(&n, half * 2, r, now);
    ym_write(&n, half * 2 + 1, v, now);
}

/* Corre el chip `ms` milisegundos (hasta 5 s) y deja el sonido en out (pares L R); devuelve cuántos
 * pares. Toma todas las muestras que salen, para que no queden en la cola para la medición siguiente */
#define OUT_PAIRS (AUDIO_RATE * 5)
static int16_t out[OUT_PAIRS * 2];
static int run_ms(int ms)
{
    int total = 0;
    u64 end = now + (u64)(MCLK_HZ * ms / 1000.0);
    while (now < end) {
        now += M68K_PER_LINE * MCLK_PER_M68K * 16;
        n.cyc = now / MCLK_PER_M68K;
        audio_end_frame(&n);
        static int16_t tmp[AUDIO_BINS * 2];
        int k = neo_audio_take(&n, tmp, AUDIO_BINS);
        if (k > OUT_PAIRS - total) k = OUT_PAIRS - total;
        memcpy(out + total * 2, tmp, (size_t)k * 4);
        total += k;
    }
    return total;
}

/* Frecuencia por los cruces por cero (de negativo a positivo) del canal c */
static double freq(int pairs, int c)
{
    int cross = 0, first = -1, last = -1;
    for (int i = 1; i < pairs; i++)
        if (out[(i - 1) * 2 + c] < 0 && out[i * 2 + c] >= 0) { if (first < 0) first = i; last = i; cross++; }
    if (cross < 2) return 0;
    return (double)(cross - 1) * AUDIO_RATE / (double)(last - first);
}

static double level(int pairs, int c)
{
    double s = 0;
    for (int i = 0; i < pairs; i++) s += (double)out[i * 2 + c] * out[i * 2 + c];
    return pairs ? sqrt(s / pairs) : 0;
}

static void reset_chip(void)
{
    ym_reset(&n.ym);
    n.ym.mclk = now;
    n.cyc = now / MCLK_PER_M68K;
    audio_reset(&n);
    n.audio.mute = 0;
    run_ms(20);                                 /* que el pasa-altos se asiente */
}

/* Un canal FM (c: 0-2 de una mitad) con solo S4 sonando (algoritmo 7), fnum y bloque */
static void fm_note(int half, int c, u16 fnum, u8 block, u8 pan)
{
    w(half, (u8)(0xB0 + c), 0x07);              /* algoritmo 7, sin realimentación */
    w(half, (u8)(0xB4 + c), pan);
    for (int o = 0; o < 4; o++) {
        u8 base = (u8)(o * 4 + c);
        w(half, (u8)(0x30 + base), 0x01);       /* múltiplo 1 */
        w(half, (u8)(0x40 + base), o == 3 ? 0x00 : 0x7F);   /* solo S4 (registro $4C) */
        w(half, (u8)(0x50 + base), 0x1F);       /* ataque inmediato */
        w(half, (u8)(0x60 + base), 0x00);
        w(half, (u8)(0x70 + base), 0x00);
        w(half, (u8)(0x80 + base), 0x0F);
    }
    w(half, (u8)(0xA4 + c), (u8)(block << 3 | fnum >> 8));
    w(half, (u8)(0xA0 + c), (u8)fnum);
    w(0, 0x28, (u8)(0xF0 | (half ? 4 : 0) | c));
}

int main(void)
{
    console_utf8();
    /* El juego: P1 con la cabecera, M1 vacía y una V ROM con muestras hechas aquí */
    static u8 cpu[0x100000], p1[0x100000], m1[0x10000], v1[0x40000];
    memcpy(cpu + 0x100, "NEO-GEO", 7);
    for (size_t i = 0; i < sizeof cpu; i += 2) { p1[i] = cpu[i + 1]; p1[i + 1] = cpu[i]; }
    memset(v1, 0x77, 0x100);                    /* $000000: 256 bytes de nibbles 7 (ADPCM-A) */
    memset(v1 + 0x100, 0x08, 0x100);            /* $000100 */
    for (int i = 0x10000; i < 0x20000; i++) v1[i] = (u8)(i & 0x80 ? 0x99 : 0x11);   /* $010000: ADPCM-B, 64 KB */
    RomPart q[3] = { { "t-p1.p1", p1, sizeof p1, 0, "" }, { "t-m1.m1", m1, sizeof m1, 0, "" }, { "t-v1.v1", v1, sizeof v1, 0, "" } };
    for (int i = 0; i < 3; i++) q[i].crc = crc32_buf(0, q[i].data, q[i].size);
    const char *err = NULL;
    if (cart_load_parts(&n.cart, q, 3, ROMFMT_ZIP, &err)) { printf("FALLA: juego de prueba: %s\n", err); return 1; }
    l0_generate(n.l0);
    neo_power_on(&n);
    const double clock = MCLK_HZ / 24000000.0;   /* el reloj del AES contra los 8 MHz de los manuales */

    /* --- FM: La de 440 Hz en el canal 2 del OPNA (FM 1): f = fnum × 2^(bloque-1) × fs / 2^20 --- */
    reset_chip();
    double fs = MCLK_HZ / MCLK_PER_FM;
    u16 fnum = (u16)lround(440.0 * 1048576.0 / (fs * 8));
    fm_note(0, 1, fnum, 4, 0xC0);
    int p = run_ms(500);
    double f = freq(p, 0);
    CHECK(fabs(f - 440.0) < 2.0, "FM: %.1f Hz (esperado 440)", f);
    CHECK(level(p, 0) > 1000 && fabs(level(p, 0) - level(p, 1)) < 1, "FM: nivel %.0f en los dos lados", level(p, 0));
    /* Panorama: solo la izquierda */
    w(0, 0xB5, 0x80);
    p = run_ms(100);
    CHECK(level(p, 0) > 1000 && level(p, 1) < 50, "FM: panorama a la izquierda (%.0f / %.0f)", level(p, 0), level(p, 1));
    /* Silenciar FM 1 */
    n.audio.mute = 1;
    p = run_ms(100);
    CHECK(level(p, 0) < 50, "FM: silenciado (%.0f)", level(p, 0));
    n.audio.mute = 0;
    /* El canal 1 del OPNA (índice 0) no existe */
    reset_chip();
    fm_note(0, 0, fnum, 4, 0xC0);
    p = run_ms(100);
    CHECK(level(p, 0) < 5, "FM: el canal 0 no existe (%.0f)", level(p, 0));
    /* El canal 6 del OPNA (FM 4) sí */
    fm_note(1, 2, fnum, 4, 0xC0);
    p = run_ms(200);
    CHECK(level(p, 0) > 1000, "FM: el canal 6 del OPNA suena");

    /* --- SSG: La con TP = 568 (250 000 / 568 = 440,1 Hz con 8 MHz) --- */
    reset_chip();
    w(0, 0x00, 568 & 0xFF); w(0, 0x01, 568 >> 8);
    w(0, 0x07, 0x3E);                           /* solo el tono A */
    w(0, 0x08, 0x0F);
    p = run_ms(500);
    f = freq(p, 0);
    double want = 250000.0 / 568 * clock;
    CHECK(fabs(f - want) < 2.0, "SSG: %.1f Hz (esperado %.1f)", f, want);
    double full = level(p, 0);
    CHECK(full > 500, "SSG: nivel %.0f", full);
    w(0, 0x08, 0x07);                           /* volumen 7: 8 pasos de 3 dB menos (1/16 de amplitud) */
    run_ms(150);                                /* el SSG no tiene signo: que el pasa-altos absorba el salto de continua */
    p = run_ms(200);
    CHECK(level(p, 0) < full / 8 && level(p, 0) > full / 32, "SSG: volumen 7 (%.0f contra %.0f)", level(p, 0), full);
    /* Ruido: muchos más cruces que el tono */
    w(0, 0x07, 0x37); w(0, 0x08, 0x0F); w(0, 0x06, 0x01);
    p = run_ms(200);
    CHECK(freq(p, 0) > 2000 && level(p, 0) > 300, "SSG: ruido (%.0f cruces por segundo)", freq(p, 0));
    /* Envolvente: forma $0D (sube y se queda arriba) con periodo corto; el canal sin tono ni ruido */
    reset_chip();
    w(0, 0x07, 0x3F); w(0, 0x08, 0x10); w(0, 0x0B, 0x10); w(0, 0x0C, 0x00); w(0, 0x0D, 0x0D);
    run_ms(50);
    CHECK(n.ym.ssg.ehold && n.ym.ssg.eatt && n.ym.ssg.estep == 31, "SSG: la envolvente $0D sube y se queda arriba");
    w(0, 0x0D, 0x09);
    run_ms(50);
    CHECK(n.ym.ssg.ehold && ((n.ym.ssg.eatt ? n.ym.ssg.estep : 31 - n.ym.ssg.estep) == 0), "SSG: la envolvente $09 baja y se queda en 0");

    /* --- ADPCM-A: canal 1, de $000000 a $0000FF (512 nibbles de 7) --- */
    reset_chip();
    w(1, 0x01, 0x3F);                           /* volumen maestro al máximo */
    w(1, 0x08, 0xDF);                           /* L R, volumen al máximo */
    w(1, 0x10, 0x00); w(1, 0x18, 0x00); w(1, 0x20, 0x00); w(1, 0x28, 0x00);
    w(0, 0x1C, 0x00);                           /* banderas sin máscara */
    w(1, 0x00, 0x01);                           /* tecla */
    /* Tres muestras del FM por muestra del ADPCM-A. Con el nibble 7 el acumulador suma (15 × paso) / 8 y
     * el índice sube 9: pasos 16, 37, 88 y 209, que dan 30, 99, 264 y 655 */
    static const int expect[4] = { 30, 99, 264, 655 };
    int ok = 1;
    for (int k = 0; k < 4; k++) {
        now += 3 * MCLK_PER_FM;
        ym_run(&n, now);
        if (n.ym.pa[0].acc != ((expect[k] + 2048) & 0xFFF) - 2048) { ok = 0; printf("  muestra %d: %d\n", k, n.ym.pa[0].acc); }
    }
    CHECK(ok, "ADPCM-A: los primeros valores de la muestra");
    run_ms(100);                                /* 512 muestras a 18 518 Hz: 27,6 ms */
    CHECK(!n.ym.pa[0].on && (ym_read(&n, 2, now) & 1), "ADPCM-A: terminó y levantó su bandera");
    w(0, 0x1C, 0x01);                           /* borrar y enmascarar */
    CHECK(!(ym_read(&n, 2, now) & 1), "ADPCM-A: $1C borra la bandera");
    w(1, 0x00, 0x01);
    run_ms(100);
    CHECK(!(ym_read(&n, 2, now) & 1), "ADPCM-A: enmascarada no se levanta");
    w(0, 0x1C, 0x00);
    w(1, 0x00, 0x01);
    now += 30 * MCLK_PER_FM;
    w(1, 0x00, 0x81);                           /* soltar */
    CHECK(!n.ym.pa[0].on, "ADPCM-A: soltar la tecla la calla");

    /* --- ADPCM-B: de $010000 a $01FFFF (131 072 nibbles) --- */
    reset_chip();
    w(0, 0x11, 0xC0); w(0, 0x1B, 0xFF);
    w(0, 0x12, 0x00); w(0, 0x13, 0x01); w(0, 0x14, 0xFF); w(0, 0x15, 0x01);
    w(0, 0x19, 0xFF); w(0, 0x1A, 0xFF);         /* Delta-N = $FFFF: casi una muestra por cada del FM */
    w(0, 0x10, 0x80);
    p = run_ms(1000);
    CHECK(n.ym.pb.on && level(p, 0) > 100, "ADPCM-B: suena (%.0f)", level(p, 0));
    run_ms(1500);                               /* 131 072 / 55 944 = 2,34 s */
    CHECK(!n.ym.pb.on && (ym_read(&n, 2, now) & 0x80), "ADPCM-B: terminó a los ~2,34 s y levantó su bandera");
    w(0, 0x1C, 0x80); w(0, 0x1C, 0x00);
    w(0, 0x19, 0x00); w(0, 0x1A, 0x80);         /* la mitad de rápido */
    w(0, 0x10, 0x00); w(0, 0x10, 0x80);
    run_ms(4000);
    CHECK(n.ym.pb.on, "ADPCM-B: con Delta-N a la mitad sigue a los 4 s");
    run_ms(1000);
    CHECK(!n.ym.pb.on, "ADPCM-B: y termina antes de los 5 s");
    w(0, 0x19, 0xFF); w(0, 0x1A, 0xFF);
    w(0, 0x10, 0x00); w(0, 0x10, 0x90);         /* con repetición */
    run_ms(3000);
    CHECK(n.ym.pb.on, "ADPCM-B: con repetición vuelve a empezar");
    w(0, 0x10, 0x01);
    CHECK(!n.ym.pb.on && n.ym.reg[0][0x10] == 0, "ADPCM-B: reset");

    /* --- Temporizadores, IRQ y ocupado --- */
    reset_chip();
    w(0, 0x24, 0xFF); w(0, 0x25, 0x03);         /* A: N = 1023, vence cada muestra */
    w(0, 0x27, 0x05);
    now += 5 * MCLK_PER_FM;
    CHECK(ym_read(&n, 0, now) & 1, "temporizador A: bandera");
    w(0, 0x27, 0x15);
    CHECK(!(ym_read(&n, 0, now) & 1), "temporizador A: borrar la bandera");
    w(0, 0x26, 0xFF);                           /* B: N = 255, vence cada 16 muestras */
    w(0, 0x27, 0x0A);
    now += 15 * MCLK_PER_FM;
    CHECK(!(ym_read(&n, 0, now) & 2), "temporizador B: todavía no");
    now += 2 * MCLK_PER_FM;
    CHECK(ym_read(&n, 0, now) & 2, "temporizador B: a las 16 muestras");
    w(0, 0x06, 0x01);
    CHECK(ym_read(&n, 0, now) & 0x80, "ocupado tras escribir un dato");
    now += 1000;
    CHECK(!(ym_read(&n, 0, now) & 0x80), "y después no");

    neo_free(&n);
    if (fallas) { printf("audiotest: %d fallas\n", fallas); return 1; }
    printf("audiotest: todo bien\n");
    return 0;
}
