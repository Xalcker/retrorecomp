/*
 * Desensamblador del 68000. Usa m68k_decode (la misma validación de opcodes que el CPU) para saber
 * qué familia es y m68k_ext_words para el largo; aquí solo se arma el texto.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dis68k.h"
#include "m68k_ops.h"

typedef struct {
    const u16 *w;
    int i;                  /* siguiente palabra de extensión */
    u32 pc;
    char *out;
    size_t size, len;
} D;

static void put(D *d, const char *fmt, ...)
{
    if (d->len >= d->size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(d->out + d->len, d->size - d->len, fmt, ap);
    va_end(ap);
    if (n > 0) d->len += (size_t)n < d->size - d->len ? (size_t)n : d->size - d->len - 1;
}

static u16 next(D *d) { return d->w[d->i++]; }

static const char *const cc_name[16] = {
    "t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"
};
static const char sz_ch[3] = { 'b', 'w', 'l' };

/* Número con signo en hexadecimal: $10, -$10 */
static void sdisp(D *d, int32_t v)
{
    if (v < 0) put(d, "-$%X", (unsigned)-v);
    else put(d, "$%X", (unsigned)v);
}

/* Operando: modo, registro y tamaño (0 b, 1 w, 2 l) */
static void ea(D *d, int mode, int reg, int sz)
{
    switch (mode) {
    case 0: put(d, "d%d", reg); return;
    case 1: put(d, "a%d", reg); return;
    case 2: put(d, "(a%d)", reg); return;
    case 3: put(d, "(a%d)+", reg); return;
    case 4: put(d, "-(a%d)", reg); return;
    case 5: sdisp(d, (int16_t)next(d)); put(d, "(a%d)", reg); return;
    case 6: {
        u16 x = next(d);
        sdisp(d, (int8_t)(x & 0xFF));
        put(d, "(a%d,%c%d.%c)", reg, (x & 0x8000) ? 'a' : 'd', x >> 12 & 7, (x & 0x800) ? 'l' : 'w');
        return; }
    default: break;
    }
    switch (reg) {
    case 0: put(d, "($%X).w", (u16)next(d)); return;
    case 1: { u32 hi = next(d); put(d, "($%X).l", hi << 16 | next(d)); return; }
    case 2: { u32 base = d->pc + 2 * (u32)d->i; put(d, "$%X(pc)", (base + (u32)(int16_t)next(d)) & 0xFFFFFF); return; }
    case 3: {
        u32 base = d->pc + 2 * (u32)d->i;
        u16 x = next(d);
        put(d, "$%X(pc,%c%d.%c)", (base + (u32)(int8_t)(x & 0xFF)) & 0xFFFFFF, (x & 0x8000) ? 'a' : 'd', x >> 12 & 7,
            (x & 0x800) ? 'l' : 'w');
        return; }
    case 4:
        if (sz == 2) { u32 hi = next(d); put(d, "#$%X", hi << 16 | next(d)); }
        else if (sz == 1) put(d, "#$%X", next(d));
        else put(d, "#$%X", next(d) & 0xFF);
        return;
    default: put(d, "?"); return;
    }
}

/* Lista de registros de MOVEM: d0-d3/a0/a6 (con -(An) los bits van al revés) */
static void reglist(D *d, u16 mask, int predec)
{
    if (predec) {
        u16 r = 0;
        for (int i = 0; i < 16; i++) if (mask & (1u << i)) r |= (u16)(1u << (15 - i));
        mask = r;
    }
    int first = 1;
    for (int i = 0; i < 16; i++) {
        if (!(mask & (1u << i))) continue;
        int j = i;
        while (j + 1 < 16 && (j + 1) % 8 && (mask & (1u << (j + 1)))) j++;
        put(d, "%s%c%d", first ? "" : "/", i < 8 ? 'd' : 'a', i & 7);
        if (j > i) put(d, "-%c%d", j < 8 ? 'd' : 'a', j & 7);
        first = 0;
        i = j;
    }
    if (first) put(d, "0");
}

static int move_sz(u16 op)
{
    int s = op >> 12 & 3;
    return s == 1 ? 0 : s == 3 ? 1 : 2;
}

int dis68k(const uint16_t *w, uint32_t pc, char *out, size_t size)
{
    D dd = { w, 1, pc, out, size, 0 }, *d = &dd;
    out[0] = 0;
    u16 op = w[0];
    int mode = op >> 3 & 7, reg = op & 7, sz = op >> 6 & 3, rx = op >> 9 & 7;
    switch (m68k_decode(op)) {
    case K_IMM: {
        static const char *const n[8] = { "ori", "andi", "subi", "addi", "?", "eori", "cmpi", "?" };
        put(d, "%s.%c ", n[rx], sz_ch[sz]);
        ea(d, 7, 4, sz);
        put(d, ",");
        ea(d, mode, reg, sz);
        break; }
    case K_ICCR: case K_ISR: {
        const char *n = rx == 0 ? "ori" : rx == 1 ? "andi" : "eori";
        put(d, "%s #$%X,%s", n, sz == 0 ? next(d) & 0xFF : next(d), sz == 0 ? "ccr" : "sr");
        break; }
    case K_BIT: {
        static const char *const n[4] = { "btst", "bchg", "bclr", "bset" };
        put(d, "%s ", n[sz]);
        if (op & 0x100) put(d, "d%d,", rx);
        else put(d, "#%u,", next(d) & 0xFF);
        ea(d, mode, reg, 0);
        break; }
    case K_MOVEP: {
        int om = op >> 6 & 7;
        u16 disp = next(d);
        put(d, "movep.%c ", (om & 1) ? 'l' : 'w');
        if (om & 2) { put(d, "d%d,", rx); sdisp(d, (int16_t)disp); put(d, "(a%d)", reg); }
        else { sdisp(d, (int16_t)disp); put(d, "(a%d),d%d", reg, rx); }
        break; }
    case K_MOVE: case K_MOVEA: {
        int s = move_sz(op);
        put(d, "%s.%c ", m68k_decode(op) == K_MOVEA ? "movea" : "move", sz_ch[s]);
        ea(d, mode, reg, s);
        put(d, ",");
        ea(d, op >> 6 & 7, rx, s);
        break; }
    case K_UNARY: {
        static const char *const n[4] = { "negx", "clr", "neg", "not" };
        put(d, "%s.%c ", n[op >> 9 & 3], sz_ch[sz]);
        ea(d, mode, reg, sz);
        break; }
    case K_MOVEFSR: put(d, "move sr,"); ea(d, mode, reg, 1); break;
    case K_MOVETCCR: put(d, "move "); ea(d, mode, reg, 1); put(d, ",ccr"); break;
    case K_MOVETSR: put(d, "move "); ea(d, mode, reg, 1); put(d, ",sr"); break;
    case K_CHK: put(d, "chk.w "); ea(d, mode, reg, 1); put(d, ",d%d", rx); break;
    case K_LEA: put(d, "lea "); ea(d, mode, reg, 2); put(d, ",a%d", rx); break;
    case K_NBCD: put(d, "nbcd "); ea(d, mode, reg, 0); break;
    case K_SWAP: put(d, "swap d%d", reg); break;
    case K_PEA: put(d, "pea "); ea(d, mode, reg, 2); break;
    case K_EXT: put(d, "ext.%c d%d", (op & 0x40) ? 'l' : 'w', reg); break;
    case K_MOVEM: case K_MOVEMR: {
        u16 mask = next(d);
        put(d, "movem.%c ", (op & 0x40) ? 'l' : 'w');
        if (m68k_decode(op) == K_MOVEM) { reglist(d, mask, mode == 4); put(d, ","); ea(d, mode, reg, 2); }
        else { ea(d, mode, reg, 2); put(d, ","); reglist(d, mask, 0); }
        break; }
    case K_TST: put(d, "tst.%c ", sz_ch[sz]); ea(d, mode, reg, sz); break;
    case K_TAS: put(d, "tas "); ea(d, mode, reg, 0); break;
    case K_TRAP: put(d, "trap #%d", op & 15); break;
    case K_LINK: put(d, "link a%d,#", reg); sdisp(d, (int16_t)next(d)); break;
    case K_UNLK: put(d, "unlk a%d", reg); break;
    case K_MUSP: if (op & 8) put(d, "move usp,a%d", reg); else put(d, "move a%d,usp", reg); break;
    case K_RESET: put(d, "reset"); break;
    case K_NOP: put(d, "nop"); break;
    case K_STOP: put(d, "stop #$%X", next(d)); break;
    case K_RTE: put(d, "rte"); break;
    case K_RTS: put(d, "rts"); break;
    case K_TRAPV: put(d, "trapv"); break;
    case K_RTR: put(d, "rtr"); break;
    case K_JSR: put(d, "jsr "); ea(d, mode, reg, 2); break;
    case K_JMP: put(d, "jmp "); ea(d, mode, reg, 2); break;
    case K_QUICK: {
        int q = rx ? rx : 8;
        put(d, "%s.%c #%d,", (op & 0x100) ? "subq" : "addq", sz_ch[sz], q);
        ea(d, mode, reg, sz);
        break; }
    case K_SCC: put(d, "s%s ", cc_name[op >> 8 & 15]); ea(d, mode, reg, 0); break;
    case K_DBCC: {
        u32 t = (pc + 2 + (u32)(int16_t)next(d)) & 0xFFFFFF;
        put(d, "db%s d%d,$%X", (op >> 8 & 15) == 1 ? "ra" : cc_name[op >> 8 & 15], reg, t);
        break; }
    case K_BCC: {
        int cc = op >> 8 & 15;
        u32 d8 = op & 0xFF, t;
        if (d8) t = (pc + 2 + (u32)(int8_t)d8) & 0xFFFFFF;
        else t = (pc + 2 + (u32)(int16_t)next(d)) & 0xFFFFFF;
        put(d, "%s.%c $%X", cc == 0 ? "bra" : cc == 1 ? "bsr" : cc == 2 ? "bhi" : cc == 3 ? "bls" :
            cc == 4 ? "bcc" : cc == 5 ? "bcs" : cc == 6 ? "bne" : cc == 7 ? "beq" : cc == 8 ? "bvc" :
            cc == 9 ? "bvs" : cc == 10 ? "bpl" : cc == 11 ? "bmi" : cc == 12 ? "bge" : cc == 13 ? "blt" :
            cc == 14 ? "bgt" : "ble", d8 ? 's' : 'w', t);
        break; }
    case K_MOVEQ: put(d, "moveq #"); sdisp(d, (int8_t)(op & 0xFF)); put(d, ",d%d", rx); break;
    case K_ALU: {
        int grp = op >> 12;
        const char *n = grp == 0x8 ? "or" : grp == 0x9 ? "sub" : grp == 0xC ? "and" : grp == 0xD ? "add" :
                        (op & 0x100) ? "eor" : "cmp";
        put(d, "%s.%c ", n, sz_ch[sz]);
        if (op & 0x100) { put(d, "d%d,", rx); ea(d, mode, reg, sz); }
        else { ea(d, mode, reg, sz); put(d, ",d%d", rx); }
        break; }
    case K_ALUA: {
        int grp = op >> 12, s = (op & 0x100) ? 2 : 1;
        put(d, "%s.%c ", grp == 0x9 ? "suba" : grp == 0xB ? "cmpa" : "adda", sz_ch[s]);
        ea(d, mode, reg, s);
        put(d, ",a%d", rx);
        break; }
    case K_MUL: case K_DIV:
        put(d, "%s%c.w ", (op >> 12) == 0xC ? "mul" : "div", (op & 0x100) ? 's' : 'u');
        ea(d, mode, reg, 1);
        put(d, ",d%d", rx);
        break;
    case K_BCD: case K_ADDX: {
        const char *n = m68k_decode(op) == K_BCD ? ((op >> 12) == 0xC ? "abcd" : "sbcd") : ((op >> 12) == 0xD ? "addx" : "subx");
        if (m68k_decode(op) == K_BCD) put(d, "%s ", n); else put(d, "%s.%c ", n, sz_ch[sz]);
        if (op & 8) put(d, "-(a%d),-(a%d)", reg, rx); else put(d, "d%d,d%d", reg, rx);
        break; }
    case K_CMPM: put(d, "cmpm.%c (a%d)+,(a%d)+", sz_ch[sz], reg, rx); break;
    case K_EXG: {
        int om = op >> 3 & 0x1F;
        if (om == 0x08) put(d, "exg d%d,d%d", rx, reg);
        else if (om == 0x09) put(d, "exg a%d,a%d", rx, reg);
        else put(d, "exg d%d,a%d", rx, reg);
        break; }
    case K_SHIFTR: {
        static const char *const n[4] = { "as", "ls", "rox", "ro" };
        put(d, "%s%c.%c ", n[op >> 3 & 3], (op & 0x100) ? 'l' : 'r', sz_ch[sz]);
        if (op & 0x20) put(d, "d%d,d%d", rx, reg); else put(d, "#%d,d%d", rx ? rx : 8, reg);
        break; }
    case K_SHIFTM: {
        static const char *const n[4] = { "as", "ls", "rox", "ro" };
        put(d, "%s%c.w ", n[rx & 3], (op & 0x100) ? 'l' : 'r');
        ea(d, mode, reg, 1);
        break; }
    case K_LINEA: put(d, "dc.w $%04X (línea A)", op); break;
    case K_LINEF: put(d, "dc.w $%04X (línea F)", op); break;
    default:
        if (op == 0x4AFC) put(d, "illegal");
        else put(d, "dc.w $%04X", op);
        break;
    }
    return 1 + m68k_ext_words(op);
}
