/*
 * inputtest: controles (estándar y Four Score) y Zapper, sin ROM: se prueba el bus directamente.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

static NES n;
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FALLA: %s (línea %d)\n", msg, __LINE__); fails++; } } while (0)

static void make(void)
{
    memset(&n, 0, sizeof n);
    n.cart.mapper = 0; n.cart.prg_size = 0x4000; n.cart.prg = calloc(1, 0x4000);
    n.cart.chr_size = 0x2000; n.cart.chr = calloc(1, 0x2000);
    mapper_reset(&n);
}

static void strobe(void) { nes_write(&n, 0x4016, 1); nes_write(&n, 0x4016, 0); }
static int rd(int port) { return nes_read(&n, 0x4016 + port) & 1; }

int main(void)
{
    /* Control estándar: los 8 botones, de A a Derecha, y después unos */
    make();
    n.pad[0] = 0xA5; n.pad[1] = 0x3C;
    strobe();
    int v0 = 0, v1 = 0;
    for (int i = 0; i < 8; i++) { v0 |= rd(0) << i; v1 |= rd(1) << i; }
    CHECK(v0 == 0xA5 && v1 == 0x3C, "control estándar: 8 botones");
    CHECK(rd(0) == 1 && rd(1) == 1, "control estándar: después de 8 lecturas, unos");
    nes_write(&n, 0x4016, 1);                                    /* con strobe alto siempre se lee el botón A */
    n.pad[0] = 1; CHECK(rd(0) == 1 && rd(0) == 1, "strobe alto");

    /* Four Score: P1, P3, firma 0,0,0,1,0,0,0,0 (puerto 1) y 0,0,1,0,0,0,0,0 (puerto 2); luego unos */
    make();
    n.fourscore = 1;
    n.pad[0] = 0x11; n.pad[1] = 0x22; n.pad[2] = 0x33; n.pad[3] = 0x44;
    strobe();
    int a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < 8; i++) { a[k] |= rd(0) << i; b[k] |= rd(1) << i; }
    CHECK(a[0] == 0x11 && a[1] == 0x33, "Four Score puerto 1: jugadores 1 y 3");
    CHECK(b[0] == 0x22 && b[1] == 0x44, "Four Score puerto 2: jugadores 2 y 4");
    CHECK(a[2] == 0x08 && b[2] == 0x04, "Four Score: firmas");
    CHECK(rd(0) == 1 && rd(1) == 1, "Four Score: después de 24 lecturas, unos");

    /* Zapper: luz si el haz acaba de pasar por un pixel claro donde apunta */
    make();
    n.zapper_on = 1;
    n.zap_x = 100; n.zap_y = 50;
    n.ppu.mask = 0x18;
    for (int i = 0; i < 256 * 240; i++) n.ppu.fb[i] = 0x0F;      /* negro */
    n.ppu.scanline = 60; n.ppu.dot = 10;
    CHECK((nes_read(&n, 0x4017) & 8) != 0, "Zapper: sin luz sobre negro");
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) n.ppu.fb[(50 + dy) * 256 + 100 + dx] = 0x30;   /* blanco */
    CHECK((nes_read(&n, 0x4017) & 8) == 0, "Zapper: luz sobre blanco");
    n.ppu.scanline = 49;
    CHECK((nes_read(&n, 0x4017) & 8) != 0, "Zapper: el haz todavía no llega a la mira");
    n.ppu.scanline = 50 + 30;
    CHECK((nes_read(&n, 0x4017) & 8) != 0, "Zapper: la luz dura poco");
    n.ppu.scanline = 55;
    n.zap_trigger = 0; CHECK((nes_read(&n, 0x4017) & 0x10) == 0, "Zapper: gatillo suelto");
    n.zap_trigger = 1; CHECK((nes_read(&n, 0x4017) & 0x10) != 0, "Zapper: gatillo apretado");
    n.zap_x = -1; CHECK((nes_read(&n, 0x4017) & 8) != 0, "Zapper: apuntando fuera de la pantalla");

    if (!fails) puts("inputtest: bien");
    return fails != 0;
}
