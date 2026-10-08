/*
 * Super Game Boy: la parte de la SNES.
 *
 * El Game Boy de adentro es un DMG normal; lo que agrega el SGB lo hace la
 * SNES con los comandos que el juego le manda por P1:
 *
 *   - Paquetes: un pulso de reinicio (P14 y P15 en bajo), 128 bits (P14 en
 *     bajo = 0, P15 en bajo = 1, cada uno seguido de ambos en alto), del bit
 *     menos significativo al más, y un bit 0 de fin. Un comando ocupa de 1 a
 *     7 paquetes: el byte 0 trae el comando (bits 3-7) y la cantidad (0-2).
 *   - Colores: cuatro paletas de 4 colores y un mapa de 20×18 que dice qué
 *     paleta usa cada mosaico de 8×8 de la pantalla. El color 0 es común a
 *     todas (el de la paleta 0).
 *   - Transferencias de VRAM (*_TRN): la SNES no lee la memoria del Game Boy,
 *     sino la imagen que sale por la pantalla. El juego dibuja los 4 KB como
 *     256 mosaicos seguidos (20 por fila) con BGP = $E4, manda el comando y
 *     espera unos cuadros. Aquí se reconstruyen los bytes a partir de los
 *     tonos del cuadro completo siguiente.
 *   - Borde: 256 mosaicos de 4 bits por píxel y un mapa de 32×28 con cuatro
 *     paletas de 16 colores. El juego queda en el centro (48,40).
 *
 * No se emulan el sonido de la SNES (SOUND, SOU_TRN), los programas para la
 * SNES (DATA_SND, DATA_TRN, JUMP) ni el borde que trae el SGB de fábrica (está
 * en la ROM de la SNES): sin borde del juego, el marco queda del color 0.
 */
#include <string.h>

#include "gb.h"

enum {
    PAL01 = 0x00, PAL23 = 0x01, PAL03 = 0x02, PAL12 = 0x03,
    ATTR_BLK = 0x04, ATTR_LIN = 0x05, ATTR_DIV = 0x06, ATTR_CHR = 0x07,
    PAL_SET = 0x0A, PAL_TRN = 0x0B, MLT_REQ = 0x11,
    CHR_TRN = 0x13, PCT_TRN = 0x14, ATTR_TRN = 0x15, ATTR_SET = 0x16,
    MASK_EN = 0x17,
};

/* Colores de arranque del SGB (la paleta 1-A, la que usa sin indicaciones) */
static const u16 default_pal[4] = { 0x67BF, 0x265B, 0x10B5, 0x2866 };

void sgb_reset(GB *g)
{
    Sgb *s = &g->sgbs;
    memset(s, 0, sizeof *s);
    s->last_p1 = 0x30;
    s->ready_for_pulse = 1;     /* P1 queda en $30 al arrancar */
    s->players = 1;
    for (int p = 0; p < 4; p++)
        memcpy(s->pal[p], default_pal, sizeof default_pal);
}

/* --- Comandos --- */

static u16 word(const u8 *p)
{
    return (u16)((p[0] | p[1] << 8) & 0x7FFF);
}

/* PALxy: color 0 y los colores 1-3 de dos paletas */
static void cmd_pal(Sgb *s, int a, int b)
{
    const u8 *d = s->packet;
    u16 c0 = word(d + 1);
    if (a == 0) s->pal[0][0] = c0;    /* el color 0 solo cuenta en la paleta 0 */
    for (int i = 1; i < 4; i++) {
        s->pal[a][i] = word(d + 1 + i * 2);
        s->pal[b][i] = word(d + 7 + i * 2);
    }
}

static void apply_atf(Sgb *s, int n)
{
    if (n >= 45) return;
    const u8 *f = &s->atf[n * 90];
    for (int i = 0; i < 20 * 18; i++)
        s->attr[i] = (f[i / 4] >> (6 - (i & 3) * 2)) & 3;
}

static void cmd_attr_blk(Sgb *s)
{
    const u8 *d = s->packet;
    int n = d[1] & 0x1F;
    for (int k = 0; k < n && 2 + k * 6 + 6 <= (int)sizeof s->packet; k++) {
        const u8 *e = d + 2 + k * 6;
        int ctl = e[0] & 7;
        int in = e[1] & 3, edge = (e[1] >> 2) & 3, out = (e[1] >> 4) & 3;
        int x1 = e[2] & 0x1F, y1 = e[3] & 0x1F, x2 = e[4] & 0x1F, y2 = e[5] & 0x1F;
        /* Solo "dentro" o solo "fuera": el borde del bloque toma esa paleta */
        if (ctl == 1) { ctl |= 2; edge = in; }
        if (ctl == 4) { ctl |= 2; edge = out; }
        for (int y = 0; y < 18; y++) {
            for (int x = 0; x < 20; x++) {
                u8 *a = &s->attr[y * 20 + x];
                if (x > x1 && x < x2 && y > y1 && y < y2) {
                    if (ctl & 1) *a = (u8)in;
                } else if (x >= x1 && x <= x2 && y >= y1 && y <= y2) {
                    if (ctl & 2) *a = (u8)edge;
                } else if (ctl & 4) {
                    *a = (u8)out;
                }
            }
        }
    }
}

static void cmd_attr_lin(Sgb *s)
{
    const u8 *d = s->packet;
    int n = d[1];
    for (int k = 0; k < n && 2 + k < (int)sizeof s->packet; k++) {
        u8 e = d[2 + k];
        int line = e & 0x1F, pal = (e >> 5) & 3;
        if (e & 0x80) {     /* línea horizontal (fila) */
            if (line < 18) for (int x = 0; x < 20; x++) s->attr[line * 20 + x] = (u8)pal;
        } else {            /* línea vertical (columna) */
            if (line < 20) for (int y = 0; y < 18; y++) s->attr[y * 20 + line] = (u8)pal;
        }
    }
}

static void cmd_attr_div(Sgb *s)
{
    const u8 *d = s->packet;
    int after = d[1] & 3, before = (d[1] >> 2) & 3, on = (d[1] >> 4) & 3;
    int horizontal = d[1] & 0x40, c = d[2] & 0x1F;
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < 20; x++) {
            int v = horizontal ? y : x;
            s->attr[y * 20 + x] = (u8)(v < c ? before : v == c ? on : after);
        }
    }
}

static void cmd_attr_chr(Sgb *s)
{
    const u8 *d = s->packet;
    int x = d[1] % 20, y = d[2] % 18;
    int n = d[3] | d[4] << 8, vertical = d[5] & 1;
    if (n > 360) n = 360;
    for (int i = 0; i < n && 6 + i / 4 < (int)sizeof s->packet; i++) {
        s->attr[y * 20 + x] = (d[6 + i / 4] >> (6 - (i & 3) * 2)) & 3;
        if (vertical) {
            if (++y == 18) { y = 0; if (++x == 20) x = 0; }
        } else {
            if (++x == 20) { x = 0; if (++y == 18) y = 0; }
        }
    }
}

static void cmd_pal_set(Sgb *s)
{
    const u8 *d = s->packet;
    for (int p = 0; p < 4; p++) {
        int n = (d[1 + p * 2] | d[2 + p * 2] << 8) & 0x1FF;
        memcpy(s->pal[p], &s->sys_pal[n * 4], sizeof s->pal[p]);
    }
    if (d[9] & 0x80) apply_atf(s, d[9] & 0x3F);
    if (d[9] & 0x40) s->mask = 0;
}

static void run_command(GB *g)
{
    Sgb *s = &g->sgbs;
    const u8 *d = s->packet;
    switch (d[0] >> 3) {
    case PAL01: cmd_pal(s, 0, 1); break;
    case PAL23: cmd_pal(s, 2, 3); break;
    case PAL03: cmd_pal(s, 0, 3); break;
    case PAL12: cmd_pal(s, 1, 2); break;
    case ATTR_BLK: cmd_attr_blk(s); break;
    case ATTR_LIN: cmd_attr_lin(s); break;
    case ATTR_DIV: cmd_attr_div(s); break;
    case ATTR_CHR: cmd_attr_chr(s); break;
    case PAL_SET: cmd_pal_set(s); break;
    case ATTR_SET:
        apply_atf(s, d[1] & 0x3F);
        if (d[1] & 0x40) s->mask = 0;
        break;
    case MASK_EN: s->mask = d[1] & 3; break;
    case MLT_REQ:
        /* 1, 2 o 4 jugadores; el jugador actual no se reinicia, solo se
         * recorta. El modo 2 (no documentado) no avanza ni recorta, y se lee
         * raro (ver sgb_joypad_id). */
        s->players = (u8)((d[1] & 3) + 1);
        if (s->players != 3) s->player &= (u8)(s->players - 1);
        break;
    case PAL_TRN: case CHR_TRN: case PCT_TRN: case ATTR_TRN:
        /* Se toma la imagen del cuadro completo que sigue al actual */
        s->trn_cmd = (u8)(d[0] >> 3);
        s->trn_arg = d[1];
        s->trn_wait = 2;
        break;
    default:
        break;   /* sonido, programas para la SNES, etc.: sin efecto */
    }
}

/* Bits 4-5 escritos en P1. Cada pulso debe ir precedido de ambos en alto. */
void sgb_p1_write(GB *g, u8 v)
{
    Sgb *s = &g->sgbs;
    u8 sel = v & 0x30, prev = s->last_p1;
    s->last_p1 = sel;

    /* Cada subida de P15 pasa al siguiente control, también a mitad de un
     * paquete (SameSuite command_mlt_req*), con 2 o 4 jugadores */
    if ((sel & 0x20) && !(prev & 0x20) && !(s->players & 1))
        s->player = (u8)((s->player + 1) & (s->players - 1));

    if (sel == 0x30) {
        s->ready_for_pulse = 1;
        return;
    }
    if (!s->ready_for_pulse) return;
    s->ready_for_pulse = 0;
    if (sel == 0x00) {          /* reinicio: empieza un paquete */
        s->receiving = 1;
        s->bits = 0;
        return;
    }
    if (!s->receiving) return;

    int bit = sel == 0x10;      /* P15 en bajo = 1, P14 en bajo = 0 */
    if (s->bits == 128) {       /* bit de fin: debe ser 0 */
        s->receiving = 0;
        if (bit) { s->packets = 0; return; }
        if (s->packets == 0) {
            s->length = s->packet[0] & 7;
            if (s->length == 0) s->length = 1;
        }
        if (++s->packets >= s->length) {
            run_command(g);
            s->packets = 0;
        }
        return;
    }
    u8 *b = &s->packet[s->packets * 16 + s->bits / 8];
    u8 m = (u8)(1 << (s->bits & 7));
    if (bit) *b |= m; else *b &= (u8)~m;
    s->bits++;
}

int sgb_joypad_id(GB *g)
{
    const Sgb *s = &g->sgbs;
    /* Modo 2: 0 y 3 se leen como el control 1; 1 y 2 como el 3
     * (deducido de SameSuite command_mlt_req) */
    if (s->players == 3) return (s->player + 1) & 2;
    return s->player;
}

/* --- Transferencias de VRAM --- */

/* Reconstruye 4 KB a partir de la pantalla: 256 mosaicos, 20 por fila */
static void screen_bytes(const u16 *shades, u8 *out)
{
    for (int t = 0; t < 256; t++) {
        int tx = (t % 20) * 8, ty = (t / 20) * 8;
        for (int r = 0; r < 8; r++) {
            u8 lo = 0, hi = 0;
            for (int x = 0; x < 8; x++) {
                u16 v = shades[(ty + r) * GB_SCREEN_W + tx + x] & 3;
                lo |= (u8)((v & 1) << (7 - x));
                hi |= (u8)((v >> 1) << (7 - x));
            }
            out[t * 16 + r * 2] = lo;
            out[t * 16 + r * 2 + 1] = hi;
        }
    }
}

static void vram_transfer(Sgb *s, const u16 *shades)
{
    static u8 data[4096];
    screen_bytes(shades, data);
    switch (s->trn_cmd) {
    case PAL_TRN:
        for (int i = 0; i < 512 * 4; i++) s->sys_pal[i] = word(data + i * 2);
        break;
    case CHR_TRN:
        memcpy(s->border_chr + (s->trn_arg & 1) * 4096, data, 4096);
        break;
    case PCT_TRN:
        for (int i = 0; i < 32 * 28; i++) s->border_map[i] = (u16)(data[i * 2] | data[i * 2 + 1] << 8);
        for (int i = 0; i < 64; i++) s->border_pal[i / 16][i % 16] = word(data + 0x800 + i * 2);
        break;
    case ATTR_TRN:
        memcpy(s->atf, data, sizeof s->atf);
        break;
    }
}

/* --- Imagen final --- */

static void draw_border(Sgb *s)
{
    u16 back = s->pal[0][0];
    for (int ty = 0; ty < 28; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            u16 e = s->border_map[ty * 32 + tx];
            const u8 *t = &s->border_chr[(e & 0xFF) * 32];
            const u16 *pal = s->border_pal[(e >> 10) & 3];
            int in_game = tx >= 6 && tx < 26 && ty >= 5 && ty < 23;
            for (int r = 0; r < 8; r++) {
                int row = (e & 0x8000) ? 7 - r : r;
                u8 p0 = t[row * 2], p1 = t[row * 2 + 1], p2 = t[16 + row * 2], p3 = t[16 + row * 2 + 1];
                u16 *dst = &s->screen[(ty * 8 + r) * SGB_SCREEN_W + tx * 8];
                for (int x = 0; x < 8; x++) {
                    int b = (e & 0x4000) ? x : 7 - x;
                    int c = ((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3;
                    /* El color 0 es transparente: deja ver el juego o el fondo */
                    if (c) dst[x] = pal[c];
                    else if (!in_game) dst[x] = back;
                }
            }
        }
    }
}

void sgb_frame(GB *g, const u16 *shades)
{
    Sgb *s = &g->sgbs;
    if (s->trn_wait && !--s->trn_wait) vram_transfer(s, shades);

    /* El juego, coloreado según el mapa de atributos */
    if (s->mask != 1) {
        for (int y = 0; y < GB_SCREEN_H; y++) {
            u16 *dst = &s->screen[(40 + y) * SGB_SCREEN_W + 48];
            const u16 *src = &shades[y * GB_SCREEN_W];
            const u8 *attr = &s->attr[(y / 8) * 20];
            for (int x = 0; x < GB_SCREEN_W; x++) {
                int c = src[x] & 3;
                u16 col = c ? s->pal[attr[x / 8]][c] : s->pal[0][0];
                if (s->mask == 2) col = 0;
                else if (s->mask == 3) col = s->pal[0][0];
                dst[x] = col;
            }
        }
    }
    draw_border(s);
}

const u16 *gb_sgb_screen(const GB *g)
{
    return g->sgb ? g->sgbs.screen : NULL;
}
