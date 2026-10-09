/*
 * 6502 (2A03): modos de direccionamiento, operaciones, tabla de opcodes y la
 * semántica de cada operación. Es la única fuente de verdad: la usan el
 * intérprete (cpu6502.c) y el código que genera nesrecomp, que las llama con
 * constantes para que el compilador las pliegue.
 */
#ifndef CPU_OPS_H
#define CPU_OPS_H

#include "nes.h"

enum { IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

enum {
    ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC, CLD, CLI, CLV,
    CMP, CPX, CPY, DEC, DEX, DEY, EOR, INC, INX, INY, JMP, JSR, LDA, LDX, LDY, LSR, NOP,
    ORA, PHA, PHP, PLA, PLP, ROL, ROR, RTI, RTS, SBC, SEC, SED, SEI, STA, STX, STY,
    TAX, TAY, TSX, TXA, TXS, TYA,
    /* no oficiales */
    SLO, RLA, SRE, RRA, SAX, LAX, DCP, ISB, ANC, ALR, ARR, XAA, AXS, AHX, TAS, SHY, SHX, LAS, JAM
};

typedef struct { u8 op, mode, cyc, pen; } Ins;
#define I(o, m, c, p) { o, m, c, p }

static const Ins ins_tab[256] = {
/*00*/ I(BRK,IMP,7,0), I(ORA,IZX,6,0), I(JAM,IMP,2,0), I(SLO,IZX,8,0), I(NOP,ZP,3,0),  I(ORA,ZP,3,0),  I(ASL,ZP,5,0),  I(SLO,ZP,5,0),
/*08*/ I(PHP,IMP,3,0), I(ORA,IMM,2,0), I(ASL,ACC,2,0), I(ANC,IMM,2,0), I(NOP,ABS,4,0), I(ORA,ABS,4,0), I(ASL,ABS,6,0), I(SLO,ABS,6,0),
/*10*/ I(BPL,REL,2,0), I(ORA,IZY,5,1), I(JAM,IMP,2,0), I(SLO,IZY,8,0), I(NOP,ZPX,4,0), I(ORA,ZPX,4,0), I(ASL,ZPX,6,0), I(SLO,ZPX,6,0),
/*18*/ I(CLC,IMP,2,0), I(ORA,ABY,4,1), I(NOP,IMP,2,0), I(SLO,ABY,7,0), I(NOP,ABX,4,1), I(ORA,ABX,4,1), I(ASL,ABX,7,0), I(SLO,ABX,7,0),
/*20*/ I(JSR,ABS,6,0), I(AND,IZX,6,0), I(JAM,IMP,2,0), I(RLA,IZX,8,0), I(BIT,ZP,3,0),  I(AND,ZP,3,0),  I(ROL,ZP,5,0),  I(RLA,ZP,5,0),
/*28*/ I(PLP,IMP,4,0), I(AND,IMM,2,0), I(ROL,ACC,2,0), I(ANC,IMM,2,0), I(BIT,ABS,4,0), I(AND,ABS,4,0), I(ROL,ABS,6,0), I(RLA,ABS,6,0),
/*30*/ I(BMI,REL,2,0), I(AND,IZY,5,1), I(JAM,IMP,2,0), I(RLA,IZY,8,0), I(NOP,ZPX,4,0), I(AND,ZPX,4,0), I(ROL,ZPX,6,0), I(RLA,ZPX,6,0),
/*38*/ I(SEC,IMP,2,0), I(AND,ABY,4,1), I(NOP,IMP,2,0), I(RLA,ABY,7,0), I(NOP,ABX,4,1), I(AND,ABX,4,1), I(ROL,ABX,7,0), I(RLA,ABX,7,0),
/*40*/ I(RTI,IMP,6,0), I(EOR,IZX,6,0), I(JAM,IMP,2,0), I(SRE,IZX,8,0), I(NOP,ZP,3,0),  I(EOR,ZP,3,0),  I(LSR,ZP,5,0),  I(SRE,ZP,5,0),
/*48*/ I(PHA,IMP,3,0), I(EOR,IMM,2,0), I(LSR,ACC,2,0), I(ALR,IMM,2,0), I(JMP,ABS,3,0), I(EOR,ABS,4,0), I(LSR,ABS,6,0), I(SRE,ABS,6,0),
/*50*/ I(BVC,REL,2,0), I(EOR,IZY,5,1), I(JAM,IMP,2,0), I(SRE,IZY,8,0), I(NOP,ZPX,4,0), I(EOR,ZPX,4,0), I(LSR,ZPX,6,0), I(SRE,ZPX,6,0),
/*58*/ I(CLI,IMP,2,0), I(EOR,ABY,4,1), I(NOP,IMP,2,0), I(SRE,ABY,7,0), I(NOP,ABX,4,1), I(EOR,ABX,4,1), I(LSR,ABX,7,0), I(SRE,ABX,7,0),
/*60*/ I(RTS,IMP,6,0), I(ADC,IZX,6,0), I(JAM,IMP,2,0), I(RRA,IZX,8,0), I(NOP,ZP,3,0),  I(ADC,ZP,3,0),  I(ROR,ZP,5,0),  I(RRA,ZP,5,0),
/*68*/ I(PLA,IMP,4,0), I(ADC,IMM,2,0), I(ROR,ACC,2,0), I(ARR,IMM,2,0), I(JMP,IND,5,0), I(ADC,ABS,4,0), I(ROR,ABS,6,0), I(RRA,ABS,6,0),
/*70*/ I(BVS,REL,2,0), I(ADC,IZY,5,1), I(JAM,IMP,2,0), I(RRA,IZY,8,0), I(NOP,ZPX,4,0), I(ADC,ZPX,4,0), I(ROR,ZPX,6,0), I(RRA,ZPX,6,0),
/*78*/ I(SEI,IMP,2,0), I(ADC,ABY,4,1), I(NOP,IMP,2,0), I(RRA,ABY,7,0), I(NOP,ABX,4,1), I(ADC,ABX,4,1), I(ROR,ABX,7,0), I(RRA,ABX,7,0),
/*80*/ I(NOP,IMM,2,0), I(STA,IZX,6,0), I(NOP,IMM,2,0), I(SAX,IZX,6,0), I(STY,ZP,3,0),  I(STA,ZP,3,0),  I(STX,ZP,3,0),  I(SAX,ZP,3,0),
/*88*/ I(DEY,IMP,2,0), I(NOP,IMM,2,0), I(TXA,IMP,2,0), I(XAA,IMM,2,0), I(STY,ABS,4,0), I(STA,ABS,4,0), I(STX,ABS,4,0), I(SAX,ABS,4,0),
/*90*/ I(BCC,REL,2,0), I(STA,IZY,6,0), I(JAM,IMP,2,0), I(AHX,IZY,6,0), I(STY,ZPX,4,0), I(STA,ZPX,4,0), I(STX,ZPY,4,0), I(SAX,ZPY,4,0),
/*98*/ I(TYA,IMP,2,0), I(STA,ABY,5,0), I(TXS,IMP,2,0), I(TAS,ABY,5,0), I(SHY,ABX,5,0), I(STA,ABX,5,0), I(SHX,ABY,5,0), I(AHX,ABY,5,0),
/*A0*/ I(LDY,IMM,2,0), I(LDA,IZX,6,0), I(LDX,IMM,2,0), I(LAX,IZX,6,0), I(LDY,ZP,3,0),  I(LDA,ZP,3,0),  I(LDX,ZP,3,0),  I(LAX,ZP,3,0),
/*A8*/ I(TAY,IMP,2,0), I(LDA,IMM,2,0), I(TAX,IMP,2,0), I(LAX,IMM,2,0), I(LDY,ABS,4,0), I(LDA,ABS,4,0), I(LDX,ABS,4,0), I(LAX,ABS,4,0),
/*B0*/ I(BCS,REL,2,0), I(LDA,IZY,5,1), I(JAM,IMP,2,0), I(LAX,IZY,5,1), I(LDY,ZPX,4,0), I(LDA,ZPX,4,0), I(LDX,ZPY,4,0), I(LAX,ZPY,4,0),
/*B8*/ I(CLV,IMP,2,0), I(LDA,ABY,4,1), I(TSX,IMP,2,0), I(LAS,ABY,4,1), I(LDY,ABX,4,1), I(LDA,ABX,4,1), I(LDX,ABY,4,1), I(LAX,ABY,4,1),
/*C0*/ I(CPY,IMM,2,0), I(CMP,IZX,6,0), I(NOP,IMM,2,0), I(DCP,IZX,8,0), I(CPY,ZP,3,0),  I(CMP,ZP,3,0),  I(DEC,ZP,5,0),  I(DCP,ZP,5,0),
/*C8*/ I(INY,IMP,2,0), I(CMP,IMM,2,0), I(DEX,IMP,2,0), I(AXS,IMM,2,0), I(CPY,ABS,4,0), I(CMP,ABS,4,0), I(DEC,ABS,6,0), I(DCP,ABS,6,0),
/*D0*/ I(BNE,REL,2,0), I(CMP,IZY,5,1), I(JAM,IMP,2,0), I(DCP,IZY,8,0), I(NOP,ZPX,4,0), I(CMP,ZPX,4,0), I(DEC,ZPX,6,0), I(DCP,ZPX,6,0),
/*D8*/ I(CLD,IMP,2,0), I(CMP,ABY,4,1), I(NOP,IMP,2,0), I(DCP,ABY,7,0), I(NOP,ABX,4,1), I(CMP,ABX,4,1), I(DEC,ABX,7,0), I(DCP,ABX,7,0),
/*E0*/ I(CPX,IMM,2,0), I(SBC,IZX,6,0), I(NOP,IMM,2,0), I(ISB,IZX,8,0), I(CPX,ZP,3,0),  I(SBC,ZP,3,0),  I(INC,ZP,5,0),  I(ISB,ZP,5,0),
/*E8*/ I(INX,IMP,2,0), I(SBC,IMM,2,0), I(NOP,IMP,2,0), I(SBC,IMM,2,0), I(CPX,ABS,4,0), I(SBC,ABS,4,0), I(INC,ABS,6,0), I(ISB,ABS,6,0),
/*F0*/ I(BEQ,REL,2,0), I(SBC,IZY,5,1), I(JAM,IMP,2,0), I(ISB,IZY,8,0), I(NOP,ZPX,4,0), I(SBC,ZPX,4,0), I(INC,ZPX,6,0), I(ISB,ZPX,6,0),
/*F8*/ I(SED,IMP,2,0), I(SBC,ABY,4,1), I(NOP,IMP,2,0), I(ISB,ABY,7,0), I(NOP,ABX,4,1), I(SBC,ABX,4,1), I(INC,ABX,7,0), I(ISB,ABX,7,0),
};


static const char *const op_names[] = {
    "adc","and","asl","bcc","bcs","beq","bit","bmi","bne","bpl","brk","bvc","bvs","clc","cld","cli","clv",
    "cmp","cpx","cpy","dec","dex","dey","eor","inc","inx","iny","jmp","jsr","lda","ldx","ldy","lsr","nop",
    "ora","pha","php","pla","plp","rol","ror","rti","rts","sbc","sec","sed","sei","sta","stx","sty",
    "tax","tay","tsx","txa","txs","tya",
    "slo","rla","sre","rra","sax","lax","dcp","isb","anc","alr","arr","xaa","axs","ahx","tas","shy","shx","las","jam"
};
/* bytes por modo de direccionamiento */
static const u8 mode_len[] = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2 };

/* Clases de acceso a memoria */
enum { K_READ, K_WRITE, K_RMW, K_OTHER };

static inline int op_kind(int op)
{
    switch (op) {
    case ADC: case AND: case BIT: case CMP: case CPX: case CPY: case EOR: case LDA: case LDX:
    case LDY: case ORA: case SBC: case LAX: case NOP: case ANC: case ALR: case ARR: case XAA:
    case AXS: case LAS: return K_READ;
    case STA: case STX: case STY: case SAX: case AHX: case TAS: case SHY: case SHX: return K_WRITE;
    case ASL: case LSR: case ROL: case ROR: case INC: case DEC:
    case SLO: case RLA: case SRE: case RRA: case DCP: case ISB: return K_RMW;
    }
    return K_OTHER;
}

static inline void set_nz(Cpu *c, u8 v)
{
    c->p = (c->p & ~(FLAG_N | FLAG_Z)) | (v & 0x80) | (v == 0 ? FLAG_Z : 0);
}
static inline void set_c(Cpu *c, int f) { c->p = (c->p & ~FLAG_C) | (f ? FLAG_C : 0); }

static inline void op_adc(Cpu *c, u8 v)
{
    unsigned s = c->a + v + (c->p & FLAG_C);
    c->p = (c->p & ~FLAG_V) | ((~(c->a ^ v) & (c->a ^ s) & 0x80) ? FLAG_V : 0);
    set_c(c, s > 0xFF);
    c->a = (u8)s;
    set_nz(c, c->a);
}
static inline void op_cmp(Cpu *c, u8 r, u8 v) { set_c(c, r >= v); set_nz(c, (u8)(r - v)); }
static inline u8 op_asl(Cpu *c, u8 v) { set_c(c, v & 0x80); v <<= 1; set_nz(c, v); return v; }
static inline u8 op_lsr(Cpu *c, u8 v) { set_c(c, v & 1); v >>= 1; set_nz(c, v); return v; }
static inline u8 op_rol(Cpu *c, u8 v) { u8 k = c->p & FLAG_C; set_c(c, v & 0x80); v = (u8)(v << 1) | k; set_nz(c, v); return v; }
static inline u8 op_ror(Cpu *c, u8 v) { u8 k = c->p & FLAG_C; set_c(c, v & 1); v = (v >> 1) | (k << 7); set_nz(c, v); return v; }

/* Operaciones que solo leen su operando (v). */
static inline void op_read(Cpu *c, int op, u8 v)
{
    switch (op) {
    case ADC: op_adc(c, v); break;
    case SBC: op_adc(c, ~v); break;
    case AND: c->a &= v; set_nz(c, c->a); break;
    case ORA: c->a |= v; set_nz(c, c->a); break;
    case EOR: c->a ^= v; set_nz(c, c->a); break;
    case BIT: c->p = (c->p & ~(FLAG_N | FLAG_V | FLAG_Z)) | (v & 0xC0) | ((c->a & v) ? 0 : FLAG_Z); break;
    case CMP: op_cmp(c, c->a, v); break;
    case CPX: op_cmp(c, c->x, v); break;
    case CPY: op_cmp(c, c->y, v); break;
    case LDA: c->a = v; set_nz(c, v); break;
    case LDX: c->x = v; set_nz(c, v); break;
    case LDY: c->y = v; set_nz(c, v); break;
    case LAX: c->a = c->x = v; set_nz(c, v); break;
    case ANC: c->a &= v; set_nz(c, c->a); set_c(c, c->a & 0x80); break;
    case ALR: c->a = op_lsr(c, c->a & v); break;
    case ARR:
        c->a = op_ror(c, c->a & v);
        set_c(c, c->a & 0x40);
        c->p = (c->p & ~FLAG_V) | (((c->a >> 6) ^ (c->a >> 5)) & 1 ? FLAG_V : 0);
        break;
    case XAA: c->a = c->x & v; set_nz(c, c->a); break;
    case AXS: { u8 t = c->a & c->x; set_c(c, t >= v); c->x = t - v; set_nz(c, c->x); break; }
    case LAS: c->a = c->x = c->sp = v & c->sp; set_nz(c, c->a); break;
    default: break;                              /* NOP */
    }
}

/* Leer-modificar-escribir: devuelve el valor nuevo (y aplica el efecto en A y flags). */
static inline u8 op_rmw(Cpu *c, int op, u8 v)
{
    u8 r;
    switch (op) {
    case INC: r = v + 1; set_nz(c, r); return r;
    case DEC: r = v - 1; set_nz(c, r); return r;
    case ASL: return op_asl(c, v);
    case LSR: return op_lsr(c, v);
    case ROL: return op_rol(c, v);
    case ROR: return op_ror(c, v);
    case SLO: r = op_asl(c, v); c->a |= r; set_nz(c, c->a); return r;
    case RLA: r = op_rol(c, v); c->a &= r; set_nz(c, c->a); return r;
    case SRE: r = op_lsr(c, v); c->a ^= r; set_nz(c, c->a); return r;
    case RRA: r = op_ror(c, v); op_adc(c, r); return r;
    case DCP: r = v - 1; op_cmp(c, c->a, r); return r;
    case ISB: r = v + 1; op_adc(c, ~r); return r;
    }
    return v;
}

/* Valor que escriben las instrucciones de almacenamiento estables. */
static inline u8 op_store(Cpu *c, int op)
{
    switch (op) {
    case STA: return c->a;
    case STX: return c->x;
    case STY: return c->y;
    default:  return c->a & c->x;               /* SAX */
    }
}

/* Operaciones de modo implícito (sin acceso a memoria). */
static inline void op_implied(Cpu *c, int op)
{
    switch (op) {
    case TAX: c->x = c->a; set_nz(c, c->x); break;
    case TAY: c->y = c->a; set_nz(c, c->y); break;
    case TXA: c->a = c->x; set_nz(c, c->a); break;
    case TYA: c->a = c->y; set_nz(c, c->a); break;
    case TSX: c->x = c->sp; set_nz(c, c->x); break;
    case TXS: c->sp = c->x; break;
    case INX: c->x++; set_nz(c, c->x); break;
    case INY: c->y++; set_nz(c, c->y); break;
    case DEX: c->x--; set_nz(c, c->x); break;
    case DEY: c->y--; set_nz(c, c->y); break;
    case CLC: c->p &= ~FLAG_C; break;
    case SEC: c->p |= FLAG_C; break;
    case CLI: c->p &= ~FLAG_I; break;
    case SEI: c->p |= FLAG_I; break;
    case CLD: c->p &= ~FLAG_D; break;
    case SED: c->p |= FLAG_D; break;
    case CLV: c->p &= ~FLAG_V; break;
    default: break;                              /* NOP */
    }
}

/* Condición de un salto relativo */
static inline int op_branch_taken(const Cpu *c, int op)
{
    switch (op) {
    case BCC: return !(c->p & FLAG_C);
    case BCS: return c->p & FLAG_C;
    case BEQ: return c->p & FLAG_Z;
    case BNE: return !(c->p & FLAG_Z);
    case BMI: return c->p & FLAG_N;
    case BPL: return !(c->p & FLAG_N);
    case BVC: return !(c->p & FLAG_V);
    default:  return c->p & FLAG_V;              /* BVS */
    }
}

/* Funciones del CPU que usa también el código recompilado (cpu6502.c) */
void nes_interrupt(NES *n, int brk);
void cpu_unstable_store(NES *n, int op, u16 base, u16 ea, int cross);

#endif
