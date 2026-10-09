/*
 * CPU 6502 del 2A03 (sin modo decimal), con los opcodes no oficiales.
 * La tabla de opcodes y la semántica de cada operación están en cpu_ops.h
 * (compartidas con el código recompilado). Cada acceso al bus, incluidas las
 * lecturas y escrituras falsas, avanza un ciclo de la consola: los ciclos de
 * cada instrucción salen solos y las columnas de ciclos de la tabla solo
 * documentan lo esperado.
 */
#include "cpu_ops.h"

#define RD(a) nes_cpu_read(n, (u16)(a))
#define WR(a, v) nes_cpu_write(n, (u16)(a), (v))

static inline void push(NES *n, u8 v) { WR(0x100 | n->cpu.sp, v); n->cpu.sp--; }
static inline u8 pull(NES *n) { n->cpu.sp++; return RD(0x100 | n->cpu.sp); }

/* NMI, IRQ (brk = 0) o BRK (brk = 1, PC ya apunta al byte de relleno). 7 ciclos.
 * Un NMI que llega mientras se empujan los registros secuestra el vector. */
void nes_interrupt(NES *n, int brk)
{
    Cpu *c = &n->cpu;
    if (brk) RD(c->pc++); else { RD(c->pc); RD(c->pc); }
    push(n, c->pc >> 8);
    push(n, c->pc & 0xFF);
    push(n, (c->p | FLAG_U) | (brk ? FLAG_B : 0));
    c->p |= FLAG_I;
    u16 vec = 0xFFFE;
    if (n->need_nmi) { n->need_nmi = 0; vec = 0xFFFA; }
    c->pc = RD(vec) | (RD(vec + 1) << 8);
    n->prev_need_nmi = 0; n->prev_run_irq = 0;
}

/* AHX, TAS, SHY, SHX: escriben un registro AND (byte alto de la base + 1) */
void cpu_unstable_store(NES *n, int op, u16 base, u16 ea, int cross)
{
    Cpu *c = &n->cpu;
    u8 reg = op == SHY ? c->y : op == SHX ? c->x : (c->a & c->x);
    if (op == TAS) c->sp = c->a & c->x;
    u8 val = reg & ((base >> 8) + 1);
    u16 dst = ea;
    if (cross) dst = (u16)((val << 8) | (ea & 0xFF));
    WR(dst, val);
}

int cpu_step(NES *n)
{
    Cpu *c = &n->cpu;
    u64 start = n->cyc;
    if (c->jammed) { RD(c->pc); return (int)(n->cyc - start); }

    u16 pc0 = c->pc;
    u8 opc = RD(c->pc++);
    if (n->trace && pc0 >= 0x8000) nes_trace_note(n, pc0);
    const Ins *in = &ins_tab[opc];
    int op = in->op, mode = in->mode;
    u16 ea = 0, base = 0;
    int cross = 0;
    u8 v = 0;

    /* Instrucciones con secuencia propia */
    switch (op) {
    case JSR: {
        u16 lo = RD(c->pc++);
        RD(0x100 | c->sp);
        push(n, c->pc >> 8); push(n, c->pc & 0xFF);
        c->pc = (u16)((RD(c->pc) << 8) | lo);
        goto done; }
    case RTS: {
        RD(c->pc); RD(0x100 | c->sp);
        u16 lo = pull(n); u16 hi = pull(n);
        c->pc = (u16)((hi << 8) | lo);
        RD(c->pc); c->pc++;
        goto done; }
    case RTI: {
        RD(c->pc); RD(0x100 | c->sp);
        c->p = (pull(n) & ~FLAG_B) | FLAG_U;
        u16 lo = pull(n); u16 hi = pull(n);
        c->pc = (u16)((hi << 8) | lo);
        goto done; }
    case BRK: nes_interrupt(n, 1); goto done;
    case PHA: RD(c->pc); push(n, c->a); goto done;
    case PHP: RD(c->pc); push(n, c->p | FLAG_B | FLAG_U); goto done;
    case PLA: RD(c->pc); RD(0x100 | c->sp); c->a = pull(n); set_nz(c, c->a); goto done;
    case PLP: RD(c->pc); RD(0x100 | c->sp); c->p = (pull(n) & ~FLAG_B) | FLAG_U; goto done;
    case BCC: case BCS: case BEQ: case BMI: case BNE: case BPL: case BVC: case BVS: {
        int t = op_branch_taken(c, op);
        int8_t off = (int8_t)RD(c->pc++);
        if (t) {
            RD(c->pc);                                   /* lectura falsa */
            u16 dst = (u16)(c->pc + off);
            if ((c->pc ^ dst) & 0xFF00) RD((c->pc & 0xFF00) | (dst & 0xFF));
            c->pc = dst;
        }
        goto done; }
    }

    int kind = op_kind(op);
    switch (mode) {
    case IMP: case ACC: RD(c->pc); break;                /* lectura falsa del siguiente byte */
    case IMM: ea = c->pc++; break;
    case ZP:  ea = RD(c->pc++); break;
    case ZPX: ea = RD(c->pc++); RD(ea); ea = (u8)(ea + c->x); break;
    case ZPY: ea = RD(c->pc++); RD(ea); ea = (u8)(ea + c->y); break;
    case ABS: ea = RD(c->pc++); ea |= RD(c->pc++) << 8; break;
    case ABX: case ABY:
        base = RD(c->pc++); base |= RD(c->pc++) << 8;
        ea = base + (mode == ABX ? c->x : c->y); cross = (base ^ ea) & 0xFF00;
        if (kind != K_READ || cross) RD((base & 0xFF00) | (ea & 0xFF));
        break;
    case IND: {
        u16 p = RD(c->pc++); p |= RD(c->pc++) << 8;
        ea = RD(p); ea |= RD((p & 0xFF00) | ((p + 1) & 0xFF)) << 8;   /* bug de página */
        break; }
    case IZX: {
        u8 z = RD(c->pc++); RD(z); z += c->x;
        ea = RD(z); ea |= RD((u8)(z + 1)) << 8; break; }
    case IZY: {
        u8 z = RD(c->pc++);
        base = RD(z); base |= RD((u8)(z + 1)) << 8;
        ea = base + c->y; cross = (base ^ ea) & 0xFF00;
        if (kind != K_READ || cross) RD((base & 0xFF00) | (ea & 0xFF));
        break; }
    }

    switch (kind) {
    case K_READ:
        if (mode != IMP) v = RD(ea);
        op_read(c, op, v);
        break;
    case K_WRITE:
        if (op == AHX || op == TAS || op == SHY || op == SHX) cpu_unstable_store(n, op, base, ea, cross);
        else WR(ea, op_store(c, op));
        break;
    case K_RMW:
        if (mode == ACC) c->a = op_rmw(c, op, c->a);
        else {
            v = RD(ea);
            WR(ea, v);                                   /* escritura falsa del valor viejo */
            WR(ea, op_rmw(c, op, v));
        }
        break;
    default:
        if (op == JMP) c->pc = ea;
        else if (op == JAM) { c->jammed = 1; c->pc--; }
        else op_implied(c, op);
        break;
    }

done:
    /* Interrupciones: lo que se vio al final del penúltimo ciclo manda */
    if (n->prev_need_nmi || n->prev_run_irq) nes_interrupt(n, 0);
    return (int)(n->cyc - start);
}
