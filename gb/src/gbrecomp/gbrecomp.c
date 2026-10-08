/*
 * gbrecomp: recompilador estático de ROMs de Game Boy (DMG) a C99.
 *
 * Uso:
 *   gbrecomp juego.gb -o salida/ [--trace archivo] [--name nombre]
 *
 * Genera en `salida/` un proyecto completo (código recompilado, runtime,
 * capa de plataforma y Makefile). El proyecto no contiene datos de la ROM:
 * el ejecutable carga la ROM del usuario y valida su SHA1.
 *
 * Pasos:
 *   1. Descubrimiento recursivo desde $0100, los vectores de interrupción y
 *      los RST, siguiendo saltos y llamadas con destino constante. Las
 *      direcciones de una traza de ejecución (--trace) se agregan como
 *      puntos de entrada extra, para resolver saltos indirectos.
 *   2. Partición de las instrucciones de cada banco en funciones (tramos
 *      contiguos de direcciones).
 *   3. Emisión de C con el mismo emisor que genera el intérprete.
 *
 * Descubrir de más no rompe nada: el código que nunca se ejecuta solo ocupa
 * espacio. Descubrir de menos tampoco: lo que falte lo ejecuta el intérprete.
 */
#include <ctype.h>
#include <errno.h>
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

#include "../emit/sm83_emit.h"
#include "../runtime/sha1.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define BANK_SIZE 0x4000
#define MAX_FUNC_INSNS 1500    /* tope de instrucciones por función */
#define MAX_GAP 32             /* bytes sin código que cortan una función */
#define MAX_FILE_INSNS 6000    /* tope de instrucciones por archivo .c */

/* Banderas por byte de cada banco */
#define F_INSN   0x01   /* empieza una instrucción */
#define F_BODY   0x02   /* pertenece a alguna instrucción */
#define F_TARGET 0x04   /* destino de un salto o llamada */
#define F_ENTRY  0x08   /* punto de entrada (vector, traza) */

static u8 *rom;
static u32 rom_size, nbanks;
static u8 **flags;

typedef struct { u32 bank; u16 addr; } Work;
static Work *work;
static size_t nwork, capwork;

static unsigned stat_unresolved;   /* saltos a $4000-$7FFF desde el banco 0 sin banco conocido */
static unsigned stat_ram_targets;  /* saltos a RAM: siempre al intérprete */

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "gbrecomp: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static u16 region_base(u32 bank) { return bank == 0 ? 0x0000 : 0x4000; }

static const u8 *bank_ptr(u32 bank) { return rom + (size_t)bank * BANK_SIZE; }

/* Byte en la dirección `addr` vista desde el banco `bank` */
static int in_region(u32 bank, u32 addr)
{
    u16 base = region_base(bank);
    return addr >= base && addr < (u32)base + BANK_SIZE;
}

static u8 *flag_at(u32 bank, u16 addr) { return &flags[bank][addr - region_base(bank)]; }

static void push_work(u32 bank, u16 addr)
{
    if (!in_region(bank, addr)) return;
    if (*flag_at(bank, addr) & F_INSN) return;
    if (nwork == capwork) {
        capwork = capwork ? capwork * 2 : 1024;
        work = realloc(work, capwork * sizeof *work);
        if (!work) die("sin memoria");
    }
    work[nwork].bank = bank;
    work[nwork].addr = addr;
    nwork++;
}

/* Banco de un destino constante saltando desde `from` (-1 si no se sabe) */
static long resolve_bank(u32 from, u16 target)
{
    if (target < 0x4000) return 0;
    if (target < 0x8000) {
        if (from != 0) return (long)from;
        if (nbanks == 2) return 1;
        stat_unresolved++;
        return -1;
    }
    stat_ram_targets++;
    return -1;
}

/* Lee la instrucción en `addr` del banco; 0 si se sale del banco */
static int fetch_insn(u32 bank, u16 addr, u8 bytes[3], Insn *in)
{
    u32 off = addr - region_base(bank);
    const u8 *p = bank_ptr(bank);
    bytes[0] = p[off];
    bytes[1] = off + 1 < BANK_SIZE ? p[off + 1] : 0;
    bytes[2] = off + 2 < BANK_SIZE ? p[off + 2] : 0;
    sm83_decode(bytes, addr, in);
    return off + in->len <= BANK_SIZE;
}

static void explore(u32 bank, u16 addr)
{
    for (;;) {
        if (!in_region(bank, addr)) return;
        if (*flag_at(bank, addr) & F_INSN) return;

        u8 b[3];
        Insn in;
        if (!fetch_insn(bank, addr, b, &in)) return;   /* cruza el fin del banco */

        *flag_at(bank, addr) |= F_INSN;
        for (int i = 0; i < in.len; i++) *flag_at(bank, (u16)(addr + i)) |= F_BODY;

        switch (in.kind) {
        case INS_JP: case INS_JR: case INS_CALL: case INS_RST: {
            long tb = resolve_bank(bank, in.target);
            if (tb >= 0) {
                *flag_at((u32)tb, in.target) |= F_TARGET;
                push_work((u32)tb, in.target);
            }
            break;
        }
        default:
            break;
        }

        if (sm83_ends_block(&in)) return;
        addr = (u16)(addr + in.len);
    }
}

static void discover(void)
{
    static const u16 vectors[] = {
        0x0100, 0x0040, 0x0048, 0x0050, 0x0058, 0x0060,
    };
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        *flag_at(0, vectors[i]) |= F_ENTRY;
        push_work(0, vectors[i]);
    }
    while (nwork) {
        Work w = work[--nwork];
        explore(w.bank, w.addr);
    }
}

/* Traza: líneas "BB:AAAA" (banco y dirección en hexadecimal) */
static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    unsigned bank, addr, n = 0;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%x:%x", &bank, &addr) != 2) continue;
        if (bank >= nbanks || addr > 0x7FFF) continue;
        if (!in_region(bank, addr)) continue;
        *flag_at(bank, (u16)addr) |= F_ENTRY;
        push_work(bank, (u16)addr);
        n++;
    }
    fclose(f);
    printf("traza: %u puntos de entrada\n", n);
}

/* ------------------------------------------------------------------ */
/* Partición en funciones                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    u32 bank;
    u16 start, end;     /* [start, end) cubre todas sus instrucciones */
    unsigned ninsns;
} Func;

static Func *funcs;
static size_t nfuncs, capfuncs;

static void add_func(u32 bank, u16 start, u16 end, unsigned n)
{
    if (nfuncs == capfuncs) {
        capfuncs = capfuncs ? capfuncs * 2 : 256;
        funcs = realloc(funcs, capfuncs * sizeof *funcs);
        if (!funcs) die("sin memoria");
    }
    funcs[nfuncs].bank = bank;
    funcs[nfuncs].start = start;
    funcs[nfuncs].end = end;
    funcs[nfuncs].ninsns = n;
    nfuncs++;
}

static void partition(void)
{
    for (u32 bank = 0; bank < nbanks; bank++) {
        u16 base = region_base(bank);
        int open = 0;
        u32 start = 0, end = 0;
        unsigned n = 0;
        for (u32 off = 0; off < BANK_SIZE; off++) {
            if (!(flags[bank][off] & F_INSN)) continue;
            u32 a = base + off;
            u8 b[3];
            Insn in;
            fetch_insn(bank, (u16)a, b, &in);
            if (open && (a > end + MAX_GAP || n >= MAX_FUNC_INSNS)) {
                add_func(bank, (u16)start, (u16)end, n);
                open = 0;
            }
            if (!open) {
                open = 1;
                start = a;
                end = a;
                n = 0;
            }
            n++;
            if (a + in.len > end) end = a + in.len;
        }
        if (open) add_func(bank, (u16)start, (u16)(end > 0xFFFF ? 0xFFFF : end), n);
    }
}

/* ------------------------------------------------------------------ */
/* Emisión                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const Func *fn;
} FnCtx;

static int insn_in_func(const Func *fn, u32 target)
{
    if (target < fn->start || target >= fn->end) return 0;
    return (*flag_at(fn->bank, (u16)target) & F_INSN) != 0;
}

static void rc_jump(Emit *e, const char *expr, int is_const, uint16_t target)
{
    FnCtx *x = e->user;
    if (is_const && insn_in_func(x->fn, target))
        emit_line(e, "goto L_%04X;", target);
    else
        emit_line(e, "c->pc = %s; return;", expr);
}

/* La instrucción puede escribir en $0000-$7FFF (registros del MBC) */
static int may_write_mbc(const u8 *b)
{
    u8 op = b[0];
    switch (op) {
    case 0x02: case 0x12: case 0x22: case 0x32:
    case 0x34: case 0x35: case 0x36:
        return 1;
    case 0xEA:
        return (b[1] | b[2] << 8) < 0x8000;
    case 0x08:
        return (b[1] | b[2] << 8) < 0x8000;
    case 0xCB:
        return (b[1] & 7) == 6 && (b[1] >> 6) != 1;
    default:
        return op >= 0x70 && op <= 0x77 && op != 0x76;
    }
}

static FILE *open_out(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s: %s", path, strerror(errno));
    return f;
}

static void emit_function(FILE *f, const Func *fn)
{
    FnCtx x = { fn };
    Emit e = { 0 };
    e.f = f;
    e.recomp = 1;
    e.jump = rc_jump;
    e.user = &x;

    fprintf(f, "/* Banco %02X, $%04X-$%04X (%u instrucciones) */\n",
            fn->bank, fn->start, fn->end - 1, fn->ninsns);
    fprintf(f, "void f%02X_%04X(GB *g)\n{\n    Cpu *c = &g->cpu;\n    switch (c->pc) {\n",
            fn->bank, fn->start);
    for (u32 a = fn->start; a < fn->end; a++)
        if (*flag_at(fn->bank, (u16)a) & F_INSN)
            fprintf(f, "    case 0x%04X: goto L_%04X;\n", a, a);
    fprintf(f, "    default: gb_interp_step(g); return;\n    }\n");

    /* Dirección de la siguiente instrucción emitida, para saber si la caída
     * natural al siguiente bloque de código es correcta */
    for (u32 a = fn->start; a < fn->end; a++) {
        if (!(*flag_at(fn->bank, (u16)a) & F_INSN)) continue;
        Insn in;
        char text[48];
        fetch_insn(fn->bank, (u16)a, e.b, &in);
        sm83_disasm(e.b, (u16)a, text, sizeof text);
        e.addr = (u16)a;

        fprintf(f, "L_%04X: /* %s */\n", a, text);
        fprintf(f, "    CHECK(0x%04X);\n", a);
        fprintf(f, "    {\n");
        e.indent = "        ";
        emit_insn(&e, e.b[0]);
        fprintf(f, "    }\n");

        if (in.kind == INS_HALT || in.kind == INS_STOP || sm83_ends_block(&in))
            continue;

        u32 next = a + in.len;
        if (fn->bank != 0 && may_write_mbc(e.b))
            fprintf(f, "    if (UNLIKELY(g->mbc.bankx != %u)) { c->pc = 0x%04X; return; }\n",
                    fn->bank, next);

        /* ¿La siguiente instrucción emitida es justo `next`? */
        u32 following = a + 1;
        while (following < fn->end && !(*flag_at(fn->bank, (u16)following) & F_INSN)) following++;
        if (insn_in_func(fn, next)) {
            if (following != next) fprintf(f, "    goto L_%04X;\n", next);
        } else {
            fprintf(f, "    c->pc = 0x%04X; return;\n", next);
        }
    }
    fprintf(f, "}\n\n");
}

static void emit_code(const char *dir)
{
    FILE *f = NULL;
    unsigned in_file = 0, file_no = 0;
    char name[64];

    for (size_t i = 0; i < nfuncs; i++) {
        const Func *fn = &funcs[i];
        if (!f || in_file >= MAX_FILE_INSNS || (i && funcs[i - 1].bank != fn->bank)) {
            if (f) fclose(f);
            if (i && funcs[i - 1].bank != fn->bank) file_no = 0;
            snprintf(name, sizeof name, "gen/bank%02X_%u.c", fn->bank, file_no++);
            f = open_out(dir, name);
            fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n\n");
            in_file = 0;
        }
        emit_function(f, fn);
        in_file += fn->ninsns;
    }
    if (f) fclose(f);
}

/* Tablas del despachador: banco → página de 256 bytes → función por dirección */
static void emit_dispatch(const char *dir)
{
    FILE *f = open_out(dir, "gen/dispatch.c");
    fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n\n");

    for (size_t i = 0; i < nfuncs; i++)
        fprintf(f, "void f%02X_%04X(GB *g);\n", funcs[i].bank, funcs[i].start);
    fprintf(f, "\n");

    size_t fi = 0;
    for (u32 bank = 0; bank < nbanks; bank++) {
        int has_page[64] = { 0 };
        u16 base = region_base(bank);
        for (int page = 0; page < 64; page++) {
            int any = 0;
            for (int k = 0; k < 256; k++)
                if (flags[bank][page * 256 + k] & F_INSN) { any = 1; break; }
            if (!any) continue;
            has_page[page] = 1;
            fprintf(f, "static const RecompFn b%02X_p%02X[256] = {\n", bank, page);
            for (int k = 0; k < 256; k++) {
                u32 a = base + page * 256 + k;
                if (!(flags[bank][page * 256 + k] & F_INSN)) continue;
                while (fi < nfuncs && (funcs[fi].bank < bank ||
                       (funcs[fi].bank == bank && funcs[fi].end <= a))) fi++;
                fprintf(f, "    [0x%02X] = f%02X_%04X,\n", k, bank, funcs[fi].start);
            }
            fprintf(f, "};\n");
        }
        int any_bank = 0;
        for (int page = 0; page < 64; page++) any_bank |= has_page[page];
        if (!any_bank) continue;
        fprintf(f, "static const RecompFn *const bank%02X[64] = {\n", bank);
        for (int page = 0; page < 64; page++)
            if (has_page[page]) fprintf(f, "    [0x%02X] = b%02X_p%02X,\n", page, bank, page);
        fprintf(f, "};\n\n");
    }

    fprintf(f, "static const RecompFn *const *const banks[%u] = {\n", nbanks);
    for (u32 bank = 0; bank < nbanks; bank++) {
        int any = 0;
        for (u32 off = 0; off < BANK_SIZE && !any; off++) any = flags[bank][off] & F_INSN;
        if (any) fprintf(f, "    [%u] = bank%02X,\n", bank, bank);
    }
    fprintf(f, "};\n\n");

    fprintf(f,
        "RecompFn recomp_lookup(GB *g, u16 pc)\n"
        "{\n"
        "    const RecompFn *const *table;\n"
        "    u32 off;\n"
        "    /* El código de cada banco se compiló para su región: el banco 0 en\n"
        "     * $0000-$3FFF y los demás en $4000-$7FFF */\n"
        "    if (pc < 0x4000) {\n"
        "        if (g->mbc.bank0 != 0) return NULL;\n"
        "        table = banks[0];\n"
        "        off = pc;\n"
        "    } else if (pc < 0x8000) {\n"
        "        u32 b = g->mbc.bankx;\n"
        "        if (b == 0 || b >= %u) return NULL;\n"
        "        table = banks[b];\n"
        "        off = pc - 0x4000u;\n"
        "    } else {\n"
        "        return NULL;\n"
        "    }\n"
        "    if (!table || !table[off >> 8]) return NULL;\n"
        "    return table[off >> 8][off & 0xFF];\n"
        "}\n", nbanks);
    fclose(f);
}

static void emit_info(const char *dir, const char *sha1, const char *title, unsigned ninsns)
{
    FILE *f = open_out(dir, "gen/recomp_info.c");
    fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n\n");
    fprintf(f, "const char recomp_rom_sha1[41] = \"%s\";\n", sha1);
    fprintf(f, "const char recomp_rom_title[] = \"%s\";\n", title);
    fprintf(f, "const unsigned recomp_instructions = %u;\n", ninsns);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Proyecto                                                            */
/* ------------------------------------------------------------------ */

/* Crea el directorio y sus padres (como mkdir -p) */
static void make_dir(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char saved = *p;
        *p = 0;
        if (MKDIR(tmp) != 0 && errno != EEXIST) die("no se pudo crear %s: %s", tmp, strerror(errno));
        *p = saved;
    }
    if (MKDIR(tmp) != 0 && errno != EEXIST) die("no se pudo crear %s: %s", tmp, strerror(errno));
}

static void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) die("no se encontró %s (¿falta compilar con make?)", from);
    FILE *out = fopen(to, "wb");
    if (!out) die("no se pudo crear %s", to);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
}

static void copy_list(const char *base, const char *subdir, const char *const *names,
                      const char *dir, const char *dest)
{
    char from[1024], to[1024];
    for (int i = 0; names[i]; i++) {
        snprintf(from, sizeof from, "%s/%s/%s", base, subdir, names[i]);
        snprintf(to, sizeof to, "%s/%s/%s", dir, dest, names[i]);
        copy_file(from, to);
    }
}

static void emit_makefile(const char *dir, const char *name)
{
    FILE *f = open_out(dir, "Makefile");
    fprintf(f,
        "# Generado por gbrecomp. Compila con cualquier gcc/clang C99 con SDL2.\n"
        "# Windows: MSYS2 UCRT64.\n\n"
        "CC     ?= cc\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen\n\n"
        "ifneq ($(shell command -v cygpath 2>/dev/null),)\n"
        "TMPDIR_WIN := $(shell mkdir -p build/tmp && cygpath -m $(CURDIR)/build/tmp)\n"
        "export TMP  := $(TMPDIR_WIN)\n"
        "export TEMP := $(TMPDIR_WIN)\n"
        "EXE := .exe\n"
        "SDL_LIBS := -static $(shell pkg-config --libs --static sdl2)\n"
        "else\n"
        "SDL_LIBS := $(shell pkg-config --libs sdl2)\n"
        "endif\n"
        "SDL_CFLAGS := $(shell pkg-config --cflags sdl2)\n\n"
        "RT_OBJ  := $(patsubst runtime/%%.c,build/rt_%%.o,$(wildcard runtime/*.c))\n"
        "GEN_OBJ := $(patsubst gen/%%.c,build/gen_%%.o,$(wildcard gen/*.c))\n\n"
        ".PHONY: all clean\n"
        "all: %s$(EXE) difftest$(EXE)\n\n"
        "build:\n\tmkdir -p build\n\n"
        "build/rt_%%.o: runtime/%%.c $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "build/gen_%%.o: gen/%%.c runtime/recomp.h runtime/cpu_ops.h runtime/gb.h | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "build/main_sdl.o: platform/main_sdl.c $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) $(SDL_CFLAGS) -c -o $@ $<\n\n"
        "%s$(EXE): build/main_sdl.o $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) -o $@ $^ $(SDL_LIBS)\n\n"
        "# Prueba diferencial: código recompilado contra el intérprete\n"
        "difftest$(EXE): platform/difftest.c $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^\n\n"
        "clean:\n\trm -rf build %s$(EXE) difftest$(EXE)\n",
        name, name, name);
    fclose(f);
}

static void sanitize(char *s)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (isalnum(ch)) *s = (char)tolower(ch);
        else *s = '_';
    }
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *out = NULL, *trace = NULL, *name = NULL;
    char base[1024];

    /* La base (src/ y build/gen/) está junto al ejecutable de gbrecomp */
    snprintf(base, sizeof base, "%s", argv[0]);
    char *slash = strrchr(base, '/');
    char *bslash = strrchr(base, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) *slash = 0;
    else strcpy(base, ".");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--base") && i + 1 < argc) snprintf(base, sizeof base, "%s", argv[++i]);
        else if (argv[i][0] == '-') die("opción desconocida: %s", argv[i]);
        else rom_path = argv[i];
    }
    if (!rom_path || !out) {
        fprintf(stderr, "uso: gbrecomp juego.gb -o salida/ [--trace archivo] [--name nombre]\n");
        return 2;
    }

    /* ROM */
    FILE *f = fopen(rom_path, "rb");
    if (!f) die("no se pudo abrir %s", rom_path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0x150) die("%s es demasiado chica para ser una ROM", rom_path);
    u8 *raw = malloc((size_t)size);
    if (!raw || fread(raw, 1, (size_t)size, f) != (size_t)size) die("no se pudo leer %s", rom_path);
    fclose(f);

    char sha1[41];
    sha1_hex(raw, (size_t)size, sha1);

    nbanks = 2;
    while (nbanks * BANK_SIZE < (u32)size) nbanks *= 2;
    rom_size = nbanks * BANK_SIZE;
    rom = malloc(rom_size);
    memset(rom, 0xFF, rom_size);
    memcpy(rom, raw, (size_t)size);
    free(raw);

    char title[17] = { 0 };
    for (int i = 0; i < 16; i++) {
        u8 ch = rom[0x134 + i];
        if (ch == 0) break;
        title[i] = (ch >= 0x20 && ch < 0x7F && ch != '"' && ch != '\\') ? (char)ch : '?';
    }

    char exe_name[64];
    snprintf(exe_name, sizeof exe_name, "%s", name ? name : (title[0] ? title : "juego"));
    sanitize(exe_name);

    flags = calloc(nbanks, sizeof *flags);
    for (u32 b = 0; b < nbanks; b++) flags[b] = calloc(BANK_SIZE, 1);

    /* 1. Descubrimiento */
    discover();
    if (trace) {
        load_trace(trace);
        discover();
    }

    /* 2. Partición */
    partition();
    unsigned ninsns = 0, nbytes = 0;
    for (size_t i = 0; i < nfuncs; i++) ninsns += funcs[i].ninsns;
    for (u32 b = 0; b < nbanks; b++)
        for (u32 off = 0; off < BANK_SIZE; off++)
            if (flags[b][off] & F_BODY) nbytes++;

    /* 3. Proyecto */
    char path[1024];
    make_dir(out);
    snprintf(path, sizeof path, "%s/gen", out); make_dir(path);
    snprintf(path, sizeof path, "%s/runtime", out); make_dir(path);
    snprintf(path, sizeof path, "%s/platform", out); make_dir(path);

    emit_code(out);
    emit_dispatch(out);
    emit_info(out, sha1, title, ninsns);
    emit_makefile(out, exe_name);

    static const char *const runtime_files[] = {
        "gb.h", "gb.c", "cpu_ops.h", "interp.c", "timer.c", "ppu.c", "apu.c", "mbc.c",
        "sha1.c", "sha1.h", "recomp.h", NULL,
    };
    static const char *const platform_files[] = { "main_sdl.c", "difftest.c", NULL };
    static const char *const generated_files[] = { "interp_ops.inc", NULL };
    copy_list(base, "src/runtime", runtime_files, out, "runtime");
    copy_list(base, "src/platform", platform_files, out, "platform");
    copy_list(base, "build/gen", generated_files, out, "runtime");

    printf("ROM: %s (%s), %u bancos, SHA1 %s\n", rom_path, title, nbanks, sha1);
    printf("código: %u instrucciones (%u bytes, %.1f%% de la ROM) en %zu funciones\n",
           ninsns, nbytes, 100.0 * nbytes / rom_size, nfuncs);
    if (stat_ram_targets)
        printf("saltos a RAM (siempre van al intérprete): %u\n", stat_ram_targets);
    if (stat_unresolved)
        printf("saltos al área de bancos sin banco conocido: %u (los resuelve la traza o el intérprete)\n",
               stat_unresolved);
    printf("proyecto generado en %s (ejecutable: %s)\n", out, exe_name);
    return 0;
}
