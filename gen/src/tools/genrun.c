/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos,
 * excepciones por vector y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de
 * espera). Sirve para ver hasta dónde llega un juego mientras falta el resto de la consola.
 *
 *   genrun rom.bin [--frames N] [--watch DIR ...] [boton:desde-hasta ...]
 *
 * Botones: up down left right a b c start x y z mode (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $FF0000-$FFFFFF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la SRAM o EEPROM del cartucho al empezar (si existe) y la guarda al terminar.
 * --tmss: consola con TMSS (el juego debe escribir "SEGA" en $A14000 antes de tocar el VDP).
 * --shot ARCHIVO.png guarda la imagen del último cuadro; --shots CARPETA --every N, una cada N cuadros.
 * --wav ARCHIVO.wav guarda el sonido (estéreo, 44 100 Hz, 16 bits) y su nivel por segundo.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"
#include "png.h"

/* La imagen del cuadro (el tamaño que tiene en ese modo) como PNG */
static int save_frame(const Gen *g, const char *path)
{
    const Vdp *v = &g->vdp;
    int w = v->width, h = v->height;
    unsigned char *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = vdp_argb(v->fb[y * FB_W + x]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (unsigned char)(c >> 16); p[1] = (unsigned char)(c >> 8); p[2] = (unsigned char)c;
        }
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    return ok;
}

static u8 *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

typedef struct { int btn; long from, to; } Hold;

/* Histograma de PC del último cuadro (direccionamiento abierto) */
#define HN 8192
static u32 hpc[HN], hcount[HN];
static void hist_add(u32 pc)
{
    u32 i = (pc * 2654435761u) % HN;
    while (hcount[i] && hpc[i] != pc) i = (i + 1) % HN;
    hpc[i] = pc; hcount[i]++;
}

/* Después de cada instrucción: VDP al día, cuenta y (en el último cuadro) histograma del PC */
static u64 steps;
static int last_frame;
static int after(Gen *g)
{
    gen_after_step(g);
    steps++;
    if (last_frame) hist_add(g->cpu.pc);
    return g->vdp.frame_done || g->cpu.halted;
}

int main(int argc, char **argv)
{
    console_utf8();
    static const char *names[] = { "up", "down", "left", "right", "b", "c", "a", "start", "z", "y", "x", "mode" };
    const char *rom = NULL;
    long frames = 600;
    Hold h[32]; int nh = 0;
    u32 watch[16]; int nw = 0; int wlast[16];
    const char *sram = NULL;
    int tmss = 0;
    const char *shot = NULL, *shots = NULL, *wav = NULL;
    long every = 60;
    for (int i = 1; i < argc; i++) {
        char name[16]; long f0, f1;
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--sram") && i + 1 < argc) sram = argv[++i];
        else if (!strcmp(argv[i], "--tmss")) tmss = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc && nw < 16) {
            const char *a = argv[++i];
            if (*a == '$') a++;
            watch[nw] = (u32)strtoul(a, NULL, 16) & 0xFFFF; wlast[nw++] = -1;
        }
        else if (sscanf(argv[i], "%15[a-z]:%ld-%ld", name, &f0, &f1) == 3) {
            int ok = 0;
            for (int k = 0; k < 12 && nh < 32; k++) if (!strcmp(name, names[k])) { h[nh++] = (Hold){ 1 << k, f0, f1 }; ok = 1; }
            if (!ok) { fprintf(stderr, "botón desconocido: %s\n", name); return 2; }
        } else if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); return 2; }
        else rom = argv[i];
    }
    if (!rom || frames <= 0) {
        fprintf(stderr, "uso: genrun rom.bin [--frames N] [--watch DIR ...] [--sram ARCHIVO] [--tmss]\n"
                        "              [--shot ARCHIVO.png] [--shots CARPETA [--every N]] [--wav ARCHIVO.wav]\n"
                        "              [boton:desde-hasta ...]\n"
                        "  botones: up down left right a b c start x y z mode\n");
        return 2;
    }
    size_t len;
    u8 *data = read_file(rom, &len);
    if (!data) { fprintf(stderr, "no se pudo leer %s\n", rom); return 1; }
    Gen *g = malloc(sizeof *g);
    const char *err = NULL;
    if (gen_load(g, data, len, &err)) { fprintf(stderr, "%s: %s\n", rom, err); return 1; }
    free(data);
    if (tmss) { g->tmss = 1; gen_reset(g); }
    FILE *wf = NULL;
    u32 wav_bytes = 0;
    double sec_sum = 0; long sec_n = 0, sec = 0;
    if (wav) {
        wf = fopen(wav, "wb");
        if (!wf) { fprintf(stderr, "no se pudo crear %s\n", wav); return 1; }
        static const u8 hdr[44] = { 0 };
        fwrite(hdr, 1, 44, wf);
    }
    if (sram && gen_sram_load(g, sram)) printf("partida cargada de %s\n", sram);

    for (long f = 0; f < frames; f++) {
        u16 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].from && f <= h[i].to) pad |= (u16)h[i].btn;
        g->pad[0] = pad;
        last_frame = f == frames - 1;
        g->vdp.frame_done = 0;
        m68k_run(g, after);
        audio_end_frame(g);
        {
            static int16_t s[4096 * 2];
            int n = gen_audio_take(g, s, 4096);
            if (wf) {
                fwrite(s, 4, (size_t)n, wf);
                wav_bytes += (u32)n * 4;
                for (int i = 0; i < n * 2; i++) sec_sum += (double)s[i] * s[i];
                sec_n += n * 2;
                if (sec_n >= AUDIO_RATE * 2) {
                    printf("audio, segundo %3ld: nivel %5.0f\n", sec++, sec_n ? sqrt(sec_sum / (double)sec_n) : 0.0);
                    sec_sum = 0; sec_n = 0;
                }
            }
        }
        if (shots && every > 0 && (f + 1) % every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%06ld.png", shots, f + 1);
            if (!save_frame(g, path)) fprintf(stderr, "no se pudo escribir %s\n", path);
        }
        for (int i = 0; i < nw; i++)
            if (g->ram[watch[i]] != wlast[i]) {
                wlast[i] = g->ram[watch[i]];
                printf("cuadro %5ld: [$FF%04X] = $%02X\n", f, watch[i], wlast[i]);
            }
        if (g->cpu.halted) {
            printf(g->tmss_locked ? "el 68000 tocó el VDP sin \"SEGA\" en $A14000 (TMSS) y se colgó en el cuadro %ld\n"
                                  : "el CPU se detuvo (doble falta) en el cuadro %ld\n", f);
            break;
        }
    }

    M68k *c = &g->cpu;
    printf("%s\n", g->cart.hdr.name_int);
    printf("cuadros:       %llu\n", (unsigned long long)g->vdp.frame);
    printf("instrucciones: %llu (%.0f por cuadro)\n", (unsigned long long)steps, (double)steps / (double)(g->vdp.frame ? g->vdp.frame : 1));
    printf("ciclos:        %llu\n", (unsigned long long)g->cyc);
    printf("PC $%06X  SR $%04X%s%s\n", c->pc, m68k_get_sr(c), c->stopped ? "  (STOP)" : "", c->halted ? "  (detenido)" : "");
    if (g->cart.mapper == MAPPER_SSF2) printf("mapper SSF2:   %u cambios de mapeo\n", g->cart.map_ver);
    if (g->cart.has_eeprom) printf("EEPROM:        %u bytes escritos\n", g->eeprom.writes);
    printf("excepciones:\n");
    for (int n = 0; n < 64; n++)
        if (g->exc_count[n]) {
            const char *nm = cart_vector_name(n);
            char buf[16];
            if (!nm) { snprintf(buf, sizeof buf, "vector %d", n); nm = buf; }
            printf("  %-22s %u\n", nm, g->exc_count[n]);
        }
    printf("PC más frecuentes en el último cuadro:\n");
    for (int k = 0; k < 8; k++) {
        u32 best = 0, bi = 0;
        for (u32 i = 0; i < HN; i++) if (hcount[i] > best) { best = hcount[i]; bi = i; }
        if (!best) break;
        printf("  $%06X  %u\n", hpc[bi], best);
        hcount[bi] = 0;
    }
    if (wf) {
        /* Cabecera RIFF/WAVE de 44 bytes (PCM, 2 canales, 16 bits) */
        u8 h[44];
        u32 rate = AUDIO_RATE;
        memcpy(h, "RIFF", 4);
        u32 v = 36 + wav_bytes; memcpy(h + 4, &v, 4);
        memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4);
        u16 w = 1; memcpy(h + 20, &w, 2);
        w = 2; memcpy(h + 22, &w, 2);
        memcpy(h + 24, &rate, 4);
        v = rate * 4; memcpy(h + 28, &v, 4);
        w = 4; memcpy(h + 32, &w, 2);
        w = 16; memcpy(h + 34, &w, 2);
        memcpy(h + 36, "data", 4);
        memcpy(h + 40, &wav_bytes, 4);
        fseek(wf, 0, SEEK_SET);
        fwrite(h, 1, 44, wf);
        fclose(wf);
        printf("sonido en %s (%.1f s)\n", wav, wav_bytes / 4.0 / AUDIO_RATE);
    }
    if (shot) {
        if (save_frame(g, shot)) printf("imagen en %s (%dx%d)\n", shot, g->vdp.width, g->vdp.height);
        else printf("no se pudo escribir %s\n", shot);
    }
    if (sram && gen_sram_dirty(g)) {
        if (gen_sram_save(g, sram)) printf("partida guardada en %s\n", sram);
        else printf("no se pudo guardar la partida en %s\n", sram);
    }
    gen_free(g);
    free(g);
    return 0;
}
