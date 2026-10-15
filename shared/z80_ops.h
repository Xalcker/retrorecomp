/*
 * Z80: semántica de cada instrucción. La única fuente de verdad del CPU, como m68k_ops.h para el 68000:
 * la usan el intérprete (z80.c) y el código que genera un recompilador (SMSRecomp), así que no pueden
 * discrepar ni en lo que hace cada instrucción ni en sus ciclos. Común a GENRecomp, NEORecomp y SMSRecomp
 * (retrorecomp/shared).
 *
 * Todo pasa por un contexto (Z80X) con el PC de la instrucción y, para el código generado, sus bytes:
 * - El intérprete (z80_step) lo arma con ops = NULL: el opcode y los operandos se leen del bus.
 * - El código generado lo arma con ops apuntando a los bytes de la instrucción (leídos de la ROM al
 *   recompilar, en un static const) y el PC constante: el compilador pliega los switch y queda la
 *   instrucción sola, con el PC siguiente como constante. ops lleva siempre 4 bytes (los que siguen a la
 *   instrucción también: un prefijo DD/FD suelto mira el byte siguiente).
 * z80_exec ejecuta una instrucción (sin interrupciones: esas las atiende z80_step), deja en x->pc la
 * dirección de la siguiente y devuelve sus ciclos T. R sube con cada M1 por los dos caminos; WZ (MEMPTR),
 * Q, P y el retraso de EI quedan en el Z80 igual que antes.
 *
 * Lo que da la consola, además del bus de z80.h:
 * - Z80_IO_AT(g, t), opcional: lo llama cada IN y OUT justo antes de acceder al puerto, con t = los ciclos
 *   desde el comienzo de la instrucción hasta el del acceso (el índice del ciclo marcado como E/S en las
 *   SingleStepTests: 9 en IN/OUT (n), 13 con un prefijo DD/FD delante; 10 en IN/OUT (C); 11 en INI, IND,
 *   INIR e INDR; 14 en OUTI, OUTD, OTIR y OTDR; z80test --recomp lo comprueba). El Master System lo usa
 *   para los contadores y las interrupciones del VDP; sin definirlo no hace nada.
 */
#ifndef Z80_OPS_H
#define Z80_OPS_H

#include "z80.h"

#if defined(__GNUC__) || defined(__clang__)
#define Z80_INL static inline __attribute__((always_inline))
#else
#define Z80_INL static inline
#endif

#ifndef Z80_IO_AT
#define Z80_IO_AT(g, t) ((void)0)
#endif

enum { Z80F_C = 0x01, Z80F_N = 0x02, Z80F_P = 0x04, Z80F_X = 0x08, Z80F_H = 0x10, Z80F_Y = 0x20, Z80F_Z = 0x40, Z80F_S = 0x80 };

/* S, Z, X e Y de cada resultado de 8 bits (z80_sz), y además la paridad en P (z80_szp) */
static const uint8_t z80_sz[256] = {
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8,
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8,
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8
};
static const uint8_t z80_szp[256] = {
    0x44, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x08, 0x0C, 0x0C, 0x08, 0x0C, 0x08, 0x08, 0x0C,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0C, 0x08, 0x08, 0x0C, 0x08, 0x0C, 0x0C, 0x08,
    0x20, 0x24, 0x24, 0x20, 0x24, 0x20, 0x20, 0x24, 0x2C, 0x28, 0x28, 0x2C, 0x28, 0x2C, 0x2C, 0x28,
    0x24, 0x20, 0x20, 0x24, 0x20, 0x24, 0x24, 0x20, 0x28, 0x2C, 0x2C, 0x28, 0x2C, 0x28, 0x28, 0x2C,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0C, 0x08, 0x08, 0x0C, 0x08, 0x0C, 0x0C, 0x08,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x08, 0x0C, 0x0C, 0x08, 0x0C, 0x08, 0x08, 0x0C,
    0x24, 0x20, 0x20, 0x24, 0x20, 0x24, 0x24, 0x20, 0x28, 0x2C, 0x2C, 0x28, 0x2C, 0x28, 0x28, 0x2C,
    0x20, 0x24, 0x24, 0x20, 0x24, 0x20, 0x20, 0x24, 0x2C, 0x28, 0x28, 0x2C, 0x28, 0x2C, 0x2C, 0x28,
    0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x8C, 0x88, 0x88, 0x8C, 0x88, 0x8C, 0x8C, 0x88,
    0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x88, 0x8C, 0x8C, 0x88, 0x8C, 0x88, 0x88, 0x8C,
    0xA4, 0xA0, 0xA0, 0xA4, 0xA0, 0xA4, 0xA4, 0xA0, 0xA8, 0xAC, 0xAC, 0xA8, 0xAC, 0xA8, 0xA8, 0xAC,
    0xA0, 0xA4, 0xA4, 0xA0, 0xA4, 0xA0, 0xA0, 0xA4, 0xAC, 0xA8, 0xA8, 0xAC, 0xA8, 0xAC, 0xAC, 0xA8,
    0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x88, 0x8C, 0x8C, 0x88, 0x8C, 0x88, 0x88, 0x8C,
    0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x8C, 0x88, 0x88, 0x8C, 0x88, 0x8C, 0x8C, 0x88,
    0xA0, 0xA4, 0xA4, 0xA0, 0xA4, 0xA0, 0xA0, 0xA4, 0xAC, 0xA8, 0xA8, 0xAC, 0xA8, 0xAC, 0xAC, 0xA8,
    0xA4, 0xA0, 0xA0, 0xA4, 0xA0, 0xA4, 0xA4, 0xA0, 0xA8, 0xAC, 0xAC, 0xA8, 0xAC, 0xA8, 0xA8, 0xAC
};

/* Contexto de una instrucción */
typedef struct {
    struct Z80_HOST *g;
    Z80 *z;
    const uint8_t *ops;     /* NULL: el intérprete (lee del bus); si no, los bytes de la instrucción (código generado) */
    unsigned oi;            /* siguiente byte de ops */
    uint16_t pc;            /* el siguiente byte que se lee; al terminar, la siguiente instrucción */
    int qf;                 /* la instrucción cambió las banderas */
    int p;                  /* LD A,I / LD A,R */
} Z80X;

Z80_INL uint8_t z_rd(Z80X *s, uint16_t a) { return z80_read(s->g, a); }
Z80_INL void z_wr(Z80X *s, uint16_t a, uint8_t v) { z80_write(s->g, a, v); }
Z80_INL uint8_t z_imm8(Z80X *s) { uint8_t v = s->ops ? s->ops[s->oi++] : z_rd(s, s->pc); s->pc++; return v; }
Z80_INL uint16_t z_imm16(Z80X *s) { uint8_t lo = z_imm8(s); return (uint16_t)(lo | z_imm8(s) << 8); }
Z80_INL void z_inc_r(Z80 *z) { z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F)); }
Z80_INL uint8_t z_m1(Z80X *s) { z_inc_r(s->z); return z_imm8(s); }
Z80_INL void z_setf(Z80X *s, uint8_t f) { s->z->f = f; s->qf = 1; }

Z80_INL uint16_t z_BC(const Z80 *z) { return (uint16_t)(z->b << 8 | z->c); }
Z80_INL uint16_t z_DE(const Z80 *z) { return (uint16_t)(z->d << 8 | z->e); }
Z80_INL uint16_t z_HL(const Z80 *z) { return (uint16_t)(z->h << 8 | z->l); }
Z80_INL void z_setBC(Z80 *z, uint16_t v) { z->b = (uint8_t)(v >> 8); z->c = (uint8_t)v; }
Z80_INL void z_setDE(Z80 *z, uint16_t v) { z->d = (uint8_t)(v >> 8); z->e = (uint8_t)v; }
Z80_INL void z_setHL(Z80 *z, uint16_t v) { z->h = (uint8_t)(v >> 8); z->l = (uint8_t)v; }

Z80_INL void z_push(Z80X *s, uint16_t v) { s->z->sp--; z_wr(s, s->z->sp, (uint8_t)(v >> 8)); s->z->sp--; z_wr(s, s->z->sp, (uint8_t)v); }
Z80_INL uint16_t z_pop(Z80X *s) { uint8_t lo = z_rd(s, s->z->sp++); return (uint16_t)(lo | z_rd(s, s->z->sp++) << 8); }

/* HL, IX o IY según el prefijo (idx: 0, 1, 2) */
Z80_INL uint16_t z_getxy(const Z80 *z, int idx) { return idx == 1 ? z->ix : idx == 2 ? z->iy : z_HL(z); }
Z80_INL void z_setxy(Z80 *z, int idx, uint16_t v) { if (idx == 1) z->ix = v; else if (idx == 2) z->iy = v; else z_setHL(z, v); }

/* Registro de 8 bits r (B C D E H L - A); con prefijo, H y L son las mitades de IX/IY */
Z80_INL uint8_t z_get8(const Z80 *z, int r, int idx)
{
    switch (r) {
    case 0: return z->b;
    case 1: return z->c;
    case 2: return z->d;
    case 3: return z->e;
    case 4: return idx ? (uint8_t)(z_getxy(z, idx) >> 8) : z->h;
    case 5: return idx ? (uint8_t)z_getxy(z, idx) : z->l;
    default: return z->a;
    }
}

Z80_INL void z_set8(Z80 *z, int r, int idx, uint8_t v)
{
    switch (r) {
    case 0: z->b = v; break;
    case 1: z->c = v; break;
    case 2: z->d = v; break;
    case 3: z->e = v; break;
    case 4: if (idx) z_setxy(z, idx, (uint16_t)((z_getxy(z, idx) & 0x00FF) | v << 8)); else z->h = v; break;
    case 5: if (idx) z_setxy(z, idx, (uint16_t)((z_getxy(z, idx) & 0xFF00) | v)); else z->l = v; break;
    default: z->a = v; break;
    }
}

/* Dirección de (HL) o de (IX+d) (con prefijo lee el desplazamiento y deja WZ) */
Z80_INL uint16_t z_mem_addr(Z80X *s, int idx)
{
    if (!idx) return z_HL(s->z);
    uint16_t a = (uint16_t)(z_getxy(s->z, idx) + (int8_t)z_imm8(s));
    s->z->wz = a;
    return a;
}

/* --- Aritmética --- */

Z80_INL void z_alu(Z80X *s, int k, uint8_t v)
{
    Z80 *z = s->z;
    uint8_t a = z->a;
    int r;
    switch (k) {
    case 0: case 1: {                                   /* ADD ADC */
        int c = k == 1 ? z->f & Z80F_C : 0;
        r = a + v + c;
        z_setf(s, (uint8_t)((z80_sz[r & 0xFF]) | ((r >> 8) & Z80F_C) | ((a ^ v ^ r) & Z80F_H) | ((((a ^ ~v) & (a ^ r)) & 0x80) ? Z80F_P : 0)));
        z->a = (uint8_t)r;
        break;
    }
    case 2: case 3: case 7: {                           /* SUB SBC CP */
        int c = k == 3 ? z->f & Z80F_C : 0;
        r = a - v - c;
        uint8_t f = (uint8_t)((z80_sz[r & 0xFF] & ~(Z80F_X | Z80F_Y)) | Z80F_N | ((r >> 8) & Z80F_C) | ((a ^ v ^ r) & Z80F_H) | ((((a ^ v) & (a ^ r)) & 0x80) ? Z80F_P : 0));
        f |= (uint8_t)((k == 7 ? v : r) & (Z80F_X | Z80F_Y));        /* CP: X e Y del operando */
        z_setf(s, f);
        if (k != 7) z->a = (uint8_t)r;
        break;
    }
    case 4: z->a = a & v; z_setf(s, (uint8_t)(z80_szp[z->a] | Z80F_H)); break;
    case 5: z->a = a ^ v; z_setf(s, z80_szp[z->a]); break;
    default: z->a = a | v; z_setf(s, z80_szp[z->a]); break;
    }
}

Z80_INL uint8_t z_inc8(Z80X *s, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    z_setf(s, (uint8_t)((s->z->f & Z80F_C) | z80_sz[r] | ((r & 0x0F) == 0 ? Z80F_H : 0) | (r == 0x80 ? Z80F_P : 0)));
    return r;
}

Z80_INL uint8_t z_dec8(Z80X *s, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    z_setf(s, (uint8_t)((s->z->f & Z80F_C) | Z80F_N | z80_sz[r] | ((v & 0x0F) == 0 ? Z80F_H : 0) | (v == 0x80 ? Z80F_P : 0)));
    return r;
}

Z80_INL uint16_t z_add16(Z80X *s, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    s->z->wz = (uint16_t)(a + 1);
    z_setf(s, (uint8_t)((s->z->f & (Z80F_S | Z80F_Z | Z80F_P)) | ((r >> 16) & Z80F_C) | (((a ^ b ^ r) >> 8) & Z80F_H) | ((r >> 8) & (Z80F_X | Z80F_Y))));
    return (uint16_t)r;
}

Z80_INL uint16_t z_adc16(Z80X *s, uint16_t a, uint16_t b, int sub)
{
    int c = s->z->f & Z80F_C;
    uint32_t r = sub ? (uint32_t)a - b - (uint32_t)c : (uint32_t)a + b + (uint32_t)c;
    uint16_t r16 = (uint16_t)r;
    uint8_t f = (uint8_t)(((r16 >> 8) & (Z80F_S | Z80F_X | Z80F_Y)) | (r16 == 0 ? Z80F_Z : 0) | ((r >> 16) & Z80F_C) | (((a ^ b ^ r) >> 8) & Z80F_H));
    if (sub) f |= (uint8_t)(Z80F_N | ((((a ^ b) & (a ^ r)) & 0x8000) ? Z80F_P : 0));
    else f |= (uint8_t)((((a ^ ~b) & (a ^ r)) & 0x8000) ? Z80F_P : 0);
    s->z->wz = (uint16_t)(a + 1);
    z_setf(s, f);
    return r16;
}

/* Rotaciones y desplazamientos del prefijo CB (k: RLC RRC RL RR SLA SRA SLL SRL) */
Z80_INL uint8_t z_rot(Z80X *s, int k, uint8_t v)
{
    uint8_t c, r;
    switch (k) {
    case 0: c = v >> 7; r = (uint8_t)(v << 1 | c); break;
    case 1: c = v & 1; r = (uint8_t)(v >> 1 | c << 7); break;
    case 2: c = v >> 7; r = (uint8_t)(v << 1 | (s->z->f & Z80F_C)); break;
    case 3: c = v & 1; r = (uint8_t)(v >> 1 | (s->z->f & Z80F_C) << 7); break;
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;
    case 5: c = v & 1; r = (uint8_t)((v >> 1) | (v & 0x80)); break;
    case 6: c = v >> 7; r = (uint8_t)(v << 1 | 1); break;
    default: c = v & 1; r = (uint8_t)(v >> 1); break;
    }
    z_setf(s, (uint8_t)(z80_szp[r] | c));
    return r;
}

Z80_INL void z_bit(Z80X *s, int b, uint8_t v, uint8_t xy)
{
    uint8_t f = (uint8_t)((s->z->f & Z80F_C) | Z80F_H | (xy & (Z80F_X | Z80F_Y)));
    if (!(v & (1 << b))) f |= Z80F_Z | Z80F_P;
    else if (b == 7) f |= Z80F_S;
    z_setf(s, f);
}

Z80_INL void z_daa(Z80X *s)
{
    Z80 *z = s->z;
    uint8_t a = z->a, corr = 0, c = z->f & Z80F_C, h;
    if ((z->f & Z80F_H) || (a & 0x0F) > 9) corr |= 0x06;
    if (c || a > 0x99) { corr |= 0x60; c = Z80F_C; }
    if (z->f & Z80F_N) { h = (z->f & Z80F_H) && (a & 0x0F) < 6; a = (uint8_t)(a - corr); }
    else { h = (a & 0x0F) > 9; a = (uint8_t)(a + corr); }
    z->a = a;
    z_setf(s, (uint8_t)(z80_szp[a] | (z->f & Z80F_N) | c | (h ? Z80F_H : 0)));
}

Z80_INL int z_cond(const Z80 *z, int cc)
{
    switch (cc) {
    case 0: return !(z->f & Z80F_Z);
    case 1: return z->f & Z80F_Z;
    case 2: return !(z->f & Z80F_C);
    case 3: return z->f & Z80F_C;
    case 4: return !(z->f & Z80F_P);
    case 5: return z->f & Z80F_P;
    case 6: return !(z->f & Z80F_S);
    default: return z->f & Z80F_S;
    }
}

/* --- Prefijo CB (y DDCB/FDCB) --- */

Z80_INL int z_exec_cb(Z80X *s)
{
    Z80 *z = s->z;
    uint8_t op = z_m1(s);
    int r = op & 7, b = op >> 3 & 7;
    if (r == 6) {
        uint16_t a = z_HL(z);
        uint8_t v = z_rd(s, a);
        if (op < 0x40) { z_wr(s, a, z_rot(s, b, v)); return 15; }
        if (op < 0x80) { z_bit(s, b, v, (uint8_t)(z->wz >> 8)); return 12; }
        z_wr(s, a, op < 0xC0 ? (uint8_t)(v & ~(1 << b)) : (uint8_t)(v | 1 << b));
        return 15;
    }
    uint8_t v = z_get8(z, r, 0);
    if (op < 0x40) z_set8(z, r, 0, z_rot(s, b, v));
    else if (op < 0x80) z_bit(s, b, v, v);
    else z_set8(z, r, 0, op < 0xC0 ? (uint8_t)(v & ~(1 << b)) : (uint8_t)(v | 1 << b));
    return 8;
}

/* DD CB d op / FD CB d op: sobre (IX+d); el resultado también va al registro r si r != 6 */
Z80_INL int z_exec_xycb(Z80X *s, int idx)
{
    Z80 *z = s->z;
    uint16_t a = (uint16_t)(z_getxy(z, idx) + (int8_t)z_imm8(s));
    uint8_t op = z_imm8(s);
    int r = op & 7, b = op >> 3 & 7;
    z->wz = a;
    uint8_t v = z_rd(s, a), res;
    if (op >= 0x40 && op < 0x80) { z_bit(s, b, v, (uint8_t)(a >> 8)); return 20; }
    if (op < 0x40) res = z_rot(s, b, v);
    else res = op < 0xC0 ? (uint8_t)(v & ~(1 << b)) : (uint8_t)(v | 1 << b);
    z_wr(s, a, res);
    if (r != 6) z_set8(z, r, 0, res);
    return 23;
}

/* --- Prefijo ED --- */

Z80_INL int z_io_flags_repeat(Z80X *s, uint8_t v)
{
    /* Banderas de INIR/INDR/OTIR/OTDR cuando repiten (se interrumpen entre vueltas) */
    Z80 *z = s->z;
    uint8_t f = (uint8_t)((z->f & ~(Z80F_X | Z80F_Y)) | ((s->pc >> 8) & (Z80F_X | Z80F_Y)));
    if (f & Z80F_C) {
        f &= (uint8_t)~Z80F_H;
        if (v & 0x80) {
            f ^= (uint8_t)((z80_szp[(z->b - 1) & 7] ^ Z80F_P) & Z80F_P);
            if ((z->b & 0x0F) == 0x00) f |= Z80F_H;
        } else {
            f ^= (uint8_t)((z80_szp[(z->b + 1) & 7] ^ Z80F_P) & Z80F_P);
            if ((z->b & 0x0F) == 0x0F) f |= Z80F_H;
        }
    } else f ^= (uint8_t)((z80_szp[z->b & 7] ^ Z80F_P) & Z80F_P);
    z_setf(s, f);
    return 0;
}

Z80_INL int z_exec_ed(Z80X *s)
{
    Z80 *z = s->z;
    uint8_t op = z_m1(s);
    if (op >= 0x40 && op < 0x80) {
        int r = op >> 3 & 7;
        uint16_t rr = 0;
        switch (op >> 4 & 3) { case 0: rr = z_BC(z); break; case 1: rr = z_DE(z); break; case 2: rr = z_HL(z); break; default: rr = z->sp; }
        switch (op & 7) {
        case 0: {                                       /* IN r,(C) */
            Z80_IO_AT(s->g, 10);
            uint8_t v = z80_in(s->g, z_BC(z));
            z->wz = (uint16_t)(z_BC(z) + 1);
            if (r != 6) z_set8(z, r, 0, v);
            z_setf(s, (uint8_t)((z->f & Z80F_C) | z80_szp[v]));
            return 12;
        }
        case 1:                                         /* OUT (C),r */
            Z80_IO_AT(s->g, 10);
            z80_out(s->g, z_BC(z), r == 6 ? 0 : z_get8(z, r, 0));
            z->wz = (uint16_t)(z_BC(z) + 1);
            return 12;
        case 2: {                                       /* SBC/ADC HL,rr */
            uint16_t v = z_adc16(s, z_HL(z), rr, !(op & 8));
            z_setHL(z, v);
            return 15;
        }
        case 3: {                                       /* LD (nn),rr / LD rr,(nn) */
            uint16_t nn = z_imm16(s);
            if (op & 8) {
                uint8_t lo = z_rd(s, nn); uint16_t v = (uint16_t)(lo | z_rd(s, (uint16_t)(nn + 1)) << 8);
                switch (op >> 4 & 3) { case 0: z_setBC(z, v); break; case 1: z_setDE(z, v); break; case 2: z_setHL(z, v); break; default: z->sp = v; }
            } else { z_wr(s, nn, (uint8_t)rr); z_wr(s, (uint16_t)(nn + 1), (uint8_t)(rr >> 8)); }
            z->wz = (uint16_t)(nn + 1);
            return 20;
        }
        case 4: { uint8_t v = z->a; z->a = 0; z_alu(s, 2, v); return 8; }          /* NEG */
        case 5: s->pc = z_pop(s); z->wz = s->pc; z->iff1 = z->iff2; return 14;    /* RETN / RETI */
        case 6: z->im = (op & 0x18) == 0x10 ? 1 : (op & 0x18) == 0x18 ? 2 : 0; return 8;
        default:
            switch (op) {
            case 0x47: z->i = z->a; return 9;
            case 0x4F: z->r = z->a; return 9;
            case 0x57: case 0x5F:
                z->a = op == 0x57 ? z->i : z->r;
                z_setf(s, (uint8_t)((z->f & Z80F_C) | z80_sz[z->a] | (z->iff2 ? Z80F_P : 0)));
                s->p = 1;
                return 9;
            case 0x67: case 0x6F: {                     /* RRD / RLD */
                uint16_t a = z_HL(z);
                uint8_t v = z_rd(s, a);
                if (op == 0x67) { z_wr(s, a, (uint8_t)(z->a << 4 | v >> 4)); z->a = (uint8_t)((z->a & 0xF0) | (v & 0x0F)); }
                else { z_wr(s, a, (uint8_t)(v << 4 | (z->a & 0x0F))); z->a = (uint8_t)((z->a & 0xF0) | (v >> 4)); }
                z->wz = (uint16_t)(a + 1);
                z_setf(s, (uint8_t)((z->f & Z80F_C) | z80_szp[z->a]));
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
            uint8_t v = z_rd(s, z_HL(z));
            z_wr(s, z_DE(z), v);
            z_setHL(z, (uint16_t)(z_HL(z) + d)); z_setDE(z, (uint16_t)(z_DE(z) + d)); z_setBC(z, (uint16_t)(z_BC(z) - 1));
            uint8_t n = (uint8_t)(v + z->a);
            z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_C)) | (z_BC(z) ? Z80F_P : 0) | (n & Z80F_X) | ((n & 2) << 4)));
            if (rep && z_BC(z)) {
                s->pc = (uint16_t)(s->pc - 2); z->wz = (uint16_t)(s->pc + 1);
                z_setf(s, (uint8_t)((z->f & ~(Z80F_X | Z80F_Y)) | ((s->pc >> 8) & (Z80F_X | Z80F_Y))));
                return 21;
            }
            return 16;
        }
        case 1: {                                       /* CPI CPD CPIR CPDR */
            uint8_t v = z_rd(s, z_HL(z));
            uint8_t r = (uint8_t)(z->a - v), hc = (uint8_t)((z->a ^ v ^ r) & Z80F_H);
            z_setHL(z, (uint16_t)(z_HL(z) + d)); z_setBC(z, (uint16_t)(z_BC(z) - 1));
            z->wz = (uint16_t)(z->wz + d);
            uint8_t n = (uint8_t)(r - (hc ? 1 : 0));
            z_setf(s, (uint8_t)((z->f & Z80F_C) | Z80F_N | hc | (z_BC(z) ? Z80F_P : 0) | (r & Z80F_S) | (r ? 0 : Z80F_Z) | (n & Z80F_X) | ((n & 2) << 4)));
            if (rep && z_BC(z) && r) {
                s->pc = (uint16_t)(s->pc - 2); z->wz = (uint16_t)(s->pc + 1);
                z_setf(s, (uint8_t)((z->f & ~(Z80F_X | Z80F_Y)) | ((s->pc >> 8) & (Z80F_X | Z80F_Y))));
                return 21;
            }
            return 16;
        }
        case 2: {                                       /* INI IND INIR INDR */
            Z80_IO_AT(s->g, 11);
            uint8_t v = z80_in(s->g, z_BC(z));
            z->wz = (uint16_t)(z_BC(z) + d);
            z->b--;
            z_wr(s, z_HL(z), v);
            z_setHL(z, (uint16_t)(z_HL(z) + d));
            int k = v + (uint8_t)(z->c + d);
            z_setf(s, (uint8_t)(z80_sz[z->b] | (v & 0x80 ? Z80F_N : 0) | (k > 255 ? Z80F_H | Z80F_C : 0) | (z80_szp[(k & 7) ^ z->b] & Z80F_P)));
            if (rep && z->b) { s->pc = (uint16_t)(s->pc - 2); z->wz = (uint16_t)(s->pc + 1); z_io_flags_repeat(s, v); return 21; }
            return 16;
        }
        default: {                                      /* OUTI OUTD OTIR OTDR */
            uint8_t v = z_rd(s, z_HL(z));
            z->b--;
            z->wz = (uint16_t)(z_BC(z) + d);
            Z80_IO_AT(s->g, 14);
            z80_out(s->g, z_BC(z), v);
            z_setHL(z, (uint16_t)(z_HL(z) + d));
            int k = v + z->l;
            z_setf(s, (uint8_t)(z80_sz[z->b] | (v & 0x80 ? Z80F_N : 0) | (k > 255 ? Z80F_H | Z80F_C : 0) | (z80_szp[(k & 7) ^ z->b] & Z80F_P)));
            if (rep && z->b) { s->pc = (uint16_t)(s->pc - 2); z->wz = (uint16_t)(s->pc + 1); z_io_flags_repeat(s, v); return 21; }
            return 16;
        }
        }
    }
    return 8;                                           /* ED sin definir: como dos NOP */
}

/* --- Sin prefijo, o con DD/FD (idx: 0 HL, 1 IX, 2 IY) --- */

static const uint8_t z_cyc_main[256] = {
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

Z80_INL int z_exec_main(Z80X *s, uint8_t op, int idx)
{
    Z80 *z = s->z;
    int t = z_cyc_main[op] + (idx ? 4 : 0);

    if (op >= 0x40 && op < 0x80) {                     /* LD r,r' / HALT */
        int dst = op >> 3 & 7, src = op & 7;
        if (op == 0x76) { z->halted = 1; return t; }
        if (src == 6) { uint16_t a = z_mem_addr(s, idx); z_set8(z, dst, 0, z_rd(s, a)); return t + (idx ? 8 : 0); }
        if (dst == 6) { uint16_t a = z_mem_addr(s, idx); z_wr(s, a, z_get8(z, src, 0)); return t + (idx ? 8 : 0); }
        z_set8(z, dst, idx, z_get8(z, src, idx));
        return t;
    }
    if (op >= 0x80 && op < 0xC0) {                     /* ALU A,r */
        int src = op & 7;
        if (src == 6) { uint16_t a = z_mem_addr(s, idx); z_alu(s, op >> 3 & 7, z_rd(s, a)); return t + (idx ? 8 : 0); }
        z_alu(s, op >> 3 & 7, z_get8(z, src, idx));
        return t;
    }

    switch (op) {
    case 0x00: return t;
    case 0x01: z_setBC(z, z_imm16(s)); return t;
    case 0x11: z_setDE(z, z_imm16(s)); return t;
    case 0x21: z_setxy(z, idx, z_imm16(s)); return t;
    case 0x31: z->sp = z_imm16(s); return t;
    case 0x02: z_wr(s, z_BC(z), z->a); z->wz = (uint16_t)(((z_BC(z) + 1) & 0xFF) | z->a << 8); return t;
    case 0x12: z_wr(s, z_DE(z), z->a); z->wz = (uint16_t)(((z_DE(z) + 1) & 0xFF) | z->a << 8); return t;
    case 0x0A: z->a = z_rd(s, z_BC(z)); z->wz = (uint16_t)(z_BC(z) + 1); return t;
    case 0x1A: z->a = z_rd(s, z_DE(z)); z->wz = (uint16_t)(z_DE(z) + 1); return t;
    case 0x22: { uint16_t nn = z_imm16(s), v = z_getxy(z, idx); z_wr(s, nn, (uint8_t)v); z_wr(s, (uint16_t)(nn + 1), (uint8_t)(v >> 8)); z->wz = (uint16_t)(nn + 1); return t; }
    case 0x2A: { uint16_t nn = z_imm16(s); uint8_t lo = z_rd(s, nn); z_setxy(z, idx, (uint16_t)(lo | z_rd(s, (uint16_t)(nn + 1)) << 8)); z->wz = (uint16_t)(nn + 1); return t; }
    case 0x32: { uint16_t nn = z_imm16(s); z_wr(s, nn, z->a); z->wz = (uint16_t)(((nn + 1) & 0xFF) | z->a << 8); return t; }
    case 0x3A: { uint16_t nn = z_imm16(s); z->a = z_rd(s, nn); z->wz = (uint16_t)(nn + 1); return t; }
    case 0x03: z_setBC(z, (uint16_t)(z_BC(z) + 1)); return t;
    case 0x13: z_setDE(z, (uint16_t)(z_DE(z) + 1)); return t;
    case 0x23: z_setxy(z, idx, (uint16_t)(z_getxy(z, idx) + 1)); return t;
    case 0x33: z->sp++; return t;
    case 0x0B: z_setBC(z, (uint16_t)(z_BC(z) - 1)); return t;
    case 0x1B: z_setDE(z, (uint16_t)(z_DE(z) - 1)); return t;
    case 0x2B: z_setxy(z, idx, (uint16_t)(z_getxy(z, idx) - 1)); return t;
    case 0x3B: z->sp--; return t;
    case 0x09: z_setxy(z, idx, z_add16(s, z_getxy(z, idx), z_BC(z))); return t;
    case 0x19: z_setxy(z, idx, z_add16(s, z_getxy(z, idx), z_DE(z))); return t;
    case 0x29: { uint16_t v = z_getxy(z, idx); z_setxy(z, idx, z_add16(s, v, v)); return t; }
    case 0x39: z_setxy(z, idx, z_add16(s, z_getxy(z, idx), z->sp)); return t;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
        int r = op >> 3 & 7; z_set8(z, r, idx, z_inc8(s, z_get8(z, r, idx))); return t;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
        int r = op >> 3 & 7; z_set8(z, r, idx, z_dec8(s, z_get8(z, r, idx))); return t;
    }
    case 0x34: { uint16_t a = z_mem_addr(s, idx); z_wr(s, a, z_inc8(s, z_rd(s, a))); return t + (idx ? 8 : 0); }
    case 0x35: { uint16_t a = z_mem_addr(s, idx); z_wr(s, a, z_dec8(s, z_rd(s, a))); return t + (idx ? 8 : 0); }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:
        z_set8(z, op >> 3 & 7, idx, z_imm8(s)); return t;
    case 0x36: { uint16_t a = z_mem_addr(s, idx); z_wr(s, a, z_imm8(s)); return t + (idx ? 5 : 0); }
    case 0x07: { uint8_t c = z->a >> 7; z->a = (uint8_t)(z->a << 1 | c); z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | (z->a & (Z80F_X | Z80F_Y)) | c)); return t; }
    case 0x0F: { uint8_t c = z->a & 1; z->a = (uint8_t)(z->a >> 1 | c << 7); z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | (z->a & (Z80F_X | Z80F_Y)) | c)); return t; }
    case 0x17: { uint8_t c = z->a >> 7; z->a = (uint8_t)(z->a << 1 | (z->f & Z80F_C)); z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | (z->a & (Z80F_X | Z80F_Y)) | c)); return t; }
    case 0x1F: { uint8_t c = z->a & 1; z->a = (uint8_t)(z->a >> 1 | (z->f & Z80F_C) << 7); z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | (z->a & (Z80F_X | Z80F_Y)) | c)); return t; }
    case 0x08: { uint16_t af = (uint16_t)(z->a << 8 | z->f); z->a = (uint8_t)(z->af_ >> 8); z->f = (uint8_t)z->af_; z->af_ = af; return t; }
    case 0x10: {                                        /* DJNZ */
        int8_t e = (int8_t)z_imm8(s);
        if (--z->b) { s->pc = (uint16_t)(s->pc + e); z->wz = s->pc; return t + 5; }
        return t;
    }
    case 0x18: { int8_t e = (int8_t)z_imm8(s); s->pc = (uint16_t)(s->pc + e); z->wz = s->pc; return t; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        int8_t e = (int8_t)z_imm8(s);
        if (z_cond(z, op >> 3 & 3)) { s->pc = (uint16_t)(s->pc + e); z->wz = s->pc; return t + 5; }
        return t;
    }
    case 0x27: z_daa(s); return t;
    case 0x2F: z->a = (uint8_t)~z->a; z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P | Z80F_C)) | Z80F_H | Z80F_N | (z->a & (Z80F_X | Z80F_Y)))); return t;
    case 0x37: z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | Z80F_C | (((z->q ^ z->f) | z->a) & (Z80F_X | Z80F_Y)))); return t;
    case 0x3F: z_setf(s, (uint8_t)((z->f & (Z80F_S | Z80F_Z | Z80F_P)) | ((z->f & Z80F_C) ? Z80F_H : Z80F_C) | (((z->q ^ z->f) | z->a) & (Z80F_X | Z80F_Y)))); return t;

    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (z_cond(z, op >> 3 & 7)) { s->pc = z_pop(s); z->wz = s->pc; return t + 6; }
        return t;
    case 0xC9: s->pc = z_pop(s); z->wz = s->pc; return t;
    case 0xC1: z_setBC(z, z_pop(s)); return t;
    case 0xD1: z_setDE(z, z_pop(s)); return t;
    case 0xE1: z_setxy(z, idx, z_pop(s)); return t;
    case 0xF1: { uint16_t v = z_pop(s); z->a = (uint8_t)(v >> 8); z->f = (uint8_t)v; return t; }
    case 0xC5: z_push(s, z_BC(z)); return t;
    case 0xD5: z_push(s, z_DE(z)); return t;
    case 0xE5: z_push(s, z_getxy(z, idx)); return t;
    case 0xF5: z_push(s, (uint16_t)(z->a << 8 | z->f)); return t;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        uint16_t nn = z_imm16(s); z->wz = nn;
        if (z_cond(z, op >> 3 & 7)) s->pc = nn;
        return t;
    }
    case 0xC3: { uint16_t nn = z_imm16(s); s->pc = nn; z->wz = nn; return t; }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        uint16_t nn = z_imm16(s); z->wz = nn;
        if (z_cond(z, op >> 3 & 7)) { z_push(s, s->pc); s->pc = nn; return t + 7; }
        return t;
    }
    case 0xCD: { uint16_t nn = z_imm16(s); z_push(s, s->pc); s->pc = nn; z->wz = nn; return t; }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        z_alu(s, op >> 3 & 7, z_imm8(s)); return t;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        z_push(s, s->pc); s->pc = op & 0x38; z->wz = s->pc; return t;
    case 0xD3: { uint8_t n = z_imm8(s); Z80_IO_AT(s->g, idx ? 13 : 9); z80_out(s->g, (uint16_t)(z->a << 8 | n), z->a); z->wz = (uint16_t)(((n + 1) & 0xFF) | z->a << 8); return t; }
    case 0xDB: { uint8_t n = z_imm8(s); uint16_t port = (uint16_t)(z->a << 8 | n); Z80_IO_AT(s->g, idx ? 13 : 9); z->a = z80_in(s->g, port); z->wz = (uint16_t)(port + 1); return t; }
    case 0xD9: {                                        /* EXX */
        uint16_t v;
        v = z_BC(z); z_setBC(z, z->bc_); z->bc_ = v;
        v = z_DE(z); z_setDE(z, z->de_); z->de_ = v;
        v = z_HL(z); z_setHL(z, z->hl_); z->hl_ = v;
        return t;
    }
    case 0xE3: {                                        /* EX (SP),HL */
        uint8_t lo = z_rd(s, z->sp), hi = z_rd(s, (uint16_t)(z->sp + 1));
        uint16_t v = z_getxy(z, idx);
        z_wr(s, z->sp, (uint8_t)v); z_wr(s, (uint16_t)(z->sp + 1), (uint8_t)(v >> 8));
        z_setxy(z, idx, (uint16_t)(lo | hi << 8));
        z->wz = (uint16_t)(lo | hi << 8);
        return t;
    }
    case 0xE9: s->pc = z_getxy(z, idx); return t;
    case 0xEB: { uint16_t v = z_DE(z); z_setDE(z, z_HL(z)); z_setHL(z, v); return t; }
    case 0xF3: z->iff1 = z->iff2 = 0; return t;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei = 1; return t;
    case 0xF9: z->sp = z_getxy(z, idx); return t;
    default: return t;
    }
}

/* z80_exec por partes. z80_exec_begin lee el primer byte (el M1) y deja la rama: z80_exec_main (sin
 * prefijo), z80_exec_cb, z80_exec_ed o z80_exec_xy (DD/FD). El código generado sabe el primer byte y llama
 * directo a su rama: el compilador no tiene que copiar y plegar las otras tres en cada instrucción (con
 * z80_exec entero, compilar el Z80 de un juego tardaba unas cinco veces más) */
Z80_INL uint8_t z80_exec_begin(Z80X *s) { s->z->ei = 0; return z_m1(s); }

Z80_INL int z80_exec_end(Z80X *s, int t)
{
    s->z->q = s->qf ? s->z->f : 0;
    s->z->p = (uint8_t)s->p;
    return t;
}

Z80_INL int z80_exec_main(Z80X *s, uint8_t op) { return z80_exec_end(s, z_exec_main(s, op, 0)); }
Z80_INL int z80_exec_cb(Z80X *s) { return z80_exec_end(s, z_exec_cb(s)); }
Z80_INL int z80_exec_ed(Z80X *s) { return z80_exec_end(s, z_exec_ed(s)); }

Z80_INL int z80_exec_xy(Z80X *s, uint8_t op)
{
    Z80 *z = s->z;
    int idx = op == 0xDD ? 1 : 2, t;
    uint8_t op2 = s->ops ? s->ops[s->oi] : z_rd(s, s->pc);
    if (op2 == 0xDD || op2 == 0xFD || op2 == 0xED) return z80_exec_end(s, 4);  /* el prefijo solo cuenta como NOP */
    if (op2 == 0xCB) { s->oi++; s->pc++; z_inc_r(z); return z80_exec_end(s, z_exec_xycb(s, idx)); }   /* el CB no se vuelve a leer */
    op2 = z_m1(s);
    z->q = 0;                           /* el prefijo no cambia las banderas (SCF/CCF lo ven) */
    t = z_exec_main(s, op2, idx);
    return z80_exec_end(s, t);
}

/* Una instrucción, sin interrupciones (las atiende z80_step): deja en s->pc la siguiente y devuelve sus
 * ciclos T. La instrucción anterior pudo ser EI: eso solo importa para atender una interrupción, así que
 * aquí se borra */
Z80_INL int z80_exec(Z80X *s)
{
    uint8_t op = z80_exec_begin(s);
    switch (op) {
    case 0xCB: return z80_exec_cb(s);
    case 0xED: return z80_exec_ed(s);
    case 0xDD: case 0xFD: return z80_exec_xy(s, op);
    default: return z80_exec_main(s, op);
    }
}

/* --- Decodificador (z80.c): lo que necesita el descubrimiento de un recompilador --- */

enum {
    Z80_INS_NORMAL,         /* sigue a la siguiente */
    Z80_INS_JP,             /* JP nn / JP cc,nn */
    Z80_INS_JR,             /* JR e / JR cc,e */
    Z80_INS_DJNZ,
    Z80_INS_CALL,           /* CALL nn / CALL cc,nn */
    Z80_INS_RST,
    Z80_INS_RET,            /* RET / RET cc */
    Z80_INS_RETI,           /* RETI / RETN (todos los ED 45-7D que terminan en 5) */
    Z80_INS_JP_IND,         /* JP (HL) / JP (IX) / JP (IY) */
    Z80_INS_HALT,
    Z80_INS_REPEAT,         /* LDIR, CPIR, INIR, OTIR y los que bajan: vuelven a sí mismos mientras repiten */
};

typedef struct {
    uint8_t  len;           /* bytes (1-4) */
    uint8_t  kind;          /* Z80_INS_* */
    uint8_t  cond;          /* 0: incondicional; 1-8: NZ Z NC C PO PE P M */
    uint16_t target;        /* destino constante (JP, JR, DJNZ, CALL, RST; REPEAT: la misma instrucción) */
    uint8_t  cycles;        /* ciclos T sin el salto (o sin repetir) */
    uint8_t  cycles_taken;  /* con el salto (o repitiendo); igual a cycles si no salta */
} Z80Insn;

/* Decodifica la instrucción de los 4 bytes de b, que está en pc */
void z80_decode(const uint8_t b[4], uint16_t pc, Z80Insn *out);

#endif
