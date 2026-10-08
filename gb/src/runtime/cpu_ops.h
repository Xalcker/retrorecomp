/*
 * Semántica de las instrucciones del SM83, compartida por el intérprete
 * (interp.c) y por el código que genera gbrecomp. El emisor (sm83_emit.c)
 * decide QUÉ operaciones y en qué orden (incluida la posición de cada M-ciclo);
 * este archivo define CÓMO se hace cada una.
 *
 * El código que usa estas macros debe tener en alcance:
 *   GB *g;  Cpu *c = &g->cpu;
 */
#ifndef CPU_OPS_H
#define CPU_OPS_H

#include "gb.h"

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#define ALWAYS_INLINE static inline
#endif

#define FZ 0x80
#define FN 0x40
#define FH 0x20
#define FC 0x10

/* --- Memoria: cada acceso es un M-ciclo (4 T-ciclos) --- */

ALWAYS_INLINE u8 mem_read(GB *g, u16 a)
{
    const u8 *p = g->rpage[a >> 8];
    u8 v;
    /* El acceso ocurre al inicio del M-ciclo: el hardware se ve como estaba
     * antes de estos 4 ciclos */
    if (LIKELY(p != NULL)) v = p[a & 0xFF];
    else v = gb_read_slow(g, a);
    g->cyc += 4;
    return v;
}

ALWAYS_INLINE void mem_write(GB *g, u16 a, u8 v)
{
    u8 *p = g->wpage[a >> 8];
    if (LIKELY(p != NULL)) p[a & 0xFF] = v;
    else gb_write_slow(g, a, v);
    g->cyc += 4;
}

#define TICK()      (g->cyc += 4)
#define RD(a)       mem_read(g, (u16)(a))
#define WR(a, v)    mem_write(g, (u16)(a), (u8)(v))
#define FETCH8()    mem_read(g, c->pc++)

ALWAYS_INLINE u16 fetch16(GB *g)
{
    Cpu *c = &g->cpu;
    u8 lo = FETCH8();
    return (u16)(lo | FETCH8() << 8);
}
#define FETCH16()   fetch16(g)

/* Pila */
ALWAYS_INLINE void push16(GB *g, u16 v)
{
    Cpu *c = &g->cpu;
    WR(--c->sp, v >> 8);
    WR(--c->sp, v & 0xFF);
}

ALWAYS_INLINE u16 pop16(GB *g)
{
    Cpu *c = &g->cpu;
    u8 lo = RD(c->sp++);
    return (u16)(lo | RD(c->sp++) << 8);
}
#define PUSH16(v)   push16(g, (u16)(v))
#define POP16()     pop16(g)

/* Pares de registros */
#define BC()  ((u16)(c->b << 8 | c->c))
#define DE()  ((u16)(c->d << 8 | c->e))
#define HL()  ((u16)(c->h << 8 | c->l))
#define AF()  ((u16)(c->a << 8 | c->f))
#define SET_BC(v) do { u16 v_ = (u16)(v); c->b = (u8)(v_ >> 8); c->c = (u8)v_; } while (0)
#define SET_DE(v) do { u16 v_ = (u16)(v); c->d = (u8)(v_ >> 8); c->e = (u8)v_; } while (0)
#define SET_HL(v) do { u16 v_ = (u16)(v); c->h = (u8)(v_ >> 8); c->l = (u8)v_; } while (0)
#define SET_AF(v) do { u16 v_ = (u16)(v); c->a = (u8)(v_ >> 8); c->f = (u8)(v_ & 0xF0); } while (0)

/* Condiciones */
#define CC_NZ (!(c->f & FZ))
#define CC_Z  ((c->f & FZ) != 0)
#define CC_NC (!(c->f & FC))
#define CC_C  ((c->f & FC) != 0)

/* --- ALU --- */

ALWAYS_INLINE void op_add(Cpu *c, u8 v)
{
    unsigned r = c->a + v;
    c->f = (u8)(((r & 0xFF) ? 0 : FZ) | (((c->a & 0xF) + (v & 0xF)) > 0xF ? FH : 0) |
                (r > 0xFF ? FC : 0));
    c->a = (u8)r;
}

ALWAYS_INLINE void op_adc(Cpu *c, u8 v)
{
    unsigned cy = (c->f & FC) ? 1 : 0;
    unsigned r = c->a + v + cy;
    c->f = (u8)(((r & 0xFF) ? 0 : FZ) | (((c->a & 0xF) + (v & 0xF) + cy) > 0xF ? FH : 0) |
                (r > 0xFF ? FC : 0));
    c->a = (u8)r;
}

ALWAYS_INLINE void op_sub(Cpu *c, u8 v)
{
    int r = c->a - v;
    c->f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | ((c->a & 0xF) < (v & 0xF) ? FH : 0) |
                (r < 0 ? FC : 0));
    c->a = (u8)r;
}

ALWAYS_INLINE void op_sbc(Cpu *c, u8 v)
{
    int cy = (c->f & FC) ? 1 : 0;
    int r = c->a - v - cy;
    c->f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | (((c->a & 0xF) - (v & 0xF) - cy) < 0 ? FH : 0) |
                (r < 0 ? FC : 0));
    c->a = (u8)r;
}

ALWAYS_INLINE void op_and(Cpu *c, u8 v) { c->a &= v; c->f = (u8)((c->a ? 0 : FZ) | FH); }
ALWAYS_INLINE void op_xor(Cpu *c, u8 v) { c->a ^= v; c->f = c->a ? 0 : FZ; }
ALWAYS_INLINE void op_or (Cpu *c, u8 v) { c->a |= v; c->f = c->a ? 0 : FZ; }

ALWAYS_INLINE void op_cp(Cpu *c, u8 v)
{
    int r = c->a - v;
    c->f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | ((c->a & 0xF) < (v & 0xF) ? FH : 0) |
                (r < 0 ? FC : 0));
}

ALWAYS_INLINE u8 op_inc(Cpu *c, u8 v)
{
    u8 r = (u8)(v + 1);
    c->f = (u8)((c->f & FC) | (r ? 0 : FZ) | ((v & 0xF) == 0xF ? FH : 0));
    return r;
}

ALWAYS_INLINE u8 op_dec(Cpu *c, u8 v)
{
    u8 r = (u8)(v - 1);
    c->f = (u8)((c->f & FC) | FN | (r ? 0 : FZ) | ((v & 0xF) == 0 ? FH : 0));
    return r;
}

ALWAYS_INLINE void op_add_hl(Cpu *c, u16 v)
{
    u16 hl = HL();
    unsigned r = (unsigned)hl + v;
    c->f = (u8)((c->f & FZ) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) |
                (r > 0xFFFF ? FC : 0));
    SET_HL(r);
}

/* SP + e con las banderas de ADD SP,e y LD HL,SP+e */
ALWAYS_INLINE u16 op_sp_e(Cpu *c, u8 e)
{
    u16 sp = c->sp;
    c->f = (u8)((((sp & 0xF) + (e & 0xF)) > 0xF ? FH : 0) |
                (((sp & 0xFF) + e) > 0xFF ? FC : 0));
    return (u16)(sp + (s8)e);
}

ALWAYS_INLINE void op_daa(Cpu *c)
{
    u8 a = c->a, adj = 0;
    int carry = (c->f & FC) != 0;
    if (c->f & FN) {
        if (c->f & FH) adj |= 0x06;
        if (carry) adj |= 0x60;
        a = (u8)(a - adj);
    } else {
        if ((c->f & FH) || (a & 0x0F) > 9) adj |= 0x06;
        if (carry || a > 0x99) { adj |= 0x60; carry = 1; }
        a = (u8)(a + adj);
    }
    c->a = a;
    c->f = (u8)((c->f & FN) | (a ? 0 : FZ) | (carry ? FC : 0));
}

ALWAYS_INLINE void op_cpl(Cpu *c) { c->a = (u8)~c->a; c->f |= FN | FH; }
ALWAYS_INLINE void op_scf(Cpu *c) { c->f = (u8)((c->f & FZ) | FC); }
ALWAYS_INLINE void op_ccf(Cpu *c) { c->f = (u8)((c->f & FZ) | ((c->f & FC) ? 0 : FC)); }

/* Rotaciones del acumulador (Z siempre en 0) */
ALWAYS_INLINE void op_rlca(Cpu *c) { u8 k = c->a >> 7; c->a = (u8)(c->a << 1 | k); c->f = k ? FC : 0; }
ALWAYS_INLINE void op_rrca(Cpu *c) { u8 k = c->a & 1; c->a = (u8)(c->a >> 1 | k << 7); c->f = k ? FC : 0; }
ALWAYS_INLINE void op_rla(Cpu *c)  { u8 k = c->a >> 7; c->a = (u8)(c->a << 1 | ((c->f & FC) ? 1 : 0)); c->f = k ? FC : 0; }
ALWAYS_INLINE void op_rra(Cpu *c)  { u8 k = c->a & 1; c->a = (u8)(c->a >> 1 | ((c->f & FC) ? 0x80 : 0)); c->f = k ? FC : 0; }

/* Prefijo CB */
#define CB_FLAGS(r, k) (c->f = (u8)(((r) ? 0 : FZ) | ((k) ? FC : 0)))
ALWAYS_INLINE u8 op_rlc(Cpu *c, u8 v)  { u8 k = v >> 7; u8 r = (u8)(v << 1 | k); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_rrc(Cpu *c, u8 v)  { u8 k = v & 1;  u8 r = (u8)(v >> 1 | k << 7); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_rl(Cpu *c, u8 v)   { u8 k = v >> 7; u8 r = (u8)(v << 1 | ((c->f & FC) ? 1 : 0)); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_rr(Cpu *c, u8 v)   { u8 k = v & 1;  u8 r = (u8)(v >> 1 | ((c->f & FC) ? 0x80 : 0)); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_sla(Cpu *c, u8 v)  { u8 k = v >> 7; u8 r = (u8)(v << 1); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_sra(Cpu *c, u8 v)  { u8 k = v & 1;  u8 r = (u8)((v >> 1) | (v & 0x80)); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE u8 op_swap(Cpu *c, u8 v) { u8 r = (u8)(v << 4 | v >> 4); CB_FLAGS(r, 0); return r; }
ALWAYS_INLINE u8 op_srl(Cpu *c, u8 v)  { u8 k = v & 1;  u8 r = (u8)(v >> 1); CB_FLAGS(r, k); return r; }
ALWAYS_INLINE void op_bit(Cpu *c, int n, u8 v)
{
    c->f = (u8)((c->f & FC) | FH | (((v >> n) & 1) ? 0 : FZ));
}

/* --- Control --- */
#define OP_EI()   do { c->ei_pend = 1; g->next_event = 0; } while (0)
#define OP_DI()   do { c->ime = 0; c->ei_pend = 0; } while (0)
#define OP_RETI() do { c->ime = 1; g->next_event = 0; } while (0)
#define OP_LDBB() (g->ld_bb_hit = 1)

#endif
