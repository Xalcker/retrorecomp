/*
 * 68000: semántica de cada instrucción. La única fuente de verdad del CPU.
 *
 * Cada familia de instrucciones es una función k_NOMBRE(g, op, ext, pc) que ejecuta la
 * instrucción `op` que está en `pc` y devuelve la dirección de la siguiente que hay que
 * ejecutar (la que sigue, el destino de un salto o el manejador de una excepción).
 *
 * - El intérprete (cpu68k.c) las llama con ext = NULL: las palabras de extensión salen de la cola
 *   de prefetch (ir, irc) y cada una que se consume pide la siguiente al bus, como el 68000.
 * - El código generado las llamará con `op`, `pc` y `ext` constantes (las palabras de extensión,
 *   leídas de la ROM al recompilar): el compilador pliega los switch y lo que queda es la
 *   instrucción sola. Las lecturas de la ROM se vuelven ciclos sin acceso al bus, y ir/irc solo se
 *   rehacen al salir del código generado.
 *
 * Los ciclos salen de los accesos al bus (4 por acceso) más los internos (m68k_idle), y se
 * comprueban con las SingleStepTests del 68000 (src/tools/m68ktest.c).
 */
#ifndef M68K_OPS_H
#define M68K_OPS_H

#include "gen.h"

#if defined(__GNUC__) || defined(__clang__)
#define M68K_INL static inline __attribute__((always_inline))
#define M68K_NORET __attribute__((noreturn))
#else
#define M68K_INL static inline
#define M68K_NORET
#endif

/* Familias de instrucciones (m68k_decode) */
enum {
    K_ILLEGAL, K_LINEA, K_LINEF,
    K_IMM, K_ICCR, K_ISR, K_BIT, K_MOVEP, K_MOVE, K_MOVEA, K_UNARY, K_MOVEFSR, K_MOVETCCR, K_MOVETSR,
    K_CHK, K_LEA, K_NBCD, K_SWAP, K_PEA, K_EXT, K_MOVEM, K_MOVEMR, K_TST, K_TAS, K_TRAP, K_LINK,
    K_UNLK, K_MUSP, K_RESET, K_NOP, K_STOP, K_RTE, K_RTS, K_TRAPV, K_RTR, K_JSR, K_JMP,
    K_QUICK, K_SCC, K_DBCC, K_BCC, K_MOVEQ, K_ALU, K_ALUA, K_MUL, K_DIV, K_BCD, K_ADDX, K_CMPM,
    K_EXG, K_SHIFTR, K_SHIFTM,
    K_COUNT
};

/* Modos de direccionamiento como bits, para validar opcodes */
enum {
    EA_DN = 1, EA_AN = 2, EA_IND = 4, EA_PI = 8, EA_PD = 16, EA_D16 = 32, EA_D8 = 64,
    EA_AW = 128, EA_AL = 256, EA_PC16 = 512, EA_PC8 = 1024, EA_IMM = 2048
};
#define EA_ALL  0xFFFu
#define EA_DATA (EA_ALL & ~(unsigned)EA_AN)
#define EA_MEM  (EA_ALL & ~(unsigned)(EA_DN | EA_AN))
#define EA_CTRL (EA_IND | EA_D16 | EA_D8 | EA_AW | EA_AL | EA_PC16 | EA_PC8)
#define EA_ALT  (EA_DN | EA_AN | EA_IND | EA_PI | EA_PD | EA_D16 | EA_D8 | EA_AW | EA_AL)
#define EA_DALT (EA_ALT & ~(unsigned)EA_AN)
#define EA_MALT (EA_ALT & ~(unsigned)(EA_DN | EA_AN))

static inline unsigned m68k_ea_bit(int mode, int reg)
{
    return mode < 7 ? 1u << mode : reg <= 4 ? 128u << reg : 0;
}

/* Contexto de una instrucción */
typedef struct {
    struct Gen *g;
    M68k *c;
    const u16 *ext;     /* NULL: intérprete; si no, las palabras de extensión (código generado) */
    int ei;             /* siguiente palabra de ext */
    u32 pc;             /* dirección del opcode */
    u32 pf;             /* dirección de la palabra que está en irc (la siguiente de extensión) */
    u16 op;
    u32 lf;             /* dirección de la última lectura de programa (el error de dirección apila lf - 2) */
} X;

/* Errores de dirección: acceso de palabra a una dirección impar (cpu68k.c). No vuelve. */
enum { AE_READ = 0x10, AE_INSN = 0x08 };       /* bits del estado que se apila: R/W e I/N (I/N = 1: fuera de una instrucción) */
M68K_NORET void m68k_address_error(X *x, u32 addr, int flags);
/* Excepción de grupo 1 o 2: apila PC y SR, salta al vector. Devuelve el manejador. */
u32  m68k_exception(X *x, int vec, u32 push_pc, int pre_idle);

#define A24(a) ((a) & 0xFFFFFFu)

M68K_INL u32 msk(int sz) { return sz == 0 ? 0xFFu : sz == 1 ? 0xFFFFu : 0xFFFFFFFFu; }
M68K_INL u32 msb(int sz) { return sz == 0 ? 0x80u : sz == 1 ? 0x8000u : 0x80000000u; }
M68K_INL u32 sx8(u32 v)  { return (u32)(int32_t)(int8_t)(u8)v; }
M68K_INL u32 sx16(u32 v) { return (u32)(int32_t)(int16_t)(u16)v; }

M68K_INL void idle(X *x, u32 n) { m68k_idle(x->g, n); }

/* --- Bus, con el error de dirección --- */

M68K_INL u16 fetchw(X *x, u32 a)
{
    if (a & 1) m68k_address_error(x, a, AE_READ | AE_INSN);
    return m68k_fetch(x->g, A24(a));
}

M68K_INL u32 rd(X *x, u32 a, int sz)
{
    if (sz == 0) return m68k_rd8(x->g, A24(a));
    if (a & 1) m68k_address_error(x, a, AE_READ);
    if (sz == 1) return m68k_rd16(x->g, A24(a));
    u32 hi = m68k_rd16(x->g, A24(a));
    return hi << 16 | m68k_rd16(x->g, A24(a + 2));
}

M68K_INL void wr(X *x, u32 a, int sz, u32 v)
{
    if (sz == 0) { m68k_wr8(x->g, A24(a), (u8)v); return; }
    if (a & 1) m68k_address_error(x, a, 0);
    if (sz == 1) { m68k_wr16(x->g, A24(a), (u16)v); return; }
    m68k_wr16(x->g, A24(a), (u16)(v >> 16));
    m68k_wr16(x->g, A24(a + 2), (u16)v);
}

M68K_INL void push32(X *x, u32 v) { x->c->a[7] -= 4; wr(x, x->c->a[7], 2, v); }
M68K_INL u32  pop32(X *x)  { u32 v = rd(x, x->c->a[7], 2); x->c->a[7] += 4; return v; }
M68K_INL u16  pop16(X *x)  { u16 v = (u16)rd(x, x->c->a[7], 1); x->c->a[7] += 2; return v; }

/* --- Prefetch y palabras de extensión --- */

/* Consume la palabra de irc y pide la siguiente al bus */
M68K_INL u16 ext(X *x)
{
    u16 v;
    if (x->ext) { v = x->ext[x->ei++]; idle(x, 4); }
    else { v = x->c->irc; x->c->irc = fetchw(x, x->pf + 2); }
    x->pf += 2;
    x->lf = x->pf;
    return v;
}

M68K_INL u32 ext32(X *x) { u32 hi = ext(x); return hi << 16 | ext(x); }

/* La palabra de irc sin consumirla */
M68K_INL u16 peek_ext(X *x) { return x->ext ? x->ext[x->ei] : x->c->irc; }

/* Consume la palabra de irc sin pedir otra: la última de una instrucción que salta (el prefetch
 * se rehace en el destino) */
M68K_INL u16 ext_last(X *x)
{
    u16 v = x->ext ? x->ext[x->ei++] : x->c->irc;
    x->pf += 2;
    return v;
}

/* Lee (o vuelve a leer) la palabra de irc: tras ext_last en MOVE a (xxx).l, y en MOVE a SR/CCR y
 * las operaciones inmediatas sobre SR/CCR */
M68K_INL void refetch(X *x)
{
    if (x->ext) idle(x, 4);
    else x->c->irc = fetchw(x, x->pf);
    x->lf = x->pf;
}

/* Fin normal: la palabra de irc pasa a ir y se pide la siguiente */
M68K_INL u32 fin(X *x)
{
    M68k *c = x->c;
    if (x->ext) idle(x, 4);
    else { c->ir = c->irc; c->pc = x->pf; c->irc = fetchw(x, x->pf + 2); }
    x->lf = x->pf + 2;
    return x->pf;
}

/* Salto: llena la cola de prefetch en el destino */
M68K_INL u32 jump(X *x, u32 target)
{
    M68k *c = x->c;
    if (target & 1) m68k_address_error(x, target, AE_READ | AE_INSN);
    if (x->ext) idle(x, 8);
    else { c->pc = target; c->ir = fetchw(x, target); c->irc = fetchw(x, target + 2); }
    return target;
}

/* --- Registro de estado --- */

M68K_INL u16 sr_of(const M68k *c)
{
    return (u16)(c->t << 15 | c->s << 13 | c->ipm << 8 | c->fx << 4 | c->fn << 3 | c->fz << 2 | c->fv << 1 | c->fc);
}

M68K_INL void ccr_set(M68k *c, u32 v)
{
    c->fx = v >> 4 & 1; c->fn = v >> 3 & 1; c->fz = v >> 2 & 1; c->fv = v >> 1 & 1; c->fc = v & 1;
}

M68K_INL void sr_set(M68k *c, u32 v)
{
    ccr_set(c, v);
    c->t = v >> 15 & 1;
    c->ipm = v >> 8 & 7;
    u8 s = v >> 13 & 1;
    if (s != c->s) { u32 t = c->a[7]; c->a[7] = c->osp; c->osp = t; c->s = s; }
}

M68K_INL u32 priv(X *x) { return m68k_exception(x, VEC_PRIV, x->pc, 4); }

/* --- Banderas --- */

M68K_INL void f_nz(M68k *c, u32 r, int sz) { c->fn = (r & msb(sz)) != 0; c->fz = (r & msk(sz)) == 0; }
M68K_INL void f_logic(M68k *c, u32 r, int sz) { f_nz(c, r, sz); c->fv = 0; c->fc = 0; }

/* d + s + xin. sticky: Z solo se borra (ADDX) */
M68K_INL u32 f_add(M68k *c, u32 s, u32 d, int sz, u32 xin, int sticky)
{
    u32 m = msb(sz), r = (d + s + xin) & msk(sz);
    c->fc = c->fx = (((s & d) | (~r & (s | d))) & m) != 0;
    c->fv = (((s ^ r) & (d ^ r)) & m) != 0;
    c->fn = (r & m) != 0;
    if (sticky) { if (r) c->fz = 0; } else c->fz = r == 0;
    return r;
}

/* d - s - xin. setx: también X (no en CMP) */
M68K_INL u32 f_sub(M68k *c, u32 s, u32 d, int sz, u32 xin, int sticky, int setx)
{
    u32 m = msb(sz), r = (d - s - xin) & msk(sz);
    c->fc = (((s & ~d) | (r & ~d) | (s & r)) & m) != 0;
    if (setx) c->fx = c->fc;
    c->fv = (((s ^ d) & (r ^ d)) & m) != 0;
    c->fn = (r & m) != 0;
    if (sticky) { if (r) c->fz = 0; } else c->fz = r == 0;
    return r;
}

/* BCD, con el comportamiento no documentado de N y V del 68000 */
M68K_INL u32 bcd_add(M68k *c, u32 s, u32 d)
{
    u32 ss = d + s + c->fx;
    u32 bc = ((d & s) | (~ss & (d | s))) & 0x88;
    u32 dc = (((ss + 0x66) ^ ss) & 0x110) >> 1;
    u32 corf = (bc | dc) - ((bc | dc) >> 2);
    u32 r = ss + corf;
    c->fc = c->fx = ((bc | (ss & ~r)) >> 7) & 1;
    c->fv = ((~ss & r) >> 7) & 1;
    r &= 0xFF;
    c->fn = r >> 7;
    if (r) c->fz = 0;
    return r;
}

M68K_INL u32 bcd_sub(M68k *c, u32 s, u32 d)
{
    u32 dd = d - s - c->fx;
    u32 bc = ((~d & s) | (dd & ~(d ^ s))) & 0x88;
    u32 corf = bc - (bc >> 2);
    u32 r = dd - corf;
    c->fc = c->fx = ((bc | (~dd & r)) >> 7) & 1;
    c->fv = ((dd & ~r) >> 7) & 1;
    r &= 0xFF;
    c->fn = r >> 7;
    if (r) c->fz = 0;
    return r;
}

M68K_INL int cond(const M68k *c, int cc)
{
    switch (cc) {
    case 0:  return 1;
    case 1:  return 0;
    case 2:  return !c->fc && !c->fz;
    case 3:  return c->fc || c->fz;
    case 4:  return !c->fc;
    case 5:  return c->fc;
    case 6:  return !c->fz;
    case 7:  return c->fz;
    case 8:  return !c->fv;
    case 9:  return c->fv;
    case 10: return !c->fn;
    case 11: return c->fn;
    case 12: return c->fn == c->fv;
    case 13: return c->fn != c->fv;
    case 14: return !c->fz && c->fn == c->fv;
    default: return c->fz || c->fn != c->fv;
    }
}

/* --- Modos de direccionamiento --- */

M68K_INL void dput(M68k *c, int r, int sz, u32 v) { u32 m = msk(sz); c->d[r] = (c->d[r] & ~m) | (v & m); }

/* (An)+ y -(An) de byte con A7 mueven 2: la pila siempre queda par */
M68K_INL u32 step(int sz, int reg) { return sz == 0 ? (reg == 7 ? 2u : 1u) : sz == 1 ? 2u : 4u; }

/* Índice de d8(An,Xn) y d8(PC,Xn) */
M68K_INL u32 index_of(const M68k *c, u16 w)
{
    int r = w >> 12 & 7;
    u32 v = (w & 0x8000) ? c->a[r] : c->d[r];
    if (!(w & 0x800)) v = sx16(v);
    return v + sx8(w);
}

typedef struct { int mode, reg; u32 addr, imm; } Ea;

/* Resuelve un operando: consume sus palabras de extensión y aplica (An)+ / -(An). pd_idle: -(An)
 * cuesta 2 ciclos internos (no en el destino de MOVE). */
M68K_INL void ea_at(X *x, Ea *e, int mode, int reg, int sz, int pd_idle)
{
    M68k *c = x->c;
    e->mode = mode; e->reg = reg; e->addr = 0; e->imm = 0;
    switch (mode) {
    case 0: case 1: return;
    case 2: e->addr = c->a[reg]; return;
    case 3: e->addr = c->a[reg]; c->a[reg] += step(sz, reg); return;
    case 4: if (pd_idle) idle(x, 2); c->a[reg] -= step(sz, reg); e->addr = c->a[reg]; return;
    case 5: e->addr = c->a[reg] + sx16(ext(x)); return;
    case 6: { u16 w = ext(x); idle(x, 2); e->addr = c->a[reg] + index_of(c, w); return; }
    default:
        switch (reg) {
        case 0: e->addr = sx16(ext(x)); return;
        case 1: e->addr = ext32(x); return;
        case 2: { u32 b = x->pf; e->addr = b + sx16(ext(x)); return; }
        case 3: { u32 b = x->pf; u16 w = ext(x); idle(x, 2); e->addr = b + index_of(c, w); return; }
        default: e->imm = sz == 2 ? ext32(x) : sz == 1 ? ext(x) : ext(x) & 0xFFu; return;
        }
    }
}

M68K_INL u32 ea_get(X *x, const Ea *e, int sz)
{
    switch (e->mode) {
    case 0: return x->c->d[e->reg] & msk(sz);
    case 1: return x->c->a[e->reg] & msk(sz);
    case 7: if (e->reg == 4) return e->imm; /* fallthrough */
    default: return rd(x, e->addr, sz);
    }
}

M68K_INL void ea_put(X *x, const Ea *e, int sz, u32 v)
{
    switch (e->mode) {
    case 0: dput(x->c, e->reg, sz, v); return;
    case 1: x->c->a[e->reg] = v; return;
    default: wr(x, e->addr, sz, v); return;
    }
}

/* Dirección de un operando de control (LEA, PEA, MOVEM). idx: ciclos internos de d8(An,Xn) */
M68K_INL u32 ea_ctrl(X *x, int mode, int reg, int idx)
{
    M68k *c = x->c;
    switch (mode) {
    case 2: return c->a[reg];
    case 5: return c->a[reg] + sx16(ext(x));
    case 6: { u16 w = ext(x); idle(x, (u32)idx); return c->a[reg] + index_of(c, w); }
    default:
        switch (reg) {
        case 0: return sx16(ext(x));
        case 1: return ext32(x);
        case 2: { u32 b = x->pf; return b + sx16(ext(x)); }
        default: { u32 b = x->pf; u16 w = ext(x); idle(x, (u32)idx); return b + index_of(c, w); }
        }
    }
}

/* Dirección de destino de JMP/JSR: la última palabra de extensión no se repone */
M68K_INL u32 ea_jump(X *x, int mode, int reg)
{
    M68k *c = x->c;
    switch (mode) {
    case 2: return c->a[reg];
    case 5: { u32 a = c->a[reg] + sx16(ext_last(x)); idle(x, 2); return a; }
    case 6: { u16 w = ext_last(x); idle(x, 6); return c->a[reg] + index_of(c, w); }
    default:
        switch (reg) {
        case 0: { u32 a = sx16(ext_last(x)); idle(x, 2); return a; }
        case 1: { u32 hi = ext(x); return hi << 16 | ext_last(x); }
        case 2: { u32 b = x->pf; u32 a = b + sx16(ext_last(x)); idle(x, 2); return a; }
        default: { u32 b = x->pf; u16 w = ext_last(x); idle(x, 6); return b + index_of(c, w); }
        }
    }
}

/* Desplazamientos y rotaciones. type: 0 AS, 1 LS, 2 ROX, 3 RO */
M68K_INL u32 shift(M68k *c, int type, int left, u32 v, int cnt, int sz)
{
    u32 m = msk(sz), hb = msb(sz);
    c->fv = 0;
    if (cnt == 0) { c->fc = type == 2 ? c->fx : 0; f_nz(c, v, sz); return v; }
    for (int i = 0; i < cnt; i++) {
        u32 out;
        if (left) {
            out = (v & hb) != 0;
            u32 in = type == 2 ? c->fx : type == 3 ? out : 0;
            u32 nv = ((v << 1) | in) & m;
            if (type == 0 && ((nv ^ v) & hb)) c->fv = 1;
            v = nv;
        } else {
            out = v & 1;
            u32 in = type == 0 ? (v & hb) : type == 2 ? (c->fx ? hb : 0) : type == 3 ? (out ? hb : 0) : 0;
            v = (v >> 1) | in;
        }
        c->fc = (u8)out;
        if (type != 3) c->fx = (u8)out;
    }
    /* ASR con más pasos que bits: las SingleStepTests dan C = X = 0 aunque el número sea negativo */
    if (type == 0 && !left && cnt > 8 << sz) c->fc = c->fx = 0;
    f_nz(c, v, sz);
    return v;
}

/* Ciclos de DIVU y DIVS (sin el operando), según el algoritmo del microcódigo */
M68K_INL u32 divu_cycles(u32 dividend, u16 divisor)
{
    if ((dividend >> 16) >= divisor) return 10;
    u32 mc = 38, hd = (u32)divisor << 16;
    for (int i = 0; i < 15; i++) {
        u32 t = dividend;
        dividend <<= 1;
        if (t & 0x80000000u) dividend -= hd;
        else { mc += 2; if (dividend >= hd) { dividend -= hd; mc--; } }
    }
    return mc * 2;
}

/* Con desborde (de cualquier tipo) termina antes, como en las SingleStepTests */
M68K_INL u32 divs_cycles(int32_t dividend, int16_t divisor, int overflow)
{
    u32 mc = 6;
    if (dividend < 0) mc++;
    u32 ad = dividend < 0 ? 0u - (u32)dividend : (u32)dividend;
    u32 av = divisor < 0 ? (u32)(-(int32_t)divisor) : (u32)divisor;
    if (overflow || (ad >> 16) >= av) return (mc + 2) * 2;
    u32 aq = ad / av;
    mc += 55;
    if (divisor >= 0) { if (dividend >= 0) mc--; else mc++; }
    for (int i = 0; i < 15; i++) { if (!(aq & 0x8000)) mc++; aq <<= 1; }
    return mc * 2;
}

/* --- Instrucciones --- */

#define OPF(name) M68K_INL u32 k_##name(struct Gen *g, u16 op, const u16 *ext_words, u32 pc)
#define SETUP X xs = { g, &g->cpu, ext_words, 0, pc, pc + 2, op, pc + 2 }; X *x = &xs; M68k *c = x->c; (void)c
#define MODE (op >> 3 & 7)
#define REG  (op & 7)
#define SZ   (op >> 6 & 3)

OPF(ILLEGAL) { SETUP; return m68k_exception(x, VEC_ILLEGAL, pc, 4); }
OPF(LINEA)   { SETUP; return m68k_exception(x, VEC_LINEA, pc, 4); }
OPF(LINEF)   { SETUP; return m68k_exception(x, VEC_LINEF, pc, 4); }

/* ORI ANDI SUBI ADDI EORI CMPI #,<ea> */
OPF(IMM)
{
    SETUP;
    int t = op >> 9 & 7, sz = SZ, mode = MODE;
    u32 s = sz == 2 ? ext32(x) : ext(x) & msk(sz);
    Ea e; ea_at(x, &e, mode, REG, sz, 1);
    u32 d = ea_get(x, &e, sz), r;
    switch (t) {
    case 0: r = d | s; f_logic(c, r, sz); break;
    case 1: r = d & s; f_logic(c, r, sz); break;
    case 5: r = d ^ s; f_logic(c, r, sz); break;
    case 2: r = f_sub(c, s, d, sz, 0, 0, 1); break;
    case 3: r = f_add(c, s, d, sz, 0, 0); break;
    default: f_sub(c, s, d, sz, 0, 0, 0); r = d; break;
    }
    if (mode == 0 && sz == 2) idle(x, t == 6 ? 2 : 4);
    if (t != 6) ea_put(x, &e, sz, r);
    return fin(x);
}

/* ORI ANDI EORI a CCR (K_ICCR) o a SR (K_ISR, privilegiada) */
M68K_INL u32 imm_sr(struct Gen *g, u16 op, const u16 *ext_words, u32 pc, int to_sr)
{
    SETUP;
    if (to_sr && !c->s) return priv(x);
    int t = op >> 9 & 7;
    u32 v = ext(x), cur = sr_of(c);
    u32 r = t == 0 ? cur | v : t == 1 ? cur & v : cur ^ v;
    if (to_sr) sr_set(c, r); else ccr_set(c, r);
    idle(x, 8);
    refetch(x);
    return fin(x);
}
OPF(ICCR) { return imm_sr(g, op, ext_words, pc, 0); }
OPF(ISR)  { return imm_sr(g, op, ext_words, pc, 1); }

/* BTST BCHG BCLR BSET, con el número de bit en Dn o inmediato */
OPF(BIT)
{
    SETUP;
    int t = SZ, mode = MODE, reg = REG;
    u32 bit = (op & 0x100) ? c->d[op >> 9 & 7] : ext(x);
    if (mode == 0) {
        bit &= 31;
        u32 m = 1u << bit, d = c->d[reg];
        c->fz = !(d & m);
        if (t == 1) d ^= m; else if (t == 2) d &= ~m; else if (t == 3) d |= m;
        c->d[reg] = d;
        idle(x, t == 0 ? 2 : t == 2 ? (bit < 16 ? 4 : 6) : (bit < 16 ? 2 : 4));
    } else {
        bit &= 7;
        Ea e; ea_at(x, &e, mode, reg, 0, 1);
        u32 d = ea_get(x, &e, 0), m = 1u << bit;
        c->fz = !(d & m);
        if (t) ea_put(x, &e, 0, t == 1 ? d ^ m : t == 2 ? d & ~m : d | m);
        else if (mode == 7 && REG == 4) idle(x, 2);
    }
    return fin(x);
}

/* MOVEP: bytes alternos (periféricos de 8 bits) */
OPF(MOVEP)
{
    SETUP;
    int dr = op >> 9 & 7, om = SZ;
    u32 a = c->a[REG] + sx16(ext(x));
    switch (om) {
    case 0: { u32 hi = rd(x, a, 0); u32 lo = rd(x, a + 2, 0); dput(c, dr, 1, hi << 8 | lo); break; }
    case 1: {
        u32 v = 0;
        for (int i = 0; i < 4; i++) v = v << 8 | rd(x, a + 2 * (u32)i, 0);
        c->d[dr] = v;
        break;
    }
    case 2: wr(x, a, 0, c->d[dr] >> 8); wr(x, a + 2, 0, c->d[dr]); break;
    default: for (int i = 0; i < 4; i++) wr(x, a + 2 * (u32)i, 0, c->d[dr] >> (24 - 8 * i)); break;
    }
    return fin(x);
}

M68K_INL int move_size(u16 op) { return (op >> 12) == 1 ? 0 : (op >> 12) == 3 ? 1 : 2; }

OPF(MOVE)
{
    SETUP;
    int sz = move_size(op), dmode = op >> 6 & 7, dreg = op >> 9 & 7;
    Ea s; ea_at(x, &s, MODE, REG, sz, 1);
    u32 v = ea_get(x, &s, sz);
    f_logic(c, v, sz);
    if (dmode == 3) {                   /* (An)+: el registro avanza después de escribir */
        wr(x, c->a[dreg], sz, v);
        c->a[dreg] += step(sz, dreg);
    } else if (dmode == 4) {            /* -(An): el prefetch va antes; un long, la palabra baja primero */
        u32 next;
        if (sz == 2) {
            c->a[dreg] -= 2;
            next = fin(x);
            wr(x, c->a[dreg], 1, v);
            c->a[dreg] -= 2;
            wr(x, c->a[dreg], 1, v >> 16);
        } else {
            c->a[dreg] -= step(sz, dreg);
            next = fin(x);
            wr(x, c->a[dreg], sz, v);
        }
        return next;
    } else if (dmode == 7 && dreg == 1 && (s.mode >= 2 && !(s.mode == 7 && s.reg == 4))) {
        /* (xxx).l con el origen en memoria: la segunda palabra se repone después de escribir */
        u32 hi = ext(x);
        u32 a = hi << 16 | ext_last(x);
        wr(x, a, sz, v);
        refetch(x);
    } else {
        Ea d; ea_at(x, &d, dmode, dreg, sz, 0);
        ea_put(x, &d, sz, v);
    }
    return fin(x);
}

OPF(MOVEA)
{
    SETUP;
    int sz = move_size(op);
    Ea s; ea_at(x, &s, MODE, REG, sz, 1);
    u32 v = ea_get(x, &s, sz);
    c->a[op >> 9 & 7] = sz == 1 ? sx16(v) : v;
    return fin(x);
}

/* NEGX CLR NEG NOT */
OPF(UNARY)
{
    SETUP;
    int t = op >> 9 & 3, sz = SZ, mode = MODE;
    Ea e; ea_at(x, &e, mode, REG, sz, 1);
    u32 d = ea_get(x, &e, sz), r;
    switch (t) {
    case 0: r = f_sub(c, d, 0, sz, c->fx, 1, 1); break;
    case 1: r = 0; f_logic(c, 0, sz); break;
    case 2: r = f_sub(c, d, 0, sz, 0, 0, 1); break;
    default: r = ~d & msk(sz); f_logic(c, r, sz); break;
    }
    if (mode == 0 && sz == 2) idle(x, 2);
    ea_put(x, &e, sz, r);
    return fin(x);
}

OPF(MOVEFSR)
{
    SETUP;
    int mode = MODE;
    Ea e; ea_at(x, &e, mode, REG, 1, 1);
    if (mode) ea_get(x, &e, 1);         /* el 68000 lee antes de escribir */
    else idle(x, 2);
    ea_put(x, &e, 1, sr_of(c));
    return fin(x);
}

M68K_INL u32 move_to_sr(struct Gen *g, u16 op, const u16 *ext_words, u32 pc, int to_sr)
{
    SETUP;
    if (to_sr && !c->s) return priv(x);
    Ea e; ea_at(x, &e, MODE, REG, 1, 1);
    u32 v = ea_get(x, &e, 1);
    if (to_sr) sr_set(c, v); else ccr_set(c, v);
    idle(x, 4);
    refetch(x);
    return fin(x);
}
OPF(MOVETCCR) { return move_to_sr(g, op, ext_words, pc, 0); }
OPF(MOVETSR)  { return move_to_sr(g, op, ext_words, pc, 1); }

OPF(CHK)
{
    SETUP;
    Ea e; ea_at(x, &e, MODE, REG, 1, 1);
    int16_t bound = (int16_t)ea_get(x, &e, 1), v = (int16_t)c->d[op >> 9 & 7];
    idle(x, 6);
    /* Primero se compara con el límite y después el signo; si no atrapa, N no cambia */
    c->fz = v == 0; c->fv = 0; c->fc = 0;
    if (v > bound) { c->fn = v < 0; return m68k_exception(x, VEC_CHK, x->pf, 4); }
    if (v < 0) { c->fn = 1; return m68k_exception(x, VEC_CHK, x->pf, 6); }
    return fin(x);
}

OPF(LEA)
{
    SETUP;
    c->a[op >> 9 & 7] = ea_ctrl(x, MODE, REG, 4);
    return fin(x);
}

OPF(PEA)
{
    SETUP;
    u32 a = ea_ctrl(x, MODE, REG, 4);
    push32(x, a);
    return fin(x);
}

OPF(NBCD)
{
    SETUP;
    int mode = MODE;
    Ea e; ea_at(x, &e, mode, REG, 0, 1);
    u32 r = bcd_sub(c, ea_get(x, &e, 0), 0);
    if (mode == 0) idle(x, 2);
    ea_put(x, &e, 0, r);
    return fin(x);
}

OPF(SWAP)
{
    SETUP;
    u32 d = c->d[REG];
    d = d << 16 | d >> 16;
    c->d[REG] = d;
    f_logic(c, d, 2);
    return fin(x);
}

OPF(EXT)
{
    SETUP;
    if (SZ == 2) { u32 v = sx8(c->d[REG]); dput(c, REG, 1, v); f_logic(c, v, 1); }
    else { u32 v = sx16(c->d[REG]); c->d[REG] = v; f_logic(c, v, 2); }
    return fin(x);
}

/* MOVEM registros -> memoria */
OPF(MOVEM)
{
    SETUP;
    int sz = (op & 0x40) ? 2 : 1, mode = MODE, reg = REG;
    u32 n = sz == 2 ? 4 : 2;
    u16 mask = ext(x);
    if (mode == 4) {
        u32 a = c->a[reg];
        for (int i = 0; i < 16; i++)
            if (mask & (1u << i)) {
                int r = 15 - i;
                u32 v = r < 8 ? c->d[r] : c->a[r - 8];
                if (sz == 2) { a -= 2; wr(x, a, 1, v); a -= 2; wr(x, a, 1, v >> 16); }     /* la palabra baja primero */
                else { a -= 2; wr(x, a, 1, v); }
            }
        c->a[reg] = a;
    } else {
        u32 a = ea_ctrl(x, mode, reg, 2);
        for (int i = 0; i < 16; i++)
            if (mask & (1u << i)) { wr(x, a, sz, i < 8 ? c->d[i] : c->a[i - 8]); a += n; }
    }
    return fin(x);
}

/* MOVEM memoria -> registros (con la lectura de más del final) */
OPF(MOVEMR)
{
    SETUP;
    int sz = (op & 0x40) ? 2 : 1, mode = MODE, reg = REG;
    u32 n = sz == 2 ? 4 : 2;
    u16 mask = ext(x);
    u32 a = mode == 3 ? c->a[reg] : ea_ctrl(x, mode, reg, 2);
    if (mode == 3 && (a & 1)) { c->a[reg] = a + 2; m68k_address_error(x, a, AE_READ); }
    for (int i = 0; i < 16; i++)
        if (mask & (1u << i)) {
            u32 v = rd(x, a, sz);
            if (sz == 1) v = sx16(v);
            if (i < 8) c->d[i] = v; else c->a[i - 8] = v;
            a += n;
        }
    rd(x, a, 1);
    if (mode == 3) c->a[reg] = a;
    return fin(x);
}

OPF(TST)
{
    SETUP;
    int sz = SZ;
    Ea e; ea_at(x, &e, MODE, REG, sz, 1);
    f_logic(c, ea_get(x, &e, sz), sz);
    return fin(x);
}

OPF(TAS)
{
    SETUP;
    int mode = MODE;
    Ea e; ea_at(x, &e, mode, REG, 0, 1);
    u32 v = ea_get(x, &e, 0);
    f_logic(c, v, 0);
    if (mode) { idle(x, 2); c->tas = 1; }
    ea_put(x, &e, 0, v | 0x80);
    c->tas = 0;
    return fin(x);
}

OPF(TRAP)  { SETUP; return m68k_exception(x, VEC_TRAP0 + (op & 15), x->pf, 6); }

OPF(LINK)
{
    SETUP;
    int r = REG;
    u32 d = sx16(ext(x));
    c->a[7] -= 4;
    wr(x, c->a[7], 2, c->a[r]);
    c->a[r] = c->a[7];
    c->a[7] += d;
    return fin(x);
}

OPF(UNLK)
{
    SETUP;
    int r = REG;
    c->a[7] = c->a[r];
    u32 v = pop32(x);
    c->a[r] = v;
    return fin(x);
}

OPF(MUSP)
{
    SETUP;
    if (!c->s) return priv(x);
    if (op & 8) c->a[REG] = c->osp; else c->osp = c->a[REG];
    return fin(x);
}

OPF(RESET)
{
    SETUP;
    if (!c->s) return priv(x);
    m68k_reset_line(g);
    idle(x, 128);
    return fin(x);
}

OPF(NOP) { SETUP; return fin(x); }

OPF(STOP)
{
    SETUP;
    if (!c->s) return priv(x);
    u32 v = ext(x);
    sr_set(c, v);
    c->stopped = 1;
    return fin(x);
}

OPF(RTE)
{
    SETUP;
    if (!c->s) return priv(x);
    u32 sr = pop16(x);
    u32 to = pop32(x);
    sr_set(c, sr);
    return jump(x, to);
}

OPF(RTS) { SETUP; u32 to = pop32(x); return jump(x, to); }

OPF(TRAPV)
{
    SETUP;
    if (c->fv) return m68k_exception(x, VEC_TRAPV, x->pf, 6);
    return fin(x);
}

OPF(RTR)
{
    SETUP;
    u32 ccr = pop16(x);
    u32 to = pop32(x);
    ccr_set(c, ccr);
    return jump(x, to);
}

OPF(JSR)
{
    SETUP;
    u32 to = ea_jump(x, MODE, REG);
    if (to & 1) m68k_address_error(x, to, AE_READ | AE_INSN);
    push32(x, x->pf);
    return jump(x, to);
}

OPF(JMP) { SETUP; u32 to = ea_jump(x, MODE, REG); return jump(x, to); }

/* ADDQ SUBQ */
OPF(QUICK)
{
    SETUP;
    u32 q = op >> 9 & 7;
    if (!q) q = 8;
    int sub = op & 0x100, sz = SZ, mode = MODE;
    if (mode == 1) {
        c->a[REG] = sub ? c->a[REG] - q : c->a[REG] + q;
        idle(x, sz == 2 ? 2 : 4);
        return fin(x);
    }
    Ea e; ea_at(x, &e, mode, REG, sz, 1);
    u32 d = ea_get(x, &e, sz);
    u32 r = sub ? f_sub(c, q, d, sz, 0, 0, 1) : f_add(c, q, d, sz, 0, 0);
    if (mode == 0 && sz == 2) idle(x, 4);
    ea_put(x, &e, sz, r);
    return fin(x);
}

OPF(SCC)
{
    SETUP;
    int t = cond(c, op >> 8 & 15), mode = MODE;
    Ea e; ea_at(x, &e, mode, REG, 0, 1);
    if (mode) ea_get(x, &e, 0);
    else if (t) idle(x, 2);
    ea_put(x, &e, 0, t ? 0xFF : 0);
    return fin(x);
}

OPF(DBCC)
{
    SETUP;
    if (cond(c, op >> 8 & 15)) { idle(x, 4); ext(x); return fin(x); }
    int r = REG;
    u32 cnt = (c->d[r] - 1) & 0xFFFF;
    c->d[r] = (c->d[r] & 0xFFFF0000u) | cnt;
    u32 to = x->pf + sx16(peek_ext(x));
    idle(x, 2);
    if (cnt != 0xFFFF) return jump(x, to);
    if (x->ext) idle(x, 4); else fetchw(x, to);     /* el 68000 lee el destino aunque no salte */
    ext(x);
    return fin(x);
}

/* Bcc BRA BSR */
OPF(BCC)
{
    SETUP;
    int cc = op >> 8 & 15;
    u32 d8 = op & 0xFF;
    u32 to = pc + 2 + (d8 ? sx8(d8) : sx16(peek_ext(x)));
    if (cc == 1) {
        u32 ret = d8 ? pc + 2 : pc + 4;
        idle(x, 2);
        push32(x, ret);
        return jump(x, to);
    }
    if (cond(c, cc)) { idle(x, 2); return jump(x, to); }
    idle(x, 4);
    if (!d8) ext(x);
    return fin(x);
}

OPF(MOVEQ)
{
    SETUP;
    u32 v = sx8(op);
    c->d[op >> 9 & 7] = v;
    f_logic(c, v, 2);
    return fin(x);
}

/* OR SUB CMP EOR AND ADD entre <ea> y Dn */
OPF(ALU)
{
    SETUP;
    int grp = op >> 12, dn = op >> 9 & 7, sz = SZ & 3, mode = MODE, reg = REG;
    Ea e; ea_at(x, &e, mode, reg, sz, 1);
    u32 r;
    if (!(op & 0x100)) {
        u32 s = ea_get(x, &e, sz), d = c->d[dn] & msk(sz);
        switch (grp) {
        case 0x8: r = d | s; f_logic(c, r, sz); break;
        case 0xC: r = d & s; f_logic(c, r, sz); break;
        case 0x9: r = f_sub(c, s, d, sz, 0, 0, 1); break;
        case 0xD: r = f_add(c, s, d, sz, 0, 0); break;
        default:  f_sub(c, s, d, sz, 0, 0, 0); r = d; break;
        }
        if (sz == 2) idle(x, grp == 0xB ? 2 : (mode <= 1 || (mode == 7 && reg == 4)) ? 4 : 2);
        if (grp != 0xB) dput(c, dn, sz, r);
    } else {
        u32 s = c->d[dn] & msk(sz), d = ea_get(x, &e, sz);
        switch (grp) {
        case 0x8: r = d | s; f_logic(c, r, sz); break;
        case 0xC: r = d & s; f_logic(c, r, sz); break;
        case 0xB: r = d ^ s; f_logic(c, r, sz); break;
        case 0x9: r = f_sub(c, s, d, sz, 0, 0, 1); break;
        default:  r = f_add(c, s, d, sz, 0, 0); break;
        }
        if (mode == 0 && sz == 2) idle(x, 4);
        ea_put(x, &e, sz, r);
    }
    return fin(x);
}

/* ADDA SUBA CMPA */
OPF(ALUA)
{
    SETUP;
    int grp = op >> 12, ar = op >> 9 & 7, sz = (op & 0x100) ? 2 : 1, mode = MODE, reg = REG;
    Ea e; ea_at(x, &e, mode, reg, sz, 1);
    u32 s = ea_get(x, &e, sz);
    if (sz == 1) s = sx16(s);
    if (grp == 0xB) { f_sub(c, s, c->a[ar], 2, 0, 0, 0); idle(x, 2); }
    else {
        c->a[ar] = grp == 0xD ? c->a[ar] + s : c->a[ar] - s;
        idle(x, sz == 1 || mode <= 1 || (mode == 7 && reg == 4) ? 4 : 2);
    }
    return fin(x);
}

M68K_INL int popcount16(u32 v) { int n = 0; for (v &= 0xFFFF; v; v &= v - 1) n++; return n; }

/* MULU MULS: 38 + 2n ciclos */
OPF(MUL)
{
    SETUP;
    int dn = op >> 9 & 7;
    Ea e; ea_at(x, &e, MODE, REG, 1, 1);
    u32 s = ea_get(x, &e, 1), r;
    int n;
    if (op & 0x100) {
        r = (u32)((int32_t)(int16_t)c->d[dn] * (int32_t)(int16_t)s);
        u32 t = s << 1;
        n = popcount16(t ^ (t >> 1));
    } else {
        r = (c->d[dn] & 0xFFFF) * s;
        n = popcount16(s);
    }
    c->d[dn] = r;
    f_logic(c, r, 2);
    idle(x, 34 + 2 * (u32)n);
    return fin(x);
}

/* DIVU DIVS */
OPF(DIV)
{
    SETUP;
    int dn = op >> 9 & 7;
    Ea e; ea_at(x, &e, MODE, REG, 1, 1);
    u32 s = ea_get(x, &e, 1);
    if (s == 0) {
        /* División entre cero: N Z V C a 0 y se apila la dirección de la instrucción. Solo un caso
         * de las SingleStepTests lo cubre (DIVU 5745). */
        c->fn = c->fz = c->fv = c->fc = 0;
        return m68k_exception(x, VEC_ZERODIV, pc, 10);
    }
    u32 dd = c->d[dn];
    if (!(op & 0x100)) {
        u32 q = dd / s, rem = dd % s;
        idle(x, divu_cycles(dd, (u16)s) - 4);
        /* Desborde: V = 1, C = 0, N = 1, Z = 0. Las SingleStepTests dejan N y Z sin cambiar, pero NBA Jam
         * Tournament Edition depende de que Z quede en 0 tras un desborde (si no, escribe su EEPROM mal y
         * no la reconoce al volver a arrancar); Genesis Plus GX hace lo mismo. */
        if (q > 0xFFFF) { c->fv = 1; c->fc = 0; c->fn = 1; c->fz = 0; }
        else { c->d[dn] = rem << 16 | q; f_nz(c, q, 1); c->fv = 0; c->fc = 0; }
    } else {
        int32_t sd = (int32_t)dd;
        int16_t sv = (int16_t)s;
        int64_t q = (int64_t)sd / sv, rem = (int64_t)sd % sv;
        idle(x, divs_cycles(sd, sv, q < -32768 || q > 32767) - 4);
        if (q < -32768 || q > 32767) { c->fv = 1; c->fc = 0; c->fn = 1; c->fz = 0; }
        else { c->d[dn] = ((u32)rem & 0xFFFF) << 16 | ((u32)q & 0xFFFF); f_nz(c, (u32)q, 1); c->fv = 0; c->fc = 0; }
    }
    return fin(x);
}

/* ABCD SBCD */
OPF(BCD)
{
    SETUP;
    int rx = op >> 9 & 7, ry = REG, add = (op >> 12) == 0xC;
    u32 s, d, a = 0, r;
    if (op & 8) {
        idle(x, 2);
        c->a[ry] -= step(0, ry); s = rd(x, c->a[ry], 0);
        c->a[rx] -= step(0, rx); a = c->a[rx]; d = rd(x, a, 0);
    } else { s = c->d[ry] & 0xFF; d = c->d[rx] & 0xFF; }
    r = add ? bcd_add(c, s, d) : bcd_sub(c, s, d);
    if (op & 8) wr(x, a, 0, r);
    else { dput(c, rx, 0, r); idle(x, 2); }
    return fin(x);
}

/* -(An) de ADDX/SUBX: un long se lee en dos pasos de 2 (si la dirección es impar, falla con An - 2) */
M68K_INL u32 rd_predec(X *x, int r, int sz)
{
    M68k *c = x->c;
    if (sz == 2) {
        c->a[r] -= 2;
        if (c->a[r] & 1) m68k_address_error(x, c->a[r], AE_READ);
        c->a[r] -= 2;
        return rd(x, c->a[r], 2);
    }
    c->a[r] -= step(sz, r);
    return rd(x, c->a[r], sz);
}

/* ADDX SUBX */
OPF(ADDX)
{
    SETUP;
    int rx = op >> 9 & 7, ry = REG, sz = SZ, add = (op >> 12) == 0xD;
    u32 s, d, a = 0, r;
    if (op & 8) {
        idle(x, 2);
        s = rd_predec(x, ry, sz);
        d = rd_predec(x, rx, sz); a = c->a[rx];
    } else { s = c->d[ry] & msk(sz); d = c->d[rx] & msk(sz); }
    r = add ? f_add(c, s, d, sz, c->fx, 1) : f_sub(c, s, d, sz, c->fx, 1, 1);
    if (op & 8) wr(x, a, sz, r);
    else { dput(c, rx, sz, r); if (sz == 2) idle(x, 4); }
    return fin(x);
}

OPF(CMPM)
{
    SETUP;
    int rx = op >> 9 & 7, ry = REG, sz = SZ;
    u32 sa = c->a[ry]; c->a[ry] += step(sz, ry);
    u32 s = rd(x, sa, sz);
    u32 da = c->a[rx]; c->a[rx] += step(sz, rx);
    u32 d = rd(x, da, sz);
    f_sub(c, s, d, sz, 0, 0, 0);
    return fin(x);
}

OPF(EXG)
{
    SETUP;
    int rx = op >> 9 & 7, ry = REG, m = op >> 3 & 0x1F;
    u32 *p = m == 0x09 ? &c->a[rx] : &c->d[rx];
    u32 *q = m == 0x08 ? &c->d[ry] : &c->a[ry];
    u32 t = *p; *p = *q; *q = t;
    idle(x, 2);
    return fin(x);
}

OPF(SHIFTR)
{
    SETUP;
    int sz = SZ, r = REG, cr = op >> 9 & 7;
    int cnt = (op & 0x20) ? (int)(c->d[cr] & 63) : (cr ? cr : 8);
    u32 v = shift(c, op >> 3 & 3, op & 0x100, c->d[r] & msk(sz), cnt, sz);
    dput(c, r, sz, v);
    idle(x, (sz == 2 ? 4u : 2u) + 2u * (u32)cnt);
    return fin(x);
}

OPF(SHIFTM)
{
    SETUP;
    Ea e; ea_at(x, &e, MODE, REG, 1, 1);
    u32 v = shift(c, op >> 9 & 3, op & 0x100, ea_get(x, &e, 1), 1, 1);
    ea_put(x, &e, 1, v);
    return fin(x);
}

#undef MODE
#undef REG
#undef SZ

#endif
