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

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "m68k_ops.h"
#include "console.h"
#include "embedded.h"

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
    "  --coverage archivo compara lo descubierto con las instrucciones que ejecutó el juego (una\n"
    "                     traza): cuántas se encontraron sin ayuda\n"
    "  --info             formato, SHA1, cabecera y vectores de la ROM (lo que hace sin -o)\n"
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
/* Descubrimiento                                                      */
/* ------------------------------------------------------------------ */

#define BANK_SIZE 0x80000u          /* 512 KB: ventana del mapper de SSF2 (y de la SRAM en $200000) */

static Gen *gen;                    /* la ROM cargada (normalizada) y su mapeo inicial */
static const u8 *rom;
static size_t rom_size;
static int verbose;

/* Por palabra de ROM: ¿empieza una instrucción ahí? y con qué dirección del CPU se descubrió */
static u8  *is_ins;
static u32 *ins_addr;
static unsigned stat_insns, stat_seeds, stat_trace;

typedef struct { u32 off, addr; } Work;
static Work *work;
static size_t nwork, capwork;

static u16 romw(size_t off) { return off + 1 < rom_size ? (u16)(rom[off] << 8 | rom[off + 1]) : 0xFFFF; }

static void push_work(u32 off, u32 addr)
{
    if (nwork == capwork) {
        capwork = capwork ? capwork * 2 : 1024;
        work = realloc(work, capwork * sizeof *work);
        if (!work) die("sin memoria");
    }
    work[nwork++] = (Work){ off, addr };
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
    if (t >= 0) push_work((u32)t, target & 0xFFFFFF);
    else if (verbose) printf("  $%06X: destino $%06X fuera de la ROM\n", addr, target & 0xFFFFFF);
}

static int ins_len(size_t off) { return 2 + 2 * m68k_ext_words(romw(off)); }

/* Destino constante de JMP/JSR (absoluto corto o largo, o relativo al PC); -1 si es indirecto */
static long jump_target(size_t off, u32 addr, u16 op)
{
    int mode = op >> 3 & 7, reg = op & 7;
    if (mode != 7) return -1;
    if (reg == 0) return (long)(sx16(romw(off + 2)) & 0xFFFFFF);
    if (reg == 1) return (long)(((u32)romw(off + 2) << 16 | romw(off + 4)) & 0xFFFFFF);
    if (reg == 2) return (long)((addr + 2 + sx16(romw(off + 2))) & 0xFFFFFF);
    return -1;
}

/* Sigue una cadena de instrucciones desde (off, addr) hasta un salto sin vuelta */
static void walk(Work w)
{
    u32 off = w.off, addr = w.addr;
    for (;;) {
        if ((off & 1) || off + 2 > rom_size || (addr & 1)) return;
        if (is_ins[off >> 1]) return;
        u16 op = romw(off);
        int kind = m68k_decode(op);
        int len = ins_len(off);
        if (off + (u32)len > rom_size) return;
        is_ins[off >> 1] = 1;
        ins_addr[off >> 1] = addr;
        stat_insns++;
        u32 next = addr + (u32)len;
        switch (kind) {
        case K_BCC: {
            u32 d8 = op & 0xFF;
            u32 to = addr + 2 + (d8 ? sx8(d8) : sx16(romw(off + 2)));
            int cc = op >> 8 & 15;
            follow(off, addr, to);
            if (cc == 0) return;                    /* BRA */
            break; }
        case K_DBCC:
            follow(off, addr, addr + 2 + sx16(romw(off + 2)));
            break;
        case K_JMP: case K_JSR: {
            long t = jump_target(off, addr, op);
            if (t >= 0) follow(off, addr, (u32)t);
            if (kind == K_JMP) return;
            break; }
        case K_RTS: case K_RTE: case K_RTR: case K_ILLEGAL: case K_LINEA: case K_LINEF:
            return;
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
    while (nwork) walk(work[--nwork]);
}

static void seed(u32 off, u32 addr)
{
    push_work(off, addr);
    drain();
}

static void seed_vectors(void)
{
    for (int v = 1; v < VEC_COUNT; v++) {
        u32 a = cart_vector(&gen->cart, v) & 0xFFFFFF;
        /* los vectores sin usar suelen valer 0: debajo de $200 están los vectores y la cabecera */
        long off = (a & 1) || a < 0x200 ? -1 : gen_code_offset(gen, a);
        if (off < 0) continue;
        stat_seeds++;
        seed((u32)off, a);
    }
}

/* Traza "OFFSET:DIRECCION" (hexadecimal) */
static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    char line[128];
    unsigned long off, addr;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%lx:%lx", &off, &addr) == 2 && off < rom_size && !(off & 1)) {
            if (!is_ins[off >> 1]) stat_trace++;
            seed((u32)off, (u32)addr & 0xFFFFFF);
        }
    fclose(f);
}

/* --coverage: de las instrucciones de una traza, cuántas se descubrieron */
static void coverage_report(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir %s", path);
    char line[128];
    unsigned long off, addr, total = 0, found = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%lx:%lx", &off, &addr) == 2 && off < rom_size) {
            total++;
            if (!(off & 1) && is_ins[off >> 1]) found++;
            else if (verbose) printf("  falta $%06lX (dirección $%06lX)\n", off, addr);
        }
    fclose(f);
    printf("cobertura: %lu de %lu instrucciones ejecutadas se descubrieron (%.1f%%)\n",
           found, total, total ? 100.0 * found / total : 100.0);
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

    fprintf(f, "L_%06X: /* $%06X: %04X", off, addr, op);
    for (int i = 0; i < n; i++) fprintf(f, " %04X", romw(off + 2 + 2 * (u32)i));
    fprintf(f, " */\n");
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
        "clean:\n\trm -rf build dist %s$(EXE) difftest$(EXE) bench$(EXE)\n",
        name, name, name, name);
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
        "  ./difftest juego.bin 3600 --seed 1 --uncovered faltante.txt\n", name, sha1);
    fclose(f);
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
    const char *rom_path = NULL, *out = NULL, *name_arg = NULL, *coverage = NULL;
    const char *traces[64];
    int ntraces = 0, info = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("genrecomp %s\n", GENRECOMP_VERSION); return 0; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "--info")) { info = 1; continue; }
        if (!strcmp(a, "--verbose")) { verbose = 1; continue; }
        if (i + 1 < argc) {
            if (!strcmp(a, "-o")) { out = argv[++i]; continue; }
            if (!strcmp(a, "--name")) { name_arg = argv[++i]; continue; }
            if (!strcmp(a, "--coverage")) { coverage = argv[++i]; continue; }
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
    if (info || (!out && !coverage)) {
        print_info(rom_path, gen);
        return 0;
    }

    rom = gen->cart.rom;
    rom_size = gen->cart.rom_size;
    const Cart *cart = &gen->cart;
    can_remap = cart->mapper != MAPPER_NONE || (cart->sram_data && (cart->sram_start & ~1u) < rom_size);
    is_ins = calloc(rom_size / 2 + 1, 1);
    ins_addr = calloc(rom_size / 2 + 1, sizeof *ins_addr);
    if (!is_ins || !ins_addr) die("sin memoria");

    seed_vectors();
    unsigned from_vectors = stat_insns;
    for (int i = 0; i < ntraces; i++) load_trace(traces[i]);
    printf("%s: %zu KB, SHA1 %s\n", rom_path, rom_size / 1024, cart->sha1);
    printf("descubrimiento: %u instrucciones (%u desde %u vectores", stat_insns, from_vectors, stat_seeds);
    if (ntraces) printf(", %u más con %u entradas nuevas de la traza", stat_insns - from_vectors, stat_trace);
    printf(")\n");
    if (coverage) coverage_report(coverage);
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
