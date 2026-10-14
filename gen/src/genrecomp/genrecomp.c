/*
 * genrecomp: recompilador estático de juegos de Sega Genesis a C99.
 *
 * Uso: genrecomp juego.bin -o salida/ [--trace traza.txt] [--name nombre]
 *      genrecomp juego.bin [--info]
 *
 * Genera un proyecto completo (código recompilado, runtime, plataforma y Makefile). El proyecto no
 * contiene datos de la ROM: el ejecutable carga la ROM del usuario y valida su SHA1.
 *
 * Pasos:
 *   1. Descubrimiento: recorrido desde los vectores de excepción que apuntan a la ROM (el de reset,
 *      las interrupciones, los TRAP...) siguiendo Bcc, BRA, BSR, DBcc y los JMP/JSR con destino
 *      constante (absoluto o relativo al PC). Las instrucciones que ejecutó el juego (--trace: las
 *      escriben gentrace, la ventana con --trace y difftest --uncovered) se agregan como puntos de
 *      entrada: así se llega a lo que solo se alcanza por saltos indirectos o por bancos de SSF2.
 *   2. Partición en funciones: tramos contiguos de ROM que no cruzan un banco de 512 KB (lo que el
 *      mapper de SSF2 cambia de lugar y donde puede aparecer la SRAM).
 *   3. Emisión de C: cada instrucción llama a su k_* de m68k_ops.h (la misma semántica que el
 *      intérprete) con el opcode, las palabras de extensión y el PC constantes.
 *
 * Las instrucciones se identifican por su desplazamiento en la ROM, no por la dirección del CPU: lo
 * que no se descubrió, o está en RAM, lo ejecuta el intérprete. Descubrir de más no rompe nada (lo
 * que se recompila es lo que hay en esa dirección); de menos, tampoco.
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "m68k_ops.h"
#include "console.h"
#include "trace_line.h"
#include "embedded.h"
#include "dis68k.h"

#ifndef GENRECOMP_VERSION
#define GENRECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "genrecomp " GENRECOMP_VERSION ": recompila una ROM de Sega Genesis a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: genrecomp juego.bin -o salida/ [opciones]\n"
    "     genrecomp juego.bin [--info]\n"
    "  -o carpeta         dónde escribir el proyecto (se crea si no existe)\n"
    "  --name nombre      nombre del ejecutable (por omisión, el de la ROM)\n"
    "  --trace archivo    instrucciones que ejecutó el juego (las escriben gentrace, la ventana con\n"
    "                     --trace y difftest --uncovered); se agregan como entradas (se puede repetir)\n"
    "  --coverage archivo compara lo descubierto sin traza con las instrucciones que ejecutó el juego\n"
    "                     (una traza): cuántas se encontraron y cuáles faltan\n"
    "  --why BB:DIRECCION explica por qué esa instrucción (banco y dirección como en la traza, o su\n"
    "                     desplazamiento hexadecimal en la ROM) se tomó como código: la cadena de saltos\n"
    "                     y tablas desde un punto de entrada (se puede repetir; sin -o no genera el proyecto)\n"
    "  --viz              mapa de la ROM en la terminal, coloreado por cómo se descubrió (con\n"
    "                     --coverage, en rojo lo que el juego ejecutó y no se encontró)\n"
    "  --color            con --viz, colorea aunque la salida no sea una terminal (lo usa la GUI)\n"
    "  --html archivo     el descubrimiento como una página HTML sola: mapa, instrucciones por celda y\n"
    "                     la cadena de \"por qué\" al hacer clic (con --coverage marca lo que falta)\n"
    "  --events archivo   cada instrucción descubierta con su origen (texto: desplazamiento, dirección,\n"
    "                     cómo se llegó, de dónde, origen, desensamblado)\n"
    "  --codemap archivo  qué bytes de la ROM se tomaron como código: una letra por byte (C = código),\n"
    "                     64 por línea; el mismo formato que difftest --executed\n"
    "  --no-tables        sin las heurísticas de tablas de saltos (para comparar)\n"
    "  --no-spec          sin los punteros sueltos: inmediatos y tablas que ningún código delata\n"
    "  --dis OFF:DIR:N    desensambla N instrucciones desde ese desplazamiento de la ROM, con esa dirección\n"
    "  --info             formato, SHA1, cabecera y vectores de la ROM (lo que hace sin otras opciones)\n"
    "  --verbose          detalles del descubrimiento\n"
    "  --version, --help\n";

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "genrecomp: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static u8 *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

/* ------------------------------------------------------------------ */
/* --info                                                              */
/* ------------------------------------------------------------------ */

/* Ancho en letras de un texto UTF-8 (printf cuenta bytes y los acentos desalinean las columnas) */
static int utf8_width(const char *s)
{
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static void print_info(const char *path, const Gen *g)
{
    const Cart *c = &g->cart;
    const GenHeader *h = &c->hdr;
    printf("%s: %zu KB, %s\n", path, c->rom_size / 1024, c->format == ROMFMT_SMD ? "SMD (entrelazado)" : "binario");
    printf("SHA1:        %s\n", c->sha1);
    if (!h->sega) printf("aviso: no hay \"SEGA\" en $100; puede no ser una ROM de Genesis\n");
    printf("sistema:     %s\n", h->system);
    printf("copyright:   %s\n", h->copyright);
    printf("nombre (JP): %s\n", h->name_dom);
    printf("nombre:      %s\n", h->name_int);
    printf("serie:       %s\n", h->serial);
    printf("región:      %s\n", h->region);
    printf("periféricos: %s\n", h->io);
    printf("suma:        $%04X (calculada $%04X, %s)\n", h->checksum, h->checksum_calc,
           h->checksum == h->checksum_calc ? "coincide" : "no coincide");
    printf("ROM:         $%06X-$%06X\n", h->rom_start, h->rom_end);
    if (c->sram)
        printf("SRAM:        $%06X-$%06X (tipo $%02X%s)\n", c->sram_start, c->sram_end, c->sram_type,
               c->sram_type & 0x40 ? ", con batería" : "");
    else
        printf("SRAM:        no\n");
    if (c->has_eeprom) printf("EEPROM:      serie\n");
    if (c->rom_size > ROM_LINEAR) printf("mapper:      ROM de más de 4 MB (necesita el mapper de SSF2)\n");
    printf("vectores:\n");
    for (int n = 0; n < VEC_COUNT; n++) {
        const char *name = cart_vector_name(n);
        char trap[16];
        if (!name && n >= VEC_TRAP0 && n < VEC_TRAP0 + 16) { snprintf(trap, sizeof trap, "TRAP #%d", n - VEC_TRAP0); name = trap; }
        if (name) printf("  %2d %s%*s $%06X\n", n, name, 20 - utf8_width(name), "", cart_vector(c, n));
    }
}

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */

#define BANK_SIZE 0x80000u          /* 512 KB: ventana del mapper de SSF2 (y de la SRAM en $200000) */

static Gen *gen;                    /* la ROM cargada (normalizada) y su mapeo inicial */
static const u8 *rom;
static size_t rom_size;
static int verbose;
static int use_tables = 1;          /* --no-tables: sin las heurísticas de tablas de saltos */
static int use_spec = 1;            /* --no-spec: sin los punteros sueltos (inmediatos y tablas sin código que las use) */

/* Cómo se llegó a una instrucción (para --why, --viz, --events y --html) */
enum { HOW_VECTOR, HOW_TRACE, HOW_LINEAR, HOW_JMP, HOW_JSR, HOW_BRANCH, HOW_TABLE, HOW_RUN, HOW_IMM, HOW_AFTER, HOW_COUNT };
static const char *const how_name[HOW_COUNT] = { "vector", "traza", "sigue", "salto", "llamada", "bifurcación", "tabla",
                                                 "tabla suelta", "puntero inmediato", "tras un final" };
enum { ORG_FLOW, ORG_TRACE, ORG_TABLE, ORG_SPEC, ORG_COUNT };
static const char *const org_name[ORG_COUNT] = { "flujo desde los vectores", "traza", "tabla de saltos",
                                                 "especulativo (punteros sueltos)" };
#define NO_PARENT 0xFFFFFFFFu

/* Por palabra de ROM (desplazamiento / 2) */
static u8  *is_ins;                 /* empieza una instrucción ahí */
static u32 *ins_addr;               /* con qué dirección del CPU se descubrió */
static u32 *parent;                 /* de qué instrucción se llegó (desplazamiento) o NO_PARENT */
static u8  *how_at, *origin;
static u8  *missing_mark;           /* --coverage: ejecutadas y no descubiertas */

static unsigned stat_insns, stat_bytes, stat_seeds, stat_trace, stat_ind, stat_unresolved;
static unsigned stat_tables, stat_table_entries, stat_runs, stat_run_entries, stat_imm;

typedef struct { u32 off, addr, from; u8 how; } Work;
typedef struct { Work *w; size_t n, cap; } Queue;
static Queue sure, spec;            /* lo especulativo se recorre cuando ya no queda nada seguro */
static u32 cur_from = NO_PARENT;    /* instrucción que está haciendo el descubrimiento */
static int cur_how = HOW_JMP;

static u16 romw(size_t off) { return off + 1 < rom_size ? (u16)(rom[off] << 8 | rom[off + 1]) : 0xFFFF; }
static u32 roml(size_t off) { return (u32)romw(off) << 16 | romw(off + 2); }
static int ins_len(size_t off) { return 2 + 2 * m68k_ext_words(romw(off)); }

/* Texto de la instrucción en off (con la dirección dada) */
static void disasm_at(size_t off, u32 addr, char *out, size_t size)
{
    u16 w[5];
    for (int i = 0; i < 5; i++) w[i] = romw(off + 2 * (size_t)i);
    dis68k(w, addr, out, size);
}

/* ------------------------------------------------------------------ */
/* Descubrimiento                                                      */
/* ------------------------------------------------------------------ */

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

/* Desplazamiento en la ROM de un destino `target` visto desde la instrucción (off, addr): en el mismo
 * banco de 512 KB, el mismo desplazamiento relativo (el mapper no cambia dentro de un tramo); si no,
 * el mapeo del arranque. -1 si no es ROM. */
static long target_offset(u32 off, u32 addr, u32 target)
{
    target &= 0xFFFFFF;
    if (target & 1) return -1;
    if ((target >> 19) == (addr >> 19) && target < 0x400000) {
        long t = (long)off + ((long)target - (long)addr);
        if (t >= 0 && (size_t)t < rom_size) return t;
    }
    return gen_code_offset(gen, target);
}

static void follow(u32 off, u32 addr, u32 target)
{
    long t = target_offset(off, addr, target);
    if (t >= 0) push_work((u32)t, target);
    else {
        stat_unresolved++;
        if (verbose) printf("  $%06X: destino $%06X fuera de la ROM\n", addr, target & 0xFFFFFF);
    }
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

/* ¿Una dirección absoluta que un programa del Genesis usaría? (cartucho, Z80, E/S, VDP, RAM) */
static int sane_abs(u32 a, int is_long)
{
    if (is_long && (a >> 24) != 0x00 && (a >> 24) != 0xFF) return 0;
    a &= 0xFFFFFF;
    return a < 0x400000 || (a >= 0xA00000 && a < 0xA20000) || (a >= 0xA11000 && a < 0xA15000) ||
           (a >= 0xC00000 && a < 0xC00020) || a >= 0xE00000;
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

/*
 * ¿Lo que hay en (off, addr) parece el comienzo de una rutina? Se decodifica en línea recta, sin
 * guardar nada: opcodes válidos (ni ILLEGAL, ni línea A/F, ni ori.b #0,d0: ceros), direcciones absolutas
 * razonables y destinos de saltos dentro de la ROM, hasta un RTS/RTE/RTR/BRA/JMP. Con `strict`
 * (punteros sueltos, sin un código que los use como tabla) ese final tiene que llegar en menos de 64
 * instrucciones; si no, también valen 64 instrucciones válidas seguidas (las rutinas de
 * inicialización son largas). Los datos al azar casi nunca pasan.
 */
static int plausible(u32 off, u32 addr, int strict)
{
    if ((off & 1) || (addr & 1) || addr < 0x200) return 0;
    for (int n = 0; n < 64; n++) {
        if (off + 2 > rom_size) return 0;
        if (is_ins[off >> 1]) return 1;                 /* se une a código ya descubierto */
        u16 op = romw(off);
        int kind = m68k_decode(op), len = ins_len(off);
        if ((op == 0x0000 && romw(off + 2) == 0x0000) || kind == K_ILLEGAL || kind == K_LINEA || kind == K_LINEF) return 0;
        if (off + (u32)len > rom_size || !sane_operands(off, op, kind)) return 0;
        switch (kind) {
        case K_BCC: {
            u32 d8 = op & 0xFF;
            u32 to = addr + 2 + (d8 ? sx8(d8) : sx16(romw(off + 2)));
            if (target_offset(off, addr, to) < 0) return 0;
            if ((op >> 8 & 15) == 0) return 1;          /* BRA */
            break; }
        case K_DBCC:
            if (target_offset(off, addr, addr + 2 + sx16(romw(off + 2))) < 0) return 0;
            break;
        case K_JMP: case K_JSR: {
            long t = jump_target(off, addr, op);
            if (t >= 0 && t < 0x400000 && target_offset(off, addr, (u32)t) < 0) return 0;
            if (kind == K_JMP) return 1;
            break; }
        case K_RTS: case K_RTE: case K_RTR: return 1;
        default: break;
        }
        off += (u32)len;
        addr += (u32)len;
        if ((off ^ (off - (u32)len)) & ~(BANK_SIZE - 1)) return 0;     /* cruzó de banco */
    }
    return !strict;
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

/* Tabla de desplazamientos de 16 bits relativos a la base (Sonic: move.w tbl(pc,d0.w),d1 /
 * jmp tbl(pc,d1.w)). Termina donde empieza la primera rutina o en la primera entrada que no apunta
 * a algo con pinta de código. */
static void scan_offset_table(u32 src_off, u32 src_addr, u32 base)
{
    long tb = target_offset(src_off, src_addr, base);
    if (tb < 0) return;
    int saved = cur_how;
    cur_how = HOW_TABLE;
    long end = (long)rom_size;
    int good = 0;
    for (int i = 0; i < 512; i++) {
        long p = tb + 2 * i;
        if (p + 2 > end || (i && is_ins[p >> 1])) break;
        u32 t = (base + sx16(romw((size_t)p))) & 0xFFFFFF;
        long to = target_offset(src_off, src_addr, t);
        if (to < 0 || !(is_ins[to >> 1] || plausible((u32)to, t, 0))) break;
        if (to > tb && to < end) end = to;              /* la tabla termina antes de la primera rutina */
        push_work((u32)to, t);
        good++;
    }
    if (good) { stat_tables++; stat_table_entries += (unsigned)good; }
    cur_how = saved;
}

/* Tabla de saltos hecha de instrucciones (jmp tbl(pc,d0.w) con tbl: bra.w a / bra.w b / ...): cada
 * entrada es código. Se aceptan BRA.W, BRA.S, JMP (xxx).w y JMP (xxx).l, todas del mismo largo. */
static void scan_branch_table(u32 src_off, u32 src_addr, u32 base)
{
    long tb = target_offset(src_off, src_addr, base);
    if (tb < 0) return;
    int saved = cur_how;
    cur_how = HOW_TABLE;
    int size = 0, good = 0;
    for (int i = 0; i < 256; i++) {
        long p = tb + (long)i * size;
        if (p + 2 > (long)rom_size) break;
        u16 w = romw((size_t)p);
        int s = w == 0x6000 ? 4 : w == 0x4EF9 ? 6 : w == 0x4EF8 ? 4 : ((w & 0xFF00) == 0x6000 && !(w & 1)) ? 2 : 0;
        if (!s) break;
        if (!size) size = s;
        if (s != size) break;
        u32 a = (base + (u32)(i * size)) & 0xFFFFFF;
        /* el destino de la entrada tiene que ser razonable */
        u32 t = s == 6 ? roml((size_t)p + 2) : s == 4 && w == 0x4EF8 ? sx16(romw((size_t)p + 2)) :
                a + 2 + (s == 4 ? sx16(romw((size_t)p + 2)) : sx8(w & 0xFF));
        long to = target_offset((u32)p, a, t);
        if (to < 0 || !(is_ins[to >> 1] || plausible((u32)to, t & 0xFFFFFF, 0))) break;
        push_work((u32)p, a);
        good++;
    }
    if (verbose) printf("  $%06X: tabla de saltos en $%06X, %d entradas\n", src_addr, base, good);
    if (good) { stat_tables++; stat_table_entries += (unsigned)good; }
    cur_how = saved;
}

/* Tabla de punteros de 32 bits (movea.l tbl(pc,d0.w),a0 / jmp (a0)) */
static void scan_ptr_table(u32 src_off, u32 src_addr, u32 base)
{
    long tb = target_offset(src_off, src_addr, base);
    if (tb < 0) return;
    int saved = cur_how;
    cur_how = HOW_TABLE;
    int good = 0;
    for (int i = 0; i < 256; i++) {
        long p = tb + 4 * i;
        if (p + 4 > (long)rom_size || (i && is_ins[p >> 1])) break;
        u32 t = roml((size_t)p);
        if (t >> 24 || t >= 0x400000) break;
        long to = gen_code_offset(gen, t);
        if (to < 0 || !(is_ins[to >> 1] || plausible((u32)to, t, 0))) break;
        push_work((u32)to, t);
        good++;
    }
    if (good) { stat_tables++; stat_table_entries += (unsigned)good; }
    cur_how = saved;
}

/* JMP/JSR con un operando que no es constante: ¿de qué tabla sale el destino? */
static void analyze_indirect(u32 off, u32 addr, u16 op)
{
    int mode = op >> 3 & 7, reg = op & 7, h = nhist - 1;      /* hist[h] es este JMP/JSR */
    u16 x = romw(off + 2);
    stat_ind++;
    if ((mode == 7 && reg == 3) || mode == 6) {
        u32 base, tbase;
        if (!indexed_base(h, mode, reg, x, addr + 2, &base)) return;
        int sz = loaded_from_table(h, (x & 0x8000) != 0, x >> 12 & 7, &tbase);
        if (sz == 1 && tbase == base) scan_offset_table(off, addr, base);
        else scan_branch_table(off, addr, base);
        return;
    }
    if (mode == 2 || mode == 5) {
        u32 d = mode == 5 ? sx16(x) : 0, base, k;
        int sz = loaded_from_table(h, 1, reg, &base);
        if (sz == 2) { scan_ptr_table(off, addr, base); return; }
        if (!sz && known_areg(h, reg, &k)) {                       /* lea rutina,a0 / jmp (a0) */
            int saved = cur_how;
            cur_how = HOW_TABLE;
            follow(off, addr, k + d);
            cur_how = saved;
        }
    }
}

/* MOVE.L #rutina,... / MOVEA.L #rutina,An / PEA rutina / LEA rutina,An: un puntero a código que se
 * guarda para llamarlo después (objetos de Sonic 2/3, tareas de Comix Zone, trampolines JMP en la RAM) */
static void immediate_pointer(u32 off, u32 addr, u16 op)
{
    u32 t;
    if ((op >> 12) == 2 && (op & 0x3F) == 0x3C && (op >> 6 & 7) != 0) t = roml(off + 2);     /* no a un Dn */
    else if (op == 0x4879) t = roml(off + 2);
    else if (op == 0x487A) t = addr + 2 + sx16(romw(off + 2));
    else if ((op & 0xF1FF) == 0x41FA) t = addr + 2 + sx16(romw(off + 2));     /* LEA d16(PC),An */
    else if ((op & 0xF1FF) == 0x41F9) t = roml(off + 2);                     /* LEA (xxx).l,An */
    else return;
    if (t >> 24 || t >= 0x400000 || t < 0x200 || (t & 1)) return;
    long to = gen_code_offset(gen, t);
    if (to < 0 || is_ins[to >> 1] || !plausible((u32)to, t, 1)) return;
    int saved = cur_how;
    cur_how = HOW_IMM;
    push_work((u32)to, t);
    stat_imm++;
    cur_how = saved;
}

/* Tablas de punteros que ningún patrón de código delata: cuatro o más largos seguidos que apuntan,
 * todos, a algo con pinta de rutina (tablas de objetos, de estados) */
static void scan_pointer_runs(void)
{
    cur_how = HOW_RUN;
    cur_from = NO_PARENT;
    size_t lim = rom_size < ROM_LINEAR ? rom_size : ROM_LINEAR;
    int run = 0;
    for (size_t o = 0x200; o + 4 <= lim; o += 2) {
        u32 v = roml(o);
        long to = -1;
        if (!(v >> 24) && v >= 0x200 && v < lim && !(v & 1)) to = gen_code_offset(gen, v);
        if (to >= 0 && (is_ins[to >> 1] || plausible((u32)to, v, 1))) {
            /* un largo cada 4 bytes: las entradas de la misma tabla van seguidas */
            run = run ? run + 1 : 1;
            if (run == 4) {
                stat_runs++;
                for (int k = 0; k < 4; k++) {
                    u32 kv = roml(o - 12 + 4 * (size_t)k);
                    push_work((u32)gen_code_offset(gen, kv), kv);
                    stat_run_entries++;
                }
            } else if (run > 4) {
                push_work((u32)to, v);
                stat_run_entries++;
            }
            o += 2;                                     /* la siguiente entrada, 4 bytes después */
        } else run = 0;
    }
}

/* Después de un final de rutina (RTS, JMP, BRA...) suele empezar otra: las rutinas van una detrás
 * de otra aunque solo se llegue a ellas por saltos calculados (los trozos desenrollados de Mortal
 * Kombat 3 que terminan en jmp (a0)). Si lo que sigue parece código, se toma. */
static unsigned stat_after;

static void after_end(u32 off, u32 addr, int len)
{
    if (!use_spec) return;
    u32 n = off + (u32)len, a = addr + (u32)len;
    if (n + 2 > rom_size || (n ^ off) & ~(BANK_SIZE - 1) || is_ins[n >> 1]) return;
    if (!plausible(n, a, 1)) return;
    int saved = cur_how;
    cur_how = HOW_AFTER;
    push_work(n, a);
    stat_after++;
    cur_how = saved;
}

/* Sigue una cadena de instrucciones desde (off, addr) hasta un salto sin vuelta */
static void walk(Work wk)
{
    u32 off = wk.off, addr = wk.addr, prev = NO_PARENT;
    nhist = 0;
    for (;;) {
        if ((off & 1) || off + 2 > rom_size || (addr & 1)) return;
        if (is_ins[off >> 1]) return;
        u16 op = romw(off);
        int kind = m68k_decode(op);
        int len = ins_len(off);
        if (off + (u32)len > rom_size) return;
        /* un opcode inválido seguido del flujo: son datos (salvo ILLEGAL a propósito o si lo dice la traza) */
        if (kind == K_ILLEGAL && op != 0x4AFC && wk.how != HOW_TRACE) return;
        is_ins[off >> 1] = 1;
        ins_addr[off >> 1] = addr;
        stat_insns++;
        stat_bytes += (unsigned)len;
        if (prev == NO_PARENT) {
            parent[off >> 1] = wk.from;
            how_at[off >> 1] = wk.how;
            origin[off >> 1] = wk.how == HOW_TRACE ? ORG_TRACE : wk.how == HOW_TABLE ? ORG_TABLE :
                               wk.how == HOW_RUN || wk.how == HOW_IMM || wk.how == HOW_AFTER ? ORG_SPEC :
                               wk.from != NO_PARENT ? origin[wk.from >> 1] : ORG_FLOW;
        } else {
            parent[off >> 1] = prev;
            how_at[off >> 1] = HOW_LINEAR;
            origin[off >> 1] = origin[prev >> 1];
        }
        prev = off;
        cur_from = off;
        hist_push(off);
        u32 next = addr + (u32)len;
        switch (kind) {
        case K_BCC: {
            u32 d8 = op & 0xFF;
            u32 to = addr + 2 + (d8 ? sx8(d8) : sx16(romw(off + 2)));
            int cc = op >> 8 & 15;
            cur_how = cc == 0 ? HOW_JMP : cc == 1 ? HOW_JSR : HOW_BRANCH;
            follow(off, addr, to);
            if (cc == 0) { after_end(off, addr, len); return; }     /* BRA */
            break; }
        case K_DBCC:
            cur_how = HOW_BRANCH;
            follow(off, addr, addr + 2 + sx16(romw(off + 2)));
            break;
        case K_JMP: case K_JSR: {
            long t = jump_target(off, addr, op);
            cur_how = kind == K_JMP ? HOW_JMP : HOW_JSR;
            if (t >= 0) follow(off, addr, (u32)t);
            else if (use_tables) analyze_indirect(off, addr, op);
            if (kind == K_JMP) { after_end(off, addr, len); return; }
            break; }
        case K_RTS: case K_RTE: case K_RTR:
            after_end(off, addr, len);
            return;
        case K_ILLEGAL: case K_LINEA: case K_LINEF:
            return;
        case K_MOVE: case K_MOVEA: case K_PEA: case K_LEA:
            if (use_spec) immediate_pointer(off, addr, op);
            break;
        default:
            break;
        }
        /* La siguiente: en el mismo banco, a continuación; si cruza, con el mapeo del arranque */
        long t = target_offset(off, addr, next);
        if (t < 0) return;
        off = (u32)t;
        addr = next;
    }
}

static void drain(void)
{
    while (sure.n || spec.n) {
        if (sure.n) walk(sure.w[--sure.n]);
        else walk(spec.w[--spec.n]);
    }
}

/* Recorre todo lo pendiente; las tablas sueltas pueden agregar más y se repite */
static void discover(void)
{
    int runs_done = 0;
    for (;;) {
        drain();
        unsigned before = stat_insns;
        if (use_spec && use_tables && !runs_done) { runs_done = 1; scan_pointer_runs(); }
        drain();
        if (stat_insns == before) break;
    }
    cur_from = NO_PARENT;
}

static void seed(u32 off, u32 addr, int how)
{
    cur_from = NO_PARENT;
    cur_how = how;
    stat_seeds++;
    push_work(off, addr);
}

static void seed_vectors(void)
{
    for (int v = 1; v < VEC_COUNT; v++) {
        u32 a = cart_vector(&gen->cart, v) & 0xFFFFFF;
        /* los vectores sin usar suelen valer 0: debajo de $200 están los vectores y la cabecera */
        long off = (a & 1) || a < 0x200 ? -1 : gen_code_offset(gen, a);
        if (off < 0) continue;
        if (verbose) printf("vector %2d = $%06X\n", v, a);
        seed((u32)off, a, HOW_VECTOR);
    }
}

#define TRACE_SHIFT 19      /* bancos de la traza: ventanas de 512 KB (GEN_TRACE_SHIFT del runtime) */

/* --why: "BB:DIRECCION" como en la traza, o el desplazamiento en la ROM */
static u32 why_arg(const char *arg)
{
    unsigned long off, addr;
    return (u32)(trace_line_read(arg, TRACE_SHIFT, &off, &addr) ? off : strtoul(arg, NULL, 16));
}

/* Traza "BANCO:DIRECCION" (trace_line.h; también el formato viejo "DESPLAZAMIENTO:DIRECCION") */
static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    char line[128];
    unsigned long off, addr;
    while (fgets(line, sizeof line, f))
        if (trace_line_read(line, TRACE_SHIFT, &off, &addr) && off < rom_size && !(off & 1)) {
            if (!is_ins[off >> 1]) stat_trace++;
            seed((u32)off, (u32)addr & 0xFFFFFF, HOW_TRACE);
        }
    fclose(f);
}

/* --coverage: de las instrucciones de una traza, cuántas encontró el análisis sin ayuda */
static void coverage_report(const char *path)
{
    missing_mark = calloc(rom_size / 2 + 1, 1);
    FILE *f = fopen(path, "r");
    if (!f || !missing_mark) die("no se pudo abrir %s", path);
    char line[128];
    unsigned long off, addr;
    unsigned total = 0, found = 0, shown = 0;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr) || off >= rom_size || (off & 1)) continue;
        total++;
        if (is_ins[off >> 1]) { found++; continue; }
        missing_mark[off >> 1] = 1;
        if (shown < 12 || verbose) {
            char d[96];
            disasm_at(off, (u32)addr, d, sizeof d);
            printf("  falta: ROM $%06lX = $%06lX  %s\n", off, addr, d);
            shown++;
        }
    }
    fclose(f);
    printf("cobertura: %u de %u instrucciones ejecutadas descubiertas sin traza (%.1f%%); descubiertas en total %u\n",
           found, total, total ? 100.0 * found / total : 100.0, stat_insns);
}

/* ------------------------------------------------------------------ */
/* --why, --viz, --events, --codemap, --html                           */
/* ------------------------------------------------------------------ */

static void explain(u32 off)
{
    if (off >= rom_size || (off & 1) || !is_ins[off >> 1]) {
        printf("ROM $%06X: no se descubrió como código\n", off);
        return;
    }
    printf("ROM $%06X ($%06X) se tomó como código así:\n", off, ins_addr[off >> 1]);
    char d[96], d0[96];
    u32 cur = off;
    int steps = 0;
    while (cur != NO_PARENT && steps++ < 4000) {
        disasm_at(cur, ins_addr[cur >> 1], d, sizeof d);
        u32 par = parent[cur >> 1], first = cur;
        int how = how_at[cur >> 1], run = 0;
        /* las instrucciones seguidas se agrupan: solo se cuenta cuántas hubo */
        while (how == HOW_LINEAR && par != NO_PARENT) { cur = par; par = parent[cur >> 1]; how = how_at[cur >> 1]; run++; }
        if (run) {
            disasm_at(cur, ins_addr[cur >> 1], d0, sizeof d0);
            printf("  $%06X $%06X  %-30s  ...%d instrucciones seguidas hasta  $%06X $%06X  %s\n", first, ins_addr[first >> 1], d,
                   run, cur, ins_addr[cur >> 1], d0);
        } else {
            printf("  $%06X $%06X  %s\n", cur, ins_addr[cur >> 1], d);
        }
        if (par == NO_PARENT) {
            printf("  └ punto de entrada: %s\n", how_name[how]);
            return;
        }
        printf("  └ %s desde\n", how_name[how]);
        cur = par;
    }
}

static void write_events(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    fprintf(f, "# desplazamiento dirección cómo-se-llegó desde origen desensamblado\n");
    for (size_t i = 0; i < rom_size; i += 2) {
        if (!is_ins[i >> 1]) continue;
        char d[96];
        disasm_at(i, ins_addr[i >> 1], d, sizeof d);
        if (parent[i >> 1] == NO_PARENT)
            fprintf(f, "%06zX %06X %s - %s %s\n", i, ins_addr[i >> 1], how_name[how_at[i >> 1]], org_name[origin[i >> 1]], d);
        else
            fprintf(f, "%06zX %06X %s %06X %s %s\n", i, ins_addr[i >> 1], how_name[how_at[i >> 1]], parent[i >> 1],
                    org_name[origin[i >> 1]], d);
    }
    fclose(f);
}

/* Bytes por celda del mapa: 256 hasta 1 MB; más, para que queden unas 64 filas de 64 celdas */
static size_t viz_cell(void)
{
    size_t c = 256;
    while (rom_size / c > 4096) c *= 2;
    return c;
}

static void viz_map(int color)
{
    static const int org_color[ORG_COUNT] = { 46, 51, 226, 201 };        /* verde, cian, amarillo, magenta */
    size_t cell = viz_cell(), row_size = cell * 64;
    printf("\nMapa de la ROM (una celda = %zu bytes; filas de %zu KB):\n", cell, row_size / 1024);
    for (int o = 0; o < ORG_COUNT; o++) {
        if (color) printf("  \033[38;5;%dm#\033[0m %s\n", org_color[o], org_name[o]);
        else printf("  %c %s\n", "#=%@"[o], org_name[o]);
    }
    if (missing_mark) printf("  %sX%s ejecutado por el juego y no descubierto\n", color ? "\033[41;97m" : "", color ? "\033[0m" : "");
    printf("  . sin código   : menos de la mitad   # la mitad o más\n\n");
    unsigned cells = 0, code_cells = 0;
    for (size_t row = 0; row < rom_size; row += row_size) {
        printf("%06zX ", row);
        for (size_t c0 = row; c0 < row + row_size && c0 < rom_size; c0 += cell) {
            unsigned bytes = 0, cnt[ORG_COUNT] = { 0 };
            int miss = 0;
            for (size_t i = c0; i < c0 + cell && i < rom_size; i += 2) {
                if (is_ins[i >> 1]) { unsigned l = (unsigned)ins_len(i); bytes += l; cnt[origin[i >> 1]] += l; }
                if (missing_mark && missing_mark[i >> 1]) miss = 1;
            }
            cells++;
            if (bytes) code_cells++;
            if (miss) { printf("%sX%s", color ? "\033[41;97m" : "", color ? "\033[0m" : ""); continue; }
            if (!bytes) { putchar('.'); continue; }
            int best = 0;
            for (int o = 1; o < ORG_COUNT; o++) if (cnt[o] > cnt[best]) best = o;
            char ch = bytes >= cell / 2 ? '#' : ':';
            if (color) printf("\033[38;5;%dm%c\033[0m", org_color[best], ch);
            else putchar(ch == '#' ? "#=%@"[best] : ':');
        }
        putchar('\n');
    }
    printf("\n%u de %u celdas con código (%.1f%% de la ROM en bytes)\n", code_cells, cells, 100.0 * stat_bytes / (double)rom_size);
}

/* --codemap: qué bytes de la ROM se tomaron como código (C) y cuáles no (.), 64 por línea. El mismo
 * formato que difftest --executed, nesrecomp --codemap y gbrecomp --codemap */
static void write_codemap(const char *path)
{
    u8 *code = calloc(rom_size ? rom_size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!code || !f) die("no se pudo crear %s", path);
    for (size_t i = 0; i < rom_size; i += 2) {
        if (!is_ins[i >> 1]) continue;
        int len = ins_len(i);
        for (int k = 0; k < len && i + (size_t)k < rom_size; k++) code[i + (size_t)k] = 1;
    }
    for (size_t i = 0; i < rom_size; i++) {
        fputc(code[i] ? 'C' : '.', f);
        if (i % 64 == 63) fputc('\n', f);
    }
    free(code);
    if (fclose(f)) die("no se pudo escribir %s", path);
}

static void json_str(FILE *f, const char *t)
{
    fputc('"', f);
    for (; *t; t++) {
        if (*t == '"' || *t == '\\') fputc('\\', f);
        if (*t == '<') { fputs("\\u003c", f); continue; }
        fputc(*t, f);
    }
    fputc('"', f);
}

#include "visor_html.h"

static void write_html(const char *path, const char *rom_name)
{
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    fputs(html_head, f);
    fputs("{\"name\":", f);
    json_str(f, rom_name);
    fprintf(f, ",\"rom\":%zu,\"cell\":%zu,\"bytes\":%u,\"orgs\":[", rom_size, viz_cell(), stat_bytes);
    for (int i = 0; i < ORG_COUNT; i++) { if (i) fputc(',', f); json_str(f, org_name[i]); }
    fputs("],\"hows\":[", f);
    for (int i = 0; i < HOW_COUNT; i++) { if (i) fputc(',', f); json_str(f, how_name[i]); }
    fputs("],\"missing\":[", f);
    int first = 1;
    if (missing_mark)
        for (size_t i = 0; i < rom_size; i += 2)
            if (missing_mark[i >> 1]) { fprintf(f, first ? "%zu" : ",%zu", i); first = 0; }
    fputs("],\"ins\":[\n", f);
    first = 1;
    for (size_t i = 0; i < rom_size; i += 2) {
        if (!is_ins[i >> 1]) continue;
        char d[96];
        disasm_at(i, ins_addr[i >> 1], d, sizeof d);
        fprintf(f, "%s[%zu,%u,%d,%ld,%d,", first ? "" : ",\n", i, ins_addr[i >> 1], how_at[i >> 1],
                parent[i >> 1] == NO_PARENT ? -1L : (long)parent[i >> 1], origin[i >> 1]);
        json_str(f, d);
        fprintf(f, ",%d]", ins_len(i));
        first = 0;
    }
    fputs("\n]}", f);
    fputs(html_tail, f);
    fclose(f);
}

/* --dis OFFSET:DIR:N */
static void dis_range(const char *arg)
{
    unsigned long off, addr, n;
    if (sscanf(arg, "%lx:%lx:%lu", &off, &addr, &n) != 3) die("--dis espera OFFSET:DIRECCION:N (hexadecimal, hexadecimal, decimal)");
    for (unsigned long k = 0; k < n && off + 2 <= rom_size; k++) {
        char d[96];
        disasm_at(off, (u32)addr, d, sizeof d);
        int len = ins_len(off);
        printf("%06lX  $%06lX  ", off, addr);
        for (int i = 0; i < 5; i++) {
            if (i < len / 2) printf("%04X ", romw(off + 2 * (size_t)i));
            else printf("     ");
        }
        printf(" %s%s\n", d, is_ins[off >> 1] ? "" : "   ; (no descubierta)");
        off += (unsigned long)len;
        addr += (unsigned long)len;
    }
}

/* ------------------------------------------------------------------ */
/* Partición en funciones                                              */
/* ------------------------------------------------------------------ */

#define FUNC_MAX_INS 400            /* instrucciones por función (tamaño de lo que compila el C) */
#define FUNC_MAX_GAP 32             /* bytes sin código que se aceptan dentro de una función */

typedef struct { u32 lo, hi; unsigned n; } Func;     /* lo, hi: primera y última instrucción (desplazamientos) */
static Func *funcs;
static size_t nfuncs;
static u32 *fn_of;                  /* por palabra de ROM: índice de función + 1 */

static void partition(void)
{
    size_t cap = 0;
    fn_of = calloc(rom_size / 2 + 1, sizeof *fn_of);
    if (!fn_of) die("sin memoria");
    size_t i = 0;
    while (i < rom_size) {
        if (!is_ins[i >> 1]) { i += 2; continue; }
        if (nfuncs == cap) {
            cap = cap ? cap * 2 : 256;
            funcs = realloc(funcs, cap * sizeof *funcs);
            if (!funcs) die("sin memoria");
        }
        Func *f = &funcs[nfuncs++];
        *f = (Func){ (u32)i, (u32)i, 1 };
        fn_of[i >> 1] = (u32)nfuncs;
        size_t j = i + 2;
        while (j < rom_size && j / BANK_SIZE == i / BANK_SIZE && f->n < FUNC_MAX_INS) {
            if (is_ins[j >> 1]) {
                fn_of[j >> 1] = (u32)nfuncs;
                f->n++;
                f->hi = (u32)j;
                j += 2;
            } else {
                /* un hueco: se acepta si es corto y sigue código en el mismo banco */
                size_t k = j;
                while (k < rom_size && !is_ins[k >> 1] && k - j < FUNC_MAX_GAP && k / BANK_SIZE == i / BANK_SIZE) k += 2;
                if (k >= rom_size || !is_ins[k >> 1] || k / BANK_SIZE != i / BANK_SIZE) break;
                j = k;
            }
        }
        i = j;
    }
}

/* ------------------------------------------------------------------ */
/* Emisión                                                             */
/* ------------------------------------------------------------------ */

static int can_remap;               /* SSF2 o SRAM encima de la ROM: vigilar el mapeo */

static FILE *open_out(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    return f;
}

static void make_dir(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char s = *p; *p = 0; MKDIR(tmp); *p = s; }
    MKDIR(tmp);
}

/* ¿La instrucción en `target` (vista desde off) es de la misma función, con el mismo mapeo? */
static long in_func(u32 off, u32 target)
{
    u32 addr = ins_addr[off >> 1];
    target &= 0xFFFFFF;
    if (target & 1) return -1;
    long t = (long)off + ((long)target - (long)addr);
    if (t < 0 || (size_t)t >= rom_size || (u32)t / BANK_SIZE != off / BANK_SIZE) return -1;
    if (!is_ins[t >> 1] || ins_addr[t >> 1] != target || fn_of[t >> 1] != fn_of[off >> 1]) return -1;
    return t;
}

static const char *kind_name(int k)
{
    static const char *n[K_COUNT] = {
        "ILLEGAL", "LINEA", "LINEF", "IMM", "ICCR", "ISR", "BIT", "MOVEP", "MOVE", "MOVEA", "UNARY", "MOVEFSR",
        "MOVETCCR", "MOVETSR", "CHK", "LEA", "NBCD", "SWAP", "PEA", "EXT", "MOVEM", "MOVEMR", "TST", "TAS",
        "TRAP", "LINK", "UNLK", "MUSP", "RESET", "NOP", "STOP", "RTE", "RTS", "TRAPV", "RTR", "JSR", "JMP",
        "QUICK", "SCC", "DBCC", "BCC", "MOVEQ", "ALU", "ALUA", "MUL", "DIV", "BCD", "ADDX", "CMPM", "EXG",
        "SHIFTR", "SHIFTM",
    };
    return n[k];
}

static void emit_ins(FILE *f, u32 off)
{
    u32 addr = ins_addr[off >> 1];
    u16 op = romw(off);
    int kind = m68k_decode(op), n = m68k_ext_words(op);
    u32 next = (addr + 2 + 2 * (u32)n) & 0xFFFFFF;

    char dis[96];
    disasm_at(off, addr, dis, sizeof dis);
    fprintf(f, "L_%06X: /* $%06X  %s */\n", off, addr, dis);
    if (n) {
        fprintf(f, "    { static const u16 e[%d] = {", n);
        for (int i = 0; i < n; i++) fprintf(f, "%s0x%04X", i ? ", " : " ", romw(off + 2 + 2 * (u32)i));
        fprintf(f, " };\n      npc = k_%s(g, 0x%04X, e, 0x%06X); }\n", kind_name(kind), op, addr);
    } else {
        fprintf(f, "    npc = k_%s(g, 0x%04X, noext, 0x%06X);\n", kind_name(kind), op, addr);
    }
    fprintf(f, "    AFTER(npc);\n");
    if (can_remap) fprintf(f, "    MAPCHK(npc);\n");

    /* Sucesores conocidos dentro de la función */
    long succ[2];
    int ns = 0, fall = 1;
    switch (kind) {
    case K_BCC: {
        u32 d8 = op & 0xFF;
        u32 to = addr + 2 + (d8 ? sx8(d8) : sx16(romw(off + 2)));
        succ[ns++] = to;
        if ((op >> 8 & 15) <= 1) fall = 0;         /* BRA, BSR */
        break; }
    case K_DBCC: succ[ns++] = addr + 2 + sx16(romw(off + 2)); break;
    case K_JMP: case K_JSR: {
        long t = jump_target(off, addr, op);
        if (t >= 0) succ[ns++] = t;
        fall = 0;
        break; }
    case K_RTS: case K_RTE: case K_RTR: case K_ILLEGAL: case K_LINEA: case K_LINEF: case K_STOP:
        fall = 0;
        break;
    default: break;
    }
    if (fall) succ[ns++] = next;
    for (int i = 0; i < ns; i++) {
        long t = in_func(off, (u32)succ[i]);
        if (t >= 0) fprintf(f, "    if (npc == 0x%06X) goto L_%06X;\n", (u32)succ[i] & 0xFFFFFF, (u32)t);
    }
    fprintf(f, "    LEAVE(npc);\n");
}

static void emit_function(FILE *f, size_t idx)
{
    const Func *fn = &funcs[idx];
    fprintf(f, "/* ROM $%06X-$%06X (%u instrucciones) */\n", fn->lo, fn->hi, fn->n);
    fprintf(f, "int f_%06X(Gen *g)\n{\n    M68k *c = &g->cpu;\n    u32 npc;\n", fn->lo);
    if (can_remap) fprintf(f, "    u32 ver0 = g->cart.map_ver;\n");
    fprintf(f, "    (void)c;\n    switch (c->pc) {\n");
    for (u32 o = fn->lo; o <= fn->hi; o += 2)
        if (is_ins[o >> 1] && fn_of[o >> 1] == idx + 1)
            fprintf(f, "    case 0x%06X: goto L_%06X;\n", ins_addr[o >> 1], o);
    fprintf(f, "    default: return 0;\n    }\n");
    for (u32 o = fn->lo; o <= fn->hi; o += 2)
        if (is_ins[o >> 1] && fn_of[o >> 1] == idx + 1) emit_ins(f, o);
    fprintf(f, "}\n\n");
}

#define INS_PER_FILE 3000

static void emit_code(const char *dir)
{
    FILE *h = open_out(dir, "gen/gen.h");
    fprintf(h, "/* Generado por genrecomp: no editar. */\n#ifndef GEN_GEN_H\n#define GEN_GEN_H\n#include \"m68k_ops.h\"\n#include \"recomp.h\"\n\n");
    for (size_t i = 0; i < nfuncs; i++) fprintf(h, "int f_%06X(Gen *g);\n", funcs[i].lo);
    fprintf(h, "\n#endif\n");
    fclose(h);

    FILE *f = NULL;
    unsigned in_file = 0, part = 0;
    for (size_t i = 0; i < nfuncs; i++) {
        if (!f || in_file >= INS_PER_FILE) {
            if (f) fclose(f);
            char name[64];
            snprintf(name, sizeof name, "gen/code_%03u.c", part++);
            f = open_out(dir, name);
            fprintf(f, "/* Generado por genrecomp: no editar. */\n#include \"gen.h\"\n\n"
                       "static const u16 noext[1];     /* instrucciones sin palabras de extensión */\n\n");
            in_file = 0;
        }
        emit_function(f, i);
        in_file += funcs[i].n;
    }
    if (f) fclose(f);
}

static void emit_dispatch(const char *dir)
{
    FILE *f = open_out(dir, "gen/dispatch.c");
    size_t nf = nfuncs ? nfuncs : 1;
    fprintf(f, "/* Generado por genrecomp: no editar. */\n#include \"gen.h\"\n\n#define NFUNCS %zu\n\n", nfuncs);
    fprintf(f, "static const u32 f_lo[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%X,", i % 8 ? "" : "\n    ", funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const u32 f_hi[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%X,", i % 8 ? "" : "\n    ", funcs[i].hi);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const RecompFn f_fn[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%sf_%06X,", i % 4 ? "" : "\n    ", funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    size_t pages = (rom_size + 1023) >> 10;
    fprintf(f, "\n};\n\n/* Por KB de ROM: la primera función cuya última instrucción está en él o después */\n");
    fprintf(f, "static const u32 page_first[%zu] = {", pages);
    size_t fi = 0;
    for (size_t p = 0; p < pages; p++) {
        while (fi < nfuncs && funcs[fi].hi < p * 1024) fi++;
        fprintf(f, "%s%zu,", p % 16 ? "" : "\n    ", fi);
    }
    fprintf(f, "\n};\n\n"
               "RecompFn recomp_lookup(Gen *g, u32 pc)\n{\n"
               "    long off = gen_code_offset(g, pc);\n"
               "    if (off < 0) return 0;\n"
               "    u32 i = page_first[off >> 10];\n"
               "    while (i < NFUNCS && f_hi[i] < (u32)off) i++;\n"
               "    return (i < NFUNCS && f_lo[i] <= (u32)off) ? f_fn[i] : 0;\n}\n");
    fclose(f);
}

static void emit_info(const char *dir, const char *sha1, const char *name)
{
    FILE *f = open_out(dir, "gen/info.c");
    fprintf(f, "/* Generado por genrecomp: no editar. */\n#include \"recomp.h\"\n\n");
    fprintf(f, "const char recomp_rom_sha1[41] = \"%s\";\n", sha1);
    fprintf(f, "const char recomp_name[] = \"%s\";\n", name);
    fprintf(f, "const unsigned recomp_instructions = %u;\n", stat_insns);
    fclose(f);
}

static void write_embedded(const char *dir)
{
    for (const EmbeddedFile *e = embedded_files; e->name; e++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->name);
        char *slash = strrchr(path, '/');
        if (slash) { *slash = 0; make_dir(path); *slash = '/'; }
        FILE *f = fopen(path, "wb");
        if (!f) die("no se pudo crear %s", path);
        fwrite(e->data, 1, e->size, f);
        fclose(f);
    }
}

static void emit_makefile(const char *dir, const char *name)
{
    FILE *f = open_out(dir, "Makefile");
    fprintf(f,
        "# Generado por genrecomp. Compila con cualquier gcc/clang C99 con SDL2 (make -j para usar\n"
        "# todos los núcleos). Windows: MSYS2 UCRT64.\n\n"
        "CC     ?= cc\n"
        "# MSYS2 sin \"cc\" (por ejemplo MINGW64): se usa gcc\n"
        "ifeq ($(origin CC),default)\n"
        "CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)\n"
        "endif\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen -Iplatform -DGEN_RECOMP\n\n"
        "# Windows (MSYS2): .exe, carpeta temporal en build/ (el make de MSYS2 no le pasa\n"
        "# TMP a gcc), SDL2 enlazado de forma estática (el .exe no pide SDL2.dll) e icono\n"
        "ifeq ($(OS),Windows_NT)\n"
        "EXE := .exe\n"
        "ifneq ($(shell command -v cygpath 2>/dev/null),)\n"
        "TMPDIR_WIN := $(shell mkdir -p build/tmp && cygpath -m $(CURDIR)/build/tmp)\n"
        "export TMP  := $(TMPDIR_WIN)\n"
        "export TEMP := $(TMPDIR_WIN)\n"
        "endif\n"
        "SDL_LIBS := -static $(shell pkg-config --libs --static sdl2) -lcomdlg32\n"
        "else\n"
        "SDL_LIBS := $(shell pkg-config --libs sdl2)\n"
        "endif\n"
        "SDL_CFLAGS := $(shell pkg-config --cflags sdl2)\n\n"
        "RT_OBJ  := $(patsubst runtime/%%.c,build/rt_%%.o,$(wildcard runtime/*.c))\n"
        "GEN_OBJ := $(patsubst gen/%%.c,build/gen_%%.o,$(wildcard gen/*.c))\n\n"
        ".PHONY: all clean dist\n"
        "all: %s$(EXE) difftest$(EXE) bench$(EXE)\n\n"
        "build:\n\tmkdir -p build\n\n"
        "build/rt_%%.o: runtime/%%.c $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "build/gen_%%.o: gen/%%.c $(wildcard runtime/*.h) gen/gen.h | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "# Plataforma: ventana, menús, controles y audio (todo menos difftest y bench)\n"
        "PLAT_OBJ := $(patsubst platform/%%.c,build/pl_%%.o,$(filter-out platform/difftest.c platform/bench.c,$(wildcard platform/*.c)))\n\n"
        "build/pl_%%.o: platform/%%.c $(wildcard platform/*.h) $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) $(SDL_CFLAGS) -c -o $@ $<\n\n"
        "# Windows: el icono del .exe sale de un recurso (windres)\n"
        "ifeq ($(OS),Windows_NT)\n"
        "WINRES := build/icono_res.o\n"
        "$(WINRES): platform/icono.rc platform/icono.ico | build\n"
        "\twindres -I platform -i $< -o $@ -O coff\n"
        "endif\n\n"
        "%s$(EXE): $(PLAT_OBJ) $(RT_OBJ) $(GEN_OBJ) $(WINRES)\n"
        "\t$(CC) -o $@ $^ $(SDL_LIBS) -lm\n\n"
        "# dist/: el ejecutable (y, si en Windows no quedó estático, las DLL de MSYS2 que pida); se puede copiar a otro equipo\n"
        "dist: %s$(EXE)\n"
        "\tmkdir -p dist\n"
        "\tcp $< dist/\n"
        "ifeq ($(OS),Windows_NT)\n"
        "\tldd $< | awk '/=> \\/(ucrt64|mingw64|mingw32|clang64|clangarm64)\\//{print $$3}' | xargs -r -I{} cp {} dist/\n"
        "endif\n\n"
        "# Prueba diferencial: código recompilado contra el intérprete\n"
        "difftest$(EXE): platform/difftest.c platform/png.c $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm\n\n"
        "bench$(EXE): platform/bench.c $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm\n\n"
        "# Navegador (WebAssembly): make web, con Emscripten o con Docker (ver platform/web.mk)\n"
        "WEB_NAME   := %s\n"
        "WEB_TITLE  := %s\n"
        "WEB_PREF   := /libsdl/GENRecomp/%s/\n"
        "WEB_CACHE  := rom.bin\n"
        "WEB_ACCEPT := .bin,.md,.gen,.smd\n"
        "include platform/web.mk\n\n"
        "clean:\n\trm -rf build dist web %s$(EXE) difftest$(EXE) bench$(EXE)\n",
        name, name, name, name, name, name, name);
    fclose(f);
}

static void emit_readme(const char *dir, const char *name, const char *sha1)
{
    FILE *f = open_out(dir, "LEEME.txt");
    fprintf(f,
        "Proyecto generado por genrecomp.\n\n"
        "Compilar (necesita un compilador C99, make y SDL2):\n"
        "  macOS:   brew install sdl2 pkg-config\n"
        "  Debian:  sudo apt install build-essential pkg-config libsdl2-dev\n"
        "  Windows: MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 make\n"
        "           (el Makefile usa gcc si no hay cc; el .exe lleva el icono)\n"
        "           el .exe enlaza SDL2 de forma estática (no pide DLLs); make dist lo deja en dist/\n\n"
        "  make -j\n"
        "  ./%s juego.bin\n\n"
        "El proyecto no contiene la ROM. El ejecutable la pide y comprueba que su SHA1 sea\n"
        "%s.\n"
        "Si se ejecuta sin argumentos busca en la carpeta actual una ROM con ese SHA1.\n\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest juego.bin 600\n"
        "Lo que el intérprete tuvo que ejecutar (para recompilar de nuevo con genrecomp --trace):\n"
        "  ./difftest juego.bin 3600 --seed 1 --uncovered faltante.txt\n\n"
        "En el navegador (WebAssembly; necesita Emscripten o Docker):\n"
        "  make web\n"
        "  python3 -m http.server -d web     y abrir http://localhost:8000/%s.html\n"
        "La página pide la ROM (no va en el build) y la guarda en el navegador con la partida.\n", name, sha1, name);
    fclose(f);
}

/* El código de una corrida anterior (gen/code_*.c) se borra: puede haber menos archivos ahora */
static void remove_old_code(const char *gdir)
{
    for (unsigned part = 0; part < 100000; part++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/code_%03u.c", gdir, part);
        if (remove(p) != 0) break;
    }
}

/* Nombre del ejecutable: letras, números, - y _ */
static void sanitize(char *s)
{
    for (char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') *p = '_';
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *out = NULL, *name_arg = NULL, *coverage = NULL, *dis_arg = NULL;
    const char *events_path = NULL, *html_path = NULL, *codemap_path = NULL;
    const char *traces[64];
    u32 why_list[32];
    int ntraces = 0, nwhy = 0, info = 0, viz = 0, force_color = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("genrecomp %s\n", GENRECOMP_VERSION); return 0; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "--info")) { info = 1; continue; }
        if (!strcmp(a, "--verbose")) { verbose = 1; continue; }
        if (!strcmp(a, "--viz")) { viz = 1; continue; }
        if (!strcmp(a, "--color")) { force_color = 1; continue; }
        if (!strcmp(a, "--no-tables")) { use_tables = 0; continue; }
        if (!strcmp(a, "--no-spec")) { use_spec = 0; continue; }
        if (i + 1 < argc) {
            if (!strcmp(a, "-o")) { out = argv[++i]; continue; }
            if (!strcmp(a, "--name")) { name_arg = argv[++i]; continue; }
            if (!strcmp(a, "--coverage")) { coverage = argv[++i]; continue; }
            if (!strcmp(a, "--dis")) { dis_arg = argv[++i]; continue; }
            if (!strcmp(a, "--events")) { events_path = argv[++i]; continue; }
            if (!strcmp(a, "--html")) { html_path = argv[++i]; continue; }
            if (!strcmp(a, "--codemap")) { codemap_path = argv[++i]; continue; }
            if (!strcmp(a, "--why")) {
                if (nwhy == 32) die("demasiados --why");
                why_list[nwhy++] = why_arg(argv[++i]);
                continue;
            }
            if (!strcmp(a, "--trace")) {
                if (ntraces == 64) die("demasiadas trazas");
                traces[ntraces++] = argv[++i];
                continue;
            }
        }
        if (a[0] == '-') { fprintf(stderr, "opción desconocida o sin valor: %s\n\n%s", a, usage_text); return 2; }
        if (rom_path) { fprintf(stderr, "argumento de más: %s\n", a); return 2; }
        rom_path = a;
    }
    if (!rom_path) { fputs(usage_text, stderr); return 2; }
    int analysis = coverage || dis_arg || nwhy || viz || events_path || html_path || codemap_path;

    size_t len;
    u8 *data = read_file(rom_path, &len);
    if (!data) { fprintf(stderr, "no se pudo leer %s\n", rom_path); return 1; }
    gen = malloc(sizeof *gen);
    const char *err = NULL;
    if (!gen || gen_load(gen, data, len, &err)) {
        fprintf(stderr, "%s: %s\n", rom_path, err ? err : "sin memoria");
        return 1;
    }
    free(data);
    if (info || (!out && !analysis)) {
        print_info(rom_path, gen);
        return 0;
    }

    rom = gen->cart.rom;
    rom_size = gen->cart.rom_size;
    const Cart *cart = &gen->cart;
    can_remap = cart->mapper != MAPPER_NONE || (cart->sram_data && (cart->sram_start & ~1u) < rom_size);
    size_t words = rom_size / 2 + 1;
    is_ins = calloc(words, 1);
    ins_addr = calloc(words, sizeof *ins_addr);
    parent = malloc(words * sizeof *parent);
    how_at = calloc(words, 1);
    origin = calloc(words, 1);
    if (!is_ins || !ins_addr || !parent || !how_at || !origin) die("sin memoria");
    for (size_t i = 0; i < words; i++) parent[i] = NO_PARENT;
    printf("%s: ROM %zu KB%s, SHA1 %s\n", rom_path, rom_size / 1024, cart->mapper != MAPPER_NONE ? " (mapper de SSF2)" : "",
           cart->sha1);

    /* Primero sin traza (lo que mide --coverage) y después con las trazas */
    seed_vectors();
    discover();
    if (coverage) coverage_report(coverage);
    unsigned base_insns = stat_insns;
    for (int i = 0; i < ntraces; i++) load_trace(traces[i]);
    discover();
    printf("código: %u instrucciones (%u bytes, %.2f%% de la ROM)", stat_insns, stat_bytes, 100.0 * stat_bytes / (double)rom_size);
    if (ntraces) printf(", %u por la traza (%u puntos de entrada)", stat_insns - base_insns, stat_trace);
    printf("\n");
    if (verbose) {
        unsigned overlap = 0;
        for (size_t i = 0; i < rom_size; i += 2)
            if (is_ins[i >> 1])
                for (int k = 2; k < ins_len(i); k += 2)
                    if (i + (size_t)k < rom_size && is_ins[(i + (size_t)k) >> 1]) { overlap++; break; }
        printf("instrucciones que pisan a otra: %u\n", overlap);
    }
    if (verbose)
        printf("saltos indirectos: %u, destinos fuera de la ROM: %u; tablas: %u (%u entradas), tablas sueltas: %u (%u entradas), "
               "punteros inmediatos: %u, tras un final: %u\n", stat_ind, stat_unresolved, stat_tables, stat_table_entries, stat_runs,
               stat_run_entries, stat_imm, stat_after);

    if (events_path) write_events(events_path);
    if (codemap_path) write_codemap(codemap_path);
    if (html_path) {
        const char *b = rom_path;
        for (const char *p = rom_path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
        write_html(html_path, b);
        printf("visor escrito en %s\n", html_path);
    }
    if (viz) viz_map(force_color || isatty(1));
    for (int i = 0; i < nwhy; i++) explain(why_list[i]);
    if (dis_arg) dis_range(dis_arg);
    if (!out) return 0;

    partition();

    char name[128];
    if (name_arg) snprintf(name, sizeof name, "%s", name_arg);
    else {
        const char *b = rom_path;
        for (const char *p = rom_path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
        snprintf(name, sizeof name, "%s", b);
        char *dot = strrchr(name, '.');
        if (dot && dot != name) *dot = 0;
    }
    sanitize(name);
    if (!name[0]) snprintf(name, sizeof name, "juego");

    make_dir(out);
    char gdir[1100];
    snprintf(gdir, sizeof gdir, "%s/gen", out);
    make_dir(gdir);
    remove_old_code(gdir);
    write_embedded(out);
    emit_code(out);
    emit_dispatch(out);
    emit_info(out, cart->sha1, name);
    emit_makefile(out, name);
    emit_readme(out, name, cart->sha1);
    printf("%zu funciones en %s/gen; el ejecutable se llamará %s\n", nfuncs, out, name);
    printf("compilar: make -C %s -j\n", out);
    gen_free(gen);
    free(gen);
    return 0;
}
