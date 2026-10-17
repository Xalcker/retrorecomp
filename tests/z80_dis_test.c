/*
 * Prueba de shared/z80_dis.c: para cada instrucción posible (los dos primeros bytes, y los DD CB d op y FD CB d op)
 * el largo es el mismo de z80_decode, y una muestra de textos con las formas sin documentar.
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -Ishared -Itests/stub -o z80_dis_test tests/z80_dis_test.c \
 *      shared/z80_dis.c shared/z80.c && ./z80_dis_test
 */
#include <stdio.h>
#include <string.h>

#include "z80_dis.h"
#include "z80.h"
#include "z80_ops.h"

/* El bus de mentira que pide z80.c (z80_step no se usa aquí) */
uint8_t z80_read(struct Z80_HOST *g, uint16_t a) { (void)g; (void)a; return 0; }
void z80_write(struct Z80_HOST *g, uint16_t a, uint8_t v) { (void)g; (void)a; (void)v; }
uint8_t z80_in(struct Z80_HOST *g, uint16_t p) { (void)g; (void)p; return 0xFF; }
void z80_out(struct Z80_HOST *g, uint16_t p, uint8_t v) { (void)g; (void)p; (void)v; }

static int fallas;

static void texto(const uint8_t *b, uint16_t pc, const char *want)
{
    char out[64];
    z80_dis(b, pc, out, sizeof out);
    if (strcmp(out, want)) { printf("FALLA %02X %02X %02X %02X: \"%s\", esperado \"%s\"\n", b[0], b[1], b[2], b[3], out, want); fallas++; }
}

int main(void)
{
    char out[64];
    unsigned n = 0;
    for (unsigned b0 = 0; b0 < 256; b0++)
        for (unsigned b1 = 0; b1 < 256; b1++) {
            uint8_t b[4] = { (uint8_t)b0, (uint8_t)b1, 0x12, 0x34 };
            Z80Insn in;
            z80_decode(b, 0x8000, &in);
            int len = z80_dis(b, 0x8000, out, sizeof out);
            n++;
            if (len != in.len) { if (fallas < 20) printf("FALLA largo %02X %02X: %d, z80_decode %d (%s)\n", b0, b1, len, in.len, out); fallas++; }
            if (!out[0]) { printf("FALLA %02X %02X: sin texto\n", b0, b1); fallas++; }
        }
    for (unsigned px = 0; px < 2; px++)
        for (unsigned op = 0; op < 256; op++) {
            uint8_t b[4] = { px ? 0xFD : 0xDD, 0xCB, 0xFE, (uint8_t)op };
            Z80Insn in;
            z80_decode(b, 0, &in);
            int len = z80_dis(b, 0, out, sizeof out);
            n++;
            if (len != in.len || len != 4) { printf("FALLA largo %02X CB FE %02X: %d, z80_decode %d\n", b[0], op, len, in.len); fallas++; }
        }
    const struct { uint8_t b[4]; uint16_t pc; const char *t; } m[] = {
        { { 0x00, 0, 0, 0 }, 0, "nop" },
        { { 0x18, 0xFE, 0, 0 }, 0x0100, "jr $0100" },
        { { 0x10, 0x05, 0, 0 }, 0x0200, "djnz $0207" },
        { { 0x28, 0x80, 0, 0 }, 0x0100, "jr z,$0082" },
        { { 0x21, 0x34, 0x12, 0 }, 0, "ld hl,$1234" },
        { { 0x32, 0xFC, 0xFF, 0 }, 0, "ld ($FFFC),a" },
        { { 0x2A, 0x00, 0xC0, 0 }, 0, "ld hl,($C000)" },
        { { 0x36, 0x7F, 0, 0 }, 0, "ld (hl),$7F" },
        { { 0x76, 0, 0, 0 }, 0, "halt" },
        { { 0x7E, 0, 0, 0 }, 0, "ld a,(hl)" },
        { { 0xCD, 0x00, 0x80, 0 }, 0, "call $8000" },
        { { 0xC4, 0x00, 0x80, 0 }, 0, "call nz,$8000" },
        { { 0xFF, 0, 0, 0 }, 0, "rst $38" },
        { { 0xD3, 0xBF, 0, 0 }, 0, "out ($BF),a" },
        { { 0xDB, 0x7E, 0, 0 }, 0, "in a,($7E)" },
        { { 0xE9, 0, 0, 0 }, 0, "jp (hl)" },
        { { 0x08, 0, 0, 0 }, 0, "ex af,af'" },
        { { 0xFE, 0x10, 0, 0 }, 0, "cp $10" },
        { { 0x96, 0, 0, 0 }, 0, "sub (hl)" },
        { { 0xCB, 0x7E, 0, 0 }, 0, "bit 7,(hl)" },
        { { 0xCB, 0x30, 0, 0 }, 0, "sll b" },
        { { 0xED, 0xB0, 0, 0 }, 0, "ldir" },
        { { 0xED, 0xB3, 0, 0 }, 0, "otir" },
        { { 0xED, 0x45, 0, 0 }, 0, "retn" },
        { { 0xED, 0x4D, 0, 0 }, 0, "reti" },
        { { 0xED, 0x56, 0, 0 }, 0, "im 1" },
        { { 0xED, 0x71, 0, 0 }, 0, "out (c),0" },
        { { 0xED, 0x70, 0, 0 }, 0, "in (c)" },
        { { 0xED, 0x78, 0, 0 }, 0, "in a,(c)" },
        { { 0xED, 0x43, 0x00, 0xC1 }, 0, "ld ($C100),bc" },
        { { 0xED, 0x00, 0, 0 }, 0, "nop (ed $00)" },
        { { 0xDD, 0x21, 0x00, 0xC0 }, 0, "ld ix,$C000" },
        { { 0xDD, 0x7E, 0x05, 0 }, 0, "ld a,(ix+$05)" },
        { { 0xFD, 0x77, 0xFE, 0 }, 0, "ld (iy-$02),a" },
        { { 0xDD, 0x66, 0x01, 0 }, 0, "ld h,(ix+$01)" },
        { { 0xDD, 0x26, 0x12, 0 }, 0, "ld ixh,$12" },
        { { 0xFD, 0x65, 0, 0 }, 0, "ld iyh,iyl" },
        { { 0xDD, 0x36, 0x02, 0x99 }, 0, "ld (ix+$02),$99" },
        { { 0xDD, 0xE9, 0, 0 }, 0, "jp (ix)" },
        { { 0xFD, 0xE5, 0, 0 }, 0, "push iy" },
        { { 0xDD, 0xEB, 0, 0 }, 0, "ex de,hl" },
        { { 0xDD, 0x09, 0, 0 }, 0, "add ix,bc" },
        { { 0xDD, 0x29, 0, 0 }, 0, "add ix,ix" },
        { { 0xDD, 0x84, 0, 0 }, 0, "add a,ixh" },
        { { 0xDD, 0xCB, 0x03, 0x46 }, 0, "bit 0,(ix+$03)" },
        { { 0xFD, 0xCB, 0xFF, 0xC6 }, 0, "set 0,(iy-$01)" },
        { { 0xDD, 0xCB, 0x00, 0x00 }, 0, "rlc (ix+$00),b" },
        { { 0xDD, 0xFD, 0, 0 }, 0, "nop ($DD)" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) texto(m[i].b, m[i].pc, m[i].t);
    if (fallas) { printf("z80_dis: %d fallas\n", fallas); return 1; }
    printf("z80_dis: %u instrucciones con el largo de z80_decode y %zu textos bien\n", n, sizeof m / sizeof m[0]);
    return 0;
}
