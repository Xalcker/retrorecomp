/*
 * Verifica un VGM grabado por un juego recompilado: lo reproduce en el APU
 * del runtime (como lo haría un reproductor de VGM, con su propio reloj de
 * 512 Hz para la secuencia de envolventes y duraciones) y compara la
 * envolvente de volumen con la del WAV grabado al mismo tiempo.
 *
 * Uso: vgmcheck musica.vgm musica.wav [salida.wav]
 *
 * Imprime la duración, cuántas escrituras trae y la correlación entre las
 * dos envolventes (ventanas de 10 ms). Cerca de 1 = el VGM reproduce la
 * misma música. Con salida.wav escribe lo que sonó al reproducir el VGM.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/gb.h"

static unsigned char *read_all(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)*size);
    if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static u32 le32(const unsigned char *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* Muestras del APU reproductor, en estéreo flotante */
static float *out;
static long out_n, out_cap;

static void drain(GB *g)
{
    float buf[4096 * 2];
    int n;
    while ((n = gb_audio_pull(g, buf, 4096)) > 0) {
        if (out_n + n > out_cap) {
            out_cap = (out_n + n) * 2;
            out = realloc(out, (size_t)out_cap * 2 * sizeof *out);
        }
        memcpy(out + out_n * 2, buf, (size_t)n * 2 * sizeof *out);
        out_n += n;
    }
}

/* Avanza el APU `dots` puntos, con el reloj de 512 Hz del reproductor */
static void advance(GB *g, u64 dots, u64 *clock)
{
    while (dots) {
        u64 to_tick = 8192 - (*clock % 8192);
        u64 n = dots < to_tick ? dots : to_tick;
        while (n) {
            u32 step = n > 4 ? 4 : (u32)n;     /* el APU avanza por M-ciclos */
            apu_mcycles(g, step, 1);
            n -= step;
            dots -= step;
            *clock += step;
        }
        if (*clock % 8192 == 0) apu_div_event(g);
        drain(g);
    }
}

/* Envolvente: valor RMS (mono) por ventana */
static double *envelope(const float *s, long n, int win, long *count)
{
    *count = n / win;
    double *e = calloc((size_t)*count + 1, sizeof *e);
    for (long w = 0; w < *count; w++) {
        double acc = 0;
        for (int i = 0; i < win; i++) {
            double m = (s[(w * win + i) * 2] + s[(w * win + i) * 2 + 1]) * 0.5;
            acc += m * m;
        }
        e[w] = sqrt(acc / win);
    }
    return e;
}

static double correlation(const double *a, const double *b, long n)
{
    double ma = 0, mb = 0;
    for (long i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (long i = 0; i < n; i++) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / sqrt(saa * sbb) : 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: vgmcheck musica.vgm musica.wav [salida.wav]\n");
        return 2;
    }
    long vsize, wsize;
    unsigned char *v = read_all(argv[1], &vsize), *w = read_all(argv[2], &wsize);
    if (!v || !w || vsize < 0x100 || memcmp(v, "Vgm ", 4) || wsize < 44 || memcmp(w, "RIFF", 4)) {
        fprintf(stderr, "no se pudieron leer los archivos (o no son VGM/WAV)\n");
        return 2;
    }
    if (le32(v + 0x80) == 0) {
        fprintf(stderr, "el VGM no declara el chip del Game Boy\n");
        return 1;
    }

    /* Una máquina mínima solo para usar su APU */
    static unsigned char rom[0x8000];
    static GB g;
    gb_init(&g, rom, sizeof rom);
    g.apu.enabled = 1;

    u32 pos = 0x34 + le32(v + 0x34);
    u64 samples = 0, clock = 0, dots_done = 0;
    long writes = 0;
    for (;;) {
        if (pos >= (u32)vsize) { fprintf(stderr, "el VGM se acaba sin $66\n"); return 1; }
        u8 c = v[pos];
        u32 wait = 0;
        if (c == 0xB3) {
            apu_write(&g, (u16)(0xFF10 + v[pos + 1]), v[pos + 2]);
            writes++;
            pos += 3;
            continue;
        }
        if (c == 0x61) { wait = v[pos + 1] | v[pos + 2] << 8; pos += 3; }
        else if (c == 0x62) { wait = 735; pos++; }
        else if (c == 0x63) { wait = 882; pos++; }
        else if ((c & 0xF0) == 0x70) { wait = (u32)(c & 15) + 1; pos++; }
        else if (c == 0x66) break;
        else { fprintf(stderr, "comando $%02X desconocido en $%X\n", c, pos); return 1; }
        samples += wait;
        u64 target = samples * GB_CPU_HZ / 44100;
        advance(&g, target - dots_done, &clock);
        dots_done = target;
    }
    if (samples != le32(v + 0x18))
        fprintf(stderr, "aviso: el encabezado dice %u muestras y hay %llu\n", le32(v + 0x18),
                (unsigned long long)samples);

    /* WAV grabado: 16 bits estéreo */
    long wn = (long)le32(w + 40) / 4;
    float *ws = malloc((size_t)wn * 2 * sizeof *ws);
    for (long i = 0; i < wn * 2; i++) ws[i] = (short)(w[44 + i * 2] | w[45 + i * 2] << 8) / 32767.0f;

    long n = out_n < wn ? out_n : wn, ea_n, eb_n;
    int win = GB_SAMPLE_RATE / 100;
    double *ea = envelope(out, n, win, &ea_n), *eb = envelope(ws, n, win, &eb_n);
    double r = correlation(ea, eb, ea_n);
    printf("VGM: %.1f s, %ld escrituras; WAV: %.1f s; correlación de envolventes: %.3f\n",
           samples / 44100.0, writes, wn / (double)GB_SAMPLE_RATE, r);
    if (getenv("VGMCHECK_DEBUG")) {
        double sa = 0, sb = 0;
        for (long i = 0; i < ea_n; i++) { sa += ea[i]; sb += eb[i]; }
        printf("  muestras: VGM %ld, WAV %ld, ventanas %ld\n", out_n, wn, ea_n);
        printf("  nivel medio: VGM %.4f, WAV %.4f\n", sa / ea_n, sb / ea_n);
        int best = 0;
        double best_r = -2;
        for (int lag = -200; lag <= 200; lag++) {
            long m = ea_n - (lag < 0 ? -lag : lag);
            double rr = lag >= 0 ? correlation(ea + lag, eb, m) : correlation(ea, eb - lag, m);
            if (rr > best_r) { best_r = rr; best = lag; }
        }
        printf("  mejor desfase: %d ventanas de 10 ms (correlación %.3f)\n", best, best_r);
        for (int s = 0; s < 60 && s * 100 < ea_n; s += 5)
            printf("  t=%2ds VGM %.4f WAV %.4f\n", s, ea[s * 100], eb[s * 100]);
    }

    if (argc > 3) {
        FILE *f = fopen(argv[3], "wb");
        if (f) {
            unsigned char h[44];
            memcpy(h, w, 44);
            u32 bytes = (u32)out_n * 4;
            h[40] = (u8)bytes; h[41] = (u8)(bytes >> 8); h[42] = (u8)(bytes >> 16); h[43] = (u8)(bytes >> 24);
            u32 riff = 36 + bytes;
            h[4] = (u8)riff; h[5] = (u8)(riff >> 8); h[6] = (u8)(riff >> 16); h[7] = (u8)(riff >> 24);
            fwrite(h, 1, 44, f);
            for (long i = 0; i < out_n * 2; i++) {
                float x = out[i] * 32767.0f;
                short s = (short)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
                fwrite(&s, 2, 1, f);
            }
            fclose(f);
        }
    }
    return r > 0.9 ? 0 : 1;
}
