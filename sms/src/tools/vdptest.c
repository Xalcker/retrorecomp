/*
 * Prueba del dibujo del VDP (make test), con la VRAM, la CRAM y los registros armados aquí y vdp_render_line:
 *   - modo 4, fondo: el color de un pixel (4 planos), la paleta de los sprites en el fondo, los volteos, la
 *     prioridad sobre los sprites (salvo en el color 0), el scroll horizontal fino y grueso (y los pixeles de
 *     la izquierda con el color de fondo), el bloqueo de las filas 0-15, el scroll vertical y el bloqueo de las
 *     columnas 24-31, la columna 0 tapada, la pantalla apagada, la tabla de nombres del modo de 224 líneas
 *   - sprites: posición (Y + 1), 8×16, el zoom, el corrimiento de 8 (EC), los patrones de la segunda mitad, el
 *     terminador $D0, el límite de 8 por línea con el desborde, la colisión (en el ciclo de su pixel) y quién
 *     queda encima
 *   - Game Gear: la CRAM de 12 bits y la ventana de 160×144; los colores del SMS en 12 bits
 *   - el modo gráfico II del TMS9918, con su paleta fija
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static Sms g;

static void reset(const char *name)
{
    static u8 rom[0x8000];
    const char *err = NULL;
    sms_free(&g);
    if (sms_load(&g, rom, sizeof rom, name, MODEL_AUTO, &err)) { printf("FALLA: %s\n", err); exit(1); }
    Vdp *v = &g.vdp;
    memset(v->vram, 0, sizeof v->vram);
    memset(v->cram, 0, sizeof v->cram);
    static const u8 regs[11] = { 0x06, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF };
    memcpy(v->reg, regs, sizeof regs);      /* modo 4, pantalla encendida, tabla de nombres en $3800, SAT en $3F00 */
    v->vscroll = 0;
    v->status = 0;
    v->col_at = 0;
}

/* Un patrón de 8×8 en el que todo el pixel x de cada fila vale color[x] */
static void pattern(int n, const u8 color[8])
{
    for (int row = 0; row < 8; row++)
        for (int p = 0; p < 4; p++) {
            u8 b = 0;
            for (int x = 0; x < 8; x++) if (color[x] >> p & 1) b |= (u8)(0x80 >> x);
            g.vdp.vram[n * 32 + row * 4 + p] = b;
        }
}

static void name(int row, int col, u16 w)
{
    u32 a = 0x3800 + (u32)(row * 32 + col) * 2;
    g.vdp.vram[a] = (u8)w;
    g.vdp.vram[a + 1] = (u8)(w >> 8);
}

static void sprite(int i, int y, int x, int n)
{
    g.vdp.vram[0x3F00 + i] = (u8)y;
    g.vdp.vram[0x3F80 + 2 * i] = (u8)x;
    g.vdp.vram[0x3F81 + 2 * i] = (u8)n;
}

static u16 px(int y, int x) { return g.vdp.fb[y * FB_W + x]; }

/* Color de 12 bits de un color del SMS */
static u16 c12(u8 c) { return (u16)(((c >> 4) & 3) * 5 << 8 | ((c >> 2) & 3) * 5 << 4 | (c & 3) * 5); }

static void background(void)
{
    static const u8 ramp[8] = { 0, 1, 2, 3, 4, 5, 6, 15 };
    reset("v.sms");
    for (int i = 0; i < 32; i++) g.vdp.cram[i] = (u8)(i + 1);
    pattern(1, ramp);
    name(0, 0, 0x0001);                         /* fila 0, columna 0: el patrón 1 */
    name(0, 1, 0x0801);                         /* la paleta de los sprites */
    name(0, 2, 0x0201);                         /* volteado en horizontal */
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(1) && px(0, 1) == c12(2) && px(0, 7) == c12(16), "colores del patrón: %03X %03X %03X", px(0, 0), px(0, 1), px(0, 7));
    CHECK(px(0, 8) == c12(17) && px(0, 15) == c12(32), "paleta de los sprites en el fondo");
    CHECK(px(0, 16) == c12(16) && px(0, 23) == c12(1), "volteo horizontal");
    /* Volteo vertical: un patrón con la fila 0 en color 3 y las demás en 0 */
    memset(g.vdp.vram + 2 * 32, 0, 32);
    g.vdp.vram[2 * 32 + 0] = 0xFF; g.vdp.vram[2 * 32 + 1] = 0xFF;
    name(0, 3, 0x0402);
    vdp_render_line(&g, 7);
    CHECK(px(7, 24) == c12(4), "volteo vertical: %03X", px(7, 24));
    /* Scroll horizontal: 3 pixeles a la derecha; los primeros 3 con el color de fondo (registro 7 = 2: CRAM 18) */
    g.vdp.reg[7] = 2;
    g.vdp.reg[8] = 3;
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(19) && px(0, 2) == c12(19) && px(0, 3) == c12(1), "scroll fino: %03X %03X", px(0, 2), px(0, 3));
    g.vdp.reg[8] = 0xF8;                        /* una columna a la izquierda: la columna 1 queda en x = 0 */
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(17), "scroll grueso: %03X", px(0, 0));
    g.vdp.reg[0] |= 0x40;                       /* filas 0-15 sin scroll */
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(1), "bloqueo de las filas 0-15");
    g.vdp.reg[0] &= (u8)~0x40;
    g.vdp.reg[8] = 0;
    /* Scroll vertical: 8 abajo; la fila 0 de la pantalla muestra la fila 1 de la tabla */
    name(1, 0, 0x0801);
    g.vdp.vscroll = 8;
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(17), "scroll vertical");
    name(1, 24, 0x0801); name(0, 24, 0x0001);
    g.vdp.reg[0] |= 0x80;                       /* columnas 24-31 sin scroll vertical */
    vdp_render_line(&g, 0);
    CHECK(px(0, 192) == c12(1) && px(0, 0) == c12(17), "bloqueo de las columnas 24-31");
    g.vdp.reg[0] &= (u8)~0x80;
    g.vdp.vscroll = 0;
    g.vdp.reg[0] |= 0x20;                       /* columna 0 tapada */
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(19) && px(0, 7) == c12(19) && px(0, 8) == c12(17), "columna 0 tapada");
    g.vdp.reg[0] &= (u8)~0x20;
    g.vdp.reg[1] &= (u8)~0x40;                  /* pantalla apagada */
    vdp_render_line(&g, 0);
    CHECK(px(0, 100) == c12(19), "pantalla apagada: %03X", px(0, 100));
    g.vdp.reg[1] |= 0x40;
    /* 224 líneas: la tabla de nombres en $x700 (registro 2 = $FF: $3700) */
    g.vdp.reg[0] |= 0x02; g.vdp.reg[1] |= 0x10;
    g.vdp.vram[0x3700] = 0x01; g.vdp.vram[0x3701] = 0x08;
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(17), "tabla de nombres del modo de 224 líneas");
}

static void sprites(void)
{
    static const u8 solid[8] = { 5, 5, 5, 5, 5, 5, 5, 5 }, half[8] = { 0, 0, 0, 0, 6, 6, 6, 6 };
    reset("s.sms");
    g.vdp.reg[6] = 0xFF;                        /* los sprites usan la segunda mitad de los patrones (registro 6, bit 2) */
    for (int i = 0; i < 32; i++) g.vdp.cram[i] = (u8)(i + 1);
    pattern(0x100 + 1, solid);
    pattern(0x100 + 2, half);
    sprite(0, 9, 20, 1);                        /* Y + 1: aparece en la línea 10 */
    sprite(1, 0xD0, 0, 1);                      /* fin de la lista */
    vdp_render_line(&g, 9);
    CHECK(px(9, 20) == c12(1), "el sprite aparece en Y");
    vdp_render_line(&g, 10);
    CHECK(px(10, 20) == c12(22) && px(10, 27) == c12(22) && px(10, 28) == c12(1) && px(10, 19) == c12(1), "sprite en (20,10)");
    g.vdp.reg[0] |= 0x08;                       /* EC: 8 a la izquierda */
    vdp_render_line(&g, 10);
    CHECK(px(10, 12) == c12(22) && px(10, 20) == c12(1), "corrimiento de 8 (EC)");
    g.vdp.reg[0] &= (u8)~0x08;
    g.vdp.reg[6] = 0xFB;                        /* con el bit 2 en 0, de la primera mitad (el patrón 1 está vacío) */
    vdp_render_line(&g, 10);
    CHECK(px(10, 20) == c12(1), "con el registro 6 en $FB, el sprite no salió de la primera mitad");
    g.vdp.reg[6] = 0xFF;
    g.vdp.reg[1] |= 0x01;                       /* zoom: 16×16 */
    vdp_render_line(&g, 25);
    CHECK(px(25, 35) == c12(22) && px(25, 36) == c12(1), "zoom");
    g.vdp.reg[1] &= (u8)~0x01;
    g.vdp.reg[1] |= 0x02;                       /* 8×16: el patrón 1 queda en 0 y 1 (par e impar) */
    pattern(0x100 + 0, half);
    vdp_render_line(&g, 10);
    vdp_render_line(&g, 18);
    CHECK(px(10, 20) == c12(1) && px(10, 24) == c12(23) && px(18, 20) == c12(22), "8×16: %03X %03X", px(10, 24), px(18, 20));
    g.vdp.reg[1] &= (u8)~0x02;
    /* Prioridad: un mosaico con prioridad tapa al sprite salvo en su color 0 */
    pattern(3, half);                           /* x 16-19 y 24-27: color 0; 20-23 y 28-31: color 6 */
    name(1, 2, 0x1003); name(1, 3, 0x1003);
    vdp_render_line(&g, 10);
    CHECK(px(10, 20) == c12(7) && px(10, 24) == c12(22), "prioridad del fondo: %03X %03X", px(10, 20), px(10, 24));
    /* Quién queda encima: el de menor número */
    sprite(1, 9, 20, 2); sprite(2, 0xD0, 0, 0);    /* el 1 es opaco en 24-27, encima de los pixeles del 0 */
    name(1, 2, 0x0000); name(1, 3, 0x0000);
    g.vdp.status = 0;
    vdp_render_line(&g, 10);
    CHECK(px(10, 24) == c12(22), "el sprite 0 no quedó encima");
    CHECK(g.vdp.col_at != 0, "sin colisión");
    CHECK(g.vdp.col_at == g.vdp.line_start + vdp_pixel_cycle(24) + 9, "la colisión no está en el ciclo de su pixel");
    /* 9 sprites en la línea: el noveno no se dibuja y queda el desborde */
    g.vdp.col_at = 0;
    for (int i = 0; i < 9; i++) sprite(i, 49, 8 + i * 16, 1);
    sprite(9, 0xD0, 0, 0);
    g.vdp.status = 0;
    vdp_render_line(&g, 50);
    CHECK((g.vdp.status & 0x40) && px(50, 8 + 7 * 16) == c12(22) && px(50, 8 + 8 * 16) == c12(1), "límite de 8 sprites y desborde");
    /* Y = $D0 termina la lista con 192 líneas, pero no con 224 */
    sprite(0, 0xD0, 0, 1); sprite(1, 49, 100, 1); sprite(2, 0xD0, 0, 0);
    g.vdp.status = 0;
    vdp_render_line(&g, 50);
    CHECK(px(50, 100) == c12(1), "con 192 líneas, el $D0 no terminó la lista");
    g.vdp.reg[0] |= 0x02; g.vdp.reg[1] |= 0x10;
    vdp_render_line(&g, 50);
    CHECK(px(50, 100) == c12(22), "con 224 líneas, el $D0 terminó la lista");
    g.vdp.reg[0] &= (u8)~0x02; g.vdp.reg[1] &= (u8)~0x10;
}

static void game_gear(void)
{
    reset("v.gg");
    CHECK(g.model == MODEL_GG && !g.sms_mode, "no es un GG en modo GG");
    static const u8 one[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    pattern(1, one);
    name(5, 10, 0x0001);
    g.vdp.cram[2] = 0x5F; g.vdp.cram[3] = 0x0A;     /* entrada 1: $0A5F */
    vdp_render_line(&g, 40);
    CHECK(px(40, 80) == 0x0A5F, "CRAM de 12 bits del GG: %03X", px(40, 80));
    int x0, y0, w, h;
    vdp_view(&g, &x0, &y0, &w, &h);
    CHECK(x0 == 48 && y0 == 24 && w == 160 && h == 144, "la ventana del GG: %d,%d %dx%d", x0, y0, w, h);
    CHECK(vdp_argb(0x0A5F) == 0xFFFF55AA, "ARGB de $0A5F: %08X", vdp_argb(0x0A5F));
    reset("v.sms");
    vdp_view(&g, &x0, &y0, &w, &h);
    CHECK(x0 == 0 && y0 == 0 && w == 256 && h == 192, "la imagen del SMS");
}

static void tms(void)
{
    reset("t.sms");
    Vdp *v = &g.vdp;
    /* Gráficos II: tabla de nombres en $3800, patrones en $0000, colores en $2000 */
    v->reg[0] = 0x02; v->reg[1] = 0xC0; v->reg[2] = 0x0E; v->reg[3] = 0xFF; v->reg[4] = 0x03; v->reg[7] = 0x01;
    v->vram[0x3800] = 0x05;                     /* fila 0, columna 0: patrón 5 */
    v->vram[5 * 8] = 0xF0;                      /* fila 0: 4 pixeles encendidos */
    v->vram[0x2000 + 5 * 8] = 0xF4;             /* encendido: 15 (blanco); apagado: 4 (azul oscuro) */
    v->vram[0x3F80] = 0xD0;                     /* sin sprites (la tabla en $3F80 con el registro 5 en $FF) */
    vdp_render_line(&g, 0);
    CHECK(px(0, 0) == c12(0x3F) && px(0, 4) == c12(0x10), "gráficos II: %03X %03X", px(0, 0), px(0, 4));
}

int main(void)
{
    console_utf8();
    background();
    sprites();
    game_gear();
    tms();
    sms_free(&g);
    if (fallas) { printf("vdptest: %d fallas\n", fallas); return 1; }
    printf("vdptest: fondo, scrolls, sprites, Game Gear y modo gráfico II bien\n");
    return 0;
}
