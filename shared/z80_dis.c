/*
 * Desensamblador del Z80 (sintaxis de Zilog en minúsculas, números en hexadecimal con $). Común
 * (retrorecomp/shared), como m68k_dis para el 68000: lo usan smsrecomp (--dis, --why, --events y --html) y los
 * comentarios del código generado. Decodifica como "Decoding Z80 opcodes" de Cristian Dinu (x, y, z, p, q), con
 * las instrucciones sin documentar (ixh, ixl, sll, los DD CB d op con registro, out (c),0) y el mismo largo que
 * z80_decode: un prefijo DD o FD seguido de DD, FD o ED es un NOP de un byte.
 */
#include <stdio.h>
#include <string.h>

#include "z80_dis.h"

static const char *const cc[8] = { "nz", "z", "nc", "c", "po", "pe", "p", "m" };
static const char *const alu[8] = { "add a,", "adc a,", "sub ", "sbc a,", "and ", "xor ", "or ", "cp " };
static const char *const rot[8] = { "rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl" };
static const char *const im_mode[8] = { "0", "0", "1", "2", "0", "0", "1", "2" };
static const char *const misc[8] = { "rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf" };
static const char *const bli[4][4] = {
    { "ldi", "cpi", "ini", "outi" }, { "ldd", "cpd", "ind", "outd" },
    { "ldir", "cpir", "inir", "otir" }, { "lddr", "cpdr", "indr", "otdr" },
};

/* El contexto de una instrucción: el índice (0, "ix" o "iy"), su desplazamiento y lo que sigue */
typedef struct {
    const uint8_t *b;
    uint16_t pc;
    const char *ix;         /* NULL sin prefijo DD/FD */
    int8_t d;               /* el desplazamiento de (ix+d) */
    int len;                /* bytes consumidos hasta ahora */
    char *out;
    size_t size;
} Dis;

static uint8_t next_byte(Dis *s) { return s->b[s->len++]; }

static uint16_t next_word(Dis *s)
{
    uint16_t lo = next_byte(s);
    return (uint16_t)(lo | next_byte(s) << 8);
}

/* El registro r[i]; mem: con índice, (hl) es (ix+d) y h, l son ixh, ixl solo si la instrucción no usa (ix+d) */
static void reg8(const Dis *s, int i, int mem, char *o, size_t n)
{
    static const char *const r[8] = { "b", "c", "d", "e", "h", "l", "(hl)", "a" };
    if (s->ix && i == 6) snprintf(o, n, "(%s%c$%02X)", s->ix, s->d < 0 ? '-' : '+', s->d < 0 ? -s->d : s->d);
    else if (s->ix && !mem && (i == 4 || i == 5)) snprintf(o, n, "%s%c", s->ix, i == 4 ? 'h' : 'l');
    else snprintf(o, n, "%s", r[i]);
}

static const char *rp(const Dis *s, int p) { static const char *const t[4] = { "bc", "de", "hl", "sp" }; return p == 2 && s->ix ? s->ix : t[p]; }
static const char *rp2(const Dis *s, int p) { static const char *const t[4] = { "bc", "de", "hl", "af" }; return p == 2 && s->ix ? s->ix : t[p]; }
static const char *hl(const Dis *s) { return s->ix ? s->ix : "hl"; }

#define OUT(...) snprintf(s->out, s->size, __VA_ARGS__)

/* El desplazamiento de (ix+d), si la instrucción usa (hl) y hay prefijo; va justo después del opcode */
static void take_d(Dis *s, int uses_mem)
{
    if (s->ix && uses_mem) s->d = (int8_t)next_byte(s);
}

static void dis_cb(Dis *s)
{
    char r[16];
    uint8_t op;
    if (s->ix) { s->d = (int8_t)next_byte(s); op = next_byte(s); }   /* DD CB d op */
    else op = next_byte(s);
    int x = op >> 6, y = op >> 3 & 7, z = op & 7;
    reg8(s, s->ix ? 6 : z, 1, r, sizeof r);
    /* DD CB d op con un registro que no es (hl): hace la operación en (ix+d) y copia el resultado al registro */
    char extra[8] = "";
    if (s->ix && z != 6 && x != 1) {
        static const char *const rr[8] = { "b", "c", "d", "e", "h", "l", "", "a" };
        snprintf(extra, sizeof extra, ",%s", rr[z]);
    }
    switch (x) {
    case 0: OUT("%s %s%s", rot[y], r, extra); break;
    case 1: OUT("bit %d,%s", y, r); break;
    case 2: OUT("res %d,%s%s", y, r, extra); break;
    default: OUT("set %d,%s%s", y, r, extra); break;
    }
}

static void dis_ed(Dis *s)
{
    uint8_t op = next_byte(s);
    int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    char r[16];
    if (x == 1) {
        reg8(s, y, 0, r, sizeof r);
        switch (z) {
        case 0: if (y == 6) OUT("in (c)"); else OUT("in %s,(c)", r); return;
        case 1: if (y == 6) OUT("out (c),0"); else OUT("out (c),%s", r); return;
        case 2: OUT("%s hl,%s", q ? "adc" : "sbc", rp(s, p)); return;
        case 3: { uint16_t nn = next_word(s); if (q) OUT("ld %s,($%04X)", rp(s, p), nn); else OUT("ld ($%04X),%s", nn, rp(s, p)); return; }
        case 4: OUT("neg"); return;
        case 5: OUT(y == 1 ? "reti" : "retn"); return;
        case 6: OUT("im %s", im_mode[y]); return;
        default: {
            static const char *const t[8] = { "ld i,a", "ld r,a", "ld a,i", "ld a,r", "rrd", "rld", "nop", "nop" };
            OUT("%s", t[y]);
            return;
        }
        }
    }
    if (x == 2 && z <= 3 && y >= 4) { OUT("%s", bli[y - 4][z]); return; }
    OUT("nop (ed $%02X)", op);     /* sin definir: un NOP de dos bytes */
}

static void dis_main(Dis *s, uint8_t op)
{
    int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    char r1[16], r2[16];
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (y == 0) OUT("nop");
            else if (y == 1) OUT("ex af,af'");
            else {
                int8_t e = (int8_t)next_byte(s);
                uint16_t t = (uint16_t)(s->pc + s->len + e);
                if (y == 2) OUT("djnz $%04X", t);
                else if (y == 3) OUT("jr $%04X", t);
                else OUT("jr %s,$%04X", cc[y - 4], t);
            }
            return;
        case 1:
            if (q) OUT("add %s,%s", hl(s), rp(s, p));
            else { uint16_t nn = next_word(s); OUT("ld %s,$%04X", rp(s, p), nn); }
            return;
        case 2: {
            static const char *const ind[2] = { "(bc)", "(de)" };
            if (p < 2) { if (q) OUT("ld a,%s", ind[p]); else OUT("ld %s,a", ind[p]); return; }
            uint16_t nn = next_word(s);
            if (p == 2) { if (q) OUT("ld %s,($%04X)", hl(s), nn); else OUT("ld ($%04X),%s", nn, hl(s)); }
            else { if (q) OUT("ld a,($%04X)", nn); else OUT("ld ($%04X),a", nn); }
            return;
        }
        case 3: OUT("%s %s", q ? "dec" : "inc", rp(s, p)); return;
        case 4: case 5:
            take_d(s, y == 6);
            reg8(s, y, 0, r1, sizeof r1);
            OUT("%s %s", z == 4 ? "inc" : "dec", r1);
            return;
        case 6: {
            take_d(s, y == 6);
            reg8(s, y, 0, r1, sizeof r1);
            uint8_t n = next_byte(s);
            OUT("ld %s,$%02X", r1, n);
            return;
        }
        default: OUT("%s", misc[y]); return;
        }
    case 1:
        if (y == 6 && z == 6) { OUT("halt"); return; }
        take_d(s, y == 6 || z == 6);
        reg8(s, y, z == 6, r1, sizeof r1);
        reg8(s, z, y == 6, r2, sizeof r2);
        OUT("ld %s,%s", r1, r2);
        return;
    case 2:
        take_d(s, z == 6);
        reg8(s, z, 0, r1, sizeof r1);
        OUT("%s%s", alu[y], r1);
        return;
    default:
        switch (z) {
        case 0: OUT("ret %s", cc[y]); return;
        case 1:
            if (!q) { OUT("pop %s", rp2(s, p)); return; }
            switch (p) {
            case 0: OUT("ret"); return;
            case 1: OUT("exx"); return;
            case 2: OUT("jp (%s)", hl(s)); return;
            default: OUT("ld sp,%s", hl(s)); return;
            }
        case 2: { uint16_t nn = next_word(s); OUT("jp %s,$%04X", cc[y], nn); return; }
        case 3:
            switch (y) {
            case 0: { uint16_t nn = next_word(s); OUT("jp $%04X", nn); return; }
            case 2: { uint8_t n = next_byte(s); OUT("out ($%02X),a", n); return; }
            case 3: { uint8_t n = next_byte(s); OUT("in a,($%02X)", n); return; }
            case 4: OUT("ex (sp),%s", hl(s)); return;
            case 5: OUT("ex de,hl"); return;
            case 6: OUT("di"); return;
            default: OUT("ei"); return;
            }
        case 4: { uint16_t nn = next_word(s); OUT("call %s,$%04X", cc[y], nn); return; }
        case 5:
            if (!q) { OUT("push %s", rp2(s, p)); return; }
            { uint16_t nn = next_word(s); OUT("call $%04X", nn); return; }     /* p == 0; los prefijos no llegan aquí */
        case 6: { uint8_t n = next_byte(s); OUT("%s$%02X", alu[y], n); return; }
        default: OUT("rst $%02X", y * 8); return;
        }
    }
}

int z80_dis(const uint8_t b[4], uint16_t pc, char *out, size_t size)
{
    Dis s = { b, pc, NULL, 0, 0, out, size };
    uint8_t op = next_byte(&s);
    if (op == 0xDD || op == 0xFD) {
        if (b[1] == 0xDD || b[1] == 0xFD || b[1] == 0xED) {
            snprintf(out, size, "nop ($%02X)", op);     /* el prefijo solo, seguido de otro: un NOP */
            return 1;
        }
        s.ix = op == 0xDD ? "ix" : "iy";
        op = next_byte(&s);
    }
    if (op == 0xCB) dis_cb(&s);
    else if (op == 0xED) dis_ed(&s);
    else dis_main(&s, op);
    return s.len;
}
