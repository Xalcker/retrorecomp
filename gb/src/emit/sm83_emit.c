#include "sm83_emit.h"

#include <stdarg.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

/* ------------------------------------------------------------------ */
/* Decodificación                                                      */
/* ------------------------------------------------------------------ */

static int is_illegal(u8 op)
{
    switch (op) {
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB:
    case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
        return 1;
    default:
        return 0;
    }
}

static int insn_len(u8 op)
{
    switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x08:
    case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA:
    case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC:
    case 0xEA: case 0xFA:
        return 3;
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
    case 0xE0: case 0xF0: case 0xE8: case 0xF8: case 0xCB: case 0x10:
        return 2;
    default:
        return 1;
    }
}

void sm83_decode(const u8 *b, u16 addr, Insn *in)
{
    u8 op = b[0];
    memset(in, 0, sizeof *in);
    in->len = (u8)insn_len(op);
    in->kind = INS_NORMAL;

    if (is_illegal(op)) { in->kind = INS_ILLEGAL; return; }

    switch (op) {
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
        in->kind = INS_JR;
        in->cond = op == 0x18 ? 0 : (u8)(((op >> 3) & 3) + 1);
        in->target = (u16)(addr + 2 + (int8_t)b[1]);
        return;
    case 0xC3: case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        in->kind = INS_JP;
        in->cond = op == 0xC3 ? 0 : (u8)(((op >> 3) & 3) + 1);
        in->target = (u16)(b[1] | b[2] << 8);
        return;
    case 0xCD: case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        in->kind = INS_CALL;
        in->cond = op == 0xCD ? 0 : (u8)(((op >> 3) & 3) + 1);
        in->target = (u16)(b[1] | b[2] << 8);
        return;
    case 0xC9: in->kind = INS_RET; return;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        in->kind = INS_RET;
        in->cond = (u8)(((op >> 3) & 3) + 1);
        return;
    case 0xD9: in->kind = INS_RETI; return;
    case 0xE9: in->kind = INS_JP_HL; return;
    case 0x76: in->kind = INS_HALT; return;
    case 0x10: in->kind = INS_STOP; return;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        in->kind = INS_RST;
        in->target = op & 0x38;
        return;
    default:
        return;
    }
}

int sm83_ends_block(const Insn *in)
{
    switch (in->kind) {
    case INS_JP: case INS_JR: case INS_RET:
        return in->cond == 0;
    case INS_RETI: case INS_JP_HL: case INS_ILLEGAL:
        return 1;
    default:
        /* CALL, RST, HALT y STOP sí regresan a la instrucción siguiente */
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Desensamblador                                                      */
/* ------------------------------------------------------------------ */

static const char *const r8n[8] = { "b", "c", "d", "e", "h", "l", "[hl]", "a" };
static const char *const r16n[4] = { "bc", "de", "hl", "sp" };
static const char *const r16pn[4] = { "bc", "de", "hl", "af" };
static const char *const ccn[4] = { "nz", "z", "nc", "c" };
static const char *const alun[8] = { "add a,", "adc a,", "sub", "sbc a,", "and", "xor", "or", "cp" };
static const char *const cbn[8] = { "rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl" };

void sm83_disasm(const u8 *b, u16 addr, char *buf, size_t size)
{
    u8 op = b[0];
    u8 n = b[1];
    u16 nn = (u16)(b[1] | b[2] << 8);
    int r = op & 7, d = (op >> 3) & 7;

    if (is_illegal(op)) { snprintf(buf, size, "db $%02X", op); return; }
    if (op == 0x76) { snprintf(buf, size, "halt"); return; }
    if (op >= 0x40 && op < 0x80) { snprintf(buf, size, "ld %s, %s", r8n[d], r8n[r]); return; }
    if (op >= 0x80 && op < 0xC0) { snprintf(buf, size, "%s %s", alun[d], r8n[r]); return; }

    switch (op) {
    case 0x00: snprintf(buf, size, "nop"); return;
    case 0x10: snprintf(buf, size, "stop"); return;
    case 0x01: case 0x11: case 0x21: case 0x31:
        snprintf(buf, size, "ld %s, $%04X", r16n[op >> 4], nn); return;
    case 0x02: snprintf(buf, size, "ld [bc], a"); return;
    case 0x12: snprintf(buf, size, "ld [de], a"); return;
    case 0x22: snprintf(buf, size, "ld [hl+], a"); return;
    case 0x32: snprintf(buf, size, "ld [hl-], a"); return;
    case 0x0A: snprintf(buf, size, "ld a, [bc]"); return;
    case 0x1A: snprintf(buf, size, "ld a, [de]"); return;
    case 0x2A: snprintf(buf, size, "ld a, [hl+]"); return;
    case 0x3A: snprintf(buf, size, "ld a, [hl-]"); return;
    case 0x03: case 0x13: case 0x23: case 0x33: snprintf(buf, size, "inc %s", r16n[op >> 4]); return;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: snprintf(buf, size, "dec %s", r16n[op >> 4]); return;
    case 0x09: case 0x19: case 0x29: case 0x39: snprintf(buf, size, "add hl, %s", r16n[op >> 4]); return;
    case 0x07: snprintf(buf, size, "rlca"); return;
    case 0x0F: snprintf(buf, size, "rrca"); return;
    case 0x17: snprintf(buf, size, "rla"); return;
    case 0x1F: snprintf(buf, size, "rra"); return;
    case 0x27: snprintf(buf, size, "daa"); return;
    case 0x2F: snprintf(buf, size, "cpl"); return;
    case 0x37: snprintf(buf, size, "scf"); return;
    case 0x3F: snprintf(buf, size, "ccf"); return;
    case 0x08: snprintf(buf, size, "ld [$%04X], sp", nn); return;
    case 0x18: snprintf(buf, size, "jr $%04X", (u16)(addr + 2 + (int8_t)n)); return;
    case 0x20: case 0x28: case 0x30: case 0x38:
        snprintf(buf, size, "jr %s, $%04X", ccn[(op >> 3) & 3], (u16)(addr + 2 + (int8_t)n)); return;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: snprintf(buf, size, "ret %s", ccn[(op >> 3) & 3]); return;
    case 0xC9: snprintf(buf, size, "ret"); return;
    case 0xD9: snprintf(buf, size, "reti"); return;
    case 0xC1: case 0xD1: case 0xE1: case 0xF1: snprintf(buf, size, "pop %s", r16pn[(op >> 4) & 3]); return;
    case 0xC5: case 0xD5: case 0xE5: case 0xF5: snprintf(buf, size, "push %s", r16pn[(op >> 4) & 3]); return;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: snprintf(buf, size, "jp %s, $%04X", ccn[(op >> 3) & 3], nn); return;
    case 0xC3: snprintf(buf, size, "jp $%04X", nn); return;
    case 0xE9: snprintf(buf, size, "jp hl"); return;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: snprintf(buf, size, "call %s, $%04X", ccn[(op >> 3) & 3], nn); return;
    case 0xCD: snprintf(buf, size, "call $%04X", nn); return;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        snprintf(buf, size, "rst $%02X", op & 0x38); return;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        snprintf(buf, size, "%s $%02X", alun[d], n); return;
    case 0xE0: snprintf(buf, size, "ldh [$FF%02X], a", n); return;
    case 0xF0: snprintf(buf, size, "ldh a, [$FF%02X]", n); return;
    case 0xE2: snprintf(buf, size, "ldh [c], a"); return;
    case 0xF2: snprintf(buf, size, "ldh a, [c]"); return;
    case 0xEA: snprintf(buf, size, "ld [$%04X], a", nn); return;
    case 0xFA: snprintf(buf, size, "ld a, [$%04X]", nn); return;
    case 0xE8: snprintf(buf, size, "add sp, %d", (int8_t)n); return;
    case 0xF8: snprintf(buf, size, "ld hl, sp%+d", (int8_t)n); return;
    case 0xF9: snprintf(buf, size, "ld sp, hl"); return;
    case 0xF3: snprintf(buf, size, "di"); return;
    case 0xFB: snprintf(buf, size, "ei"); return;
    case 0xCB: {
        int cr = n & 7, cb = (n >> 3) & 7;
        switch (n >> 6) {
        case 0: snprintf(buf, size, "%s %s", cbn[cb], r8n[cr]); return;
        case 1: snprintf(buf, size, "bit %d, %s", cb, r8n[cr]); return;
        case 2: snprintf(buf, size, "res %d, %s", cb, r8n[cr]); return;
        default: snprintf(buf, size, "set %d, %s", cb, r8n[cr]); return;
        }
    }
    default: break;
    }
    if ((op & 0xC7) == 0x04) { snprintf(buf, size, "inc %s", r8n[d]); return; }
    if ((op & 0xC7) == 0x05) { snprintf(buf, size, "dec %s", r8n[d]); return; }
    if ((op & 0xC7) == 0x06) { snprintf(buf, size, "ld %s, $%02X", r8n[d], n); return; }
    snprintf(buf, size, "db $%02X", op);
}

/* ------------------------------------------------------------------ */
/* Emisión de C                                                        */
/* ------------------------------------------------------------------ */

void emit_line(Emit *e, const char *fmt, ...)
{
    va_list ap;
    fputs(e->indent ? e->indent : "", e->f);
    va_start(ap, fmt);
    vfprintf(e->f, fmt, ap);
    va_end(ap);
    fputc('\n', e->f);
}

#define L(...) emit_line(e, __VA_ARGS__)

static const char *const R8[8] = { "c->b", "c->c", "c->d", "c->e", "c->h", "c->l", NULL, "c->a" };
static const char *const CC[4] = { "CC_NZ", "CC_Z", "CC_NC", "CC_C" };
static const char *const ALU[8] = { "op_add", "op_adc", "op_sub", "op_sbc", "op_and", "op_xor", "op_or", "op_cp" };
static const char *const CBOP[8] = { "op_rlc", "op_rrc", "op_rl", "op_rr", "op_sla", "op_sra", "op_swap", "op_srl" };

/* Operando inmediato de 8 bits: emite su lectura y devuelve la expresión */
static const char *imm8(Emit *e, char *buf)
{
    if (!e->recomp) {
        L("u8 n = FETCH8();");
        return "n";
    }
    L("TICK();");
    if (e->imm8_expr) return e->imm8_expr;
    snprintf(buf, 16, "0x%02X", e->b[1]);
    return buf;
}

static const char *imm16(Emit *e, char *buf)
{
    if (!e->recomp) {
        L("u16 nn = FETCH16();");
        return "nn";
    }
    L("TICK(); TICK();");
    snprintf(buf, 16, "0x%04X", (unsigned)(e->b[1] | e->b[2] << 8));
    return buf;
}

/* Dirección de la instrucción siguiente */
static const char *next_pc(Emit *e, int len, char *buf)
{
    if (!e->recomp) return "c->pc";
    snprintf(buf, 16, "0x%04X", (u16)(e->addr + len));
    return buf;
}

/* Lee un operando r8; para [hl] emite el acceso y devuelve "v" */
static const char *src8(Emit *e, int r)
{
    if (r != 6) return R8[r];
    L("u8 v = RD(HL());");
    return "v";
}

static void jump_const(Emit *e, u16 target)
{
    char buf[16];
    snprintf(buf, sizeof buf, "0x%04X", target);
    e->jump(e, buf, 1, target);
}

/* Destino de un salto con operando: constante en recompilado */
static void jump_operand(Emit *e, const char *expr, u16 target)
{
    if (e->recomp) jump_const(e, target);
    else e->jump(e, expr, 0, 0);
}

static void emit_cond_open(Emit *e, int cc)
{
    L("if (%s) {", CC[cc]);
}

static void emit_cb(Emit *e, u8 op)
{
    int r = op & 7, n = (op >> 3) & 7, grp = op >> 6;
    const char *x = R8[r];

    if (r == 6) {
        L("u8 v = RD(HL());");
        x = "v";
    }
    switch (grp) {
    case 0: L("%s = %s(c, %s);", x, CBOP[n], x); break;
    case 1: L("op_bit(c, %d, %s);", n, x); break;
    case 2: L("%s &= (u8)~0x%02X;", x, 1 << n); break;
    default: L("%s |= 0x%02X;", x, 1 << n); break;
    }
    if (r == 6 && grp != 1)
        L("WR(HL(), v);");
}

void emit_insn(Emit *e, u8 op)
{
    char b1[16], b2[16];
    int len = insn_len(op);
    int r = op & 7, d = (op >> 3) & 7;
    const char *s;

    if (e->recomp)
        L("TICK();");   /* lectura del opcode */

    if (is_illegal(op)) {
        L("gb_lockup(g);");
        if (e->recomp) L("c->pc = 0x%04X; return;", e->addr);
        return;
    }

    /* ld r, r' y halt */
    if (op >= 0x40 && op < 0x80) {
        if (op == 0x76) {
            L("gb_halt(g);");
            if (e->recomp) L("c->pc = 0x%04X; return;", (u16)(e->addr + 1));
            return;
        }
        if (d == 6) { L("WR(HL(), %s);", R8[r]); return; }
        if (r == 6) { L("%s = RD(HL());", R8[d]); return; }
        if (r != d) L("%s = %s;", R8[d], R8[r]);
        if (op == 0x40) L("OP_LDBB();");
        return;
    }

    /* ALU a, r */
    if (op >= 0x80 && op < 0xC0) {
        s = src8(e, r);
        L("%s(c, %s);", ALU[d], s);
        return;
    }

    switch (op) {
    case 0x00: return;

    case 0x10:  /* STOP: el segundo byte se ignora */
        if (e->recomp) {
            L("TICK();");
            L("c->pc = 0x%04X; gb_stop(g); return;", (u16)(e->addr + 2));
        } else {
            L("c->pc++; g->cyc += 4;");
            L("gb_stop(g);");
        }
        return;

    case 0x01: case 0x11: case 0x21: case 0x31: {
        const char *v = imm16(e, b1);
        switch (op >> 4) {
        case 0: L("SET_BC(%s);", v); break;
        case 1: L("SET_DE(%s);", v); break;
        case 2: L("SET_HL(%s);", v); break;
        default: L("c->sp = %s;", v); break;
        }
        return;
    }

    case 0x02: L("WR(BC(), c->a);"); return;
    case 0x12: L("WR(DE(), c->a);"); return;
    case 0x0A: L("c->a = RD(BC());"); return;
    case 0x1A: L("c->a = RD(DE());"); return;
    case 0x22: L("{ u16 hl = HL(); WR(hl, c->a); SET_HL(hl + 1); }"); return;
    case 0x32: L("{ u16 hl = HL(); WR(hl, c->a); SET_HL(hl - 1); }"); return;
    case 0x2A: L("{ u16 hl = HL(); c->a = RD(hl); SET_HL(hl + 1); }"); return;
    case 0x3A: L("{ u16 hl = HL(); c->a = RD(hl); SET_HL(hl - 1); }"); return;

    case 0x03: case 0x13: case 0x23: case 0x33:
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: {
        const char *delta = (op & 8) ? "- 1" : "+ 1";
        L("TICK();");
        switch (op >> 4) {
        case 0: L("SET_BC(BC() %s);", delta); break;
        case 1: L("SET_DE(DE() %s);", delta); break;
        case 2: L("SET_HL(HL() %s);", delta); break;
        default: L("c->sp = (u16)(c->sp %s);", delta); break;
        }
        return;
    }

    case 0x09: case 0x19: case 0x29: case 0x39: {
        static const char *const rr[4] = { "BC()", "DE()", "HL()", "c->sp" };
        L("TICK();");
        L("op_add_hl(c, %s);", rr[op >> 4]);
        return;
    }

    case 0x07: L("op_rlca(c);"); return;
    case 0x0F: L("op_rrca(c);"); return;
    case 0x17: L("op_rla(c);"); return;
    case 0x1F: L("op_rra(c);"); return;
    case 0x27: L("op_daa(c);"); return;
    case 0x2F: L("op_cpl(c);"); return;
    case 0x37: L("op_scf(c);"); return;
    case 0x3F: L("op_ccf(c);"); return;

    case 0x08: {
        const char *a = imm16(e, b1);
        L("WR(%s, c->sp & 0xFF);", a);
        L("WR((u16)(%s + 1), c->sp >> 8);", a);
        return;
    }

    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: {
        const char *n = imm8(e, b1);
        u16 target = (u16)(e->addr + 2 + (int8_t)e->b[1]);
        char expr[48];
        snprintf(expr, sizeof expr, "(u16)(c->pc + (s8)%s)", n);
        if (op != 0x18) emit_cond_open(e, d & 3);
        L("TICK();");
        jump_operand(e, expr, target);
        if (op != 0x18) L("}");
        return;
    }

    case 0xC3: case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
        const char *nn = imm16(e, b1);
        u16 target = (u16)(e->b[1] | e->b[2] << 8);
        if (op != 0xC3) emit_cond_open(e, d & 3);
        L("TICK();");
        jump_operand(e, nn, target);
        if (op != 0xC3) L("}");
        return;
    }

    case 0xE9:
        e->jump(e, "HL()", 0, 0);
        return;

    case 0xCD: case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
        const char *nn = imm16(e, b1);
        u16 target = (u16)(e->b[1] | e->b[2] << 8);
        if (op != 0xCD) emit_cond_open(e, d & 3);
        L("TICK();");
        L("PUSH16(%s);", next_pc(e, len, b2));
        jump_operand(e, nn, target);
        if (op != 0xCD) L("}");
        return;
    }

    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        L("TICK();");
        L("PUSH16(%s);", next_pc(e, len, b2));
        jump_const(e, op & 0x38);
        return;

    case 0xC9:
        L("{ u16 t = POP16(); TICK();");
        e->jump(e, "t", 0, 0);
        L("}");
        return;
    case 0xD9:
        L("{ u16 t = POP16(); TICK(); OP_RETI();");
        e->jump(e, "t", 0, 0);
        L("}");
        return;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        L("TICK();");
        emit_cond_open(e, d & 3);
        L("u16 t = POP16(); TICK();");
        e->jump(e, "t", 0, 0);
        L("}");
        return;

    case 0xC1: L("SET_BC(POP16());"); return;
    case 0xD1: L("SET_DE(POP16());"); return;
    case 0xE1: L("SET_HL(POP16());"); return;
    case 0xF1: L("SET_AF(POP16());"); return;
    case 0xC5: L("TICK(); PUSH16(BC());"); return;
    case 0xD5: L("TICK(); PUSH16(DE());"); return;
    case 0xE5: L("TICK(); PUSH16(HL());"); return;
    case 0xF5: L("TICK(); PUSH16(AF());"); return;

    case 0xC6: case 0xCE: case 0xD6: case 0xDE:
    case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        s = imm8(e, b1);
        L("%s(c, %s);", ALU[d], s);
        return;

    case 0xE0: s = imm8(e, b1); L("WR(0xFF00 | %s, c->a);", s); return;
    case 0xF0: s = imm8(e, b1); L("c->a = RD(0xFF00 | %s);", s); return;
    case 0xE2: L("WR(0xFF00 | c->c, c->a);"); return;
    case 0xF2: L("c->a = RD(0xFF00 | c->c);"); return;
    case 0xEA: s = imm16(e, b1); L("WR(%s, c->a);", s); return;
    case 0xFA: s = imm16(e, b1); L("c->a = RD(%s);", s); return;

    case 0xE8: s = imm8(e, b1); L("c->sp = op_sp_e(c, %s); TICK(); TICK();", s); return;
    case 0xF8: s = imm8(e, b1); L("SET_HL(op_sp_e(c, %s)); TICK();", s); return;
    case 0xF9: L("c->sp = HL(); TICK();"); return;

    case 0xF3: L("OP_DI();"); return;
    case 0xFB: L("OP_EI();"); return;

    case 0xCB:
        if (e->recomp) {
            L("TICK();");
            emit_cb(e, e->b[1]);
        } else {
            const char *saved = e->indent;
            L("u8 cb = FETCH8();");
            L("switch (cb) {");
            for (int i = 0; i < 256; i++) {
                L("case 0x%02X: {", i);
                e->indent = "        ";
                emit_cb(e, (u8)i);
                L("break;");
                e->indent = saved;
                L("}");
            }
            L("}");
        }
        return;

    default:
        break;
    }

    /* inc r, dec r, ld r, n */
    if ((op & 0xC7) == 0x04 || (op & 0xC7) == 0x05) {
        const char *fn = (op & 1) ? "op_dec" : "op_inc";
        if (d == 6) L("{ u8 v = RD(HL()); WR(HL(), %s(c, v)); }", fn);
        else L("%s = %s(c, %s);", R8[d], fn, R8[d]);
        return;
    }
    if ((op & 0xC7) == 0x06) {
        s = imm8(e, b1);
        if (d == 6) L("WR(HL(), %s);", s);
        else L("%s = %s;", R8[d], s);
        return;
    }

    L("/* opcode $%02X sin emitir */", op);
}
