/*
 * Z80: intérprete completo, con las instrucciones y banderas no documentadas (X/Y, MEMPTR/WZ, Q,
 * mitades de IX/IY, SLL, la copia del resultado de DDCB/FDCB a un registro, las banderas de las
 * instrucciones de bloque al repetir) y las interrupciones IM 0/1/2. Se comprueba con las
 * SingleStepTests del Z80 (src/tools/z80test.c).
 *
 * Los ciclos T salen de la instrucción (no se cuentan accesos al bus: en el Genesis el Z80 solo
 * espera al leer el bus del 68000, y eso lo agrega z80bus.c).
 */
#include <string.h>

#include "z80.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

enum { FC = 0x01, FN = 0x02, FP = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80 };

static u8 sz[256], szp[256];
static int tables_ready;

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        u8 s = (u8)((i & (FS | FX | FY)) | (i == 0 ? FZ : 0));
        int bits = 0;
        for (int k = 0; k < 8; k++) bits += i >> k & 1;
        sz[i] = s;
        szp[i] = (u8)(s | (bits & 1 ? 0 : FP));
    }
    tables_ready = 1;
}

/* Contexto de una instrucción */
typedef struct {
    struct Gen *g;
    Z80 *z;
    int qf;             /* la instrucción cambió las banderas */
    int p;              /* LD A,I / LD A,R */
} C;

static inline u8 rd(C *s, u16 a) { return z80_read(s->g, a); }
static inline void wr(C *s, u16 a, u8 v) { z80_write(s->g, a, v); }
static inline u8 imm8(C *s) { return rd(s, s->z->pc++); }
static inline u16 imm16(C *s) { u8 lo = imm8(s); return (u16)(lo | imm8(s) << 8); }
static inline void inc_r(Z80 *z) { z->r = (u8)((z->r & 0x80) | ((z->r + 1) & 0x7F)); }
static inline u8 m1(C *s) { inc_r(s->z); return rd(s, s->z->pc++); }
static inline void setf(C *s, u8 f) { s->z->f = f; s->qf = 1; }

static inline u16 BC(const Z80 *z) { return (u16)(z->b << 8 | z->c); }
static inline u16 DE(const Z80 *z) { return (u16)(z->d << 8 | z->e); }
static inline u16 HL(const Z80 *z) { return (u16)(z->h << 8 | z->l); }
static inline void setBC(Z80 *z, u16 v) { z->b = (u8)(v >> 8); z->c = (u8)v; }
static inline void setDE(Z80 *z, u16 v) { z->d = (u8)(v >> 8); z->e = (u8)v; }
static inline void setHL(Z80 *z, u16 v) { z->h = (u8)(v >> 8); z->l = (u8)v; }

static inline void push(C *s, u16 v) { s->z->sp--; wr(s, s->z->sp, (u8)(v >> 8)); s->z->sp--; wr(s, s->z->sp, (u8)v); }
static inline u16 pop(C *s) { u8 lo = rd(s, s->z->sp++); return (u16)(lo | rd(s, s->z->sp++) << 8); }

/* HL, IX o IY según el prefijo (idx: 0, 1, 2) */
static inline u16 getxy(const Z80 *z, int idx) { return idx == 1 ? z->ix : idx == 2 ? z->iy : HL(z); }
static inline void setxy(Z80 *z, int idx, u16 v) { if (idx == 1) z->ix = v; else if (idx == 2) z->iy = v; else setHL(z, v); }

/* Registro de 8 bits r (B C D E H L - A); con prefijo, H y L son las mitades de IX/IY */
static inline u8 get8(const Z80 *z, int r, int idx)
{
    switch (r) {
    case 0: return z->b;
    case 1: return z->c;
    case 2: return z->d;
    case 3: return z->e;
    case 4: return idx ? (u8)(getxy(z, idx) >> 8) : z->h;
    case 5: return idx ? (u8)getxy(z, idx) : z->l;
    default: return z->a;
    }
}

static inline void set8(Z80 *z, int r, int idx, u8 v)
{
    switch (r) {
    case 0: z->b = v; break;
    case 1: z->c = v; break;
    case 2: z->d = v; break;
    case 3: z->e = v; break;
    case 4: if (idx) setxy(z, idx, (u16)((getxy(z, idx) & 0x00FF) | v << 8)); else z->h = v; break;
    case 5: if (idx) setxy(z, idx, (u16)((getxy(z, idx) & 0xFF00) | v)); else z->l = v; break;
    default: z->a = v; break;
    }
}

/* Dirección de (HL) o de (IX+d) (con prefijo lee el desplazamiento y deja WZ) */
static inline u16 mem_addr(C *s, int idx)
{
    if (!idx) return HL(s->z);
    u16 a = (u16)(getxy(s->z, idx) + (int8_t)imm8(s));
    s->z->wz = a;
    return a;
}

/* --- Aritmética --- */

static void alu(C *s, int k, u8 v)
{
    Z80 *z = s->z;
    u8 a = z->a;
    int r;
    switch (k) {
    case 0: case 1: {                                   /* ADD ADC */
        int c = k == 1 ? z->f & FC : 0;
        r = a + v + c;
        setf(s, (u8)((sz[r & 0xFF]) | ((r >> 8) & FC) | ((a ^ v ^ r) & FH) | ((((a ^ ~v) & (a ^ r)) & 0x80) ? FP : 0)));
        z->a = (u8)r;
        break;
    }
    case 2: case 3: case 7: {                           /* SUB SBC CP */
        int c = k == 3 ? z->f & FC : 0;
        r = a - v - c;
        u8 f = (u8)((sz[r & 0xFF] & ~(FX | FY)) | FN | ((r >> 8) & FC) | ((a ^ v ^ r) & FH) | ((((a ^ v) & (a ^ r)) & 0x80) ? FP : 0));
        f |= (u8)((k == 7 ? v : r) & (FX | FY));        /* CP: X e Y del operando */
        setf(s, f);
        if (k != 7) z->a = (u8)r;
        break;
    }
    case 4: z->a = a & v; setf(s, (u8)(szp[z->a] | FH)); break;
    case 5: z->a = a ^ v; setf(s, szp[z->a]); break;
    default: z->a = a | v; setf(s, szp[z->a]); break;
    }
}

static u8 inc8(C *s, u8 v)
{
    u8 r = (u8)(v + 1);
    setf(s, (u8)((s->z->f & FC) | sz[r] | ((r & 0x0F) == 0 ? FH : 0) | (r == 0x80 ? FP : 0)));
    return r;
}

static u8 dec8(C *s, u8 v)
{
    u8 r = (u8)(v - 1);
    setf(s, (u8)((s->z->f & FC) | FN | sz[r] | ((v & 0x0F) == 0 ? FH : 0) | (v == 0x80 ? FP : 0)));
    return r;
}

static u16 add16(C *s, u16 a, u16 b)
{
    u32 r = (u32)a + b;
    s->z->wz = (u16)(a + 1);
    setf(s, (u8)((s->z->f & (FS | FZ | FP)) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) | ((r >> 8) & (FX | FY))));
    return (u16)r;
}

static u16 adc16(C *s, u16 a, u16 b, int sub)
{
    int c = s->z->f & FC;
    u32 r = sub ? (u32)a - b - (u32)c : (u32)a + b + (u32)c;
    u16 r16 = (u16)r;
    u8 f = (u8)(((r16 >> 8) & (FS | FX | FY)) | (r16 == 0 ? FZ : 0) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH));
    if (sub) f |= (u8)(FN | ((((a ^ b) & (a ^ r)) & 0x8000) ? FP : 0));
    else f |= (u8)((((a ^ ~b) & (a ^ r)) & 0x8000) ? FP : 0);
    s->z->wz = (u16)(a + 1);
    setf(s, f);
    return r16;
}

/* Rotaciones y desplazamientos del prefijo CB (k: RLC RRC RL RR SLA SRA SLL SRL) */
static u8 rot(C *s, int k, u8 v)
{
    u8 c, r;
    switch (k) {
    case 0: c = v >> 7; r = (u8)(v << 1 | c); break;
    case 1: c = v & 1; r = (u8)(v >> 1 | c << 7); break;
    case 2: c = v >> 7; r = (u8)(v << 1 | (s->z->f & FC)); break;
    case 3: c = v & 1; r = (u8)(v >> 1 | (s->z->f & FC) << 7); break;
    case 4: c = v >> 7; r = (u8)(v << 1); break;
    case 5: c = v & 1; r = (u8)((v >> 1) | (v & 0x80)); break;
    case 6: c = v >> 7; r = (u8)(v << 1 | 1); break;
    default: c = v & 1; r = (u8)(v >> 1); break;
    }
    setf(s, (u8)(szp[r] | c));
    return r;
}

static void bit(C *s, int b, u8 v, u8 xy)
{
    u8 f = (u8)((s->z->f & FC) | FH | (xy & (FX | FY)));
    if (!(v & (1 << b))) f |= FZ | FP;
    else if (b == 7) f |= FS;
    setf(s, f);
}

static void daa(C *s)
{
    Z80 *z = s->z;
    u8 a = z->a, corr = 0, c = z->f & FC, h;
    if ((z->f & FH) || (a & 0x0F) > 9) corr |= 0x06;
    if (c || a > 0x99) { corr |= 0x60; c = FC; }
    if (z->f & FN) { h = (z->f & FH) && (a & 0x0F) < 6; a = (u8)(a - corr); }
    else { h = (a & 0x0F) > 9; a = (u8)(a + corr); }
    z->a = a;
    setf(s, (u8)(szp[a] | (z->f & FN) | c | (h ? FH : 0)));
}

static inline int cond(const Z80 *z, int cc)
{
    switch (cc) {
    case 0: return !(z->f & FZ);
    case 1: return z->f & FZ;
    case 2: return !(z->f & FC);
    case 3: return z->f & FC;
    case 4: return !(z->f & FP);
    case 5: return z->f & FP;
    case 6: return !(z->f & FS);
    default: return z->f & FS;
    }
}

/* --- Prefijo CB (y DDCB/FDCB) --- */

static int exec_cb(C *s)
{
    Z80 *z = s->z;
    u8 op = m1(s);
    int r = op & 7, b = op >> 3 & 7;
    if (r == 6) {
        u16 a = HL(z);
        u8 v = rd(s, a);
        if (op < 0x40) { wr(s, a, rot(s, b, v)); return 15; }
        if (op < 0x80) { bit(s, b, v, (u8)(z->wz >> 8)); return 12; }
        wr(s, a, op < 0xC0 ? (u8)(v & ~(1 << b)) : (u8)(v | 1 << b));
        return 15;
    }
    u8 v = get8(z, r, 0);
    if (op < 0x40) set8(z, r, 0, rot(s, b, v));
    else if (op < 0x80) bit(s, b, v, v);
    else set8(z, r, 0, op < 0xC0 ? (u8)(v & ~(1 << b)) : (u8)(v | 1 << b));
    return 8;
}

/* DD CB d op / FD CB d op: sobre (IX+d); el resultado también va al registro r si r != 6 */
static int exec_xycb(C *s, int idx)
{
    Z80 *z = s->z;
    u16 a = (u16)(getxy(z, idx) + (int8_t)imm8(s));
    u8 op = imm8(s);
    int r = op & 7, b = op >> 3 & 7;
    z->wz = a;
    u8 v = rd(s, a), res;
    if (op >= 0x40 && op < 0x80) { bit(s, b, v, (u8)(a >> 8)); return 20; }
    if (op < 0x40) res = rot(s, b, v);
    else res = op < 0xC0 ? (u8)(v & ~(1 << b)) : (u8)(v | 1 << b);
    wr(s, a, res);
    if (r != 6) set8(z, r, 0, res);
    return 23;
}

/* --- Prefijo ED --- */

static int io_flags_repeat(C *s, u8 v)
{
    /* Banderas de INIR/INDR/OTIR/OTDR cuando repiten (se interrumpen entre vueltas) */
    Z80 *z = s->z;
    u8 f = (u8)((z->f & ~(FX | FY)) | ((z->pc >> 8) & (FX | FY)));
    if (f & FC) {
        f &= (u8)~FH;
        if (v & 0x80) {
            f ^= (u8)((szp[(z->b - 1) & 7] ^ FP) & FP);
            if ((z->b & 0x0F) == 0x00) f |= FH;
        } else {
            f ^= (u8)((szp[(z->b + 1) & 7] ^ FP) & FP);
            if ((z->b & 0x0F) == 0x0F) f |= FH;
        }
    } else f ^= (u8)((szp[z->b & 7] ^ FP) & FP);
    setf(s, f);
    return 0;
}

static int exec_ed(C *s)
{
    Z80 *z = s->z;
    u8 op = m1(s);
    if (op >= 0x40 && op < 0x80) {
        int r = op >> 3 & 7;
        u16 rr = 0;
        switch (op >> 4 & 3) { case 0: rr = BC(z); break; case 1: rr = DE(z); break; case 2: rr = HL(z); break; default: rr = z->sp; }
        switch (op & 7) {
        case 0: {                                       /* IN r,(C) */
            u8 v = z80_in(s->g, BC(z));
            z->wz = (u16)(BC(z) + 1);
            if (r != 6) set8(z, r, 0, v);
            setf(s, (u8)((z->f & FC) | szp[v]));
            return 12;
        }
        case 1:                                         /* OUT (C),r */
            z80_out(s->g, BC(z), r == 6 ? 0 : get8(z, r, 0));
            z->wz = (u16)(BC(z) + 1);
            return 12;
        case 2: {                                       /* SBC/ADC HL,rr */
            u16 v = adc16(s, HL(z), rr, !(op & 8));
            setHL(z, v);
            return 15;
        }
        case 3: {                                       /* LD (nn),rr / LD rr,(nn) */
            u16 nn = imm16(s);
            if (op & 8) {
                u8 lo = rd(s, nn); u16 v = (u16)(lo | rd(s, (u16)(nn + 1)) << 8);
                switch (op >> 4 & 3) { case 0: setBC(z, v); break; case 1: setDE(z, v); break; case 2: setHL(z, v); break; default: z->sp = v; }
            } else { wr(s, nn, (u8)rr); wr(s, (u16)(nn + 1), (u8)(rr >> 8)); }
            z->wz = (u16)(nn + 1);
            return 20;
        }
        case 4: { u8 v = z->a; z->a = 0; alu(s, 2, v); return 8; }          /* NEG */
        case 5: z->pc = pop(s); z->wz = z->pc; z->iff1 = z->iff2; return 14;    /* RETN / RETI */
        case 6: z->im = (op & 0x18) == 0x10 ? 1 : (op & 0x18) == 0x18 ? 2 : 0; return 8;
        default:
            switch (op) {
            case 0x47: z->i = z->a; return 9;
            case 0x4F: z->r = z->a; return 9;
            case 0x57: case 0x5F:
                z->a = op == 0x57 ? z->i : z->r;
                setf(s, (u8)((z->f & FC) | sz[z->a] | (z->iff2 ? FP : 0)));
                s->p = 1;
                return 9;
            case 0x67: case 0x6F: {                     /* RRD / RLD */
                u16 a = HL(z);
                u8 v = rd(s, a);
                if (op == 0x67) { wr(s, a, (u8)(z->a << 4 | v >> 4)); z->a = (u8)((z->a & 0xF0) | (v & 0x0F)); }
                else { wr(s, a, (u8)(v << 4 | (z->a & 0x0F))); z->a = (u8)((z->a & 0xF0) | (v >> 4)); }
                z->wz = (u16)(a + 1);
                setf(s, (u8)((z->f & FC) | szp[z->a]));
                return 18;
            }
            default: return 8;
            }
        }
    }
    if (op >= 0xA0 && op <= 0xBB && (op & 7) <= 3 && (op & 0x04) == 0) {
        int dec = op & 8, rep = op & 0x10, d = dec ? -1 : 1;
        switch (op & 3) {
        case 0: {                                       /* LDI LDD LDIR LDDR */
            u8 v = rd(s, HL(z));
            wr(s, DE(z), v);
            setHL(z, (u16)(HL(z) + d)); setDE(z, (u16)(DE(z) + d)); setBC(z, (u16)(BC(z) - 1));
            u8 n = (u8)(v + z->a);
            setf(s, (u8)((z->f & (FS | FZ | FC)) | (BC(z) ? FP : 0) | (n & FX) | ((n & 2) << 4)));
            if (rep && BC(z)) {
                z->pc = (u16)(z->pc - 2); z->wz = (u16)(z->pc + 1);
                setf(s, (u8)((z->f & ~(FX | FY)) | ((z->pc >> 8) & (FX | FY))));
                return 21;
            }
            return 16;
        }
        case 1: {                                       /* CPI CPD CPIR CPDR */
            u8 v = rd(s, HL(z));
            u8 r = (u8)(z->a - v), hc = (u8)((z->a ^ v ^ r) & FH);
            setHL(z, (u16)(HL(z) + d)); setBC(z, (u16)(BC(z) - 1));
            z->wz = (u16)(z->wz + d);
            u8 n = (u8)(r - (hc ? 1 : 0));
            setf(s, (u8)((z->f & FC) | FN | hc | (BC(z) ? FP : 0) | (r & FS) | (r ? 0 : FZ) | (n & FX) | ((n & 2) << 4)));
            if (rep && BC(z) && r) {
                z->pc = (u16)(z->pc - 2); z->wz = (u16)(z->pc + 1);
                setf(s, (u8)((z->f & ~(FX | FY)) | ((z->pc >> 8) & (FX | FY))));
                return 21;
            }
            return 16;
        }
        case 2: {                                       /* INI IND INIR INDR */
            u8 v = z80_in(s->g, BC(z));
            z->wz = (u16)(BC(z) + d);
            z->b--;
            wr(s, HL(z), v);
            setHL(z, (u16)(HL(z) + d));
            int k = v + (u8)(z->c + d);
            setf(s, (u8)(sz[z->b] | (v & 0x80 ? FN : 0) | (k > 255 ? FH | FC : 0) | (szp[(k & 7) ^ z->b] & FP)));
            if (rep && z->b) { z->pc = (u16)(z->pc - 2); z->wz = (u16)(z->pc + 1); io_flags_repeat(s, v); return 21; }
            return 16;
        }
        default: {                                      /* OUTI OUTD OTIR OTDR */
            u8 v = rd(s, HL(z));
            z->b--;
            z->wz = (u16)(BC(z) + d);
            z80_out(s->g, BC(z), v);
            setHL(z, (u16)(HL(z) + d));
            int k = v + z->l;
            setf(s, (u8)(sz[z->b] | (v & 0x80 ? FN : 0) | (k > 255 ? FH | FC : 0) | (szp[(k & 7) ^ z->b] & FP)));
            if (rep && z->b) { z->pc = (u16)(z->pc - 2); z->wz = (u16)(z->pc + 1); io_flags_repeat(s, v); return 21; }
            return 16;
        }
        }
    }
    return 8;                                           /* ED sin definir: como dos NOP */
}

/* --- Sin prefijo, o con DD/FD (idx: 0 HL, 1 IX, 2 IY) --- */

static const u8 cyc_main[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11,
};

static int exec_main(C *s, u8 op, int idx)
{
    Z80 *z = s->z;
    int t = cyc_main[op] + (idx ? 4 : 0);

    if (op >= 0x40 && op < 0x80) {                     /* LD r,r' / HALT */
        int dst = op >> 3 & 7, src = op & 7;
        if (op == 0x76) { z->halted = 1; return t; }
        if (src == 6) { u16 a = mem_addr(s, idx); set8(z, dst, 0, rd(s, a)); return t + (idx ? 8 : 0); }
        if (dst == 6) { u16 a = mem_addr(s, idx); wr(s, a, get8(z, src, 0)); return t + (idx ? 8 : 0); }
        set8(z, dst, idx, get8(z, src, idx));
        return t;
    }
    if (op >= 0x80 && op < 0xC0) {                     /* ALU A,r */
        int src = op & 7;
        if (src == 6) { u16 a = mem_addr(s, idx); alu(s, op >> 3 & 7, rd(s, a)); return t + (idx ? 8 : 0); }
        alu(s, op >> 3 & 7, get8(z, src, idx));
        return t;
    }

    switch (op) {
    case 0x00: return t;
    case 0x01: setBC(z, imm16(s)); return t;
    case 0x11: setDE(z, imm16(s)); return t;
    case 0x21: setxy(z, idx, imm16(s)); return t;
    case 0x31: z->sp = imm16(s); return t;
    case 0x02: wr(s, BC(z), z->a); z->wz = (u16)(((BC(z) + 1) & 0xFF) | z->a << 8); return t;
    case 0x12: wr(s, DE(z), z->a); z->wz = (u16)(((DE(z) + 1) & 0xFF) | z->a << 8); return t;
    case 0x0A: z->a = rd(s, BC(z)); z->wz = (u16)(BC(z) + 1); return t;
    case 0x1A: z->a = rd(s, DE(z)); z->wz = (u16)(DE(z) + 1); return t;
    case 0x22: { u16 nn = imm16(s), v = getxy(z, idx); wr(s, nn, (u8)v); wr(s, (u16)(nn + 1), (u8)(v >> 8)); z->wz = (u16)(nn + 1); return t; }
    case 0x2A: { u16 nn = imm16(s); u8 lo = rd(s, nn); setxy(z, idx, (u16)(lo | rd(s, (u16)(nn + 1)) << 8)); z->wz = (u16)(nn + 1); return t; }
    case 0x32: { u16 nn = imm16(s); wr(s, nn, z->a); z->wz = (u16)(((nn + 1) & 0xFF) | z->a << 8); return t; }
    case 0x3A: { u16 nn = imm16(s); z->a = rd(s, nn); z->wz = (u16)(nn + 1); return t; }
    case 0x03: setBC(z, (u16)(BC(z) + 1)); return t;
    case 0x13: setDE(z, (u16)(DE(z) + 1)); return t;
    case 0x23: setxy(z, idx, (u16)(getxy(z, idx) + 1)); return t;
    case 0x33: z->sp++; return t;
    case 0x0B: setBC(z, (u16)(BC(z) - 1)); return t;
    case 0x1B: setDE(z, (u16)(DE(z) - 1)); return t;
    case 0x2B: setxy(z, idx, (u16)(getxy(z, idx) - 1)); return t;
    case 0x3B: z->sp--; return t;
    case 0x09: setxy(z, idx, add16(s, getxy(z, idx), BC(z))); return t;
    case 0x19: setxy(z, idx, add16(s, getxy(z, idx), DE(z))); return t;
    case 0x29: { u16 v = getxy(z, idx); setxy(z, idx, add16(s, v, v)); return t; }
    case 0x39: setxy(z, idx, add16(s, getxy(z, idx), z->sp)); return t;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
        int r = op >> 3 & 7; set8(z, r, idx, inc8(s, get8(z, r, idx))); return t;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
        int r = op >> 3 & 7; set8(z, r, idx, dec8(s, get8(z, r, idx))); return t;
    }
    case 0x34: { u16 a = mem_addr(s, idx); wr(s, a, inc8(s, rd(s, a))); return t + (idx ? 8 : 0); }
    case 0x35: { u16 a = mem_addr(s, idx); wr(s, a, dec8(s, rd(s, a))); return t + (idx ? 8 : 0); }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:
        set8(z, op >> 3 & 7, idx, imm8(s)); return t;
    case 0x36: { u16 a = mem_addr(s, idx); wr(s, a, imm8(s)); return t + (idx ? 5 : 0); }
    case 0x07: { u8 c = z->a >> 7; z->a = (u8)(z->a << 1 | c); setf(s, (u8)((z->f & (FS | FZ | FP)) | (z->a & (FX | FY)) | c)); return t; }
    case 0x0F: { u8 c = z->a & 1; z->a = (u8)(z->a >> 1 | c << 7); setf(s, (u8)((z->f & (FS | FZ | FP)) | (z->a & (FX | FY)) | c)); return t; }
    case 0x17: { u8 c = z->a >> 7; z->a = (u8)(z->a << 1 | (z->f & FC)); setf(s, (u8)((z->f & (FS | FZ | FP)) | (z->a & (FX | FY)) | c)); return t; }
    case 0x1F: { u8 c = z->a & 1; z->a = (u8)(z->a >> 1 | (z->f & FC) << 7); setf(s, (u8)((z->f & (FS | FZ | FP)) | (z->a & (FX | FY)) | c)); return t; }
    case 0x08: { u16 af = (u16)(z->a << 8 | z->f); z->a = (u8)(z->af_ >> 8); z->f = (u8)z->af_; z->af_ = af; return t; }
    case 0x10: {                                        /* DJNZ */
        int8_t e = (int8_t)imm8(s);
        if (--z->b) { z->pc = (u16)(z->pc + e); z->wz = z->pc; return t + 5; }
        return t;
    }
    case 0x18: { int8_t e = (int8_t)imm8(s); z->pc = (u16)(z->pc + e); z->wz = z->pc; return t; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        int8_t e = (int8_t)imm8(s);
        if (cond(z, op >> 3 & 3)) { z->pc = (u16)(z->pc + e); z->wz = z->pc; return t + 5; }
        return t;
    }
    case 0x27: daa(s); return t;
    case 0x2F: z->a = (u8)~z->a; setf(s, (u8)((z->f & (FS | FZ | FP | FC)) | FH | FN | (z->a & (FX | FY)))); return t;
    case 0x37: setf(s, (u8)((z->f & (FS | FZ | FP)) | FC | (((z->q ^ z->f) | z->a) & (FX | FY)))); return t;
    case 0x3F: setf(s, (u8)((z->f & (FS | FZ | FP)) | ((z->f & FC) ? FH : FC) | (((z->q ^ z->f) | z->a) & (FX | FY)))); return t;

    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond(z, op >> 3 & 7)) { z->pc = pop(s); z->wz = z->pc; return t + 6; }
        return t;
    case 0xC9: z->pc = pop(s); z->wz = z->pc; return t;
    case 0xC1: setBC(z, pop(s)); return t;
    case 0xD1: setDE(z, pop(s)); return t;
    case 0xE1: setxy(z, idx, pop(s)); return t;
    case 0xF1: { u16 v = pop(s); z->a = (u8)(v >> 8); z->f = (u8)v; return t; }
    case 0xC5: push(s, BC(z)); return t;
    case 0xD5: push(s, DE(z)); return t;
    case 0xE5: push(s, getxy(z, idx)); return t;
    case 0xF5: push(s, (u16)(z->a << 8 | z->f)); return t;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        u16 nn = imm16(s); z->wz = nn;
        if (cond(z, op >> 3 & 7)) z->pc = nn;
        return t;
    }
    case 0xC3: { u16 nn = imm16(s); z->pc = nn; z->wz = nn; return t; }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        u16 nn = imm16(s); z->wz = nn;
        if (cond(z, op >> 3 & 7)) { push(s, z->pc); z->pc = nn; return t + 7; }
        return t;
    }
    case 0xCD: { u16 nn = imm16(s); push(s, z->pc); z->pc = nn; z->wz = nn; return t; }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(s, op >> 3 & 7, imm8(s)); return t;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(s, z->pc); z->pc = op & 0x38; z->wz = z->pc; return t;
    case 0xD3: { u8 n = imm8(s); z80_out(s->g, (u16)(z->a << 8 | n), z->a); z->wz = (u16)(((n + 1) & 0xFF) | z->a << 8); return t; }
    case 0xDB: { u8 n = imm8(s); u16 port = (u16)(z->a << 8 | n); z->a = z80_in(s->g, port); z->wz = (u16)(port + 1); return t; }
    case 0xD9: {                                        /* EXX */
        u16 v;
        v = BC(z); setBC(z, z->bc_); z->bc_ = v;
        v = DE(z); setDE(z, z->de_); z->de_ = v;
        v = HL(z); setHL(z, z->hl_); z->hl_ = v;
        return t;
    }
    case 0xE3: {                                        /* EX (SP),HL */
        u8 lo = rd(s, z->sp), hi = rd(s, (u16)(z->sp + 1));
        u16 v = getxy(z, idx);
        wr(s, z->sp, (u8)v); wr(s, (u16)(z->sp + 1), (u8)(v >> 8));
        setxy(z, idx, (u16)(lo | hi << 8));
        z->wz = (u16)(lo | hi << 8);
        return t;
    }
    case 0xE9: z->pc = getxy(z, idx); return t;
    case 0xEB: { u16 v = DE(z); setDE(z, HL(z)); setHL(z, v); return t; }
    case 0xF3: z->iff1 = z->iff2 = 0; return t;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei = 1; return t;
    case 0xF9: z->sp = getxy(z, idx); return t;
    default: return t;
    }
}

void z80_reset(Z80 *z)
{
    memset(z, 0, sizeof *z);
    z->a = z->f = 0xFF;
    z->sp = 0xFFFF;
}

int z80_step(struct Gen *g, Z80 *z)
{
    if (!tables_ready) init_tables();
    C s = { g, z, 0, 0 };
    int ei_was = z->ei;
    z->ei = 0;
    int t;
    if (z->irq && z->iff1 && !ei_was) {
        /* Interrupción: en el Genesis el bus de datos lee $FF (IM 0 ejecuta RST 38h) */
        if (z->halted) { z->halted = 0; z->pc++; }
        z->iff1 = z->iff2 = 0;
        inc_r(z);
        push(&s, z->pc);
        if (z->im == 2) {
            u16 v = (u16)(z->i << 8 | 0xFF);
            u8 lo = rd(&s, v);
            z->pc = (u16)(lo | rd(&s, (u16)(v + 1)) << 8);
            t = 19;
        } else { z->pc = 0x38; t = 13; }
        z->wz = z->pc;
        z->q = 0;
        z->p = 0;
        return t;
    }
    if (z->halted) { inc_r(z); z->q = 0; z->p = 0; return 4; }
    u8 op = m1(&s);
    switch (op) {
    case 0xCB: t = exec_cb(&s); break;
    case 0xED: t = exec_ed(&s); break;
    case 0xDD: case 0xFD: {
        int idx = op == 0xDD ? 1 : 2;
        u8 op2 = rd(&s, z->pc);
        if (op2 == 0xDD || op2 == 0xFD || op2 == 0xED) { t = 4; break; }   /* el prefijo solo cuenta como NOP */
        if (op2 == 0xCB) { z->pc++; inc_r(z); t = exec_xycb(&s, idx); break; }
        op2 = m1(&s);
        z->q = 0;                       /* el prefijo no cambia las banderas (SCF/CCF lo ven) */
        t = exec_main(&s, op2, idx);
        break;
    }
    default: t = exec_main(&s, op, 0); break;
    }
    z->q = s.qf ? z->f : 0;
    z->p = (u8)s.p;
    return t;
}
