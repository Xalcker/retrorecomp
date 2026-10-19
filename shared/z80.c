/*
 * Z80: intérprete completo, con las instrucciones y banderas no documentadas (X/Y, MEMPTR/WZ, Q,
 * mitades de IX/IY, SLL, la copia del resultado de DDCB/FDCB a un registro, las banderas de las
 * instrucciones de bloque al repetir), las interrupciones IM 0/1/2 y la NMI (el Neo Geo la usa para
 * cada comando del 68000; el Master System, para la Pausa; el Genesis no). Se comprueba con las
 * SingleStepTests del Z80 y con z80test --int y --nmi (z80test.c). Común a GENRecomp, NEORecomp y
 * SMSRecomp (retrorecomp/shared).
 *
 * La semántica de cada instrucción está en z80_ops.h, la misma que usa el código generado (z80test
 * --recomp la prueba por ese camino); aquí van las interrupciones, el HALT y el decodificador.
 *
 * Los ciclos T salen de la instrucción (no se cuentan accesos al bus; en el Genesis el Z80 solo
 * espera al leer el bus del 68000, y eso lo agrega su z80bus.c).
 */
#include <stdio.h>
#include <string.h>

#include "z80_ops.h"

Z80Stat z80_stat;
const void *z80_stat_of;
const void *z80_interp_only;

void z80_stat_print(void)
{
    uint64_t n = z80_stat.native + z80_stat.interp + z80_stat.skipped;
    if (!n) return;
    printf("Z80: %llu ciclos T: %.1f%% nativo, %.1f%% interpretado, %.1f%% saltado (HALT y bucles de espera)\n",
           (unsigned long long)n, 100.0 * (double)z80_stat.native / (double)n,
           100.0 * (double)z80_stat.interp / (double)n, 100.0 * (double)z80_stat.skipped / (double)n);
}

void z80_reset(Z80 *z)
{
    memset(z, 0, sizeof *z);
    z->a = z->f = 0xFF;
    z->sp = 0xFFFF;
}

/* La entrada a una NMI o a una interrupción, o un paso de HALT: lo que hace z80_step sin ejecutar una instrucción. Devuelve
 * los ciclos T, o 0 si toca ejecutar una instrucción (y entonces no cambia nada). Aparte del intérprete para que el código
 * recompilado lo use sin pasar por él (F4.1 de docs/PLAN-ORDEN.md) */
int z80_service(struct Z80_HOST *g, Z80 *z)
{
    if (!z->nmi && !(z->irq && z->iff1 && !z->ei) && !z->halted) return 0;
    Z80X s = { g, z, NULL, 0, z->pc, 0, 0 };
    int ei_was = z->ei;
    z->ei = 0;
    int t;
    if (z->nmi) {
        /* NMI: no se enmascara y no espera a la instrucción que sigue a EI. Borra IFF1 y deja IFF2
         * como estaba (RETN lo devuelve a IFF1) y salta a $0066; 11 ciclos T */
        z->nmi = 0;
        z->halted = 0;                  /* HALT ya dejó el PC en la instrucción siguiente: es la que se apila */
        z->iff1 = 0;
        z_inc_r(z);
        z_push(&s, z->pc);
        z->pc = 0x66;
        z->wz = z->pc;
        z->q = 0;
        z->p = 0;
        return 11;
    }
    if (z->irq && z->iff1 && !ei_was) {
        /* Interrupción: el bus de datos lee $FF (IM 0 ejecuta RST 38h). Desde un HALT se apila la
         * instrucción siguiente, donde ya quedó el PC (en GENRecomp se apilaba una más: corregido aquí) */
        z->halted = 0;
        z->iff1 = z->iff2 = 0;
        z_inc_r(z);
        z_push(&s, z->pc);
        if (z->im == 2) {
            uint16_t v = (uint16_t)(z->i << 8 | 0xFF);
            uint8_t lo = z_rd(&s, v);
            z->pc = (uint16_t)(lo | z_rd(&s, (uint16_t)(v + 1)) << 8);
            t = 19;
        } else { z->pc = 0x38; t = 13; }
        z->wz = z->pc;
        z->q = 0;
        z->p = 0;
        return t;
    }
    z_inc_r(z);                         /* HALT */
    z->q = 0;
    z->p = 0;
    return 4;
}

int z80_step(struct Z80_HOST *g, Z80 *z)
{
    int t = z80_service(g, z);
    if (t) return t;
    Z80X s = { g, z, NULL, 0, z->pc, 0, 0 };
    z->ei = 0;
    t = z80_exec(&s);
    z->pc = s.pc;
    return t;
}

/* --- Decodificador --- */

/* La instrucción sin prefijo op usa (HL) en memoria (con DD/FD, (IX+d): lleva el desplazamiento) */
static int uses_hl_mem(uint8_t op)
{
    if (op == 0x34 || op == 0x35 || op == 0x36) return 1;
    if (op >= 0x40 && op < 0x80) return op != 0x76 && ((op & 7) == 6 || (op >> 3 & 7) == 6);
    if (op >= 0x80 && op < 0xC0) return (op & 7) == 6;
    return 0;
}

/* Bytes de operando inmediato de la instrucción sin prefijo op (sin contar el desplazamiento de IX/IY) */
static int imm_bytes(uint8_t op)
{
    switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x22: case 0x2A: case 0x32: case 0x3A:
    case 0xC3: case 0xCD:
        return 2;
    case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0xD3: case 0xDB:
        return 1;
    }
    if ((op & 0xC7) == 0xC2 || (op & 0xC7) == 0xC4) return 2;      /* JP cc / CALL cc */
    if ((op & 0xC7) == 0x06 || (op & 0xC7) == 0xC6) return 1;      /* LD r,n / ALU A,n */
    return 0;
}

static void decode_ed(const uint8_t *b, uint16_t pc, Z80Insn *out)
{
    uint8_t o = b[1];
    int t = 8;
    out->len = 2;
    if (o >= 0x40 && o < 0x80) {
        switch (o & 7) {
        case 0: case 1: t = 12; break;                          /* IN r,(C) / OUT (C),r */
        case 2: t = 15; break;                                  /* SBC/ADC HL,rr */
        case 3: t = 20; out->len = 4; break;                    /* LD (nn),rr / LD rr,(nn) */
        case 5: t = 14; out->kind = Z80_INS_RETI; break;        /* RETN / RETI */
        case 7: t = o == 0x67 || o == 0x6F ? 18 : o < 0x60 ? 9 : 8; break;   /* RRD/RLD; LD I/R; sin definir */
        default: break;                                         /* NEG, IM */
        }
    } else if ((o & 0xE4) == 0xA0) {                            /* LDI CPI INI OUTI y los de bloque */
        t = 16;
        if (o & 0x10) { out->kind = Z80_INS_REPEAT; out->target = pc; out->cycles_taken = 21; }
    }
    out->cycles = (uint8_t)t;
    if (out->kind != Z80_INS_REPEAT) out->cycles_taken = (uint8_t)t;
}

void z80_decode(const uint8_t b[4], uint16_t pc, Z80Insn *out)
{
    memset(out, 0, sizeof *out);
    uint8_t op = b[0];
    int pre = 0;
    if (op == 0xDD || op == 0xFD) {
        if (b[1] == 0xDD || b[1] == 0xFD || b[1] == 0xED) {     /* el prefijo solo cuenta como NOP */
            out->len = 1; out->cycles = out->cycles_taken = 4;
            return;
        }
        if (b[1] == 0xCB) {                                     /* DD CB d op */
            out->len = 4;
            out->cycles = out->cycles_taken = b[3] >= 0x40 && b[3] < 0x80 ? 20 : 23;
            return;
        }
        pre = 1;
        op = b[1];
    } else if (op == 0xCB) {
        uint8_t o = b[1];
        out->len = 2;
        out->cycles = out->cycles_taken = (o & 7) != 6 ? 8 : o >= 0x40 && o < 0x80 ? 12 : 15;
        return;
    } else if (op == 0xED) {
        decode_ed(b, pc, out);
        return;
    }
    const uint8_t *p = b + pre + 1;                             /* operandos, tras el opcode */
    int t = z_cyc_main[op] + (pre ? 4 : 0), extra = 0;
    int len = 1 + pre;
    if (pre && uses_hl_mem(op)) { len++; p++; t += op == 0x36 ? 5 : 8; }
    len += imm_bytes(op);
    uint16_t next = (uint16_t)(pc + len);
    out->len = (uint8_t)len;
    switch (op) {
    case 0x10: out->kind = Z80_INS_DJNZ; out->target = (uint16_t)(next + (int8_t)p[0]); extra = 5; break;
    case 0x18: out->kind = Z80_INS_JR; out->target = (uint16_t)(next + (int8_t)p[0]); break;
    case 0x20: case 0x28: case 0x30: case 0x38:
        out->kind = Z80_INS_JR; out->cond = (uint8_t)((op >> 3 & 3) + 1);
        out->target = (uint16_t)(next + (int8_t)p[0]); extra = 5;
        break;
    case 0xC3: out->kind = Z80_INS_JP; out->target = (uint16_t)(p[0] | p[1] << 8); break;
    case 0xCD: out->kind = Z80_INS_CALL; out->target = (uint16_t)(p[0] | p[1] << 8); break;
    case 0xC9: out->kind = Z80_INS_RET; break;
    case 0xE9: out->kind = Z80_INS_JP_IND; break;
    case 0x76: out->kind = Z80_INS_HALT; break;
    default:
        if ((op & 0xC7) == 0xC2) { out->kind = Z80_INS_JP; out->cond = (uint8_t)((op >> 3 & 7) + 1); out->target = (uint16_t)(p[0] | p[1] << 8); }
        else if ((op & 0xC7) == 0xC4) { out->kind = Z80_INS_CALL; out->cond = (uint8_t)((op >> 3 & 7) + 1); out->target = (uint16_t)(p[0] | p[1] << 8); extra = 7; }
        else if ((op & 0xC7) == 0xC0) { out->kind = Z80_INS_RET; out->cond = (uint8_t)((op >> 3 & 7) + 1); extra = 6; }
        else if ((op & 0xC7) == 0xC7) { out->kind = Z80_INS_RST; out->target = op & 0x38; }
        break;
    }
    out->cycles = (uint8_t)t;
    out->cycles_taken = (uint8_t)(t + extra);
}
