/*
 * Lo común del recompilador del 68000 de GENRecomp y NEORecomp (F4.2 de docs/PLAN-ORDEN.md; Xalcker/retrorecomp#64): las
 * piezas del descubrimiento que son iguales en genrecomp.c y neorecomp.c. Como z80_recomp.h para el Z80, pero en este primer
 * paso solo se movieron, sin cambiar un bit (--codemap, --events y el proyecto generado iguales): cada recompilador lo incluye
 * en su descubrimiento, después de definir lo que estas funciones usan y es propio de la consola.
 *
 * Lo que tiene que estar definido antes de incluirlo:
 *   - los tipos u8, u16, u32, sx8, sx16, m68k_decode y los K_* (m68k_ops.h);
 *   - die(fmt, ...);
 *   - el espacio que se analiza: rom_size, romw(off), roml(off), y por palabra (desplazamiento / 2) is_ins e ins_addr;
 *   - la cola: los tipos Work y Queue, sure y spec, cur_from y cur_how, y HOW_AFTER, HOW_IMM y HOW_RUN (lo especulativo);
 *   - sane_abs(a, is_long): qué direcciones absolutas usaría un programa de esa consola.
 */
#ifndef M68K_RECOMP_H
#define M68K_RECOMP_H

/* Agrega (off, addr) a la cola, si es una instrucción nueva: lo especulativo (punteros sueltos, tras un final) va a la suya,
 * que se recorre cuando ya no queda nada seguro */
static void push_work(u32 off, u32 addr)
{
    if ((off & 1) || off >= rom_size || is_ins[off >> 1]) return;
    Queue *q = cur_how == HOW_AFTER || cur_how == HOW_IMM || cur_how == HOW_RUN ? &spec : &sure;
    if (q->n == q->cap) {
        q->cap = q->cap ? q->cap * 2 : 1024;
        q->w = realloc(q->w, q->cap * sizeof *q->w);
        if (!q->w) die("sin memoria");
    }
    q->w[q->n++] = (Work){ off, addr & 0xFFFFFF, cur_from, (u8)cur_how };
}

/* Destino constante de JMP/JSR (absoluto corto o largo, o relativo al PC); -1 si es indirecto */
static long jump_target(size_t off, u32 addr, u16 op)
{
    int mode = op >> 3 & 7, reg = op & 7;
    if (mode != 7) return -1;
    if (reg == 0) return (long)(sx16(romw(off + 2)) & 0xFFFFFF);
    if (reg == 1) return (long)(roml(off + 2) & 0xFFFFFF);
    if (reg == 2) return (long)((addr + 2 + sx16(romw(off + 2))) & 0xFFFFFF);
    return -1;
}

/* Las palabras de extensión de un operando con dirección absoluta: ¿son razonables? i = índice de
 * la palabra del operando dentro de la instrucción (1 = la primera de extensión) */
static int sane_ea(size_t off, int mode, int reg, int sz, int *i)
{
    int ok = 1;
    if (mode == 5 || mode == 6) (*i)++;
    else if (mode == 7) {
        switch (reg) {
        case 0: ok = sane_abs(sx16(romw(off + 2 * (size_t)*i)), 0); (*i)++; break;
        case 1: ok = sane_abs(roml(off + 2 * (size_t)*i), 1); *i += 2; break;
        case 2: case 3: (*i)++; break;
        case 4: *i += sz == 2 ? 2 : 1; break;
        }
    }
    return ok;
}

/* Las direcciones absolutas de los operandos de una instrucción son razonables (solo MOVE y las
 * operaciones de un operando; lo demás se acepta) */
static int sane_operands(size_t off, u16 op, int kind)
{
    int i = 1;
    switch (kind) {
    case K_MOVE: case K_MOVEA: {
        int s = (op >> 12 & 3) == 1 ? 0 : (op >> 12 & 3) == 3 ? 1 : 2;
        if (!sane_ea(off, op >> 3 & 7, op & 7, s, &i)) return 0;
        return sane_ea(off, op >> 6 & 7, op >> 9 & 7, s, &i); }
    case K_TST: case K_UNARY: case K_ALU: case K_QUICK:
        return sane_ea(off, op >> 3 & 7, op & 7, op >> 6 & 3, &i);
    case K_JMP: case K_JSR: case K_LEA: case K_PEA: {
        int mode = op >> 3 & 7, reg = op & 7;
        if (mode == 7 && reg == 1) return sane_abs(roml(off + 2), 1);
        return 1; }
    default: return 1;
    }
}

/* --- Tablas de saltos --- */

/* Las últimas instrucciones del recorrido actual: sirven para reconocer cómo se arma un salto
 * indirecto (la tabla y el registro con el que se indexa) */
static u32 hist[16];
static int nhist;

static void hist_push(u32 off)
{
    if (nhist == 16) { memmove(hist, hist + 1, 15 * sizeof *hist); nhist = 15; }
    hist[nhist++] = off;
}

/* ¿La instrucción en off cambia el registro de direcciones An? */
static int writes_areg(u32 off, int an)
{
    u16 op = romw(off);
    switch (m68k_decode(op)) {
    case K_MOVEA: case K_LEA: return (op >> 9 & 7) == an;
    case K_ALUA: return (op >> 12) != 0xB && (op >> 9 & 7) == an;
    case K_QUICK: return (op >> 3 & 7) == 1 && (op & 7) == an;
    case K_EXG: { int om = op >> 3 & 0x1F; return (om == 0x09 && ((op >> 9 & 7) == an || (op & 7) == an)) || (om == 0x11 && (op & 7) == an); }
    case K_MOVEMR: return (romw(off + 2) >> (8 + an)) & 1;
    case K_UNLK: case K_MUSP: return (op & 7) == an;
    default: return 0;
    }
}

/* Valor constante de An antes de la instrucción hist[h], si lo carga un LEA o un MOVEA.L #imm
 * reciente sin que nada lo cambie después; 0 si no se sabe */
static int known_areg(int h, int an, u32 *val)
{
    for (int i = h - 1; i >= 0 && i >= h - 10; i--) {
        u32 o = hist[i];
        if (!writes_areg(o, an)) continue;
        u16 op = romw(o);
        u32 addr = ins_addr[o >> 1];
        if (m68k_decode(op) == K_LEA) {
            int mode = op >> 3 & 7, reg = op & 7;
            if (mode == 7 && reg == 2) { *val = (addr + 2 + sx16(romw(o + 2))) & 0xFFFFFF; return 1; }
            if (mode == 7 && reg == 1) { *val = roml(o + 2) & 0xFFFFFF; return 1; }
            if (mode == 7 && reg == 0) { *val = sx16(romw(o + 2)) & 0xFFFFFF; return 1; }
            return 0;
        }
        if (op == (0x207C | an << 9)) { *val = roml(o + 2) & 0xFFFFFF; return 1; }    /* MOVEA.L #imm,An */
        return 0;
    }
    return 0;
}

/* Base de una lectura indexada (d8,PC,Xn) o (d8,An,Xn) con An conocido, en la instrucción hist[h]
 * con el operando fuente en (mode, reg) y su palabra de extensión `x` en dirección `xaddr` */
static int indexed_base(int h, int mode, int reg, u16 x, u32 xaddr, u32 *base)
{
    if (mode == 7 && reg == 3) { *base = (xaddr + sx8(x & 0xFF)) & 0xFFFFFF; return 1; }
    if (mode == 6) {
        u32 a;
        if (!known_areg(h, reg, &a)) return 0;
        *base = (a + sx8(x & 0xFF)) & 0xFFFFFF;
        return 1;
    }
    return 0;
}

/* El registro índice: ¿lo cargó un MOVE desde una tabla indexada? Busca hacia atrás desde hist[h]
 * el último MOVE (o MOVEA) que lo tenga de destino. Devuelve el tamaño (1 = palabra, 2 = largo) y
 * la base de esa tabla, o 0. */
static int loaded_from_table(int h, int is_a, int r, u32 *base)
{
    for (int i = h - 1; i >= 0 && i >= h - 8; i--) {
        u32 o = hist[i];
        u16 op = romw(o);
        int kind = m68k_decode(op);
        if (kind != K_MOVE && kind != K_MOVEA) continue;
        int dmode = op >> 6 & 7, dreg = op >> 9 & 7;
        if (dmode != (is_a ? 1 : 0) || dreg != r) continue;
        int sz = (op >> 12 & 3) == 3 ? 1 : (op >> 12 & 3) == 2 ? 2 : 0;
        int smode = op >> 3 & 7, sreg = op & 7;
        if (sz == 0 || !((smode == 7 && sreg == 3) || smode == 6)) return 0;
        u32 addr = ins_addr[o >> 1];
        if (!indexed_base(i, smode, sreg, romw(o + 2), addr + 2, base)) return 0;
        return sz;
    }
    return 0;
}

#endif
