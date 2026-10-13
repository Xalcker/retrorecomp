/*
 * Usa la tarjeta de memoria por la interfaz de la BIOS (CARD, $C00468), desde afuera: corre el juego
 * hasta un cuadro, pone las variables de CARD en la RAM, llama a la rutina con las interrupciones
 * enmascaradas y muestra la respuesta. Sirve para ver qué deja en la tarjeta cada BIOS (la de SNK o la
 * propia) sin leer su código, y para comparar las dos.
 *
 *   neocard juego --card ARCHIVO [--bios archivo] [--frames N] orden ...
 *
 * Órdenes (números en hexadecimal):
 *   formatear                      comando 0
 *   buscar:FCB                     comando 1 (SUB: las partidas que hay, un bit cada una)
 *   cargar:FCB:SUB:SIZE            comando 2 (muestra lo cargado)
 *   guardar:FCB:SUB:SIZE:TITULO    comando 3 (el título en los primeros 20 bytes y un patrón después)
 *   borrar:FCB:SUB                 comando 4
 *   titulo:FCB:SUB                 comando 5 (muestra los 20 bytes leídos)
 *   nombre:TEXTO                   comando 6 (guarda el nombre del usuario, 16 bytes)
 *   leernombre                     comando 7
 *   comando:N                      cualquier otro comando, sin variables
 *   poner:DIR=VALOR                el byte DIR de la tarjeta (para dañarla a propósito)
 *   error:RESP[:BOTON:DESDE:HASTA] CARD_ERROR con BIOS_CARD_ANSWER = RESP, con las interrupciones como estén y
 *                                  hasta 1200 cuadros; el botón (a b c d start) se mantiene esos cuadros
 *                                  contados desde la llamada; con --shots CARPETA, una imagen cada 30
 *   proteger / desproteger / sacar / meter
 * Al final guarda la tarjeta (2 KB) en ARCHIVO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "png.h"

#define BUF 0x10F000                /* datos de START (RAM del juego: el juego no corre durante la llamada) */
#define CARD_ENTRY 0xC00468
#define STEP_MAX 20000000

static u32 ret_pc, ret_sp;
static long call_steps;
static int after_call(Neo *g)
{
    neo_after_step(g);
    return (g->cpu.pc == ret_pc && g->cpu.a[7] == ret_sp) || g->cpu.halted || ++call_steps >= STEP_MAX;
}
static int after_frame(Neo *g)
{
    neo_after_step(g);
    return g->lspc.frame_done || g->cpu.halted;
}
static int after_call_frame(Neo *g)
{
    neo_after_step(g);
    return (g->cpu.pc == ret_pc && g->cpu.a[7] == ret_sp) || g->lspc.frame_done || g->cpu.halted;
}

static int save_frame(const Neo *g, const char *path)
{
    static unsigned char rgb[SCREEN_W * SCREEN_H * 3];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u32 c = g->fb[i];
        rgb[i * 3] = (unsigned char)(c >> 16); rgb[i * 3 + 1] = (unsigned char)(c >> 8); rgb[i * 3 + 2] = (unsigned char)c;
    }
    return png_write_rgb(path, rgb, SCREEN_W, SCREEN_H);
}

static void wr8(Neo *g, u32 a, u8 v) { g->ram[a & 0xFFFF] = v; }
static void wr16(Neo *g, u32 a, u16 v) { wr8(g, a, (u8)(v >> 8)); wr8(g, a + 1, (u8)v); }
static void wr32(Neo *g, u32 a, u32 v) { wr16(g, a, (u16)(v >> 16)); wr16(g, a + 2, (u16)v); }
static u8 rd8(const Neo *g, u32 a) { return g->ram[a & 0xFFFF]; }
static u16 rd16(const Neo *g, u32 a) { return (u16)(rd8(g, a) << 8 | rd8(g, a + 1)); }

static void jump(Neo *g, u32 pc)
{
    g->cpu.pc = pc;
    g->cpu.ir = m68k_peek16(g, pc);
    g->cpu.irc = m68k_peek16(g, pc + 2);
}

/* Llama a CARD con las variables puestas; devuelve la respuesta (o -1 si no volvió) */
static int card_call(Neo *g, u8 cmd, u32 start, u16 size, u16 fcb, u16 sub)
{
    M68k *c = &g->cpu;
    wr8(g, 0x10FDC4, cmd);
    wr8(g, 0x10FDC6, 0xEE);
    wr32(g, 0x10FDC8, start);
    wr16(g, 0x10FDCC, size);
    wr16(g, 0x10FDCE, fcb);
    wr8(g, 0x10FDD0, (u8)sub);
    u16 sr = m68k_get_sr(c);
    u32 pc = c->pc;
    m68k_set_sr(c, (u16)(sr | 0x2700));         /* supervisor, sin interrupciones */
    c->stopped = 0;
    c->a[7] -= 4;
    wr32(g, c->a[7], pc);
    ret_pc = pc; ret_sp = c->a[7] + 4;
    jump(g, CARD_ENTRY);
    u64 cyc0 = g->cyc;
    call_steps = 0;
    m68k_run(g, after_call);
    int ok = c->pc == ret_pc && c->a[7] == ret_sp;
    printf("  (%llu ciclos)", (unsigned long long)(g->cyc - cyc0));
    m68k_set_sr(c, sr);
    jump(g, pc);
    return ok ? rd8(g, 0x10FDC6) : -1;
}

/* CARD_ERROR: es interactiva (puede mostrar un mensaje y esperar un botón), así que corre cuadro por cuadro */
static int card_error(Neo *g, u8 answer, int btn, long from, long to, const char *shots)
{
    M68k *c = &g->cpu;
    wr8(g, 0x10FDC6, answer);
    u32 pc = c->pc;
    c->stopped = 0;
    c->a[7] -= 4;
    wr32(g, c->a[7], pc);
    ret_pc = pc; ret_sp = c->a[7] + 4;
    jump(g, 0xC0046E);
    static u16 vram0[0x10000], pal0[2][0x1000];
    static u8 ram0[0x10000];
    memcpy(ram0, g->ram, sizeof ram0);
    memcpy(vram0, g->lspc.vram, sizeof vram0);
    memcpy(pal0, g->pal, sizeof pal0);
    long f = 0;
    while (f < 1200 && !(c->pc == ret_pc && c->a[7] == ret_sp) && !c->halted) {
        g->pad[0] = (u16)(f >= from && f <= to ? btn : 0);
        g->lspc.frame_done = 0;
        m68k_run(g, after_call_frame);
        if (g->lspc.frame_done) {
            audio_end_frame(g);
            if (shots && ++f % 30 == 0) {
                char path[1024];
                snprintf(path, sizeof path, "%s/error%02X_%04ld.png", shots, answer, f);
                save_frame(g, path);
            } else if (!shots) f++;
        }
    }
    g->pad[0] = 0;
    int ok = c->pc == ret_pc && c->a[7] == ret_sp;
    printf("  (%ld cuadros)", f);
    if (shots) {
        /* La capa fija como quedó (filas con algo distinto del mosaico de la esquina) */
        u16 blank = g->lspc.vram[0x7000];
        printf("\n  capa fija (mosaico de fondo $%04X):\n", blank);
        for (int y = 0; y < 32; y++) {
            int any = 0;
            for (int x = 0; x < 40; x++) any |= g->lspc.vram[0x7000 + x * 32 + y] != blank;
            if (!any) continue;
            printf("   fila %2d:", y);
            for (int x = 0; x < 40; x++) {
                u16 t = g->lspc.vram[0x7000 + x * 32 + y];
                if (t != blank) printf(" %d:%04X", x, t);
            }
            printf("\n");
        }
        /* Qué cambió en la VRAM y las paletas desde que se llamó */
        static const struct { const char *n; u32 a, b; } zones[] = {
            { "SCB1 (mosaicos)", 0, 0x6000 }, { "capa fija", 0x7000, 0x7800 }, { "SCB2 (achique)", 0x8000, 0x8200 },
            { "SCB3 (Y y alto)", 0x8200, 0x8400 }, { "SCB4 (X)", 0x8400, 0x8600 },
        };
        for (int z = 0; z < 5; z++) {
            int n = 0;
            for (u32 k = zones[z].a; k < zones[z].b; k++) n += g->lspc.vram[k] != vram0[k];
            printf("  %s: %d palabras cambiaron\n", zones[z].n, n);
        }
        for (int b = 0; b < 2; b++) {
            int n = 0;
            for (int k = 0; k < 0x1000; k++) n += g->pal[b][k] != pal0[b][k];
            printf("  paleta del banco %d: %d colores cambiaron", b, n);
            for (int k = 0; k < 0x1000; k++)
                if (g->pal[b][k] != pal0[b][k]) printf(" [%d] $%04X->$%04X", k, pal0[b][k], g->pal[b][k]);
            printf("\n");
        }
        {
            int n = 0, z = 0;
            for (int k = 0; k < 381; k++) if (g->lspc.vram[0x8200 + k] != vram0[0x8200 + k]) { n++; z += g->lspc.vram[0x8200 + k] == 0; }
            printf("  SCB3: %d cambiaron, %d quedaron en 0; los demás:", n, z);
            for (int k = 0; k < 381; k++)
                if (g->lspc.vram[0x8200 + k] != vram0[0x8200 + k] && g->lspc.vram[0x8200 + k]) printf(" %d:$%04X->$%04X", k, vram0[0x8200 + k], g->lspc.vram[0x8200 + k]);
            printf("\n");
        }
        {
            /* La RAM: bytes que cambiaron en cada página de 256 */
            printf("  RAM (bytes cambiados por página):");
            for (u32 p = 0; p < 0x100; p++) {
                int n = 0;
                for (u32 k = p << 8; k < (p + 1) << 8; k++) n += g->ram[k] != ram0[k];
                if (n) printf(" $10%02X00:%d", p, n);
            }
            printf("\n");
        }
        if (g->lspc.vram[0x8200] != vram0[0x8200]) printf("  SCB3[0] $%04X -> $%04X\n", vram0[0x8200], g->lspc.vram[0x8200]);
        printf("  paleta 0: ");
        for (int k = 0; k < 16; k++) printf(" %04X", g->pal[(g->latch & LATCH_PALBANK1) ? 1 : 0][k]);
        printf("\n");
    }
    if (!ok) jump(g, pc);
    return ok ? rd8(g, 0x10FDC6) : -1;
}

static void dump(const Neo *g, u32 a, u32 n)
{
    for (u32 i = 0; i < n; i += 16) {
        printf("    %06X:", a + i);
        for (u32 k = i; k < n && k < i + 16; k++) printf(" %02X", rd8(g, a + k));
        printf("  ");
        for (u32 k = i; k < n && k < i + 16; k++) { u8 b = rd8(g, a + k); putchar(b >= 0x20 && b < 0x7F ? b : '.'); }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *bios = NULL, *card = NULL, *shots = NULL;
    long frames = 1300;
    int first = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios = argv[++i];
        else if (!strcmp(argv[i], "--card") && i + 1 < argc) card = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); return 2; }
        else if (!rom) rom = argv[i];
        else { first = i; break; }
    }
    if (!rom || !card) {
        fprintf(stderr, "uso: neocard juego --card ARCHIVO [--bios archivo] [--frames N] orden ...\n");
        return 2;
    }
    Neo *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || neo_load(g, rom, bios, &err)) { fprintf(stderr, "%s: %s\n", rom, err ? err : "sin memoria"); return 1; }
    if (!neo_card_insert(g, card)) { fprintf(stderr, "no se pudo meter la tarjeta\n"); return 1; }
    for (long f = 0; f < frames && !g->cpu.halted; f++) { g->lspc.frame_done = 0; m68k_run(g, after_frame); audio_end_frame(g); }
    printf("BIOS: %s; cuadro %llu\n", g->bios.ver ? g->bios.ver->desc : "?", (unsigned long long)g->lspc.frame);

    Card saved = g->card;
    int fails = 0;
    for (int i = first; first && i < argc; i++) {
        const char *o = argv[i];
        unsigned fcb = 0, sub = 0, size = 0;
        char text[64] = "";
        int r = -2;
        printf("%s:", o);
        if (!strcmp(o, "formatear")) r = card_call(g, 0, 0, 0, 0, 0);
        else if (sscanf(o, "buscar:%x", &fcb) == 1) r = card_call(g, 1, 0, 0, (u16)fcb, 0);
        else if (sscanf(o, "cargar:%x:%x:%x", &fcb, &sub, &size) == 3) {
            for (u32 k = 0; k < size; k++) wr8(g, BUF + k, 0xEE);
            r = card_call(g, 2, BUF, (u16)size, (u16)fcb, (u16)sub);
        } else if (sscanf(o, "guardar:%x:%x:%x:%63[^\n]", &fcb, &sub, &size, text) >= 3) {
            for (u32 k = 0; k < size; k++) wr8(g, BUF + k, (u8)(k < 20 ? (text[k] ? text[k] : ' ') : (k * 7 + sub)));
            r = card_call(g, 3, BUF, (u16)size, (u16)fcb, (u16)sub);
        } else if (sscanf(o, "borrar:%x:%x", &fcb, &sub) == 2) r = card_call(g, 4, 0, 0, (u16)fcb, (u16)sub);
        else if (sscanf(o, "titulo:%x:%x", &fcb, &sub) == 2) {
            for (u32 k = 0; k < 20; k++) wr8(g, BUF + k, 0xEE);
            r = card_call(g, 5, BUF, 0, (u16)fcb, (u16)sub);
        } else if (sscanf(o, "nombre:%63[^\n]", text) == 1) {
            for (u32 k = 0; k < 16; k++) wr8(g, BUF + k, (u8)(k < strlen(text) ? text[k] : ' '));
            r = card_call(g, 6, BUF, 0, 0, 0);
        } else if (!strcmp(o, "leernombre")) {
            for (u32 k = 0; k < 16; k++) wr8(g, BUF + k, 0xEE);
            r = card_call(g, 7, BUF, 0, 0, 0);
        } else if (sscanf(o, "error:%x", &size) == 1) {
            char bn[16] = "";
            long f0 = 0, f1 = -1;
            int btn = 0;
            if (sscanf(o, "error:%*x:%15[a-z]:%ld:%ld", bn, &f0, &f1) == 3) {
                static const char *names[] = { "up", "down", "left", "right", "a", "b", "c", "d", "start", "select" };
                for (int k = 0; k < 10; k++) if (!strcmp(bn, names[k])) btn = 1 << k;
            }
            r = card_error(g, (u8)size, btn, f0, f1, shots);
        } else if (sscanf(o, "poner:%x=%x", &fcb, &size) == 2) {
            if (g->card.data && fcb < g->card.size) { g->card.data[fcb] = (u8)size; g->card.dirty = 1; }
        } else if (sscanf(o, "comando:%x", &size) == 1) r = card_call(g, (u8)size, BUF, 0, 0, 0);
        else if (!strcmp(o, "proteger")) g->card.protect = 1;
        else if (!strcmp(o, "desproteger")) g->card.protect = 0;
        else if (!strcmp(o, "sacar")) { saved = g->card; g->card.data = NULL; }
        else if (!strcmp(o, "meter")) g->card = saved;
        else { printf(" orden desconocida\n"); return 2; }
        if (r == -1) { printf(" no volvió\n"); fails++; continue; }
        if (r == -2) { printf(" hecho\n"); continue; }
        printf(" respuesta $%02X, SUB $%04X\n", r, rd16(g, 0x10FDD0));
        if (r == 0 && (!strncmp(o, "cargar", 6))) dump(g, BUF, size);
        if (r == 0 && (!strncmp(o, "titulo", 6))) dump(g, BUF, 20);
        if (r == 0 && !strcmp(o, "leernombre")) dump(g, BUF, 16);
    }
    if (!g->card.data) g->card = saved;
    if (!neo_card_save(g, card)) { fprintf(stderr, "no se pudo guardar %s\n", card); return 1; }
    return fails ? 1 : 0;
}
