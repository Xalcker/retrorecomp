/*
 * Recompilador del Z80: descubrimiento y emisión de C. Común a NEORecomp, GENRecomp y SMSRecomp
 * (retrorecomp/shared); lo incluye el recompilador de cada consola, no el runtime.
 *
 * Trabaja sobre un tramo de código fijo: las direcciones lo..hi (incluidas) con sus bytes en img. Lo que
 * no está ahí (ventanas con bancos, RAM) lo sigue haciendo el intérprete, o la consola arma otro tramo
 * con otro prefijo (las vistas por página y ranura de SMSRecomp, el driver que el 68000 sube a la RAM del
 * Z80 en GENRecomp, con su guarda).
 *
 * Descubrimiento: desde las semillas (z80rc_seed: el reset, los RST, $0038, $0066, lo que diga la traza),
 * se sigue el flujo con z80_decode: la siguiente, los destinos constantes de JP, JR, DJNZ, CALL y RST, y
 * la que sigue a un CALL, un RST, un HALT o un salto condicional. Descubrir de más no rompe nada: cada
 * instrucción se compila con sus propios bytes y se entra por el PC exacto, así que lo que el juego nunca
 * ejecuta nunca corre. Una instrucción que no cabe entera en el tramo no se compila.
 *
 * Emisión: funciones de tramos contiguos (con huecos cortos) de hasta Z80RC_FUNC_MAX instrucciones. Cada
 * una empieza con un switch sobre el PC (si no es suyo devuelve 0 y el intérprete ejecuta una
 * instrucción) y tiene una etiqueta por instrucción que llama a la rama de z80_exec de su primer byte
 * (z80_exec_main, _cb, _ed o _xy, de z80_ops.h; z80test --recomp las prueba igual) con los bytes y el PC
 * constantes: el compilador pliega los switch y queda la instrucción sola, con la misma semántica y los
 * mismos ciclos que el intérprete. La consola pone en su z80_recomp_host.h:
 *   Z80RC_CPU(g)          el Z80 de la consola
 *   Z80RC_BEFORE(pc)      antes de cada instrucción, lo mismo que el bucle del intérprete antes de
 *                         z80_step; sale con `return r` si se acabó el tiempo (target) o hay algo que
 *                         atiende el intérprete (NMI, INT, HALT)
 *   Z80RC_AFTER(pc0, t)   después, lo mismo que tras z80_step: el reloj, Z80_STAT(g, native, t), los
 *                         bucles de espera
 *   Z80RC_BYTE(a)         solo con guarda (código en RAM): el byte que hay hoy en a
 * Con guarda (r->guard = 1, para código en RAM), cada instrucción compara antes sus bytes con Z80RC_BYTE y,
 * si alguno cambió (otro driver, código que se modifica), sale y la ejecuta el intérprete con lo que hay. Las que la
 * consola marca en r->vol (sus operandos cambian al correr) comparan solo los opcodes y leen los operandos de la RAM.
 * Dentro de la función están g, z (el Z80), target y r (1 desde que corrió una instrucción). Después de
 * cada instrucción se sigue con un goto si el destino es de la misma función; si no, el switch del
 * principio vuelve a buscarlo y, si tampoco está, la función devuelve 1 y la consola busca otra.
 *
 * z80rc_emit escribe en dir: base.h (las declaraciones), base_NNN.c (las funciones) y base_dispatch.c
 * (<prefijo>lookup(pc): la función que contiene pc, o NULL).
 */
#ifndef Z80_RECOMP_H
#define Z80_RECOMP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80_ops.h"

/* Compilar una instrucción cuesta ~20 ms (el compilador copia la rama de z80_ops.h entera y después la
 * pliega), y más si la función es grande: funciones chicas y archivos chicos, para make -j */
#ifndef Z80RC_FUNC_MAX
#define Z80RC_FUNC_MAX 64       /* instrucciones por función */
#endif
#ifndef Z80RC_FUNC_GAP
#define Z80RC_FUNC_GAP 64       /* hueco máximo (bytes) dentro de una función */
#endif
#ifndef Z80RC_PER_FILE
#define Z80RC_PER_FILE 400      /* instrucciones por archivo .c */
#endif

enum { Z80RC_SEED = 1, Z80RC_FLOW, Z80RC_TRACE };     /* de dónde salió una instrucción */

typedef struct { uint16_t lo, hi; unsigned n; } Z80RcFunc;

typedef struct {
    char     prefix[16];        /* las funciones se llaman prefijo + dirección ("z_" → z_0123) */
    uint16_t lo, hi;            /* el tramo de código fijo, con las dos puntas */
    const uint8_t *img;         /* sus bytes: img[a - lo] */
    int      guard;             /* 1: código en RAM, cada instrucción comprueba sus bytes (Z80RC_BYTE) */
    const uint8_t *vol;         /* con guarda, opcional: vol[a - lo] != 0 si los operandos de la instrucción en a cambian
                                   al correr (código que se modifica): la guarda mira solo sus opcodes y los operandos se
                                   leen de la RAM al ejecutar, como en el intérprete */
    uint8_t  ins[0x10000];     /* 0, o de dónde salió la instrucción que empieza en a (Z80RC_*) */
    uint8_t  queued[0x10000];   /* ya se puso en la pila */
    uint16_t stack[0x10000];
    uint8_t  how[0x10000];      /* el origen con que se puso en la pila */
    uint16_t from[0x10000];     /* con Z80RC_FLOW: la instrucción desde la que se llegó (el «por qué» de --why) */
    uint32_t cur;               /* la instrucción que está descubriendo (0x10000: ninguna, es una semilla) */
    unsigned nstack;
    unsigned ninsns;            /* instrucciones descubiertas */
    Z80RcFunc *funcs;
    unsigned nfuncs;
    uint16_t fn_of[0x10000];    /* 0, o 1 + la función de la instrucción en a */
} Z80Rc;

/* r es grande (unos 500 KB): mejor static o con malloc */
static inline void z80rc_init(Z80Rc *r, const char *prefix, uint16_t lo, uint16_t hi, const uint8_t *img)
{
    memset(r, 0, sizeof *r);
    snprintf(r->prefix, sizeof r->prefix, "%s", prefix);
    r->lo = lo;
    r->hi = hi;
    r->img = img;
    r->cur = 0x10000;
}

static inline void z80rc_free(Z80Rc *r) { free(r->funcs); r->funcs = NULL; r->nfuncs = 0; }

static inline int z80rc_in(const Z80Rc *r, uint32_t a) { return a >= r->lo && a <= r->hi; }

/* Los 4 bytes desde a (0 fuera del tramo) */
static inline void z80rc_bytes(const Z80Rc *r, uint16_t a, uint8_t b[4])
{
    for (int i = 0; i < 4; i++) b[i] = z80rc_in(r, (uint32_t)a + i) ? r->img[a + i - r->lo] : 0;
}

/* ¿El byte i de la instrucción b es parte del opcode? (los demás son operandos: inmediatos, direcciones, desplazamientos).
 * Prefijos CB y ED: dos bytes; DD/FD: dos, y con DD/FD CB d op, también el cuarto */
static inline int z80rc_es_opcode(const uint8_t b[4], int i)
{
    if (i == 0) return 1;
    if (b[0] == 0xCB || b[0] == 0xED) return i == 1;
    if (b[0] == 0xDD || b[0] == 0xFD) return i == 1 || (i == 3 && b[1] == 0xCB);
    return 0;
}

/* La instrucción en a cabe entera en el tramo (un prefijo DD/FD suelto mira también el byte siguiente) */
static inline int z80rc_fits(const Z80Rc *r, uint16_t a, const uint8_t b[4], const Z80Insn *in)
{
    unsigned need = in->len;
    if (in->len == 1 && (b[0] == 0xDD || b[0] == 0xFD)) need = 2;
    return z80rc_in(r, a) && z80rc_in(r, (uint32_t)a + need - 1);
}

/* Las direcciones a las que puede seguir la instrucción en a: la siguiente primero, si puede seguir ahí */
static inline int z80rc_succ(uint16_t a, const Z80Insn *in, uint16_t out[2])
{
    uint16_t next = (uint16_t)(a + in->len);
    int n = 0;
    switch (in->kind) {
    case Z80_INS_JP: case Z80_INS_JR:
        if (in->cond) out[n++] = next;
        out[n++] = in->target;
        break;
    case Z80_INS_DJNZ: case Z80_INS_CALL: case Z80_INS_RST:
        out[n++] = next;
        out[n++] = in->target;
        break;
    case Z80_INS_RET:
        if (in->cond) out[n++] = next;
        break;
    case Z80_INS_RETI: case Z80_INS_JP_IND:
        break;
    default:            /* NORMAL, HALT (la interrupción vuelve a la siguiente) y REPEAT (a sí misma o la siguiente) */
        out[n++] = next;
        break;
    }
    return n;
}

static inline void z80rc_seed(Z80Rc *r, uint16_t a, int how)
{
    if (!z80rc_in(r, a) || r->queued[a]) return;
    r->queued[a] = 1;
    r->how[a] = (uint8_t)how;
    r->from[a] = (uint16_t)r->cur;
    r->stack[r->nstack++] = a;
}

static inline void z80rc_discover(Z80Rc *r)
{
    while (r->nstack) {
        uint16_t a = r->stack[--r->nstack];
        uint8_t b[4];
        Z80Insn in;
        z80rc_bytes(r, a, b);
        z80_decode(b, a, &in);
        if (!z80rc_fits(r, a, b, &in)) continue;
        r->ins[a] = r->how[a];
        r->ninsns++;
        uint16_t s[2];
        int n = z80rc_succ(a, &in, s);
        r->cur = a;
        for (int i = 0; i < n; i++) z80rc_seed(r, s[i], Z80RC_FLOW);
        r->cur = 0x10000;
    }
}

/* Tramos contiguos de instrucciones, con huecos de hasta Z80RC_FUNC_GAP bytes */
static inline int z80rc_partition(Z80Rc *r)
{
    unsigned cap = 0;
    z80rc_free(r);
    memset(r->fn_of, 0, sizeof r->fn_of);
    uint32_t a = r->lo;
    while (a <= r->hi) {
        if (!r->ins[a]) { a++; continue; }
        if (r->nfuncs == cap) {
            cap = cap ? cap * 2 : 64;
            Z80RcFunc *f = realloc(r->funcs, cap * sizeof *f);
            if (!f) return 0;
            r->funcs = f;
        }
        Z80RcFunc *f = &r->funcs[r->nfuncs++];
        *f = (Z80RcFunc){ (uint16_t)a, (uint16_t)a, 0 };
        uint32_t last = a;
        while (a <= r->hi && f->n < Z80RC_FUNC_MAX && a - last <= Z80RC_FUNC_GAP) {
            if (r->ins[a]) { r->fn_of[a] = (uint16_t)r->nfuncs; f->n++; f->hi = (uint16_t)a; last = a; }
            a++;
        }
        a = last + 1;
    }
    return 1;
}

static inline void z80rc_emit_ins(FILE *f, const Z80Rc *r, uint16_t a)
{
    uint8_t b[4];
    Z80Insn in;
    z80rc_bytes(r, a, b);
    z80_decode(b, a, &in);
    fprintf(f, "L_%04X: /*", a);
    for (int i = 0; i < in.len; i++) fprintf(f, " %02X", b[i]);
    fprintf(f, " */\n    Z80RC_BEFORE(0x%04X);\n", a);
    int vol = r->guard && r->vol && r->vol[a - r->lo];
    if (r->guard) {     /* los bytes de la instrucción (y el siguiente tras un prefijo DD/FD suelto): XOR, sin saltos */
        int need = in.len == 1 && (b[0] == 0xDD || b[0] == 0xFD) ? 2 : in.len, k = 0;
        fprintf(f, "    if (");
        for (int i = 0; i < need; i++)
            if (!vol || z80rc_es_opcode(b, i))
                fprintf(f, "%s(Z80RC_BYTE(0x%04X) ^ 0x%02X)", k++ ? " | " : "", (uint16_t)(a + i), b[i]);
        fprintf(f, ") return r;\n");
    }
    if (vol) {          /* los operandos cambian: de la RAM al ejecutar (ops = NULL, como el intérprete) */
        fprintf(f, "    { Z80X s = { g, z, NULL, 0, 0x%04X, 0, 0 };\n", a);
    } else {
        fprintf(f, "    { static const uint8_t b[4] = { 0x%02X, 0x%02X, 0x%02X, 0x%02X };\n", b[0], b[1], b[2], b[3]);
        fprintf(f, "      Z80X s = { g, z, b, 0, 0x%04X, 0, 0 };\n", a);
    }
    switch (b[0]) {     /* la rama de z80_exec, sin copiar las otras (como z80test --recomp) */
    case 0xCB: fprintf(f, "      z80_exec_begin(&s);\n      t = z80_exec_cb(&s);\n"); break;
    case 0xED: fprintf(f, "      z80_exec_begin(&s);\n      t = z80_exec_ed(&s);\n"); break;
    case 0xDD: case 0xFD: fprintf(f, "      t = z80_exec_xy(&s, z80_exec_begin(&s));\n"); break;
    default: fprintf(f, "      t = z80_exec_main(&s, z80_exec_begin(&s));\n"); break;
    }
    fprintf(f, "      z->pc = s.pc; }\n");
    fprintf(f, "    r = 1;\n    Z80RC_AFTER(0x%04X, t);\n", a);
    uint16_t s[2];
    int n = z80rc_succ(a, &in, s);
    for (int i = 0; i < n; i++)
        if (r->ins[s[i]] && r->fn_of[s[i]] == r->fn_of[a])
            fprintf(f, "    if (z->pc == 0x%04X) goto L_%04X;\n", s[i], s[i]);
    fprintf(f, "    goto top;\n");
}

static inline void z80rc_emit_function(FILE *f, const Z80Rc *r, unsigned idx)
{
    const Z80RcFunc *fn = &r->funcs[idx];
    fprintf(f, "/* $%04X-$%04X (%u instrucciones) */\n", fn->lo, fn->hi, fn->n);
    fprintf(f, "int %s%04X(struct Z80_HOST *g, uint64_t target)\n{\n"
               "    Z80 *z = Z80RC_CPU(g);\n    int t, r = 0;\n    (void)target;\ntop:\n    switch (z->pc) {\n",
            r->prefix, fn->lo);
    for (uint32_t a = fn->lo; a <= fn->hi; a++)
        if (r->fn_of[a] == idx + 1) fprintf(f, "    case 0x%04X: goto L_%04X;\n", a, a);
    fprintf(f, "    default: return r;\n    }\n");
    for (uint32_t a = fn->lo; a <= fn->hi; a++)
        if (r->fn_of[a] == idx + 1) z80rc_emit_ins(f, r, (uint16_t)a);
    fprintf(f, "}\n\n");
}

static inline FILE *z80rc_open(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) fprintf(stderr, "no se pudo escribir %s\n", path);
    return f;
}

/* Particiona y escribe el código en dir (base.h, base_NNN.c, base_dispatch.c). `tool` va en el
 * encabezado. 1 si todo bien */
static inline int z80rc_emit(Z80Rc *r, const char *dir, const char *base, const char *tool)
{
    char name[256];
    if (!z80rc_partition(r)) { fprintf(stderr, "sin memoria\n"); return 0; }
    snprintf(name, sizeof name, "%s.h", base);
    FILE *h = z80rc_open(dir, name);
    if (!h) return 0;
    fprintf(h, "/* Generado por %s: no editar. El Z80 en código nativo (z80_recomp.h, en retrorecomp). */\n"
               "#ifndef Z80RC_%s_H\n#define Z80RC_%s_H\n#include \"z80_ops.h\"\n#include \"z80_recomp_host.h\"\n\n",
            tool, r->prefix, r->prefix);
    for (unsigned i = 0; i < r->nfuncs; i++) fprintf(h, "int %s%04X(struct Z80_HOST *g, uint64_t target);\n", r->prefix, r->funcs[i].lo);
    fprintf(h, "Z80RecompFn %slookup(uint16_t pc);\n\n#endif\n", r->prefix);
    if (fclose(h)) return 0;

    FILE *f = NULL;
    unsigned in_file = 0, part = 0;
    for (unsigned i = 0; i < r->nfuncs; i++) {
        if (!f || in_file >= Z80RC_PER_FILE) {
            if (f && fclose(f)) return 0;
            snprintf(name, sizeof name, "%s_%03u.c", base, part++);
            if (!(f = z80rc_open(dir, name))) return 0;
            fprintf(f, "/* Generado por %s: no editar. */\n#include \"%s.h\"\n\n", tool, base);
            in_file = 0;
        }
        z80rc_emit_function(f, r, i);
        in_file += r->funcs[i].n;
    }
    if (f && fclose(f)) return 0;

    snprintf(name, sizeof name, "%s_dispatch.c", base);
    if (!(f = z80rc_open(dir, name))) return 0;
    /* Una tabla directa por dirección (1 + la función de la instrucción que empieza ahí, o 0): en GENRecomp el
     * Z80 entra al código generado tras casi cada instrucción del 68000, y buscar costaba */
    unsigned nf = r->nfuncs ? r->nfuncs : 1, size = (unsigned)(r->hi - r->lo) + 1;
    fprintf(f, "/* Generado por %s: no editar. */\n#include \"%s.h\"\n\n", tool, base);
    fprintf(f, "static const Z80RecompFn f_fn[%u] = {", nf);
    for (unsigned i = 0; i < r->nfuncs; i++) fprintf(f, "%s%s%04X,", i % 4 ? " " : "\n    ", r->prefix, r->funcs[i].lo);
    if (!r->nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\n\n/* Desde $%04X: 1 + la función de la instrucción que empieza en cada dirección, o 0 */\n"
               "static const uint%d_t fn_at[%u] = {", r->lo, r->nfuncs < 255 ? 8 : 16, size);
    for (unsigned i = 0; i < size; i++) fprintf(f, "%s%u,", i % 32 ? "" : "\n    ", r->fn_of[r->lo + i]);
    fprintf(f, "\n};\n\nZ80RecompFn %slookup(uint16_t pc)\n{\n"
               "    unsigned i = (unsigned)(pc - 0x%04X);\n"
               "    return i < %uu && fn_at[i] ? f_fn[fn_at[i] - 1] : 0;\n}\n",
            r->prefix, r->lo, size);
    return fclose(f) == 0;
}

#endif
