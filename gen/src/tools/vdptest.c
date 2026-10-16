/*
 * Prueba sintética del VDP (make test): puertos de control y de datos (VRAM con dirección impar,
 * CRAM, VSRAM, incremento), los tres DMA (desde el 68000 con el 68000 detenido, relleno y copia),
 * estado y contador V, y el dibujo: plano A, ventana, sprites (con el límite por línea, el desborde y
 * la máscara de X = 0), prioridades y sombra/resalte.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static Gen *g;
static void reg(int r, int v) { m68k_wr16(g, 0xC00004, (u16)(0x8000 | r << 8 | v)); }
/* Comando de dirección: código CD5-CD0 y dirección */
static void cmd(int code, u16 addr)
{
    m68k_wr16(g, 0xC00004, (u16)((code & 3) << 14 | (addr & 0x3FFF)));
    m68k_wr16(g, 0xC00004, (u16)((code & 0x3C) << 2 | addr >> 14));
}
static void data(u16 w) { m68k_wr16(g, 0xC00000, w); }
static u16 rdata(void) { return m68k_rd16(g, 0xC00000); }
static u16 c9(u16 cram) { return (u16)((cram >> 1 & 7) | (cram >> 2 & 0x38) | (cram >> 3 & 0x1C0)); }
static u16 px(int x, int y) { return g->vdp.fb[y * FB_W + x]; }

static void setup(void)
{
    static u8 rom[0x20000];
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0x100, "SEGA MEGA DRIVE ", 16);
    rom[2] = 0xFE; rom[3] = 0x00; rom[1] = 0xFF;            /* SSP $00FFFE00 */
    rom[6] = 0x02;                                          /* reset en $200: bra.s * */
    rom[0x200] = 0x60; rom[0x201] = 0xFE;
    g = malloc(sizeof *g);
    const char *err;
    if (gen_load(g, rom, sizeof rom, &err)) { printf("no carga: %s\n", err); exit(1); }
    reg(1, 0x04);                                           /* modo 5 */
    reg(15, 2);
}

static void test_puertos(void)
{
    cmd(0x01, 0x1234); data(0xABCD);
    CHECK(g->vdp.vram[0x1234] == 0xAB && g->vdp.vram[0x1235] == 0xCD, "VRAM: escritura de palabra");
    cmd(0x01, 0x1001); data(0xABCD);
    CHECK(g->vdp.vram[0x1001] == 0xAB && g->vdp.vram[0x1000] == 0xCD, "VRAM con dirección impar: los bytes cambian de lugar");
    cmd(0x01, 0x2000); data(0x1111); data(0x2222);
    CHECK(g->vdp.vram[0x2002] == 0x22, "incremento del registro 15");
    cmd(0x00, 0x1234);
    CHECK(rdata() == 0xABCD, "lectura de VRAM");
    cmd(0x03, 0x0002); data(0x0EEE);
    cmd(0x08, 0x0002);
    CHECK(g->vdp.cram[1] == 0x0EEE && (rdata() & 0x0EEE) == 0x0EEE, "CRAM: escritura y lectura");
    cmd(0x05, 0x0004); data(0x0123);
    CHECK(g->vdp.vsram[2] == 0x0123, "VSRAM");
    reg(11, 0x55); reg(1, 0x00); reg(12, 0x81);
    CHECK(g->vdp.reg[11] == 0x55 && g->vdp.reg[12] == 0, "en modo 4 solo se escriben los registros 0-10");
    reg(1, 0x04); reg(11, 0);
}

static void test_dma(void)
{
    for (int i = 0; i < 8; i++) g->ram[i] = (u8)(0x10 + i);
    reg(1, 0x14);                                           /* modo 5 y DMA */
    reg(19, 4); reg(20, 0);                                 /* 4 palabras */
    reg(21, 0x00); reg(22, 0x80); reg(23, 0x7F);            /* fuente $FF0000 */
    u64 c0 = g->cyc;
    cmd(0x21, 0x4000);
    CHECK(!memcmp(g->vdp.vram + 0x4000, g->ram, 8), "DMA del 68000 a la VRAM");
    CHECK(g->cyc - c0 > 8, "el 68000 queda detenido mientras dura el DMA (%llu ciclos)", (unsigned long long)(g->cyc - c0));
    CHECK(g->vdp.reg[19] == 0 && g->vdp.reg[20] == 0 && g->vdp.reg[21] == 0x04, "registros de DMA al terminar");

    reg(15, 1);                                             /* el relleno se hace con incremento 1 */
    reg(19, 0x10); reg(20, 0); reg(23, 0x80);               /* relleno de 16 bytes */
    cmd(0x21, 0x5000);
    data(0x5566);
    int ok = 1;
    for (int i = 0x5002; i < 0x5010; i++) if (g->vdp.vram[i] != 0x55) ok = 0;     /* las orillas no están confirmadas */
    CHECK(ok, "DMA de relleno con el byte alto");
    CHECK(m68k_rd16(g, 0xC00004) & 0x02, "estado: DMA en curso tras el relleno");

    reg(19, 8); reg(20, 0); reg(21, 0x00); reg(22, 0x40); reg(23, 0xC0);   /* copia de 8 bytes de $4000 */
    cmd(0x30, 0x6000);
    CHECK(!memcmp(g->vdp.vram + 0x6000, g->vdp.vram + 0x4000, 8), "DMA de copia en la VRAM");
    reg(15, 2);
    reg(1, 0x04);
}

static void test_estado(void)
{
    reg(1, 0x44);
    while (g->vdp.line != 230) { g->cyc += 100; vdp_sync(g); }
    CHECK(m68k_rd16(g, 0xC00004) & 0x08, "VBLANK en la línea 230");
    CHECK((m68k_rd16(g, 0xC00008) >> 8) == 230, "contador V en la línea 230");
    while (g->vdp.line != 240) { g->cyc += 100; vdp_sync(g); }
    CHECK((m68k_rd16(g, 0xC00008) >> 8) == 240 - 6, "contador V salta de $EA a $E5");
    while (g->vdp.line != 100) { g->cyc += 100; vdp_sync(g); }
    CHECK(!(m68k_rd16(g, 0xC00004) & 0x08), "sin VBLANK en la línea 100");
}

/* Mosaico 1: lleno del color 1; mosaico 2: lleno del color 14; mosaico 3: del 15 */
static void tiles(void)
{
    cmd(0x01, 0x0020); for (int i = 0; i < 16; i++) data(0x1111);
    cmd(0x01, 0x0040); for (int i = 0; i < 16; i++) data(0xEEEE);
    cmd(0x01, 0x0060); for (int i = 0; i < 16; i++) data(0xFFFF);
    cmd(0x03, 0x0000); data(0x0E00);                        /* fondo: azul */
    cmd(0x03, 0x0002); data(0x000E);                        /* paleta 0 color 1: rojo */
    cmd(0x03, 0x0022); data(0x00E0);                        /* paleta 1 color 1: verde */
}

static void clear_planes(void)
{
    cmd(0x01, 0xC000); for (int i = 0; i < 0x2000; i++) data(0);       /* planos A y B en $C000 y $E000 */
    cmd(0x01, 0xF800); for (int i = 0; i < 0x200; i++) data(0);        /* sprites */
}

static void sprite(int n, int y, int size, int link, u16 attr, int x)
{
    cmd(0x01, (u16)(0xF800 + n * 8));
    data((u16)(y + 128)); data((u16)(size << 8 | link)); data(attr); data((u16)(x + 128));
}

static void test_dibujo(void)
{
    reg(1, 0x44); reg(12, 0x81);                            /* H40 */
    reg(2, 0x30); reg(4, 0x07); reg(3, 0x3C); reg(5, 0x7C); reg(13, 0x3F);
    reg(16, 0x01); reg(7, 0x00); reg(17, 0); reg(18, 0); reg(11, 0);
    tiles();
    clear_planes();
    cmd(0x01, 0xC000); data(0x0001);                        /* plano A, celda (0,0): mosaico 1 */
    sprite(0, 0, 0, 0, 0xA001, 16);                         /* sprite 0: mosaico 1, paleta 1, con prioridad */
    vdp_sync(g);
    vdp_render_line(g, 3);
    CHECK(px(2, 3) == c9(0x000E), "plano A: mosaico rojo en x 0-7");
    CHECK(px(9, 3) == c9(0x0E00), "fondo azul donde no hay nada");
    CHECK(px(18, 3) == c9(0x00E0), "sprite verde en x 16-23");
    vdp_render_line(g, 9);
    CHECK(px(18, 9) == c9(0x0E00), "el sprite de 8x8 no llega a la línea 9");

    /* Ventana: las primeras 2 columnas de celdas (16 pixeles) a la izquierda, sin scroll */
    cmd(0x01, 0xF000); data(0x0001);
    reg(17, 0x01);
    vdp_render_line(g, 3);
    CHECK(px(2, 3) == c9(0x000E) && px(12, 3) == c9(0x0E00), "ventana en x 0-15");
    reg(17, 0);

    /* 21 sprites de 8x8 en la línea: el 21 no se dibuja y se levanta el desborde */
    for (int i = 0; i < 21; i++) sprite(i, 0, 0, i + 1 < 21 ? i + 1 : 0, 0x2001, 40 + i * 8);
    m68k_rd16(g, 0xC00004);
    vdp_render_line(g, 2);
    CHECK(px(40 + 19 * 8 + 1, 2) == c9(0x00E0) && px(40 + 20 * 8 + 1, 2) == c9(0x0E00), "límite de 20 sprites por línea");
    CHECK(m68k_rd16(g, 0xC00004) & 0x40, "desborde de sprites en el estado");

    /* Un sprite en X = 0 después de otro tapa a los que siguen */
    clear_planes();
    sprite(0, 0, 0, 1, 0x2001, 40);
    sprite(1, 0, 0, 2, 0x2001, -128);
    sprite(2, 0, 0, 0, 0x2001, 80);
    vdp_render_line(g, 2);
    CHECK(px(41, 2) == c9(0x00E0) && px(81, 2) == c9(0x0E00), "máscara de sprites con X = 0");

    /* Sombra y resalte */
    clear_planes();
    reg(12, 0x89);
    cmd(0x01, 0xC000); data(0x0001); data(0x8001);          /* celda 0 sin prioridad, celda 1 con */
    cmd(0x03, 0x007C); data(0x0EEE);                        /* paleta 3 color 14 (el operador no se dibuja) */
    sprite(0, 0, 0, 0, 0x6002, 24);                         /* operador de resalte (paleta 3, color 14) */
    vdp_render_line(g, 2);
    CHECK(px(2, 2) == (c9(0x000E) | 1 << 9), "sombra: plano sin prioridad");
    CHECK(px(10, 2) == c9(0x000E), "sin sombra: plano con prioridad");
    CHECK(px(26, 2) == c9(0x0E00), "resalte sobre fondo en sombra: queda normal");
    reg(12, 0x81);
}

int main(void)
{
    console_utf8();
    setup();
    test_puertos();
    test_dma();
    test_estado();
    test_dibujo();
    gen_free(g);
    free(g);
    if (!fallas) printf("vdptest: todo bien\n");
    return fallas != 0;
}
