/*
 * mkcontrato: arma el juego de prueba del contrato común (tests/contract.sh) y lo escribe como .neo:
 *   mkcontrato testroms/neo/contrato.neo
 * Es un programa propio (MIT), hecho aquí y no bajado de ningún lado, para que el contrato se cumpla con
 * una prueba libre. Corre con la BIOS del AES del usuario (y con la propia de la fase 10). Hace lo que
 * pide la BIOS (wiki de NeoGeoDev: cabecera, USER, SYSTEM_RETURN, SYSTEM_IO, el código de seguridad de
 * SNK) y depende de las entradas en las tres salidas que compara el contrato:
 *   - pantalla: un mosaico blanco de la capa fija por cada botón apretado (A B C D y la cruz en la fila
 *     14, Start en la columna 36)
 *   - traza: los saltos que dibujan cada mosaico
 *   - sonido: cuando cambian los botones, manda un comando al Z80, que toca un tono del SSG con periodo
 *     $200 + comando (callado con 0)
 *
 * 68000 (en P1):
 *   $000000  vectores: VBLANK $500, temporizador $600, arranque $680; los errores, los de la BIOS
 *   $000100  cabecera (NGH $0999, sin logo de SNK, entradas en $400, $480, $4C0)
 *   $000200  código de seguridad (lo compara la BIOS)
 *   $000400  USER: con USER_REQUEST 2 salta al juego ($800); si no, vuelve a SYSTEM_RETURN
 *   $000480  PLAYER_START: no deja empezar (borra START_FLAG)
 *   $000500  VBLANK: si la BIOS lo maneja (SYSTEM_MODE bit 7 en 0) salta a SYSTEM_INT1; si no, reconoce,
 *            patea al perro guardián, llama a SYSTEM_IO y marca $100000
 *   $000800  el juego: paleta, borra la capa fija y los sprites, y en cada cuadro dibuja los botones
 * Z80 (M1): habilita la NMI y espera; la NMI lee el comando, pone el tono A del SSG y contesta.
 *
 * Con --ram (mkcontrato --ram salida.neo, la prueba del código en RAM de tests/run_tests.sh), el juego además
 * copia al arrancar una rutina de $780 a $100100 (move.w #5,d0, un bucle con dbf, guarda en $101000, rts),
 * la llama en cada cuadro y le parcha el inmediato (addq.b #1,$100103).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "console.h"

static u8 cpu[0x100000];
static u32 pc;
static u32 label[16];
static int pass;
static int ram_test;        /* --ram */

static void w16(u16 v) { cpu[pc] = (u8)(v >> 8); cpu[pc + 1] = (u8)v; pc += 2; }
static void w32(u32 v) { w16((u16)(v >> 16)); w16((u16)v); }
static void at(u32 a) { pc = a; }
static void mark(int l) { label[l] = pc; }

/* Bcc corto a una etiqueta (en la primera pasada, las de adelante todavía valen 0) */
static void bcc(u16 op, int l)
{
    int d = (int)label[l] - (int)(pc + 2);
    if (pass && (d < -128 || d > 127 || d == 0)) { fprintf(stderr, "mkcontrato: salto fuera de alcance\n"); exit(1); }
    w16((u16)(op | (d & 0xFF)));
}
static void dbf(int n, int l) { w16((u16)(0x51C8 | n)); w16((u16)((int)label[l] - (int)pc)); }

static void move_w_imm_abs(u16 v, u32 a) { w16(0x33FC); w16(v); w32(a); }
static void move_b_d0_watchdog(void) { w16(0x13C0); w32(0x300001); }

/* Código de seguridad: la BIOS lo compara palabra por palabra con su copia (wiki de NeoGeoDev,
 * "Security code", del documento oficial de SNK). La wiki tiene una errata en la palabra 25: dice
 * $0606, pero la BIOS espera $6606 (un bne.s; así está en los juegos y lo delató la comparación) */
static const u16 security[] = {
    0x7600, 0x4A6D, 0x0A14, 0x6600, 0x003C, 0x206D, 0x0A04, 0x3E2D, 0x0A08, 0x13C0, 0x0030, 0x0001,
    0x3210, 0x0C01, 0x00FF, 0x671A, 0x3028, 0x0002, 0xB02D, 0x0ACE, 0x6610, 0x3028, 0x0004, 0xB02D,
    0x0ACF, 0x6606, 0xB22D, 0x0AD0, 0x6708, 0x5088, 0x51CF, 0xFFD4, 0x3607, 0x4E75, 0x206D, 0x0A04,
    0x3E2D, 0x0A08, 0x3210, 0xE049, 0x0C01, 0x00FF, 0x671A, 0x3010, 0xB02D, 0x0ACE, 0x6612, 0x3028,
    0x0002, 0xE048, 0xB02D, 0x0ACF, 0x6606, 0xB22D, 0x0AD0, 0x6708, 0x5888, 0x51CF, 0xFFD8, 0x3607,
    0x4E75,
};

enum { L_FIX, L_SPR, L_LOOP, L_BTN, L_NOSND, L_COPY };

static void program(void)
{
    memset(cpu, 0xFF, sizeof cpu);
    /* Vectores */
    at(0);
    for (int n = 0; n < 64; n++) w32(0x000700);
    at(0); w32(0x0010F300); w32(0x00C00402); w32(0x00C00408); w32(0x00C0040E); w32(0x00C00414);
    at(8 * 4); w32(0x00C0041A); w32(0x00C00420);
    at(24 * 4); w32(0x00C00432); w32(0x000500); w32(0x000600); w32(0x000680);

    /* Cabecera */
    at(0x100);
    memcpy(cpu + 0x100, "NEO-GEO", 7); cpu[0x107] = 0;
    at(0x108); w16(0x0999); w32(0x100000); w32(0x10F000); w16(0);
    cpu[0x114] = 2; cpu[0x115] = 0;                         /* sin logo de SNK */
    at(0x116); w32(0x300); w32(0x300); w32(0x300);           /* DIP por software (el AES no las usa) */
    at(0x122); w16(0x4EF9); w32(0x400);                      /* USER */
    w16(0x4EF9); w32(0x480);                                 /* PLAYER_START */
    w16(0x4EF9); w32(0x4C0);                                 /* DEMO_END */
    w16(0x4EF9); w32(0x4C0);                                 /* COIN_SOUND */
    at(0x182); w32(0x200);
    at(0x200);
    for (size_t i = 0; i < sizeof security / sizeof security[0]; i++) w16(security[i]);
    memcpy(cpu + 0x300, "NEORECOMP PRUEBA", 16);

    /* USER */
    at(0x400);
    move_b_d0_watchdog();
    w16(0x0C39); w16(0x0002); w32(0x10FDAE);                 /* cmpi.b #2,BIOS_USER_REQUEST */
    w16(0x6606);                                             /* bne.s: no es el juego */
    w16(0x4EF9); w32(0x800);                                 /* jmp juego */
    w16(0x4EF9); w32(0xC00444);                              /* jmp SYSTEM_RETURN */
    /* PLAYER_START: no deja empezar */
    at(0x480);
    w16(0x4239); w32(0x10FDB4);                              /* clr.b BIOS_START_FLAG */
    w16(0x4E75);
    at(0x4C0); w16(0x4E75);                                  /* DEMO_END, COIN_SOUND */

    /* VBLANK */
    at(0x500);
    w16(0x0839); w16(7); w32(0x10FD80);                      /* btst #7,BIOS_SYSTEM_MODE */
    w16(0x6606);                                             /* bne.s: el juego lo maneja */
    w16(0x4EF9); w32(0xC00438);                              /* jmp SYSTEM_INT1 */
    w16(0x48E7); w16(0xFFFE);                                /* movem.l d0-d7/a0-a6,-(sp) */
    move_w_imm_abs(4, 0x3C000C);                             /* IRQACK */
    move_b_d0_watchdog();
    w16(0x4EB9); w32(0xC0044A);                              /* jsr SYSTEM_IO */
    w16(0x50F9); w32(0x100000);                              /* st $100000 */
    w16(0x4CDF); w16(0x7FFF);                                /* movem.l (sp)+,d0-d7/a0-a6 */
    w16(0x4E73);
    at(0x600); move_w_imm_abs(2, 0x3C000C); w16(0x4E73);     /* temporizador */
    at(0x680); move_w_imm_abs(1, 0x3C000C); w16(0x4E73);     /* arranque */
    at(0x700); w16(0x60FE);                                  /* excepciones: se queda aquí */
    if (ram_test) {                                          /* la rutina que se copia a $100100 */
        static const u16 r[] = { 0x303C, 0x0005, 0x7200, 0xD240, 0x51C8, 0xFFFC, 0x33C1, 0x0010, 0x1000, 0x4E75 };
        at(0x780);
        for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) w16(r[i]);
    }

    /* El juego */
    at(0x800);
    w16(0x46FC); w16(0x2700);
    w16(0x0039); w16(0x0080); w32(0x10FD80);                 /* ori.b #$80,BIOS_SYSTEM_MODE: el VBLANK es del juego */
    move_w_imm_abs(7, 0x3C000C);
    move_w_imm_abs(0x8000, 0x400000);                        /* color de referencia */
    move_w_imm_abs(0x7FFF, 0x400022);                        /* paleta 1, color 1: blanco */
    move_w_imm_abs(0x8000, 0x401FFE);                        /* fondo negro */
    move_w_imm_abs(0x7000, 0x3C0000);                        /* borra la capa fija */
    move_w_imm_abs(1, 0x3C0004);
    w16(0x323C); w16(0x04FF);                                /* move.w #$4FF,d1 */
    mark(L_FIX); move_w_imm_abs(0, 0x3C0002); dbf(1, L_FIX);
    move_w_imm_abs(0x8200, 0x3C0000);                        /* y el alto de los sprites (SCB3) */
    w16(0x323C); w16(0x01FF);
    mark(L_SPR); move_w_imm_abs(0, 0x3C0002); dbf(1, L_SPR);
    w16(0x4239); w32(0x100000);                              /* clr.b $100000 */
    w16(0x4239); w32(0x100002);                              /* clr.b $100002: los botones de antes */
    w16(0x13FC); w16(0x0000); w32(0x320000);                 /* calla el Z80 */
    if (ram_test) {
        w16(0x41F9); w32(0x780);                             /* lea $780,a0 */
        w16(0x43F9); w32(0x100100);                          /* lea $100100,a1 */
        w16(0x7009);                                         /* moveq #9,d0 */
        mark(L_COPY); w16(0x32D8); dbf(0, L_COPY);           /* move.w (a0)+,(a1)+ */
    }
    w16(0x46FC); w16(0x2000);
    mark(L_LOOP);
    w16(0x4A39); w32(0x100000);                              /* tst.b $100000 */
    bcc(0x6700, L_LOOP);                                     /* beq.s: espera el VBLANK */
    w16(0x4239); w32(0x100000);
    if (ram_test) {
        w16(0x4EB9); w32(0x100100);                          /* jsr $100100 */
        w16(0x5239); w32(0x100103);                          /* addq.b #1,$100103: parcha el inmediato */
    }
    w16(0x1039); w32(0x10FD96);                              /* move.b BIOS_P1CURRENT,d0 */
    w16(0x1A00);                                             /* move.b d0,d5 */
    w16(0x7407);                                             /* moveq #7,d2 */
    w16(0x363C); w16(0x708E);                                /* move.w #$7000+4*32+14,d3 */
    mark(L_BTN);
    w16(0x33C3); w32(0x3C0000);                              /* move.w d3,VRAMADDR */
    w16(0x7800);                                             /* moveq #0,d4 */
    w16(0xE208);                                             /* lsr.b #1,d0 */
    w16(0x6404);                                             /* bcc.s: no apretado */
    w16(0x383C); w16(0x1001);                                /* move.w #$1001,d4: mosaico 1, paleta 1 */
    w16(0x33C4); w32(0x3C0002);                              /* move.w d4,VRAMRW */
    w16(0x0643); w16(0x0080);                                /* addi.w #128,d3: 4 columnas a la derecha */
    dbf(2, L_BTN);
    move_w_imm_abs(0x748E, 0x3C0000);                        /* Start, columna 36 */
    w16(0x7800);
    w16(0x0839); w16(0); w32(0x10FDAC);                      /* btst #0,BIOS_STATCURNT */
    w16(0x6704);                                             /* beq.s */
    w16(0x383C); w16(0x1001);
    w16(0x33C4); w32(0x3C0002);
    w16(0xBA39); w32(0x100002);                              /* cmp.b $100002,d5 */
    bcc(0x6700, L_NOSND);                                    /* beq.s: no cambiaron */
    w16(0x13C5); w32(0x100002);                              /* move.b d5,$100002 */
    w16(0x13C5); w32(0x320000);                              /* move.b d5,REG_SOUND */
    mark(L_NOSND);
    bcc(0x6000, L_LOOP);                                     /* bra.s */
}

/* El programa del Z80 */
static void z80_program(u8 *m)
{
    static const u8 main_[] = {
        0xF3, 0x31, 0xFF, 0xFF, 0xED, 0x56,                     /* di; ld sp,$FFFF; im 1 */
        0x3E, 0x07, 0xD3, 0x04, 0x3E, 0x3E, 0xD3, 0x05,         /* SSG $07 = $3E: solo el tono A */
        0x3E, 0x08, 0xD3, 0x04, 0xAF, 0xD3, 0x05,               /* SSG $08 = 0: callado */
        0xD3, 0x08, 0x18, 0xFE,                                 /* out ($08),a: NMI; jr $ */
    };
    static const u8 nmi[] = {
        0xF5, 0xC5, 0xDB, 0x00, 0x47,                           /* push af; push bc; in a,($00); ld b,a */
        0x3E, 0x00, 0xD3, 0x04, 0x78, 0xD3, 0x05,               /* SSG $00 = comando */
        0x3E, 0x01, 0xD3, 0x04, 0x3E, 0x02, 0xD3, 0x05,         /* SSG $01 = 2: periodo $200 + comando */
        0x3E, 0x08, 0xD3, 0x04, 0x78, 0xB7, 0x28, 0x02, 0x3E, 0x0F, 0xD3, 0x05,   /* SSG $08 = comando ? 15 : 0 */
        0x78, 0xD3, 0x0C,                                       /* contesta el comando */
        0xC1, 0xF1, 0xED, 0x45,                                 /* pop bc; pop af; retn */
    };
    memcpy(m, main_, sizeof main_);
    memcpy(m + 0x66, nmi, sizeof nmi);
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc == 3 && !strcmp(argv[1], "--ram")) { ram_test = 1; argv++; argc--; }
    if (argc != 2) { fprintf(stderr, "uso: mkcontrato [--ram] salida.neo\n"); return 2; }
    for (pass = 0; pass < 2; pass++) program();
    static u8 p1[0x100000], s1[0x20000], m1[0x10000];
    for (size_t i = 0; i < sizeof cpu; i += 2) { p1[i] = cpu[i + 1]; p1[i + 1] = cpu[i]; }
    memset(s1 + 32, 0x11, 32);                                  /* mosaico 1 de la capa fija: todo color 1 */
    z80_program(m1);
    RomPart q[3] = { { "c-p1.p1", p1, sizeof p1, 0 }, { "c-s1.s1", s1, sizeof s1, 0 }, { "c-m1.m1", m1, sizeof m1, 0 } };
    for (int i = 0; i < 3; i++) q[i].crc = crc32_buf(0, q[i].data, q[i].size);
    static Cart c;
    const char *err = NULL;
    if (cart_load_parts(&c, q, 3, ROMFMT_ZIP, &err)) { fprintf(stderr, "mkcontrato: %s\n", err); return 1; }
    snprintf(c.title, sizeof c.title, "NEORecomp prueba del contrato");
    snprintf(c.maker, sizeof c.maker, "NEORecomp");
    c.year = 2026;
    if (!cart_write_neo(&c, argv[1])) { fprintf(stderr, "mkcontrato: no se pudo escribir %s\n", argv[1]); return 1; }
    printf("%s (SHA1 %s)\n", argv[1], c.sha1);
    return 0;
}
