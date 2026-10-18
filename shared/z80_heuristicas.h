/*
 * Heurísticas de descubrimiento del Z80: dónde hay código que el flujo desde los vectores no alcanza. Común a
 * SMSRecomp, GENRecomp y NEORecomp (retrorecomp/shared); lo incluye el recompilador de cada consola, después de
 * z80_recomp.h.
 *
 * Las heurísticas miran el código ya descubierto de un tramo (Z80Rc) y siembran direcciones nuevas. Descubrir de más no
 * rompe nada (lo que se compila son los bytes que hay ahí y se entra por el PC exacto), pero cuesta tiempo de
 * compilación: por eso todo destino tiene que parecer código (plausible).
 *
 *   tablas de saltos      un JP (HL), (IX) o (IY) con un LD HL/IX/IY,nn poco antes (24 bytes) y, entre los dos, el
 *                         ADD HL/IX/IY,rr que la indexa; o la base armada con ADD A,lo / LD L,A / ADC A,hi / SUB L /
 *                         LD H,A (HL = hi:lo + A): nn es una tabla de direcciones de 16 bits
 *   JP (HL) constante     un JP (HL) cuyo HL es un LD HL,nn anterior, aun con EXX de por medio
 *   direcciones apiladas  LD rr,nn seguido de PUSH rr y, en los 24 bytes que siguen, un JP, JR o JP (HL)/(IX)/(IY) sin un
 *                         CALL ni un RET antes: nn es la vuelta de un RET (en el código de SDCC, un PUSH de un inmediato
 *                         es casi siempre un argumento: por eso se pide el salto)
 *   despachadores         LD HL,tabla y, justo después, un CALL, RST o JP a una rutina que lee el puntero de la tabla
 *                         que le deja el llamador (LD H,(HL) y JP (HL); LD D,(HL), EX DE,HL y JP (HL); EX (SP),HL o
 *                         PUSH HL y RET; o un CALL/RST a otra que devuelve el puntero con un JP (HL) justo después)
 *   cambios de banco      LD A,n y LD (registro de la ranura),A seguidos de un CALL, JP o RST a esa ranura: el destino es
 *                         de la página n (solo si la consola tiene bancos: bank_slot y seed_bank)
 *
 * Lo que cambia entre consolas es el Z80Host: cómo se lee un byte (en SMS, de la página que está en cada ranura),
 * dónde se siembra una dirección, qué identifica a una instrucción en --why y cómo se cambia de banco.
 */
#ifndef Z80_HEURISTICAS_H
#define Z80_HEURISTICAS_H

#include <stdint.h>

#include "z80_ops.h"
#include "z80_recomp.h"

/* Cómo se llegó a una instrucción con estas heurísticas (se suman a Z80RC_SEED, Z80RC_FLOW y Z80RC_TRACE; los de la
 * consola, como el salto a otra ranura de SMS, van por debajo) */
enum { Z80H_TABLE = 16, Z80H_PUSHED, Z80H_CONST, Z80H_BANK, Z80H_LOOSE };

typedef struct Z80Host {
    /* El byte que ve el Z80 en a, o -1 si no es código fijo (la RAM, lo que está fuera del mapa) */
    int      (*peek)(void *ctx, uint32_t a);
    /* Siembra a donde corresponde (el tramo, o el que ocupa esa dirección); how es Z80H_*; from, lo que devuelve origin.
     * 1 si es nueva */
    int      (*seed)(void *ctx, uint16_t a, int how, uint32_t from);
    /* Qué identifica a la instrucción en a (para --why): en SMS, su desplazamiento en la ROM */
    uint32_t (*origin)(void *ctx, uint16_t a);
    /* Opcional (NULL si la consola no tiene bancos): ¿LD (nn),A cambia el banco de una ventana? Devuelve 1 y la ventana
     * (lo..hi) a la que cae el destino; si no, 0 */
    int      (*bank_slot)(void *ctx, uint32_t nn, uint32_t *lo, uint32_t *hi, int *slot);
    /* Opcional: siembra a en la página que dice el cambio de banco (no en la del encendido). 1 si es nueva */
    int      (*seed_bank)(void *ctx, int page, int slot, uint16_t a, int how, uint32_t from);
    uint32_t min_target, max_target;    /* los destinos que valen: min_target <= t < max_target (SMS: $0008-$BFFF) */
    unsigned loose_min;                 /* punteros sueltos: cuántas entradas debe tener la tabla (0: apagado) */
    /* Estadísticas, para --verbose */
    unsigned tables, table_entries, pushed, consts, banks, loose;
} Z80Host;

/* El byte de r->img en a, o 0 si cae fuera del tramo */
static inline uint8_t z80h_img(const Z80Rc *r, uint32_t a)
{
    return a >= r->lo && a <= r->hi ? r->img[a - r->lo] : 0;
}

static inline int z80h_fetch4(const Z80Host *h, void *ctx, uint32_t t, uint8_t b[4])
{
    for (int k = 0; k < 4; k++) {
        int c = h->peek(ctx, t + (uint32_t)k);
        if (c < 0) return 0;
        b[k] = (uint8_t)c;
    }
    return 1;
}

/* ¿Lo que hay en t parece código? Se decodifica hasta el primer salto incondicional (o 24 instrucciones) y se descarta
 * si aparece algo que en un juego casi nunca es código: RST $38 ($FF, lo que hay en la ROM vacía), un ED sin definir, un
 * prefijo DD/FD seguido de otro prefijo, o cuatro NOP seguidos. Con eso, una entrada que apunta a datos no arrastra
 * miles de instrucciones de mentira */
static inline int z80h_plausible(const Z80Host *h, void *ctx, uint16_t t)
{
    int nops = 0;
    for (int i = 0; i < 24; i++) {
        uint8_t b[4];
        if (!z80h_fetch4(h, ctx, t, b)) return 0;
        Z80Insn in;
        z80_decode(b, t, &in);
        if (b[0] == 0xFF) return 0;
        if ((b[0] == 0xDD || b[0] == 0xFD) && (b[1] == 0xDD || b[1] == 0xFD || b[1] == 0xED)) return 0;
        if (b[0] == 0xED) {
            int x = b[1] >> 6, y = b[1] >> 3 & 7, z = b[1] & 7;
            if (x == 0 || x == 3 || (x == 2 && (z > 3 || y < 4))) return 0;
        }
        nops = b[0] == 0x00 ? nops + 1 : 0;
        if (nops >= 4) return 0;
        if (!in.cond && (in.kind == Z80_INS_JP || in.kind == Z80_INS_JR || in.kind == Z80_INS_RET || in.kind == Z80_INS_RETI ||
                         in.kind == Z80_INS_JP_IND)) return 1;
        t = (uint16_t)(t + in.len);
    }
    return 1;
}

/* La tabla de direcciones de 16 bits que empieza en base (hasta 128): se toman mientras caigan donde valen
 * (min_target..max_target), parezcan código, no lleguen a código descubierto ni a una dirección a la que apunta la misma
 * tabla; se siembran si son dos o más. from: lo que devuelve origin. Cuántas nuevas, o -1 si no hay tabla (menos de dos) */
static inline int z80h_seed_table_n(Z80Host *h, void *ctx, const Z80Rc *r, uint32_t base, uint32_t from, unsigned min_n, int how)
{
    uint16_t entry[128];
    unsigned n = 0;
    int added = 0;
    for (; n < 128; n++) {
        uint32_t at = base + 2 * n;
        if (n && z80rc_in(r, at) && at < 0xFFFF && (r->ins[at] || r->ins[at + 1])) break;     /* la tabla llegó a código */
        int lo = h->peek(ctx, at), hi = h->peek(ctx, at + 1);
        if (lo < 0 || hi < 0) break;
        uint32_t t = (uint32_t)(lo | hi << 8);
        if (t >= h->max_target || t < h->min_target || (t >= base && t <= at + 1) || !z80h_plausible(h, ctx, (uint16_t)t)) break;
        entry[n] = (uint16_t)t;
    }
    if (n < min_n) return -1;
    for (unsigned k = 0; k < n; k++) added += h->seed(ctx, entry[k], how, from);
    if (how == Z80H_LOOSE) {
        h->loose++;
    } else {
        h->tables++;
        h->table_entries += n;
    }
    return added;
}

static inline int z80h_seed_table(Z80Host *h, void *ctx, const Z80Rc *r, uint32_t base, uint32_t from)
{
    return z80h_seed_table_n(h, ctx, r, base, from, 2, Z80H_TABLE);
}

/* Rutinas que leen un puntero de una tabla cuya base les deja el llamador en HL (sin un LD HL,nn propio):
 *   Z80H_FETCH     devuelve el puntero en HL: ...LD H,(HL)... RET (o LD D,(HL), EX DE,HL, RET)
 *   Z80H_DISPATCH  salta a él: lo mismo y JP (HL); o EX (SP),HL / PUSH HL y RET; o un CALL/RST a una Z80H_FETCH seguido
 *                  de JP (HL)
 * Se decodifica en línea recta desde t (los saltos condicionales siguen de largo; el primero incondicional corta) */
enum { Z80H_NONE, Z80H_FETCH, Z80H_DISPATCH };

static inline int z80h_routine_kind(const Z80Host *h, void *ctx, uint16_t t, int depth)
{
    int hfetch = 0, dfetch = 0, ex = 0, last = 0;               /* last: el primer byte de la instrucción anterior */
    uint32_t pc = t;
    for (int i = 0; i < 24; i++) {
        uint8_t b[4];
        Z80Insn in;
        if (pc >= h->max_target || !z80h_fetch4(h, ctx, pc, b)) return Z80H_NONE;
        z80_decode(b, (uint16_t)pc, &in);
        if (b[0] == 0x21) return Z80H_NONE;                                 /* LD HL,nn: la tabla es suya */
        if (b[0] == 0x66 || ((b[0] == 0xDD || b[0] == 0xFD) && b[1] == 0x66)) hfetch = 1;   /* LD H,(HL) / LD H,(IX+d) */
        if (b[0] == 0x56) dfetch = 1;                                       /* LD D,(HL) */
        if (b[0] == 0xEB) ex = 1;                                           /* EX DE,HL */
        int fetched = hfetch || (dfetch && ex);
        if (in.kind == Z80_INS_JP_IND) return b[0] == 0xE9 && fetched ? Z80H_DISPATCH : Z80H_NONE;
        if (in.kind == Z80_INS_RET && !in.cond) {
            if (!fetched) return Z80H_NONE;
            return last == 0xE3 || last == 0xE5 ? Z80H_DISPATCH : Z80H_FETCH;   /* EX (SP),HL o PUSH HL, y RET: salta */
        }
        if ((in.kind == Z80_INS_CALL || in.kind == Z80_INS_RST) && !in.cond && !depth && !fetched &&
            z80h_routine_kind(h, ctx, in.target, 1) == Z80H_FETCH) {
            uint8_t n[4];
            return z80h_fetch4(h, ctx, pc + in.len, n) && n[0] == 0xE9 ? Z80H_DISPATCH : Z80H_NONE;
        }
        if (!in.cond && (in.kind == Z80_INS_JP || in.kind == Z80_INS_JR || in.kind == Z80_INS_RETI ||
                         in.kind == Z80_INS_HALT)) return Z80H_NONE;
        last = b[0];
        pc += in.len;
    }
    return Z80H_NONE;
}

/* Tablas de despachadores: un CALL, RST o JP a una rutina que despacha por una tabla que ella no trae (o a una que la lee
 * y vuelve, con un JP (HL) justo después) y, antes en el llamador, el LD HL,nn con la base: LD HL,tabla / CALL despachar.
 * Si la primera entrada no es código se prueba tabla + 2 (el índice del llamador empieza en 1) */
static inline int z80h_dispatch_tables(Z80Host *h, void *ctx, const Z80Rc *r)
{
    int added = 0;
    for (uint32_t a = r->lo; a <= r->hi; a++) {
        if (!r->ins[a]) continue;
        uint8_t b[4];
        Z80Insn in;
        if (!z80h_fetch4(h, ctx, a, b)) continue;
        z80_decode(b, (uint16_t)a, &in);
        if (in.kind != Z80_INS_CALL && in.kind != Z80_INS_RST && in.kind != Z80_INS_JP) continue;
        int rk = z80h_routine_kind(h, ctx, in.target, 0);
        if (rk == Z80H_NONE) continue;
        if (rk == Z80H_FETCH) {
            uint8_t n[4];
            if (in.kind == Z80_INS_JP || !z80h_fetch4(h, ctx, a + in.len, n) || n[0] != 0xE9) continue;
        }
        uint32_t base = 0x10000;
        for (uint32_t x = a > (uint32_t)r->lo + 20 ? a - 20 : r->lo; x < a; x++) {
            if (!r->ins[x]) continue;
            if (z80h_img(r, x) == 0x21 && x + 2 < a) base = (uint32_t)(z80h_img(r, x + 1) | z80h_img(r, x + 2) << 8);
        }
        if (base >= h->max_target) continue;
        int n = z80h_seed_table(h, ctx, r, base, h->origin(ctx, (uint16_t)a));
        if (n < 0) n = z80h_seed_table(h, ctx, r, base + 2, h->origin(ctx, (uint16_t)a));
        added += n > 0 ? n : 0;
    }
    return added;
}

/* Cambios de banco constantes: LD A,n seguido de LD (nn),A donde nn es el registro de una ventana (bank_slot) y, en las
 * instrucciones que siguen (40 bytes), un CALL, JP o RST a una dirección de esa ventana: el destino es de la página n. Sin
 * esto, un salto a otra ventana se busca en la página que está ahí al encender y el código de las demás páginas solo lo
 * ve la traza */
static inline int z80h_bank_switches(Z80Host *h, void *ctx, const Z80Rc *r)
{
    if (!h->bank_slot || !h->seed_bank) return 0;
    int added = 0;
    uint32_t prev = 0x10000;
    for (uint32_t a = r->lo; a <= r->hi; a++) {
        if (!r->ins[a]) continue;
        uint32_t p0 = prev;
        prev = a;
        if (z80h_img(r, a) != 0x32 || a + 2 > r->hi) continue;                          /* LD (nn),A */
        uint32_t nn = (uint32_t)(z80h_img(r, a + 1) | z80h_img(r, a + 2) << 8);
        uint32_t lo, hi;
        int slot;
        if (!h->bank_slot(ctx, nn, &lo, &hi, &slot)) continue;
        if (p0 + 2 != a || z80h_img(r, p0) != 0x3E) continue;                           /* LD A,n justo antes */
        int page = z80h_img(r, p0 + 1);
        int found = 0;
        for (uint32_t x = a + 3; x <= r->hi && x < a + 43 && found < 3; x++) {
            if (!r->ins[x]) continue;
            uint8_t b[4];
            Z80Insn in;
            if (!z80h_fetch4(h, ctx, x, b)) break;
            z80_decode(b, (uint16_t)x, &in);
            if ((in.kind == Z80_INS_CALL || in.kind == Z80_INS_JP || in.kind == Z80_INS_RST) && in.target >= lo && in.target <= hi) {
                found++;
                if (h->seed_bank(ctx, page, slot, in.target, Z80H_BANK, h->origin(ctx, (uint16_t)x))) {
                    added++;
                    h->banks++;
                }
            }
        }
    }
    return added;
}

/* El valor constante que tiene HL en el JP (HL) de a: el último LD HL,nn hacia atrás, en el juego de registros que está
 * activo (cada EXX los cambia) y sin que nada lo toque en medio. Se mira el código descubierto antes de a, sin seguir los
 * saltos (hasta 512 bytes). -1 si no hay */
static inline int z80h_hl_constant(const Z80Rc *r, uint32_t a)
{
    int set = 0;
    for (uint32_t x = a; x > r->lo && a - x < 512;) {
        x--;
        if (!r->ins[x]) continue;
        uint8_t o0 = z80h_img(r, x), o1 = z80h_img(r, x + 1);
        if (o0 == 0xD9) { set ^= 1; continue; }                                 /* EXX */
        if (set) continue;
        if (o0 == 0x21) return x + 2 < a ? (int)(z80h_img(r, x + 1) | z80h_img(r, x + 2) << 8) : -1;
        if (o0 == 0xE1 || o0 == 0xEB || o0 == 0xE3 || o0 == 0x2A || (o0 & 0xCF) == 0x09 || o0 == 0x23 || o0 == 0x2B ||
            o0 == 0x26 || o0 == 0x2E || (o0 >= 0x60 && o0 <= 0x6F) || o0 == 0x24 || o0 == 0x25 || o0 == 0x2C ||
            o0 == 0x2D)
            return -1;                                                          /* algo toca HL */
        if (o0 == 0xED && (o1 >= 0xA0 || (o1 & 0xC7) == 0x42 || o1 == 0x6B || o1 == 0x67 || o1 == 0x6F)) return -1;
    }
    return -1;
}

/* Tablas de saltos, JP (HL) constantes y direcciones apiladas: lo que se ve mirando alrededor de cada instrucción
 * descubierta del tramo */
static inline int z80h_tables_and_pushed(Z80Host *h, void *ctx, const Z80Rc *r)
{
    int added = 0;
    for (uint32_t a = r->lo; a <= r->hi; a++) {
        if (!r->ins[a]) continue;
        uint8_t b0 = z80h_img(r, a);
        uint8_t b1 = a + 1 <= r->hi ? z80h_img(r, a + 1) : 0;
        int ix = (b0 == 0xDD || b0 == 0xFD) && b1 == 0xE9;
        int pushret = (b0 == 0xC5 || b0 == 0xD5 || b0 == 0xE5) && b1 == 0xC9;      /* PUSH rr / RET: un salto por la pila */
        if (b0 == 0xE9 || ix || pushret) {       /* JP (HL) / JP (IX) / JP (IY) / PUSH rr y RET */
            uint8_t want0 = ix ? b0 : 0x21;
            uint32_t base = 0x10000;
            int indexed = 0;
            for (uint32_t x = a > (uint32_t)r->lo + 24 ? a - 24 : r->lo; x < a; x++) {
                if (!r->ins[x]) continue;
                uint8_t o0 = z80h_img(r, x), o1 = z80h_img(r, x + 1);
                if (!ix && o0 == 0x21 && x + 2 < a) {
                    base = (uint32_t)(z80h_img(r, x + 1) | z80h_img(r, x + 2) << 8);
                    indexed = 0;
                } else if (ix && o0 == want0 && x + 3 < a && o1 == 0x21) {
                    base = (uint32_t)(z80h_img(r, x + 2) | z80h_img(r, x + 3) << 8);
                    indexed = 0;
                } else if (!ix && o0 == 0xC6 && x + 6 < a && z80h_img(r, x + 2) == 0x6F && z80h_img(r, x + 3) == 0xCE &&
                           z80h_img(r, x + 5) == 0x95 && z80h_img(r, x + 6) == 0x67) {   /* ADD A,lo / LD L,A / ADC A,hi / SUB L / LD H,A: HL = hi:lo + A */
                    base = (uint32_t)(o1 | z80h_img(r, x + 4) << 8);
                    indexed = 1;
                } else if (!ix && (o0 == 0xDD || o0 == 0xFD) && o1 == 0x21 && x + 3 < a) {   /* LD IX/IY,nn: la tabla de un */
                    base = (uint32_t)(z80h_img(r, x + 2) | z80h_img(r, x + 3) << 8);        /* LD L,(IX+0) / LD H,(IX+1) */
                    indexed = 0;
                } else if (!ix && (o0 == 0xDD || o0 == 0xFD) && (o1 & 0xCF) == 0x09) indexed = 1;   /* ADD IX/IY,rr */
                else if (!ix && (o0 & 0xCF) == 0x09) indexed = 1;                   /* ADD HL,rr */
                else if (ix && o0 == want0 && (o1 & 0xCF) == 0x09) indexed = 1;     /* ADD IX/IY,rr */
            }
            if (!ix && !indexed) {                  /* sin tabla: un JP (HL) a una constante (LD HL,nn, EXX... EXX, JP (HL)) */
                if (pushret && b0 != 0xE5) continue;       /* PUSH BC/DE: no se sabe qué lleva */
                int t = z80h_hl_constant(r, a);
                if (t >= (int)h->min_target && (uint32_t)t < h->max_target && z80h_plausible(h, ctx, (uint16_t)t) &&
                    h->seed(ctx, (uint16_t)t, Z80H_CONST, h->origin(ctx, (uint16_t)a))) {
                    added++;
                    h->consts++;
                }
                continue;
            }
            if (base >= h->max_target || !indexed) continue;
            int n = z80h_seed_table(h, ctx, r, base, h->origin(ctx, (uint16_t)a));
            added += n > 0 ? n : 0;
        } else if ((b0 & 0xCF) == 0x01 && b0 != 0x31) {   /* LD BC/DE/HL,nn y después PUSH del mismo par */
            uint32_t nn = (uint32_t)(b1 | (a + 2 <= r->hi ? z80h_img(r, a + 2) : 0) << 8);
            uint8_t push = (uint8_t)(0xC5 | (b0 & 0x30));
            int jumps = 0;
            for (uint32_t x = a + 4; x <= r->hi && x < a + 28 && !jumps; x++) {
                if (!r->ins[x]) continue;
                uint8_t o0 = z80h_img(r, x), o1 = x + 1 <= r->hi ? z80h_img(r, x + 1) : 0;
                if (o0 == 0xC3 || o0 == 0x18 || o0 == 0xE9 || ((o0 == 0xDD || o0 == 0xFD) && o1 == 0xE9)) jumps = 1;
                else if (o0 == 0xCD || o0 == 0xC9 || (o0 & 0xC7) == 0xC4 || (o0 & 0xC7) == 0xC0 || (o0 & 0xC7) == 0xC7) break;
            }
            if (jumps && a + 3 <= r->hi && z80h_img(r, a + 3) == push && nn < h->max_target && nn >= h->min_target &&
                z80h_plausible(h, ctx, (uint16_t)nn) && h->seed(ctx, (uint16_t)nn, Z80H_PUSHED, h->origin(ctx, (uint16_t)a))) {
                added++;
                h->pushed++;
            }
        }
    }
    return added;
}

/* Punteros sueltos: un LD HL/DE/BC/IX/IY,nn cuyo nn es el comienzo de una tabla de al menos loose_min direcciones de código
 * plausible (las tablas que recorre un bucle o que lee una rutina, sin un JP (HL) cerca que las delate). Más estricto
 * que las de arriba (4 entradas y no 2) porque el inmediato puede ser cualquier dato. Descubre mucho más código, y
 * compilarlo tarda: la consola decide con loose_min (0: apagado) */
static inline int z80h_loose_tables(Z80Host *h, void *ctx, const Z80Rc *r)
{
    int added = 0;
    if (!h->loose_min) return 0;
    for (uint32_t a = r->lo; a <= r->hi; a++) {
        if (!r->ins[a]) continue;
        uint8_t b0 = z80h_img(r, a), b1 = a + 1 <= r->hi ? z80h_img(r, a + 1) : 0;
        uint32_t nn;
        if ((b0 == 0x21 || b0 == 0x11 || b0 == 0x01) && a + 2 <= r->hi) nn = (uint32_t)(z80h_img(r, a + 1) | z80h_img(r, a + 2) << 8);
        else if ((b0 == 0xDD || b0 == 0xFD) && b1 == 0x21 && a + 3 <= r->hi) nn = (uint32_t)(z80h_img(r, a + 2) | z80h_img(r, a + 3) << 8);
        else continue;
        if (nn < h->min_target || nn >= h->max_target) continue;
        int n = z80h_seed_table_n(h, ctx, r, nn, h->origin(ctx, (uint16_t)a), h->loose_min, Z80H_LOOSE);
        added += n > 0 ? n : 0;
    }
    return added;
}

/* Todas las heurísticas sobre un tramo, en este orden: tablas y direcciones apiladas, despachadores, cambios de banco y
 * punteros sueltos. Cuántas direcciones nuevas sembró (0: no queda nada por descubrir con ellas) */
static inline int z80h_heuristics(Z80Host *h, void *ctx, const Z80Rc *r)
{
    int added = z80h_tables_and_pushed(h, ctx, r);
    added |= z80h_dispatch_tables(h, ctx, r);
    added |= z80h_bank_switches(h, ctx, r);
    added |= z80h_loose_tables(h, ctx, r);
    return added;
}

#endif
