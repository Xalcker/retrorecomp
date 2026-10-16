/*
 * mkcontrato: arma el juego de prueba del contrato común (tests/contract.sh) y lo escribe como .sms:
 *   mkcontrato testroms/sms/contrato.sms
 *   mkcontrato --minima salida.sms        la ROM mínima de make webtest y make apktest (fase 6)
 * Es un programa propio (MIT), hecho aquí y no bajado de ningún lado, para que el contrato se cumpla con una
 * prueba libre, sin ensamblador: este archivo pone los bytes del Z80. Depende de las entradas en las tres
 * salidas que compara el contrato:
 *   - pantalla: un mosaico blanco por cada botón apretado (arriba, abajo, izquierda, derecha, 1 y 2, en la
 *     fila 10; Start del SMS es el botón 1)
 *   - traza: cada botón tiene su propio camino (el que dibuja y el que borra)
 *   - sonido: cuando cambian los botones, el tono 0 del PSG con periodo $100 + botones (callado con ninguno)
 * Y prueba de paso lo que necesitará el recompilador (fase 5):
 *   - el mapper Sega: 64 KB en 4 páginas; en cada cuadro la ranura 2 pasa a la página 3, se llama a una rutina
 *     que hay ahí (la página 2 tiene otra, que deja otro valor) y vuelve a la 2
 *   - la RAM del cartucho: al arrancar se mapea en la ranura 2, se escribe y se lee
 *   - código en RAM: una rutina copiada a $C100 que se llama en cada cuadro, con el inmediato parchado
 *   - interrupciones: la de cuadro (marca $C000) y la de línea cada 64 líneas (cuenta en $C010); la Pausa
 *     (NMI) cuenta en $C005
 *   - el VDP en modo 4: registros, VRAM, CRAM y la tabla de sprites
 *
 * Mapa:
 *   $0000  DI, IM 1, SP en $DFF0, salta al arranque
 *   $0038  interrupción: salta a irq
 *   $0066  NMI (la Pausa)
 *   $0100  arranque, bucle principal y rutinas
 *   $7FF0  cabecera TMR SEGA (región 4, 64 KB, con su suma)
 *   $8000  (página 2) rutina que deja $22 en $C003
 *   $C000  (página 3) rutina que deja $5A en $C003
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"

#define ROM_SIZE 0x10000

static unsigned char rom[ROM_SIZE];
static unsigned pc;
static unsigned label[64];
static int pass;

enum {
    L_INIT, L_IRQ, L_IRQ_LINE, L_MAIN, L_WAIT, L_SET_TILE, L_CLEAR, L_TILE, L_REGS, L_RAMSRC, L_SAME, L_MUTE,
    L_OFF0, L_NEXT0 = L_OFF0 + 6, L_END = L_NEXT0 + 6
};

static void b(int v) { rom[pc++] = (unsigned char)v; }
static void w(unsigned v) { b(v & 0xFF); b(v >> 8); }
static void at(unsigned a) { pc = a; }
static void mark(int l) { label[l] = pc; }

/* JR cc a una etiqueta (en la primera pasada, las de adelante todavía valen 0) */
static void jr(int op, int l)
{
    int d = (int)label[l] - (int)(pc + 2);
    if (pass && (d < -128 || d > 127)) { fprintf(stderr, "mkcontrato: salto fuera de alcance\n"); exit(1); }
    b(op); b(d & 0xFF);
}
static void jp(int l) { b(0xC3); w(label[l]); }
static void call(int l) { b(0xCD); w(label[l]); }
static void ld_a(int n) { b(0x3E); b(n); }
static void out_a(int port) { b(0xD3); b(port); }
static void in_a(int port) { b(0xDB); b(port); }
static void st_a(unsigned addr) { b(0x32); w(addr); }         /* LD (nn),A */
static void ld_a_mem(unsigned addr) { b(0x3A); w(addr); }     /* LD A,(nn) */
static void ld_hl(unsigned v) { b(0x21); w(v); }
/* La dirección del VDP para escribir: dos bytes en el puerto de control */
static void vdp_addr(unsigned a) { ld_a(a & 0xFF); out_a(0xBF); ld_a(a >> 8); out_a(0xBF); }

#define JR    0x18
#define JR_NZ 0x20
#define JR_Z  0x28

/* La entrada de la tabla de nombres de la columna x de la fila 10 (la tabla en $3800), para escribir */
static unsigned name_at(int x) { return 0x4000 | (0x3800 + (10 * 32 + x) * 2); }

static void program(void)
{
    memset(rom, 0xFF, sizeof rom);
    at(0x0000);
    b(0xF3);                        /* DI */
    b(0xED); b(0x56);               /* IM 1 */
    b(0x31); w(0xDFF0);             /* LD SP,$DFF0 */
    jp(L_INIT);
    at(0x0038);
    jp(L_IRQ);
    at(0x0066);
    b(0xE5); ld_hl(0xC005); b(0x34); b(0xE1);     /* PUSH HL; LD HL,$C005; INC (HL); POP HL */
    b(0xED); b(0x45);               /* RETN */

    at(0x0100);
    /* irq: el estado del VDP dice cuál fue (bit 7: la de cuadro) */
    mark(L_IRQ);
    b(0xF5);                        /* PUSH AF */
    in_a(0xBF);
    b(0xB7);                        /* OR A */
    b(0xF2); w(label[L_IRQ_LINE]);  /* JP P,irq_line */
    ld_a(1); st_a(0xC000);
    b(0xF1); b(0xFB); b(0xED); b(0x4D);           /* POP AF; EI; RETI */
    mark(L_IRQ_LINE);
    b(0xE5); ld_hl(0xC010); b(0x34); b(0xE1);
    b(0xF1); b(0xFB); b(0xED); b(0x4D);

    /* set_tile: escribe el mosaico A (paleta 0, sin volteo) en la dirección de la VRAM de HL */
    mark(L_SET_TILE);
    b(0xF5); b(0x7D); out_a(0xBF); b(0x7C); out_a(0xBF);   /* PUSH AF; LD A,L; OUT; LD A,H; OUT */
    b(0xF1); out_a(0xBE); b(0xAF); out_a(0xBE); b(0xC9);   /* POP AF; OUT; XOR A; OUT; RET */

    mark(L_INIT);
    /* La RAM del cartucho en la ranura 2: se escribe y se lee ($C004 = $A5 si anda) */
    ld_a(0x08); st_a(0xFFFC);
    ld_a(0xA5); st_a(0x8000);
    ld_a_mem(0x8000); st_a(0xC004);
    b(0xAF); st_a(0xFFFC);          /* XOR A: otra vez la ROM */
    /* Los registros del VDP: modo 4 con la interrupción de línea, pantalla y la de cuadro */
    ld_hl(label[L_REGS]);
    b(0x06); b(22);                 /* LD B,22 */
    b(0x0E); b(0xBF);               /* LD C,$BF */
    b(0xED); b(0xB3);               /* OTIR */
    /* La VRAM en cero */
    vdp_addr(0x4000);
    b(0x01); w(0x4000);             /* LD BC,$4000 */
    mark(L_CLEAR);
    b(0xAF); out_a(0xBE); b(0x0B); b(0x78); b(0xB1);       /* XOR A; OUT; DEC BC; LD A,B; OR C */
    jr(JR_NZ, L_CLEAR);
    /* El mosaico 1: todo del color 1 */
    vdp_addr(0x4020);
    b(0x06); b(8);
    mark(L_TILE);
    ld_a(0xFF); out_a(0xBE); b(0xAF); out_a(0xBE); out_a(0xBE); out_a(0xBE);
    b(0x10); b((label[L_TILE] - (pc + 1)) & 0xFF);         /* DJNZ */
    /* Sin sprites: $D0 en la primera Y de la tabla ($3F00) */
    vdp_addr(0x7F00); ld_a(0xD0); out_a(0xBE);
    /* La paleta: 0 negro, 1 blanco */
    vdp_addr(0xC000); b(0xAF); out_a(0xBE); ld_a(0x3F); out_a(0xBE);
    /* La rutina en RAM */
    ld_hl(label[L_RAMSRC]);
    b(0x11); w(0xC100);             /* LD DE,$C100 */
    b(0x01); w(6);                  /* LD BC,6 */
    b(0xED); b(0xB0);               /* LDIR */
    /* El PSG callado */
    ld_a(0x9F); out_a(0x7F); ld_a(0xBF); out_a(0x7F); ld_a(0xDF); out_a(0x7F); ld_a(0xFF); out_a(0x7F);
    b(0xFB);                        /* EI */

    mark(L_MAIN);
    mark(L_WAIT);
    b(0x76);                        /* HALT */
    ld_a_mem(0xC000); b(0xB7);
    jr(JR_Z, L_WAIT);
    b(0xAF); st_a(0xC000);
    /* Los botones del mando 1 (en 0 lo apretado): B = arriba, abajo, izquierda, derecha, 1, 2 en los bits 0-5 */
    in_a(0xDC); b(0x2F); b(0xE6); b(0x3F); b(0x47);       /* CPL; AND $3F; LD B,A */
    for (int i = 0; i < 6; i++) {
        b(0xCB); b(0x40 | i << 3);  /* BIT i,B */
        jr(JR_Z, L_OFF0 + i);
        ld_hl(name_at(4 + 2 * i)); ld_a(1); call(L_SET_TILE);
        jr(JR, L_NEXT0 + i);
        mark(L_OFF0 + i);
        ld_hl(name_at(4 + 2 * i)); b(0xAF); call(L_SET_TILE);
        mark(L_NEXT0 + i);
    }
    /* El sonido, si cambiaron los botones */
    b(0x78); ld_hl(0xC006); b(0xBE);                        /* LD A,B; LD HL,$C006; CP (HL) */
    jr(JR_Z, L_SAME);
    b(0x77); b(0xB7);                                       /* LD (HL),A; OR A */
    jr(JR_Z, L_MUTE);
    b(0x4F); b(0xE6); b(0x0F); b(0xF6); b(0x80); out_a(0x7F);       /* LD C,A; AND $0F; OR $80: tono 0, bits bajos */
    b(0x79); b(0x0F); b(0x0F); b(0x0F); b(0x0F); b(0xE6); b(0x0F); b(0xF6); b(0x10); out_a(0x7F);  /* $100 + botones */
    ld_a(0x90); out_a(0x7F);                                /* volumen al máximo */
    jr(JR, L_SAME);
    mark(L_MUTE);
    ld_a(0x9F); out_a(0x7F);
    mark(L_SAME);
    /* El código en RAM: se le parcha el inmediato con el cuadro y se llama */
    ld_a_mem(0xC001); b(0x3C); st_a(0xC001); st_a(0xC101); /* INC A */
    b(0xCD); w(0xC100);
    /* El mapper: la página 3 en la ranura 2, su rutina, y otra vez la página 2 */
    ld_a(3); st_a(0xFFFF);
    b(0xCD); w(0x8000);
    ld_a(2); st_a(0xFFFF);
    jp(L_MAIN);

    /* Lo que va a $C100: LD A,n; LD ($C002),A; RET */
    mark(L_RAMSRC);
    b(0x3E); b(0x00); b(0x32); w(0xC002); b(0xC9);
    /* R0 $14 (modo 4, interrupción de línea), R1 $E0 (pantalla, interrupción de cuadro), tablas en $3800 y
     * $3F00, patrones de los sprites en $0000, fondo 0, sin scroll, línea cada 64 */
    mark(L_REGS);
    static const unsigned char regs[11] = { 0x14, 0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0x3F };
    for (int r = 0; r < 11; r++) { b(regs[r]); b(0x80 | r); }
    mark(L_END);
    if (label[L_END] >= 0x7FF0) { fprintf(stderr, "mkcontrato: el programa no cabe\n"); exit(1); }

    /* Las rutinas de las páginas 2 y 3, en la ranura 2 */
    at(0x8000); ld_a(0x22); st_a(0xC003); b(0xC9);
    at(0xC000); ld_a(0x5A); st_a(0xC003); b(0xC9);
}

/* La cabecera TMR SEGA: región 4 (SMS de exportación) y 64 KB, con la suma que calcula la BIOS */
static void header(void)
{
    unsigned char *h = rom + 0x7FF0;
    memcpy(h, "TMR SEGA", 8);
    h[8] = h[9] = 0;
    h[0xC] = 0x99; h[0xD] = 0x09; h[0xE] = 0x00;           /* código de producto 0999, versión 0 */
    h[0xF] = 0x4E;
    unsigned sum = 0;
    for (unsigned a = 0; a < ROM_SIZE; a++) if (a < 0x7FF0 || a >= 0x8000) sum += rom[a];
    h[0xA] = (unsigned char)sum; h[0xB] = (unsigned char)(sum >> 8);
}

/* La mínima: apaga las interrupciones y se queda en un HALT, con la pantalla apagada (32 KB, sin mapper) */
static size_t minima(void)
{
    memset(rom, 0, 0x8000);
    at(0); b(0xF3); b(0x76); b(0x18); b(0xFD);              /* DI; HALT; JR -3 */
    return 0x8000;
}

int main(int argc, char **argv)
{
    console_utf8();
    int min = argc == 3 && !strcmp(argv[1], "--minima");
    if (argc != 2 && !min) { fprintf(stderr, "uso: mkcontrato [--minima] salida.sms\n"); return 2; }
    size_t size = ROM_SIZE;
    if (min) size = minima();
    else {
        for (pass = 0; pass < 2; pass++) program();
        header();
    }
    const char *out = argv[argc - 1];
    FILE *f = fopen(out, "wb");
    if (!f || fwrite(rom, 1, size, f) != size || fclose(f)) { fprintf(stderr, "no se pudo escribir %s\n", out); return 2; }
    printf("%s: %zu bytes\n", out, size);
    return 0;
}
