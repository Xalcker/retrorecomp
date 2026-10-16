/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos, interrupciones,
 * cambios de mapeo, el estado del VDP y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de
 * espera). Sirve para ver hasta dónde llega un juego mientras falta el resto de la consola, como genrun.
 *
 *   smsrun rom.sms [--frames N] [--model sms|gg] [--watch DIR ...] [--sram ARCHIVO] [--sram-text] [--sdsc]
 *                  [--shot ARCHIVO.png] [--shots CARPETA [--every N]] [--wav ARCHIVO.wav] [boton:desde-hasta ...]
 *
 * Botones: up down left right a b start pause (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $C000-$DFFF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la RAM del cartucho al empezar (si existe) y la guarda al terminar (si el juego la usa).
 * --sram-text imprime al final el texto ASCII de la RAM del cartucho (ZEXALL deja ahí su resultado).
 * --sdsc imprime lo que el juego escribe en la consola de depuración SDSC (puerto $FD, solo emuladores).
 * --shot ARCHIVO.png guarda la imagen del último cuadro; --shots CARPETA --every N, una cada N cuadros (600). Es lo
 * que se ve: 256×192 (o 224) en el Master System y la ventana de 160×144 en el Game Gear en modo GG.
 * --wav ARCHIVO.wav guarda el sonido (estéreo, 44 100 Hz, 16 bits) y dice su nivel por segundo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "romload.h"
#include "console.h"
#include "png.h"
#include "wav.h"
#include <math.h>

/* La imagen del cuadro, lo que se ve, como PNG */
static int save_frame(const Sms *g, const char *path)
{
    int x0, y0, w, h;
    vdp_view(g, &x0, &y0, &w, &h);
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = vdp_argb(g->vdp.fb[(y0 + y) * FB_W + x0 + x]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (unsigned char)(c >> 16); p[1] = (unsigned char)(c >> 8); p[2] = (unsigned char)c;
        }
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    return ok;
}

typedef struct { int btn; long from, to; } Hold;

/* Histograma de PC del último cuadro (direccionamiento abierto) */
#define HN 8192
static u32 hpc[HN], hcount[HN];

static void hist_add(u32 pc)
{
    u32 h = (pc * 2654435761u) & (HN - 1);
    while (hcount[h] && hpc[h] != pc) h = (h + 1) & (HN - 1);
    hpc[h] = pc;
    hcount[h]++;
}

/* La consola SDSC: los caracteres que se pueden imprimir y los saltos de línea */
static void sdsc_print(Sms *g, u8 v)
{
    (void)g;
    if (v == 10 || v == 13 || (v >= 32 && v < 127)) putchar(v == 13 ? '\n' : v);
}

int main(int argc, char **argv)
{
    console_utf8();
    static const char *const names[8] = { "up", "down", "left", "right", "a", "b", "start", "pause" };
    const char *rom = NULL, *sram = NULL, *shot = NULL, *shots = NULL, *wav = NULL;
    long frames = 600, every = 600;
    int model = MODEL_AUTO, sram_text = 0, sdsc = 0;
    unsigned watch[16];
    int nwatch = 0, nh = 0;
    Hold holds[64];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = model_from_name(argv[++i]);
            if (model < MODEL_AUTO) { fprintf(stderr, "modelo desconocido: %s (sms o gg)\n", argv[i]); return 2; }
        } else if (!strcmp(argv[i], "--watch") && i + 1 < argc && nwatch < 16) watch[nwatch++] = (unsigned)strtoul(argv[++i], NULL, 16) & 0x1FFF;
        else if (!strcmp(argv[i], "--sram") && i + 1 < argc) sram = argv[++i];
        else if (!strcmp(argv[i], "--sram-text")) sram_text = 1;
        else if (!strcmp(argv[i], "--sdsc")) sdsc = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (strchr(argv[i], ':') && argv[i][0] != '-' && nh < 64) {
            char name[16];
            long a, b;
            if (sscanf(argv[i], "%15[a-z]:%ld-%ld", name, &a, &b) != 3) { fprintf(stderr, "botón mal escrito: %s\n", argv[i]); return 2; }
            int ok = 0;
            for (int k = 0; k < 8; k++)
                if (!strcmp(name, names[k])) { holds[nh].btn = 1 << k; holds[nh].from = a; holds[nh].to = b; nh++; ok = 1; }
            if (!ok) { fprintf(stderr, "botón desconocido: %s\n", name); return 2; }
        } else if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); return 2; }
        else rom = argv[i];
    }
    if (!rom) {
        fprintf(stderr, "uso: smsrun rom.sms [--frames N] [--model sms|gg] [--watch DIR ...] [--sram ARCHIVO] [--sram-text] [--sdsc]\n"
                        "                    [--shot ARCHIVO.png] [--shots CARPETA [--every N]] [--wav ARCHIVO.wav]\n"
                        "                    [boton:desde-hasta ...]   (botones: up down left right a b start pause)\n");
        return 2;
    }
    Sms *g = load_sms(rom, model);
    if (!g) return 1;
    if (sdsc) g->sdsc_out = sdsc_print;
    if (sram && sms_sram_load(g, sram)) printf("partida cargada de %s\n", sram);
    FILE *wf = NULL;
    uint32_t wav_bytes = 0;
    double sec_sum = 0;
    long sec_n = 0, sec = 0;
    if (wav) {
        wf = fopen(wav, "wb");
        if (!wf) { fprintf(stderr, "no se pudo crear %s\n", wav); return 1; }
        wav_put_header(wf, AUDIO_RATE, 2, 0);
    }
    u8 wlast[16];
    for (int i = 0; i < nwatch; i++) wlast[i] = g->ram[watch[i]];
    for (long f = 0; f < frames; f++) {
        u8 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= holds[i].from && f <= holds[i].to) pad |= (u8)holds[i].btn;
        g->pad[0] = pad;
        if (f == frames - 1) {             /* el último cuadro, paso a paso, para el histograma */
            sms_frame_begin(g);
            while (!g->vdp.frame_done) { hist_add(g->z80.pc); sms_step(g); }
        } else sms_run_frame(g);
        if (wf) {
            static int16_t snd[AUDIO_BINS * 2];
            int n = sms_audio_take(g, snd, AUDIO_BINS);
            fwrite(snd, 4, (size_t)n, wf);
            wav_bytes += (uint32_t)n * 4;
            for (int k = 0; k < n * 2; k++) sec_sum += (double)snd[k] * snd[k];
            sec_n += n * 2;
            if (sec_n >= AUDIO_RATE * 2) {
                printf("audio, segundo %3ld: nivel %5.0f\n", sec++, sqrt(sec_sum / (double)sec_n));
                sec_sum = 0; sec_n = 0;
            }
        } else g->audio.n = 0;
        if (shots && every > 0 && (f + 1) % every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%06ld.png", shots, f + 1);
            if (!save_frame(g, path)) fprintf(stderr, "no se pudo escribir %s\n", path);
        }
        for (int i = 0; i < nwatch; i++)
            if (g->ram[watch[i]] != wlast[i]) {
                wlast[i] = g->ram[watch[i]];
                printf("cuadro %5ld: [$%04X] = $%02X\n", f, 0xC000 | watch[i], wlast[i]);
            }
    }
    if (sdsc) printf("\n");
    const Cart *c = &g->cart;
    const Z80 *z = &g->z80;
    const Vdp *v = &g->vdp;
    printf("%s (%s%s, %s)\n", c->sdsc.found && c->sdsc.name[0] ? c->sdsc.name : rom, model_name(g->model),
           g->model == MODEL_GG && g->sms_mode ? " en modo SMS" : "", mapper_name(c->mapper));
    printf("cuadros:        %llu\n", (unsigned long long)v->frame);
    printf("ciclos:         %llu\n", (unsigned long long)g->cyc);
    printf("PC $%04X  SP $%04X  AF $%02X%02X  IFF1 %d  IM %d%s\n", z->pc, z->sp, z->a, z->f, z->iff1, z->im,
           z->halted ? "  (HALT)" : "");
    printf("interrupciones: %u (y %u NMI)\n", g->irq_count, g->nmi_count);
    printf("mapeo:          %u cambios; ranuras en $%05X $%05X $%05X%s%s\n", g->map.map_ver, g->map.slot[0], g->map.slot[1],
           g->map.slot[2], g->map.ram_slot2 || g->map.ram_a000 ? ", RAM del cartucho en la ranura 2" : "",
           g->map.ram_high ? ", RAM del cartucho en $C000" : "");
    printf("VDP:            pantalla %s, interrupción de cuadro %s, de línea %s, %u líneas;",
           v->reg[1] & 0x40 ? "encendida" : "apagada", v->reg[1] & 0x20 ? "sí" : "no", v->reg[0] & 0x10 ? "sí" : "no", v->height);
    for (int i = 0; i <= 10; i++) printf(" %02X", v->reg[i]);
    printf("\n");
    printf("PC más frecuentes en el último cuadro:\n");
    for (int k = 0; k < 8; k++) {
        u32 best = 0, bi = 0;
        for (u32 h = 0; h < HN; h++) if (hcount[h] > best) { best = hcount[h]; bi = h; }
        if (!best) break;
        printf("  $%04X  %u\n", hpc[bi], best);
        hcount[bi] = 0;
    }
    if (sram_text) {
        printf("texto de la RAM del cartucho:\n");
        int col = 0;
        for (u32 a = 0; a < CART_RAM_SIZE; a++) {
            u8 ch = c->ram[a];
            if (ch == 10 || ch == 13) { if (col) putchar('\n'); col = 0; }
            else if (ch >= 32 && ch < 127) { putchar(ch); col++; }
        }
        if (col) putchar('\n');
    }
    if (wf) {
        wav_put_header(wf, AUDIO_RATE, 2, wav_bytes);
        fclose(wf);
        printf("sonido en %s (%.1f s)\n", wav, wav_bytes / 4.0 / AUDIO_RATE);
    }
    if (shot) {
        int x0, y0, w, h;
        vdp_view(g, &x0, &y0, &w, &h);
        if (save_frame(g, shot)) printf("imagen en %s (%dx%d)\n", shot, w, h);
        else printf("no se pudo escribir %s\n", shot);
    }
    if (sram) {
        if (sms_sram_save(g, sram)) printf("partida guardada en %s\n", sram);
        else if (c->ram_used) printf("no se pudo guardar la partida en %s\n", sram);
    }
    sms_free(g);
    free(g);
    return 0;
}
