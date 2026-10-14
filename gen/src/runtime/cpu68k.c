/*
 * 68000: intérprete. La semántica está en m68k_ops.h; aquí van el decodificador (qué familia es
 * cada opcode y si es válido), las excepciones, las interrupciones y el paso de una instrucción.
 */
#include <setjmp.h>
#include <string.h>

#include "m68k_ops.h"

/* --- Decodificador --- */

int m68k_decode(uint16_t op)
{
    int mode = op >> 3 & 7, reg = op & 7, sz = op >> 6 & 3, opm = op >> 6 & 7;
    unsigned ea = m68k_ea_bit(mode, reg);
    switch (op >> 12) {
    case 0x0:
        if (op & 0x100) {
            if (mode == 1) return K_MOVEP;
            if (sz == 0) return (ea & EA_DATA) ? K_BIT : K_ILLEGAL;         /* BTST Dn,<ea> admite #imm y PC */
            return (ea & EA_DALT) ? K_BIT : K_ILLEGAL;
        }
        switch (op >> 9 & 7) {
        case 4:
            if (sz == 0) return (ea & (EA_DATA & ~(unsigned)EA_IMM)) ? K_BIT : K_ILLEGAL;
            return (ea & EA_DALT) ? K_BIT : K_ILLEGAL;
        case 0: case 1: case 5:
            if ((op & 0xFF) == 0x3C) return K_ICCR;
            if ((op & 0xFF) == 0x7C) return K_ISR;
            /* fallthrough */
        case 2: case 3: case 6:
            if (sz == 3) return K_ILLEGAL;
            return (ea & EA_DALT) ? K_IMM : K_ILLEGAL;
        default: return K_ILLEGAL;
        }
    case 0x1: case 0x2: case 0x3: {
        int dmode = op >> 6 & 7, dreg = op >> 9 & 7;
        if (!ea || ((op >> 12) == 1 && mode == 1)) return K_ILLEGAL;
        if (dmode == 1) return (op >> 12) == 1 ? K_ILLEGAL : K_MOVEA;
        return (m68k_ea_bit(dmode, dreg) & EA_DALT) ? K_MOVE : K_ILLEGAL;
    }
    case 0x4:
        if (op & 0x100) {
            if (opm == 7) return (ea & EA_CTRL) ? K_LEA : K_ILLEGAL;
            if (opm == 6) return (ea & EA_DATA) ? K_CHK : K_ILLEGAL;
            return K_ILLEGAL;
        }
        switch (op >> 8 & 0xF) {
        case 0x0: return (ea & EA_DALT) ? (sz == 3 ? K_MOVEFSR : K_UNARY) : K_ILLEGAL;
        case 0x2: return sz != 3 && (ea & EA_DALT) ? K_UNARY : K_ILLEGAL;
        case 0x4: if (sz == 3) return (ea & EA_DATA) ? K_MOVETCCR : K_ILLEGAL;
                  return (ea & EA_DALT) ? K_UNARY : K_ILLEGAL;
        case 0x6: if (sz == 3) return (ea & EA_DATA) ? K_MOVETSR : K_ILLEGAL;
                  return (ea & EA_DALT) ? K_UNARY : K_ILLEGAL;
        case 0x8:
            if (sz == 0) return (ea & EA_DALT) ? K_NBCD : K_ILLEGAL;
            if (sz == 1) return mode == 0 ? K_SWAP : (ea & EA_CTRL) ? K_PEA : K_ILLEGAL;
            if (mode == 0) return K_EXT;
            return (ea & ((EA_CTRL & ~(unsigned)(EA_PC16 | EA_PC8)) | EA_PD)) ? K_MOVEM : K_ILLEGAL;
        case 0xA:
            if (op == 0x4AFC) return K_ILLEGAL;
            if (sz == 3) return (ea & EA_DALT) ? K_TAS : K_ILLEGAL;
            return (ea & EA_DALT) ? K_TST : K_ILLEGAL;
        case 0xC:
            if (sz < 2) return K_ILLEGAL;
            return (ea & (EA_CTRL | EA_PI)) ? K_MOVEMR : K_ILLEGAL;
        case 0xE:
            if (sz == 2) return (ea & EA_CTRL) ? K_JSR : K_ILLEGAL;
            if (sz == 3) return (ea & EA_CTRL) ? K_JMP : K_ILLEGAL;
            if (sz == 0) return K_ILLEGAL;
            switch (op >> 3 & 7) {
            case 0: case 1: return K_TRAP;
            case 2: return K_LINK;
            case 3: return K_UNLK;
            case 4: case 5: return K_MUSP;
            case 6:
                switch (op & 7) {
                case 0: return K_RESET;
                case 1: return K_NOP;
                case 2: return K_STOP;
                case 3: return K_RTE;
                case 5: return K_RTS;
                case 6: return K_TRAPV;
                case 7: return K_RTR;
                default: return K_ILLEGAL;
                }
            default: return K_ILLEGAL;
            }
        default: return K_ILLEGAL;
        }
    case 0x5:
        if (sz == 3) return mode == 1 ? K_DBCC : (ea & EA_DALT) ? K_SCC : K_ILLEGAL;
        if (mode == 1 && sz == 0) return K_ILLEGAL;
        return (ea & EA_ALT) ? K_QUICK : K_ILLEGAL;
    case 0x6: return K_BCC;
    case 0x7: return (op & 0x100) ? K_ILLEGAL : K_MOVEQ;
    case 0x8: case 0xC:
        if (opm == 3 || opm == 7) return (ea & EA_DATA) ? ((op >> 12) == 8 ? K_DIV : K_MUL) : K_ILLEGAL;
        if (opm == 4 && mode <= 1) return K_BCD;
        if ((op >> 12) == 0xC && ((opm == 5 && mode <= 1) || (opm == 6 && mode == 1))) return K_EXG;
        if (opm < 3) return (ea & EA_DATA) ? K_ALU : K_ILLEGAL;
        return (ea & EA_MALT) ? K_ALU : K_ILLEGAL;
    case 0x9: case 0xD:
        if (opm == 3 || opm == 7) return ea ? K_ALUA : K_ILLEGAL;
        if (opm >= 4 && mode <= 1) return K_ADDX;
        if (opm < 3) return ea && !(sz == 0 && mode == 1) ? K_ALU : K_ILLEGAL;
        return (ea & EA_MALT) ? K_ALU : K_ILLEGAL;
    case 0xA: return K_LINEA;
    case 0xB:
        if (opm == 3 || opm == 7) return ea ? K_ALUA : K_ILLEGAL;
        if (opm < 3) return ea && !(sz == 0 && mode == 1) ? K_ALU : K_ILLEGAL;
        if (mode == 1) return K_CMPM;
        return (ea & EA_DALT) ? K_ALU : K_ILLEGAL;
    case 0xE:
        if (sz == 3) return !(op & 0x800) && (ea & EA_MALT) ? K_SHIFTM : K_ILLEGAL;
        return K_SHIFTR;
    default: return K_LINEF;
    }
}

/* Palabras de extensión de un operando */
static int ea_words(int mode, int reg, int sz)
{
    if (mode == 5 || mode == 6) return 1;
    if (mode != 7) return 0;
    switch (reg) {
    case 1: return 2;
    case 4: return sz == 2 ? 2 : 1;
    default: return 1;
    }
}

int m68k_ext_words(uint16_t op)
{
    int mode = op >> 3 & 7, reg = op & 7, sz = op >> 6 & 3;
    switch (m68k_decode(op)) {
    case K_IMM:      return (sz == 2 ? 2 : 1) + ea_words(mode, reg, sz);
    case K_ICCR: case K_ISR: case K_MOVEP: case K_LINK: case K_STOP: case K_DBCC: return 1;
    case K_BIT:      return ((op & 0x100) ? 0 : 1) + ea_words(mode, reg, 0);
    case K_MOVE: case K_MOVEA: {
        int s = move_size(op);
        return ea_words(mode, reg, s) + ea_words(op >> 6 & 7, op >> 9 & 7, s);
    }
    case K_UNARY: case K_TST: case K_ALU: return ea_words(mode, reg, sz & 3);
    case K_MOVEFSR: case K_MOVETCCR: case K_MOVETSR: case K_CHK: case K_MUL: case K_DIV:
    case K_SHIFTM:   return ea_words(mode, reg, 1);
    case K_NBCD: case K_TAS: case K_SCC: return ea_words(mode, reg, 0);
    case K_LEA: case K_PEA: case K_JSR: case K_JMP: return ea_words(mode, reg, 2);
    case K_MOVEM: case K_MOVEMR: return 1 + ea_words(mode, reg, 2);
    case K_QUICK:    return ea_words(mode, reg, sz);
    case K_BCC:      return (op & 0xFF) ? 0 : 1;
    case K_ALUA:     return ea_words(mode, reg, (op & 0x100) ? 2 : 1);
    default:         return 0;
    }
}

/* --- Excepciones --- */

uint16_t m68k_get_sr(const M68k *c) { return sr_of(c); }
void m68k_set_sr(M68k *c, uint16_t sr) { sr_set(c, sr); }

/* Pasa a supervisor (sin traza) y devuelve el SR de antes */
static u16 enter_super(M68k *c)
{
    u16 sr = sr_of(c);
    if (!c->s) { u32 t = c->a[7]; c->a[7] = c->osp; c->osp = t; c->s = 1; }
    c->t = 0;
    return sr;
}

u32 m68k_exception(X *x, int vec, u32 push_pc, int pre_idle)
{
    M68k *c = x->c;
    u16 sr = enter_super(c);
    c->stopped = 0;
    x->g->exc_count[vec]++;
    idle(x, (u32)pre_idle);
    c->a[7] -= 6;
    wr(x, c->a[7] + 4, 1, push_pc & 0xFFFF);
    wr(x, c->a[7], 1, sr);
    wr(x, c->a[7] + 2, 1, push_pc >> 16);
    u32 h = rd(x, (u32)vec * 4, 2);
    return jump(x, h);
}

void m68k_address_error(X *x, u32 addr, int flags)
{
    M68k *c = x->c;
    struct Gen *g = x->g;
    int was_super = c->s;
    u16 sr = enter_super(c);
    c->stopped = 0;
    g->exc_count[VEC_ADDRESS]++;
    u16 ir = x->op;
    /* Estado: los bits altos son los del IR; R/W (1 = lectura), I/N (1 = lectura de programa) y el
     * código de función (supervisor/usuario, programa/datos). Al saltar a una dirección impar se
     * apila el destino menos 4; en un acceso de datos, la dirección de la última palabra leída. */
    int insn = flags & AE_INSN;
    u16 status = (u16)((ir & 0xFFE0) | (flags & (AE_READ | AE_INSN)) | (was_super ? 4 : 0) | (insn ? 2 : 1));
    u32 push_pc = insn ? addr - 4 : x->lf - 2;
    idle(x, 6);
    /* Una segunda falta durante el error de dirección detiene el CPU */
    u32 sp = c->a[7] - 14;
    if (sp & 1) { c->halted = 1; longjmp(*(jmp_buf *)g->jb, 1); }
    c->a[7] = sp;
    m68k_wr16(g, A24(sp + 12), (u16)push_pc);
    m68k_wr16(g, A24(sp + 8), sr);
    m68k_wr16(g, A24(sp + 10), (u16)(push_pc >> 16));
    m68k_wr16(g, A24(sp + 6), ir);
    m68k_wr16(g, A24(sp + 4), (u16)addr);
    m68k_wr16(g, A24(sp), status);
    m68k_wr16(g, A24(sp + 2), (u16)(addr >> 16));
    u32 h = (u32)m68k_rd16(g, VEC_ADDRESS * 4) << 16 | m68k_rd16(g, VEC_ADDRESS * 4 + 2);
    if (h & 1) { c->halted = 1; longjmp(*(jmp_buf *)g->jb, 1); }
    if (x->ext) { idle(x, 8); c->pc = h; c->ir = m68k_peek16(g, A24(h)); c->irc = m68k_peek16(g, A24(h + 2)); }
    else { c->pc = h; c->ir = m68k_fetch(g, A24(h)); c->irc = m68k_fetch(g, A24(h + 2)); }
    longjmp(*(jmp_buf *)g->jb, 1);
}

/* --- Intérprete --- */

typedef u32 (*KindFn)(struct Gen *, u16, const u16 *, u32);

#define I(name) static u32 i_##name(struct Gen *g, u16 op, const u16 *e, u32 pc) { (void)e; return k_##name(g, op, NULL, pc); }
I(ILLEGAL) I(LINEA) I(LINEF) I(IMM) I(ICCR) I(ISR) I(BIT) I(MOVEP) I(MOVE) I(MOVEA) I(UNARY) I(MOVEFSR)
I(MOVETCCR) I(MOVETSR) I(CHK) I(LEA) I(NBCD) I(SWAP) I(PEA) I(EXT) I(MOVEM) I(MOVEMR) I(TST) I(TAS) I(TRAP)
I(LINK) I(UNLK) I(MUSP) I(RESET) I(NOP) I(STOP) I(RTE) I(RTS) I(TRAPV) I(RTR) I(JSR) I(JMP) I(QUICK) I(SCC)
I(DBCC) I(BCC) I(MOVEQ) I(ALU) I(ALUA) I(MUL) I(DIV) I(BCD) I(ADDX) I(CMPM) I(EXG) I(SHIFTR) I(SHIFTM)
#undef I

static const KindFn kind_fn[K_COUNT] = {
#define F(name) [K_##name] = i_##name
    F(ILLEGAL), F(LINEA), F(LINEF), F(IMM), F(ICCR), F(ISR), F(BIT), F(MOVEP), F(MOVE), F(MOVEA), F(UNARY),
    F(MOVEFSR), F(MOVETCCR), F(MOVETSR), F(CHK), F(LEA), F(NBCD), F(SWAP), F(PEA), F(EXT), F(MOVEM),
    F(MOVEMR), F(TST), F(TAS), F(TRAP), F(LINK), F(UNLK), F(MUSP), F(RESET), F(NOP), F(STOP), F(RTE),
    F(RTS), F(TRAPV), F(RTR), F(JSR), F(JMP), F(QUICK), F(SCC), F(DBCC), F(BCC), F(MOVEQ), F(ALU),
    F(ALUA), F(MUL), F(DIV), F(BCD), F(ADDX), F(CMPM), F(EXG), F(SHIFTR), F(SHIFTM),
#undef F
};

static u8 kind_tab[65536];
static int kind_ready;

static void kinds_init(void)
{
    for (u32 op = 0; op < 65536; op++) kind_tab[op] = (u8)m68k_decode((u16)op);
    kind_ready = 1;
}

/* Interrupción de nivel `lvl` (autovector, como en el Genesis) */
static void interrupt(struct Gen *g, int lvl)
{
    M68k *c = &g->cpu;
    X xs = { g, c, NULL, 0, c->pc, c->pc + 2, c->ir, c->pc + 2 };
    u16 sr = enter_super(c);
    c->ipm = (u8)lvl;
    c->stopped = 0;
    g->exc_count[VEC_AUTO1 + lvl - 1]++;
    m68k_int_ack(g, lvl);
    idle(&xs, 16);
    c->a[7] -= 6;
    wr(&xs, c->a[7] + 4, 1, c->pc & 0xFFFF);
    wr(&xs, c->a[7], 1, sr);
    wr(&xs, c->a[7] + 2, 1, c->pc >> 16);
    jump(&xs, rd(&xs, (u32)(VEC_AUTO1 + lvl - 1) * 4, 2));
}

/* Con la traza activa, lo que el intérprete ejecuta desde RAM (gen_trace_start pone gen_ram_note; un
 * puntero para que el CPU no dependa del resto de la consola, como en m68ktest) */
void (*m68k_ram_trace)(struct Gen *g, u32 pc);

/* Una instrucción (o la interrupción pendiente). El error de dirección sale con longjmp a g->jb. */
static void step_body(struct Gen *g)
{
    M68k *c = &g->cpu;
    if (c->halted) { m68k_idle(g, 4); return; }
    int lvl = c->ipl;
    if (lvl > c->ipm || (lvl == 7 && c->nmi_edge)) {
        c->nmi_edge = 0;
        interrupt(g, lvl);
    } else if (c->stopped) {
        m68k_idle(g, 4);
    } else {
        int trace = c->t;
        u16 op = c->ir;
        if (g->trace && c->pc < 0x400000) {             /* traza de lo que ejecuta el intérprete en ROM */
            u32 off = cart_rom_offset(&g->cart, c->pc);
            if (g->cart.mapper == MAPPER_NONE) off &= g->cart.rom_mask;
            if (off < g->cart.rom_size && !g->trace[off >> 1]) g->trace[off >> 1] = 0x80000000u | c->pc;
        } else if (g->trace && (c->pc & 0xFF0000) == 0xFF0000 && m68k_ram_trace) {
            m68k_ram_trace(g, c->pc);                   /* y en RAM (gen_ram_note) */
        }
        kind_fn[kind_tab[op]](g, op, NULL, c->pc);
        if (trace) {
            X xs = { g, c, NULL, 0, c->pc, c->pc + 2, c->ir, c->pc + 2 };
            m68k_exception(&xs, VEC_TRACE, c->pc, 4);
        }
    }
}

int m68k_step(struct Gen *g)
{
    u64 c0 = g->cyc;
    if (!kind_ready) kinds_init();
    jmp_buf jb;
    void *prev = g->jb;
    g->jb = &jb;
    if (setjmp(jb) == 0) step_body(g);
    g->jb = prev;
    return (int)(g->cyc - c0);
}

/* Muchas instrucciones con un solo setjmp (en macOS, setjmp guarda la máscara de señales con una
 * llamada al sistema: uno por instrucción costaba la mitad del tiempo). after() corre después de
 * cada instrucción; si devuelve distinto de 0, se para. */
void m68k_run(struct Gen *g, int (*after)(struct Gen *))
{
    if (!kind_ready) kinds_init();
    jmp_buf jb;
    void *prev = g->jb;
    g->jb = &jb;
    if (setjmp(jb) && after(g)) goto out;      /* un error de dirección terminó la instrucción */
    for (;;) {
        step_body(g);
        if (after(g)) break;
    }
out:
    g->jb = prev;
}

/* Como m68k_run, pero antes de cada instrucción prueba native(): el código recompilado. native()
 * devuelve 0 si no tiene esa instrucción (la ejecuta el intérprete), 1 si corrió (y ya puso al día
 * VDP y Z80 tras cada instrucción) y 2 si además terminó el cuadro. Solo se llama sin interrupción
 * pendiente, sin traza, sin STOP y con el CPU andando: eso lo atiende el intérprete. */
void m68k_run_hybrid(struct Gen *g, int (*after)(struct Gen *), int (*native)(struct Gen *))
{
    if (!kind_ready) kinds_init();
    jmp_buf jb;
    void *prev = g->jb;
    g->jb = &jb;
    if (setjmp(jb) && after(g)) goto out;
    for (;;) {
        if (m68k_native_ok(&g->cpu)) {
            int r = native(g);
            if (r == 2) break;
            if (r) continue;
        }
        step_body(g);
        if (after(g)) break;
    }
out:
    g->jb = prev;
}

void m68k_reset(struct Gen *g)
{
    M68k *c = &g->cpu;
    memset(c, 0, sizeof *c);
    c->s = 1;
    c->ipm = 7;
    jmp_buf jb;
    void *prev = g->jb;
    g->jb = &jb;
    if (setjmp(jb) == 0) {
        X xs = { g, c, NULL, 0, 0, 2, 0, 2 };
        m68k_idle(g, 16);
        c->a[7] = rd(&xs, 0, 2);
        jump(&xs, rd(&xs, 4, 2));
    }
    g->jb = prev;
}
