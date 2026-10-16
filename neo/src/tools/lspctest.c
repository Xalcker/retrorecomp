/*
 * Prueba del dibujo del LSPC (make test), con mosaicos sintéticos en un juego hecho aquí y la VRAM y
 * la paleta puestas a mano: capa fija y transparencia, sprite entero con volteos, reducción
 * horizontal y sprites pegajosos, reducción vertical con la tabla L0, autoanimación, prioridad (el
 * sprite de número mayor encima, la capa fija encima de todo), el límite de 96 sprites por línea, los
 * colores y el bit de oscurecido, y que cada línea se dibuja con la VRAM que había al empezar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

#define C_SIZE 0x40000          /* C1 + C2: 256 KB intercalados = 2048 mosaicos */
#define S_SIZE 0x20000

static u8 cdata[C_SIZE], sdata[S_SIZE];

/* Un pixel de un mosaico de sprite, en el formato de la imagen normalizada (ver lspc_render.c) */
static void spr_px(u32 tile, int x, int y, int c)
{
    int block = (x < 8 ? 2 : 0) + (y >= 8);
    int k = block * 16 + (y & 7) * 2, b = x & 7;
    u8 *t = cdata + tile * 128;
    u8 *pl[4] = { &t[2 * k], &t[2 * k + 2], &t[2 * k + 1], &t[2 * k + 3] };    /* planos 0 1 2 3 */
    for (int p = 0; p < 4; p++) {
        *pl[p] = (u8)(*pl[p] & ~(1 << b));
        if (c >> p & 1) *pl[p] = (u8)(*pl[p] | 1 << b);
    }
}

/* Un pixel de un mosaico de la capa fija */
static void fix_px(u32 tile, int x, int y, int c)
{
    u8 *b = &sdata[tile * 32 + (x < 4 ? 16 : 0) + ((x & 2) ? 8 : 0) + y];
    *b = (u8)((x & 1) ? (*b & 0x0F) | c << 4 : (*b & 0xF0) | c);
}

static void build(Neo *g)
{
    memset(g, 0, sizeof *g);
    /* Mosaico 1: cada pixel vale (x + y) % 15 + 1. Mosaico 2: fila y en color y % 15 + 1 (para ver qué
     * fila se dibuja). Mosaicos 8-15: todo del color (número - 7) (autoanimación). Mosaico 3: solo
     * el pixel (0,0) en color 5 */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            spr_px(1, x, y, (x + y) % 15 + 1);
            spr_px(2, x, y, y % 15 + 1);
            for (u32 t = 8; t < 16; t++) spr_px(t, x, y, (int)(t - 7));
        }
    spr_px(3, 0, 0, 5);
    /* Capa fija: mosaico 1 con el pixel x en color x + 1 (fila 0) y el resto transparente */
    for (int x = 0; x < 8; x++) fix_px(1, x, 0, x + 1);

    static u8 p1[0x100000], cpu[0x100000];
    memset(cpu, 0, sizeof cpu);
    memcpy(cpu + 0x100, "NEO-GEO", 7);
    for (size_t i = 0; i < sizeof cpu; i += 2) { p1[i] = cpu[i + 1]; p1[i + 1] = cpu[i]; }
    static u8 c1[C_SIZE / 2], c2[C_SIZE / 2], m1[0x10000];
    for (int i = 0; i < C_SIZE / 2; i++) { c1[i] = cdata[2 * i]; c2[i] = cdata[2 * i + 1]; }
    RomPart q[5] = {
        { "t-p1.p1", p1, sizeof p1, 0, "" }, { "t-s1.s1", sdata, S_SIZE, 0, "" }, { "t-m1.m1", m1, sizeof m1, 0, "" },
        { "t-c1.c1", c1, sizeof c1, 0, "" }, { "t-c2.c2", c2, sizeof c2, 0, "" },
    };
    for (int i = 0; i < 5; i++) q[i].crc = crc32_buf(0, q[i].data, q[i].size);
    const char *err = NULL;
    if (cart_load_parts(&g->cart, q, 5, ROMFMT_ZIP, &err)) { printf("FALLA: juego de prueba: %s\n", err); exit(1); }
    l0_generate(g->l0);
    neo_power_on(g);
    /* Paleta 1: color c = c; paleta 2: color c = $100 + c; fondo ($0FFF) = $7777 */
    for (int c = 0; c < 16; c++) { g->pal[0][16 + c] = (u16)c; g->pal[0][32 + c] = (u16)(0x100 + c); }
    g->pal[0][0xFFF] = 0x7777;
    pal_refresh(g);
    /* Capa fija toda en el mosaico 0 (transparente) */
    for (int i = 0; i < 0x500; i++) g->lspc.vram[0x7000 + i] = 0;
}

/* Sprite s: mosaico tile en todas sus filas, paleta pal y atributos attr; Y, alto, X y reducciones */
static void sprite(Neo *g, int s, u32 tile, int pal, int attr, int sy, int size, int x, int hz, int vz, int sticky)
{
    for (int i = 0; i < 32; i++) {
        g->lspc.vram[s * 64 + i * 2] = (u16)tile;
        g->lspc.vram[s * 64 + i * 2 + 1] = (u16)(pal << 8 | ((tile >> 16) & 0xF) << 4 | attr);
    }
    g->lspc.vram[0x8000 + s] = (u16)(hz << 8 | vz);
    g->lspc.vram[0x8200 + s] = (u16)(((496 - sy) & 0x1FF) << 7 | (sticky ? 0x40 : 0) | size);
    g->lspc.vram[0x8400 + s] = (u16)((x & 0x1FF) << 7);
}

static u32 px(const Neo *g, int x, int y) { return g->fb[y * SCREEN_W + x]; }
static u32 col(const Neo *g, int pal, int c) { return g->pal_argb[0][pal * 16 + c]; }

int main(void)
{
    console_utf8();
    static Neo n;
    Neo *g = &n;
    build(g);
    u32 back = neo_color(0x7777);

    /* Colores: el bit de oscurecido es un bit más de cada componente, al revés */
    CHECK(neo_color(0x8000) == 0xFF000000u && neo_color(0x0000) == 0xFF040404u, "negro: $%08X / $%08X", neo_color(0x8000), neo_color(0x0000));
    CHECK(neo_color(0x7FFF) == 0xFFFFFFFFu && neo_color(0xFFFF) == 0xFFFBFBFBu, "blanco: $%08X / $%08X", neo_color(0x7FFF), neo_color(0xFFFF));
    CHECK(neo_color(0x4F00) == 0xFFFF0404u, "rojo: $%08X", neo_color(0x4F00));

    /* Nada: el fondo */
    lspc_render_line(g, 0);
    CHECK(px(g, 0, 0) == back && px(g, 319, 0) == back, "fondo");

    /* Capa fija: mosaico 1 en la columna 3, fila 2 (la primera visible) con la paleta 1 */
    g->lspc.vram[0x7000 + 3 * 32 + 2] = 0x1001;
    lspc_render_line(g, 0);
    CHECK(px(g, 24, 0) == col(g, 1, 1) && px(g, 31, 0) == col(g, 1, 8) && px(g, 23, 0) == back, "capa fija");
    lspc_render_line(g, 1);
    CHECK(px(g, 24, 1) == back, "capa fija: la fila 1 del mosaico es transparente");
    g->lspc.vram[0x7000 + 3 * 32 + 2] = 0;

    /* Sprite 1 entero en (16, 10), un mosaico de alto, mosaico 1 */
    sprite(g, 1, 1, 1, 0, 10, 1, 16, 15, 0xFF, 0);
    lspc_render_line(g, 10);
    CHECK(px(g, 16, 10) == col(g, 1, 1) && px(g, 31, 10) == col(g, 1, 16 % 15 + 0) && px(g, 15, 10) == back && px(g, 32, 10) == back,
          "sprite entero, fila 0");
    lspc_render_line(g, 25);
    CHECK(px(g, 16, 25) == col(g, 1, 15 % 15 + 1) && px(g, 31, 25) == col(g, 1, 30 % 15 + 1), "sprite entero, fila 15");
    lspc_render_line(g, 26);
    CHECK(px(g, 16, 26) == back, "el sprite mide 16 líneas");
    lspc_render_line(g, 9);
    CHECK(px(g, 16, 9) == back, "el sprite empieza en la línea 10");
    /* Volteos */
    sprite(g, 1, 1, 1, 1, 10, 1, 16, 15, 0xFF, 0);
    lspc_render_line(g, 10);
    /* (los extremos de la fila 0 valen lo mismo: el pixel 1 del mosaico es el color 2, el 14 el 15) */
    CHECK(px(g, 16, 10) == col(g, 1, 15 % 15 + 1) && px(g, 17, 10) == col(g, 1, 15) && px(g, 30, 10) == col(g, 1, 2),
          "volteo horizontal");
    /* Volteado y reducido a 8: quedan los pixeles pares (0, 2 ... 14), de derecha a izquierda */
    sprite(g, 1, 1, 1, 1, 10, 1, 16, 7, 0xFF, 0);
    lspc_render_line(g, 10);
    CHECK(px(g, 16, 10) == col(g, 1, 15) && px(g, 17, 10) == col(g, 1, 13) && px(g, 23, 10) == col(g, 1, 1) &&
          px(g, 24, 10) == back, "volteo con reducción horizontal");
    sprite(g, 1, 2, 1, 2, 10, 1, 16, 15, 0xFF, 0);
    lspc_render_line(g, 10);
    CHECK(px(g, 20, 10) == col(g, 1, 15 % 15 + 1), "volteo vertical: la fila 15 arriba");
    /* Reducción horizontal: 8 pixeles (los pares), y el pegajoso a la derecha */
    sprite(g, 1, 1, 1, 0, 10, 1, 16, 7, 0xFF, 0);
    sprite(g, 2, 1, 2, 0, 0, 0, 0, 15, 0xFF, 1);
    lspc_render_line(g, 10);
    CHECK(px(g, 16, 10) == col(g, 1, 1) && px(g, 17, 10) == col(g, 1, 3) && px(g, 23, 10) == col(g, 1, 15), "reducción horizontal a 8");
    CHECK(px(g, 24, 10) == col(g, 2, 1) && px(g, 39, 10) == col(g, 2, 16 % 15 + 0), "el pegajoso va a la derecha");
    lspc_render_line(g, 26);
    CHECK(px(g, 24, 26) == back, "el pegajoso toma el alto del anterior");
    /* Prioridad: el sprite 2 encima del 1 */
    sprite(g, 1, 1, 1, 0, 10, 1, 16, 15, 0xFF, 0);
    sprite(g, 2, 3, 2, 0, 10, 1, 16, 15, 0xFF, 0);
    lspc_render_line(g, 10);
    CHECK(px(g, 16, 10) == col(g, 2, 5) && px(g, 17, 10) == col(g, 1, 2), "el sprite mayor encima; su color 0 deja ver el de abajo");
    /* La capa fija encima de los sprites */
    g->lspc.vram[0x7000 + 2 * 32 + 3] = 0x1001;
    lspc_render_line(g, 8);
    CHECK(px(g, 16, 8) == col(g, 1, 1), "capa fija encima");
    g->lspc.vram[0x7000 + 2 * 32 + 3] = 0;
    memset(&g->lspc.vram[0x8200], 0, 0x200 * 2);

    /* Reducción vertical: un sprite de 2 mosaicos con vz = $7F (128 líneas para los 16 de arriba): la
     * línea k del sprite es la entrada k de la tabla L0 de $7F */
    sprite(g, 1, 2, 1, 0, 40, 2, 100, 15, 0x7F, 0);
    int bien = 1;
    for (int k = 0; k < 32; k++) {
        lspc_render_line(g, 40 + k);
        u8 e = g->l0[0x7F * 256 + k];
        int row = e & 15;
        if (px(g, 100, 40 + k) != col(g, 1, row % 15 + 1)) { bien = 0; printf("  línea %d: fila esperada %d\n", k, row); break; }
    }
    CHECK(bien, "reducción vertical con la tabla L0");
    memset(&g->lspc.vram[0x8200], 0, 0x200 * 2);

    /* Autoanimación de 8 cuadros: el mosaico 8 + contador */
    sprite(g, 1, 8, 1, 8, 50, 1, 50, 15, 0xFF, 0);
    g->lspc.anim = 5;
    lspc_render_line(g, 50);
    CHECK(px(g, 50, 50) == col(g, 1, 13 - 7), "autoanimación de 8 cuadros (mosaico 13)");
    sprite(g, 1, 8, 1, 4, 50, 1, 50, 15, 0xFF, 0);
    lspc_render_line(g, 50);
    CHECK(px(g, 50, 50) == col(g, 1, 9 - 7), "autoanimación de 4 cuadros (mosaico 9)");
    g->lspc.mode = 8;
    lspc_render_line(g, 50);
    CHECK(px(g, 50, 50) == col(g, 1, 8 - 7), "autoanimación apagada");
    g->lspc.mode = 0;
    memset(&g->lspc.vram[0x8200], 0, 0x200 * 2);

    /* Límite: 100 sprites en la línea 60, cada uno de 1 pixel en una X distinta; se ven los 96 primeros */
    for (int s = 1; s <= 100; s++) sprite(g, s, 1, 1, 0, 60, 1, s * 3, 0, 0xFF, 0);
    lspc_render_line(g, 60);
    CHECK(px(g, 96 * 3 + 0, 60) != back, "el sprite 96 se ve");
    CHECK(px(g, 97 * 3, 60) == back && px(g, 100 * 3, 60) == back, "el 97 y los siguientes no");
    memset(&g->lspc.vram[0x8200], 0, 0x200 * 2);

    /* Cada línea se dibuja al empezar, con la VRAM de ese momento */
    sprite(g, 1, 1, 1, 0, 80, 4, 200, 15, 0xFF, 0);
    {
        Lspc *l = &g->lspc;
        u64 frame0 = l->line_start - (u64)l->line * M68K_PER_LINE + (u64)M68K_PER_FRAME;
        g->cyc = frame0 + (u64)(FIRST_VISIBLE_LINE + 85) * M68K_PER_LINE + 10;      /* dentro de la línea 85 de la pantalla */
        lspc_sync(g);
        g->lspc.vram[0x8400 + 1] = (u16)(250 << 7);                                /* lo mueve a X = 250 */
        g->cyc += 2 * M68K_PER_LINE;
        lspc_sync(g);
        CHECK(px(g, 200, 85) == col(g, 1, 6) && px(g, 250, 87) == col(g, 1, 8) && px(g, 200, 87) == back,
              "la línea 85 con la X de antes, la 87 con la nueva");
    }

    neo_free(g);
    if (fallas) { printf("lspctest: %d fallas\n", fallas); return 1; }
    printf("lspctest: todo bien\n");
    return 0;
}
