/*
 * Corre un juego sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos,
 * excepciones por vector, el Z80, las variables de la BIOS y dónde se le fue el tiempo al CPU en el
 * último cuadro (los bucles de espera). Sirve para ver hasta dónde llega un juego mientras falta el
 * resto de la consola.
 *
 *   neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR ...] [--fix]
 *                [boton:desde-hasta ...]
 *
 * Botones: up down left right a b c d start select (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $100000-$10FFFF) imprime el byte cada vez que cambia.
 * --card ARCHIVO mete una tarjeta de memoria (la carga si existe) y la guarda al terminar.
 * --shot ARCHIVO.png guarda la imagen del último cuadro; --shots CARPETA --every N, una cada N cuadros.
 * --fix muestra la capa fija al final como texto: muchos juegos ponen su fuente en el código ASCII
 *       dentro de un bloque de 256 mosaicos (Metal Slug, en $300 + ASCII), así que se mira el byte bajo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "console.h"
#include "png.h"

/* La imagen del cuadro (320 × 224) como PNG */
static int save_frame(const Neo *g, const char *path)
{
    unsigned char *rgb = malloc(SCREEN_W * SCREEN_H * 3);
    if (!rgb) return 0;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u32 c = g->fb[i];
        rgb[i * 3] = (unsigned char)(c >> 16); rgb[i * 3 + 1] = (unsigned char)(c >> 8); rgb[i * 3 + 2] = (unsigned char)c;
    }
    int ok = png_write_rgb(path, rgb, SCREEN_W, SCREEN_H);
    free(rgb);
    return ok;
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

static u64 steps;
static int last_frame;
static int after(Neo *g)
{
    neo_after_step(g);
    steps++;
    if (last_frame) hist_add(g->cpu.pc);
    return g->lspc.frame_done || g->cpu.halted;
}

/* La capa fija: 40 columnas × 32 filas en $7000 de la VRAM (por columnas); las filas 2-29 se ven */
static void print_fix(const Neo *g)
{
    printf("capa fija (filas 2-29; el byte bajo de cada mosaico como ASCII, '.' si no lo es):\n");
    for (int y = 2; y < 30; y++) {
        char line[41];
        for (int x = 0; x < 40; x++) {
            u8 t = (u8)g->lspc.vram[0x7000 + x * 32 + y];
            line[x] = t >= 0x20 && t < 0x7F ? (char)t : '.';
        }
        line[40] = 0;
        printf("  |%s|\n", line);
    }
}

static u8 ram8(const Neo *g, u32 a) { return g->ram[a & 0xFFFF]; }

int main(int argc, char **argv)
{
    console_utf8();
    static const char *names[] = { "up", "down", "left", "right", "a", "b", "c", "d", "start", "select" };
    const char *rom = NULL, *bios = NULL, *card = NULL;
    long frames = 600;
    Hold h[32]; int nh = 0;
    u32 watch[16]; int nw = 0; int wlast[16];
    int fix = 0;
    const char *shot = NULL, *shots = NULL;
    long every = 60;
    for (int i = 1; i < argc; i++) {
        char name[16]; long f0, f1;
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios = argv[++i];
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--fix")) fix = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc && nw < 16) {
            const char *a = argv[++i];
            if (*a == '$') a++;
            watch[nw] = (u32)strtoul(a, NULL, 16) & 0xFFFF; wlast[nw++] = -1;
        }
        else if (sscanf(argv[i], "%15[a-z]:%ld-%ld", name, &f0, &f1) == 3) {
            int ok = 0;
            for (int k = 0; k < 10 && nh < 32; k++) if (!strcmp(name, names[k])) { h[nh++] = (Hold){ 1 << k, f0, f1 }; ok = 1; }
            if (!ok) { fprintf(stderr, "botón desconocido: %s\n", name); return 2; }
        } else if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); return 2; }
        else rom = argv[i];
    }
    if (!rom || frames <= 0) {
        fprintf(stderr, "uso: neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR ...] [--fix]\n"
                        "              [--shot ARCHIVO.png] [--shots CARPETA [--every N]]\n"
                        "              [boton:desde-hasta ...]\n"
                        "  botones: up down left right a b c d start select\n");
        return 2;
    }
    Neo *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || neo_load(g, rom, bios, &err)) { fprintf(stderr, "%s: %s\n", rom, err ? err : "sin memoria"); return 1; }
    int fatal;
    const char *u = cart_unsupported(&g->cart, &fatal);
    if (u) { fprintf(stderr, "%s%s\n", fatal ? "no se puede correr: " : "aviso: ", u); if (fatal) return 1; }
    if (card) { neo_card_insert(g, card); printf("tarjeta de memoria: %s\n", card); }

    for (long f = 0; f < frames; f++) {
        u16 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].from && f <= h[i].to) pad |= (u16)h[i].btn;
        g->pad[0] = pad;
        last_frame = f == frames - 1;
        g->lspc.frame_done = 0;
        m68k_run(g, after);
        if (shots && every > 0 && (f + 1) % every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%06ld.png", shots, f + 1);
            if (!save_frame(g, path)) fprintf(stderr, "no se pudo escribir %s\n", path);
        }
        for (int i = 0; i < nw; i++)
            if (g->ram[watch[i]] != wlast[i]) {
                wlast[i] = g->ram[watch[i]];
                printf("cuadro %5ld: [$10%04X] = $%02X\n", f, watch[i], wlast[i]);
            }
        if (g->cpu.halted) { printf("el 68000 se detuvo (doble falta) en el cuadro %ld\n", f); break; }
    }

    M68k *c = &g->cpu;
    printf("%s\n", g->cart.title[0] ? g->cart.title : rom);
    printf("BIOS:          %s\n", g->bios.ver ? g->bios.ver->desc : "?");
    printf("cuadros:       %llu\n", (unsigned long long)g->lspc.frame);
    printf("instrucciones: %llu (%.0f por cuadro)\n", (unsigned long long)steps, (double)steps / (double)(g->lspc.frame ? g->lspc.frame : 1));
    printf("ciclos:        %llu\n", (unsigned long long)g->cyc);
    printf("PC $%06X  SR $%04X%s%s\n", c->pc, m68k_get_sr(c), c->stopped ? "  (STOP)" : "", c->halted ? "  (detenido)" : "");
    printf("vectores:      %s; paleta %d\n", (g->latch & LATCH_SWPROM) ? "del juego" : "de la BIOS", (g->latch & LATCH_PALBANK1) ? 1 : 0);
    if (g->cart.p_banks > 1) printf("bancos P2:     %u cambios, banco %u\n", g->map_ver - 1, g->p2_bank);
    printf("BIOS (RAM):    SYSTEM_MODE $%02X, USER_REQUEST %u, USER_MODE %u, MVS_FLAG %u, COUNTRY_CODE %u\n",
           ram8(g, 0xFD80), ram8(g, 0xFDAE), ram8(g, 0xFDAF), ram8(g, 0xFD82), ram8(g, 0xFD83));
    printf("Z80:           PC $%04X, %u comandos, último $%02X, respuesta $%02X, NMI %s, YM2610: temporizadores %s%s, banderas %u\n",
           g->z80.pc, g->sound_cmds, g->sound_cmd, g->sound_reply, g->nmi_enabled ? "habilitada" : "deshabilitada",
           g->ym.ta_at != NEVER ? "A" : "", g->ym.tb_at != NEVER ? "B" : (g->ym.ta_at != NEVER ? "" : "parados"), g->ym.flags);
    printf("excepciones:\n");
    for (int n = 0; n < 64; n++)
        if (g->exc_count[n]) {
            const char *nm = cart_vector_name(n);
            char buf[16];
            if (!nm) { snprintf(buf, sizeof buf, "vector %d", n); nm = buf; }
            printf("  %-24s %u\n", nm, g->exc_count[n]);
        }
    printf("PC más frecuentes en el último cuadro:\n");
    for (int k = 0; k < 8; k++) {
        u32 best = 0, bi = 0;
        for (u32 i = 0; i < HN; i++) if (hcount[i] > best) { best = hcount[i]; bi = i; }
        if (!best) break;
        printf("  $%06X  %u%s\n", hpc[bi], best, hpc[bi] >= 0xC00000 && hpc[bi] < 0xD00000 ? "  (BIOS)" : "");
        hcount[bi] = 0;
    }
    if (fix) print_fix(g);
    {
        /* Colores distintos en la imagen del último cuadro (para ver de un vistazo si dibuja algo) */
        static u32 seen[SCREEN_W * SCREEN_H];
        int n = 0;
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            int k = 0;
            while (k < n && seen[k] != g->fb[i]) k++;
            if (k == n && n < 4096) seen[n++] = g->fb[i];
        }
        printf("imagen:        %d colores distintos\n", n);
    }
    if (shot) {
        if (save_frame(g, shot)) printf("imagen en %s (%dx%d)\n", shot, SCREEN_W, SCREEN_H);
        else printf("no se pudo escribir %s\n", shot);
    }
    if (card && g->card.dirty) {
        if (neo_card_save(g, card)) printf("tarjeta guardada en %s\n", card);
        else printf("no se pudo guardar la tarjeta en %s\n", card);
    }
    neo_free(g);
    free(g);
    return 0;
}
