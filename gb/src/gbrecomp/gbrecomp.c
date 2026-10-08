/*
 * gbrecomp: recompilador estático de ROMs de Game Boy (DMG, Color y Super
 * Game Boy) a C99.
 *
 * Uso: gbrecomp juego.gb -o salida/ [opciones]   (ver --help)
 *
 *   --codemap escribe qué bytes de la ROM se descubrieron como código (para
 *   compararlo con un desensamblado; ver tools/truth/).
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
#include "../runtime/console.h"
#include "../runtime/sha1.h"
#include "embedded.h"

#ifndef GBRECOMP_VERSION
#define GBRECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "gbrecomp " GBRECOMP_VERSION ": recompila una ROM de Game Boy (DMG, Color o Super Game Boy)\n"
    "a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: gbrecomp juego.gb -o salida/ [opciones]\n"
    "  -o carpeta         dónde escribir el proyecto (se crea si no existe)\n"
    "  --name nombre      nombre del ejecutable y de la carpeta de datos\n"
    "                     (por omisión, el título de la cabecera)\n"
    "  --trace archivo    direcciones ejecutadas que no se descubrieron (las que\n"
    "                     escribe el juego con --uncovered); se agregan como\n"
    "                     puntos de entrada. Repite el ciclo hasta que no queden.\n"
    "  --codemap archivo  qué bytes se tomaron como código (para compararlo con\n"
    "                     un desensamblado)\n"
    "  --dis BB:AAAA:N    desensambla N instrucciones del banco BB en $AAAA\n"
    "  --verbose          detalles del descubrimiento\n"
    "  --version, --help\n"
    "\n"
    "Luego: cd salida && make. Las instrucciones están en salida/LEEME.txt.\n"
    "El proyecto no contiene datos de la ROM: el ejecutable carga la tuya y\n"
    "valida su SHA1.\n";

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
#define F_TABLE  0x10   /* tabla de punteros en línea (datos) */
#define F_REF    0x20   /* el código carga esta dirección como constante */

static u8 *rom;
static u32 rom_size, nbanks;
static u8 **flags;

typedef struct {
    u32 bank;
    u16 addr;
    int bank_ctx;       /* banco activo conocido al llegar (V_UNK = -1 si no se sabe) */
} Work;

static int push_ctx = -1;   /* contexto de banco para los push_work del recorrido actual */
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
    work[nwork].bank_ctx = push_ctx;
    nwork++;
}

/* Banco de una dirección de ROM vista desde el banco `from` (-1 si no se sabe) */
static long bank_of(u32 from, u16 a)
{
    if (a < 0x4000) return 0;
    if (a >= 0x8000) return -1;
    if (from != 0) return (long)from;
    return nbanks == 2 ? 1 : -1;
}

/* Igual, para destinos de salto: lleva la cuenta de los que no se resuelven */
static long resolve_bank(u32 from, u16 target)
{
    long b = bank_of(from, target);
    if (b < 0) {
        if (target < 0x8000) stat_unresolved++;
        else stat_ram_targets++;
    }
    return b;
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

/*
 * ¿La rutina en `addr` es un despachador con tabla en línea, como el
 * TableJump de `rst $28` en muchos juegos? Se reconoce porque, sin
 * ramificarse, saca de la pila su propia dirección de retorno (un pop antes de
 * cualquier push) y termina en `jp hl`. Quien la llama pone la tabla de
 * punteros justo después de la instrucción de llamada.
 */
static int is_table_dispatcher(u32 bank, u16 addr)
{
    int popped = 0;
    for (int i = 0; i < 16; i++) {
        if (!in_region(bank, addr)) return 0;
        u8 b[3];
        Insn in;
        if (!fetch_insn(bank, addr, b, &in)) return 0;
        u8 op = b[0];
        if (op == 0xC1 || op == 0xD1 || op == 0xE1) popped = 1;
        if ((op == 0xC5 || op == 0xD5 || op == 0xE5 || op == 0xF5) && !popped) return 0;
        if (in.kind == INS_JP_HL) return popped;
        if (in.kind != INS_NORMAL) return 0;
        addr = (u16)(addr + in.len);
    }
    return 0;
}

/* Tablas de punteros a código pendientes de leer */
typedef struct {
    u32 bank;
    u16 addr;
    int inline_table;   /* 1: justo después de la llamada a un despachador;
                         * 0: fuera de línea (ld hl, Tabla ... jp hl) */
} Table;

static Table *tables;
static size_t ntables, captables;
static unsigned stat_tables, stat_table_entries;
static int verbose;

static void add_table(u32 bank, u16 addr, int inline_table)
{
    for (size_t i = 0; i < ntables; i++)
        if (tables[i].bank == bank && tables[i].addr == addr) return;
    if (ntables == captables) {
        captables = captables ? captables * 2 : 16;
        tables = realloc(tables, captables * sizeof *tables);
        if (!tables) die("sin memoria");
    }
    tables[ntables].bank = bank;
    tables[ntables].addr = addr;
    tables[ntables].inline_table = inline_table;
    ntables++;
}

/* ------------------------------------------------------------------ */
/* Seguimiento de registros                                            */
/* ------------------------------------------------------------------ */

/*
 * Valores conocidos de los registros durante un recorrido lineal. Un valor es
 * V_UNK (desconocido), una constante de 0 a 255, o V_SYM(r): "lo que traía el
 * registro r al entrar" (solo en el análisis simbólico de trampolines).
 * Índices como en los opcodes: B C D E H L (hl) A.
 */
#define V_UNK      (-1)
#define V_SYM(r)   (0x100 + (r))
#define IS_CONST(v) ((v) >= 0 && (v) < 0x100)
#define R_B 0
#define R_C 1
#define R_D 2
#define R_E 3
#define R_H 4
#define R_L 5
#define R_A 7

typedef struct {
    int r[8];
    int bank;           /* último valor escrito al registro de banco ($2000-$3FFF) */
    int stk_hi[16], stk_lo[16], depth;
    u16 mem_addr[8];    /* celdas de RAM con valor conocido (p. ej. hROMBank) */
    int mem_val[8], nmem;
} Regs;

static void regs_unknown(Regs *s)
{
    for (int i = 0; i < 8; i++) s->r[i] = V_UNK;
    s->bank = V_UNK;
    s->depth = 0;
    s->nmem = 0;
}

static int pair_value(const Regs *s, int hi, int lo)
{
    if (!IS_CONST(s->r[hi]) || !IS_CONST(s->r[lo])) return -1;
    return s->r[hi] << 8 | s->r[lo];
}

static void set_pair(Regs *s, int hi, int lo, long v)
{
    s->r[hi] = v < 0 ? V_UNK : (int)((v >> 8) & 0xFF);
    s->r[lo] = v < 0 ? V_UNK : (int)(v & 0xFF);
}

static void mem_store(Regs *s, long addr, int v)
{
    if (addr < 0) return;
    if (addr >= 0x2000 && addr < 0x4000) { s->bank = v; return; }
    if (addr < 0xC000) return;      /* solo RAM interna */
    for (int i = 0; i < s->nmem; i++)
        if (s->mem_addr[i] == addr) { s->mem_val[i] = v; return; }
    if (s->nmem < 8) {
        s->mem_addr[s->nmem] = (u16)addr;
        s->mem_val[s->nmem++] = v;
    }
}

static int mem_load(const Regs *s, long addr)
{
    for (int i = 0; addr >= 0 && i < s->nmem; i++)
        if (s->mem_addr[i] == addr) return s->mem_val[i];
    return V_UNK;
}

/* Operación de ALU sobre A con operando `v` (`self`: el operando es A mismo) */
static void regs_alu(Regs *s, int op, int v, int self)
{
    int a = s->r[R_A];
    switch (op) {
    case 0: s->r[R_A] = IS_CONST(a) && IS_CONST(v) ? (a + v) & 0xFF : V_UNK; break;
    case 2: s->r[R_A] = IS_CONST(a) && IS_CONST(v) ? (a - v) & 0xFF : (self ? 0 : V_UNK); break;
    case 4: if (!self) s->r[R_A] = IS_CONST(a) && IS_CONST(v) ? a & v : V_UNK; break;
    case 5: s->r[R_A] = self ? 0 : IS_CONST(a) && IS_CONST(v) ? a ^ v : V_UNK; break;
    case 6: if (!self) s->r[R_A] = IS_CONST(a) && IS_CONST(v) ? a | v : V_UNK; break;
    case 7: break;                  /* cp no cambia A */
    default: s->r[R_A] = V_UNK; break;  /* adc, sbc dependen del acarreo */
    }
}

/* Efecto de una instrucción (que no sea de control) sobre los registros */
static void regs_step(Regs *s, const u8 *b)
{
    u8 op = b[0];
    int d = (op >> 3) & 7, r = op & 7;
    u16 nn = (u16)(b[1] | b[2] << 8);
    long hl = pair_value(s, R_H, R_L);

    if (op >= 0x40 && op < 0x80 && op != 0x76) {
        if (d == 6) mem_store(s, hl, s->r[r]);
        else s->r[d] = r == 6 ? mem_load(s, hl) : s->r[r];
        return;
    }
    if (op >= 0x80 && op < 0xC0) {
        regs_alu(s, d, r == 6 ? V_UNK : s->r[r], r == R_A);
        return;
    }
    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
        if (d == 6) mem_store(s, hl, b[1]);
        else s->r[d] = b[1];
        return;
    case 0x01: set_pair(s, R_B, R_C, nn); return;
    case 0x11: set_pair(s, R_D, R_E, nn); return;
    case 0x21: set_pair(s, R_H, R_L, nn); return;
    case 0x03: case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: {
        int hi = (op >> 4) * 2, lo = hi + 1;
        long v = pair_value(s, hi, lo);
        set_pair(s, hi, lo, v < 0 ? -1 : (v + ((op & 8) ? -1 : 1)) & 0xFFFF);
        return;
    }
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C:
        s->r[d] = IS_CONST(s->r[d]) ? (s->r[d] + 1) & 0xFF : V_UNK;
        return;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D:
        s->r[d] = IS_CONST(s->r[d]) ? (s->r[d] - 1) & 0xFF : V_UNK;
        return;
    case 0x09: case 0x19: case 0x29: case 0x39: case 0xF8:
        set_pair(s, R_H, R_L, -1);
        return;
    case 0x02: mem_store(s, pair_value(s, R_B, R_C), s->r[R_A]); return;
    case 0x12: mem_store(s, pair_value(s, R_D, R_E), s->r[R_A]); return;
    case 0x22: case 0x32:
        mem_store(s, hl, s->r[R_A]);
        set_pair(s, R_H, R_L, hl < 0 ? -1 : (hl + (op == 0x22 ? 1 : -1)) & 0xFFFF);
        return;
    case 0x2A: case 0x3A:
        s->r[R_A] = mem_load(s, hl);
        set_pair(s, R_H, R_L, hl < 0 ? -1 : (hl + (op == 0x2A ? 1 : -1)) & 0xFFFF);
        return;
    case 0x0A: s->r[R_A] = mem_load(s, pair_value(s, R_B, R_C)); return;
    case 0x1A: s->r[R_A] = mem_load(s, pair_value(s, R_D, R_E)); return;
    case 0xEA: mem_store(s, nn, s->r[R_A]); return;
    case 0xFA: s->r[R_A] = mem_load(s, nn); return;
    case 0xE0: mem_store(s, 0xFF00 | b[1], s->r[R_A]); return;
    case 0xF0: s->r[R_A] = mem_load(s, 0xFF00 | b[1]); return;
    case 0xF2: s->r[R_A] = V_UNK; return;
    case 0x07: case 0x0F: case 0x17: case 0x1F: case 0x27: case 0x2F:
        s->r[R_A] = V_UNK;
        return;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        regs_alu(s, d, b[1], 0);
        return;
    case 0xC5: case 0xD5: case 0xE5: case 0xF5: {
        int hi = op == 0xF5 ? R_A : ((op >> 4) & 3) * 2;
        if (s->depth < 16) {
            s->stk_hi[s->depth] = s->r[hi];
            s->stk_lo[s->depth] = op == 0xF5 ? V_UNK : s->r[hi + 1];
            s->depth++;
        }
        return;
    }
    case 0xC1: case 0xD1: case 0xE1: case 0xF1: {
        int hi = op == 0xF1 ? R_A : ((op >> 4) & 3) * 2;
        int vh = V_UNK, vl = V_UNK;
        if (s->depth > 0) { s->depth--; vh = s->stk_hi[s->depth]; vl = s->stk_lo[s->depth]; }
        s->r[hi] = vh;
        if (op != 0xF1) s->r[hi + 1] = vl;
        return;
    }
    case 0xCB:
        if ((b[1] & 7) != 6 && (b[1] >> 6) != 1) s->r[b[1] & 7] = V_UNK;
        return;
    case 0xE8: case 0xF9:
        s->depth = 0;   /* se movió la pila: ya no sabemos qué hay */
        return;
    default:
        return;
    }
}

/*
 * Clasificación de rutinas del banco 0 por ejecución simbólica (registros con
 * "lo que traían al entrar", siguiendo llamadas internas, la pila y celdas de
 * RAM):
 *   - RK_TRAMPOLINE: escribe en el registro de banco un valor que le llega en
 *     un registro y salta a HL sin cambiarlo (Bankswitch de Pokémon Rojo,
 *     FarCall de Pokémon Oro).
 *   - RK_BANK_SETTER: escribe en el registro de banco un valor que le llega en
 *     un registro y regresa (el `rst $10` de Pokémon Oro). Después de llamarla,
 *     el banco activo es ese valor.
 *   - RK_TABLE_JUMPER: lee a través del HL con que la llamaron y termina en
 *     `jp hl` (el `rst $28` de Pokémon Oro): HL apunta a una tabla de punteros.
 */
enum { RK_UNKNOWN, RK_NONE, RK_TRAMPOLINE, RK_BANK_SETTER, RK_TABLE_JUMPER };

static signed char rk_cache[0x4000];    /* RK_* (0 = sin analizar) */
static u8 rk_reg[0x4000];               /* registro que trae el banco */
static unsigned stat_tramp_calls, stat_bank_setters, stat_table_jumps;

/* ¿La instrucción lee memoria a través de HL? */
static int reads_via_hl(u8 op)
{
    if (op >= 0x40 && op < 0x80 && op != 0x76 && (op & 7) == 6) return 1;   /* ld r, [hl] */
    if (op >= 0x80 && op < 0xC0 && (op & 7) == 6) return 1;                 /* alu [hl] */
    return op == 0x2A || op == 0x3A;
}

/* ¿La instrucción reemplaza H o L con algo que no viene de HL? */
static int overwrites_hl(u8 op)
{
    switch (op) {
    case 0x21: case 0xE1: case 0x26: case 0x2E: case 0xF8:
        return 1;
    default:
        /* ld h, r / ld l, r (sin contar ld h, [hl] / ld l, [hl]) */
        return op >= 0x60 && op < 0x70 && (op & 7) != 6;
    }
}

static int routine_kind(u16 addr, int *bank_reg)
{
    if (addr >= 0x4000) return RK_NONE;
    if (rk_cache[addr]) {
        *bank_reg = rk_reg[addr];
        return rk_cache[addr];
    }
    rk_cache[addr] = RK_NONE;

    Regs s;
    regs_unknown(&s);
    for (int i = 0; i < 8; i++) s.r[i] = V_SYM(i);
    u16 ret_stack[4];
    int calls = 0;
    int hl_is_input = 1;    /* HL todavía deriva del que trajo la llamada */
    int read_input_hl = 0;  /* se leyó memoria a través de ese HL */
    u16 pc = addr;
    int kind = RK_NONE;
    int bank_sym = 0;

    for (int step = 0; step < 96 && pc < 0x4000; step++) {
        u8 b[3];
        Insn in;
        if (!fetch_insn(0, pc, b, &in)) break;
        bank_sym = s.bank >= V_SYM(0) && s.bank <= V_SYM(7) && s.bank != V_SYM(6);
        if (in.kind == INS_JP_HL) {
            if (s.r[R_H] == V_SYM(R_H) && s.r[R_L] == V_SYM(R_L) && bank_sym)
                kind = RK_TRAMPOLINE;
            else if (read_input_hl && calls == 0)
                kind = RK_TABLE_JUMPER;
            break;
        }
        if (in.kind == INS_CALL || in.kind == INS_RST) {
            if (in.cond || calls == 4) break;
            ret_stack[calls++] = (u16)(pc + in.len);
            pc = in.target;
            continue;
        }
        if (in.kind == INS_RET) {
            if (in.cond) break;
            if (calls == 0) {
                if (bank_sym) kind = RK_BANK_SETTER;
                break;
            }
            pc = ret_stack[--calls];
            continue;
        }
        if (in.kind == INS_JP || in.kind == INS_JR) {
            if (in.cond) break;
            pc = in.target;
            continue;
        }
        if (in.kind != INS_NORMAL) break;
        if (hl_is_input && reads_via_hl(b[0])) read_input_hl = 1;
        if (overwrites_hl(b[0])) hl_is_input = 0;
        regs_step(&s, b);
        pc = (u16)(pc + in.len);
    }

    rk_cache[addr] = (signed char)kind;
    if (kind == RK_TRAMPOLINE || kind == RK_BANK_SETTER) rk_reg[addr] = (u8)(s.bank - V_SYM(0));
    *bank_reg = rk_reg[addr];
    if (verbose && kind != RK_NONE) {
        static const char *const names[] = { "", "", "trampolín", "fijador de banco", "saltador por tabla" };
        fprintf(stderr, "%s 00:%04X", names[kind], addr);
        if (kind != RK_TABLE_JUMPER) fprintf(stderr, " (banco en el registro %d)", rk_reg[addr]);
        fprintf(stderr, "\n");
    }
    return kind;
}


/* La instrucción (no de control) deja A intacto */
static int keeps_a(u8 op)
{
    if (op >= 0x40 && op < 0x78) return op != 0x76;     /* ld r, r' con destino distinto de A */
    if (op >= 0xB8 && op <= 0xBF) return 1;             /* cp r */
    switch (op) {
    case 0x00: case 0x02: case 0x12: case 0x22: case 0x32:
    case 0x01: case 0x11: case 0x21: case 0x31:
    case 0x03: case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: case 0x33: case 0x3B:
    case 0x04: case 0x05: case 0x0C: case 0x0D: case 0x14: case 0x15: case 0x1C: case 0x1D:
    case 0x24: case 0x25: case 0x2C: case 0x2D: case 0x34: case 0x35:
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36:
    case 0x09: case 0x19: case 0x29: case 0x39: case 0x37: case 0x3F:
    case 0x7F: case 0xA7: case 0xB7: case 0xFE:
    case 0xE0: case 0xE2: case 0xEA:
    case 0xC5: case 0xD5: case 0xE5: case 0xF5: case 0xF3: case 0xFB:
        return 1;
    default:
        return 0;
    }
}

/* Banco efectivo de un número escrito al registro de banco */
static u32 mbc_bank(int v)
{
    u32 b = (u32)v % nbanks;
    return b == 0 && nbanks > 2 ? 1 : b;   /* MBC1/3: el banco 0 se lee como 1 */
}

static void explore_walk(u32 bank, u16 addr, int bank_ctx)
{
    /* Último `ld hl, nn` con nn en ROM dentro de este tramo lineal, y cuántas
     * instrucciones han pasado desde entonces */
    long table_base = -1;
    int since_base = 0;
    Regs st;                /* registros conocidos en este recorrido lineal */
    long pushed_const = -1; /* constante recién empujada a la pila */
    int bank_tied_to_a = 0; /* el banco activo es el valor actual de A */
    int last_cp = -1;       /* operando de un `cp n` en la instrucción anterior */
    regs_unknown(&st);
    /* En código de un banco N, el banco activo es N; en el banco 0, el que
     * traía quien saltó aquí */
    st.bank = bank != 0 ? (int)bank : bank_ctx;

    for (;;) {
        /* Los destinos que se descubran heredan el banco conocido hasta aquí */
        push_ctx = IS_CONST(st.bank) ? st.bank : V_UNK;
        if (!in_region(bank, addr)) return;
        if (*flag_at(bank, addr) & (F_INSN | F_TABLE)) return;

        u8 b[3];
        Insn in;
        if (!fetch_insn(bank, addr, b, &in)) return;   /* cruza el fin del banco */

        *flag_at(bank, addr) |= F_INSN;
        for (int i = 0; i < in.len; i++) *flag_at(bank, (u16)(addr + i)) |= F_BODY;

        /* Tabla de punteros fuera de línea: `ld hl, Tabla` y, poco después,
         * `jp hl` (con una llamada intermedia que lee la entrada) */
        /* Una dirección de ROM cargada como constante (ld bc/de/hl, nn)
         * suele ser el inicio de un objeto: tabla, cadena, gráficos */
        if (b[0] == 0x01 || b[0] == 0x11 || b[0] == 0x21) {
            u16 nn = (u16)(b[1] | b[2] << 8);
            long rb = bank_of(bank, nn);
            if (rb >= 0) *flag_at((u32)rb, nn) |= F_REF;
        }
        if (b[0] == 0x21) {
            u16 nn = (u16)(b[1] | b[2] << 8);
            table_base = nn < 0x8000 && !(nn >= 0x0104 && nn < 0x0150) ? nn : -1;
            since_base = 0;
        } else if (++since_base > 8) {
            table_base = -1;
        }
        if (in.kind == INS_JP_HL && table_base >= 0) {
            long tb = bank_of(bank, (u16)table_base);
            if (tb >= 0) add_table((u32)tb, (u16)table_base, 0);
        }

        /* `ld rr, nn` + `push rr` justo antes de un salto indirecto: nn es la
         * dirección de regreso que usará un `ret` (como en Bankswitch) */
        if (pushed_const >= 0 && (in.kind == INS_JP_HL || in.kind == INS_RET ||
                                  (in.kind == INS_JP && in.cond == 0))) {
            long rb = bank_of(bank, (u16)pushed_const);
            if (rb >= 0) {
                *flag_at((u32)rb, (u16)pushed_const) |= F_TARGET;
                push_work((u32)rb, (u16)pushed_const);
            }
        }
        pushed_const = -1;

        switch (in.kind) {
        case INS_JP: case INS_JR: case INS_CALL: case INS_RST: {
            long tb;
            /* Al área de bancos con un banco escrito como constante */
            if (in.target >= 0x4000 && in.target < 0x8000 && IS_CONST(st.bank))
                tb = mbc_bank(st.bank);
            else
                tb = resolve_bank(bank, in.target);
            if (tb >= 0) {
                *flag_at((u32)tb, in.target) |= F_TARGET;
                push_work((u32)tb, in.target);
                /* Llamada a un despachador: lo que sigue es su tabla */
                if ((in.kind == INS_CALL || in.kind == INS_RST) && in.cond == 0 &&
                    is_table_dispatcher((u32)tb, in.target)) {
                    add_table(bank, (u16)(addr + in.len), 1);
                    return;
                }
            }
            /* Rutinas especiales del banco 0 (ver routine_kind) */
            int breg = 0, kind = RK_NONE;
            if ((in.kind == INS_CALL || in.kind == INS_RST || in.kind == INS_JP) && in.target < 0x4000)
                kind = routine_kind(in.target, &breg);
            long hlv = pair_value(&st, R_H, R_L);
            int new_bank = st.bank;
            if (kind == RK_TRAMPOLINE && IS_CONST(st.r[breg]) && hlv >= 0 && hlv < 0x8000) {
                /* Trampolín con banco y destino constantes */
                u32 fb = hlv < 0x4000 ? 0 : mbc_bank(st.r[breg]);
                *flag_at(fb, (u16)hlv) |= F_TARGET;
                push_work(fb, (u16)hlv);
                stat_tramp_calls++;
            } else if (kind == RK_BANK_SETTER) {
                new_bank = st.r[breg];
                stat_bank_setters++;
            } else if (kind == RK_TABLE_JUMPER && hlv >= 0 && hlv < 0x8000) {
                /* Tabla de punteros apuntada por HL */
                long tb2 = IS_CONST(st.bank) && hlv >= 0x4000 ? (long)mbc_bank(st.bank) : bank_of(bank, (u16)hlv);
                if (tb2 >= 0) {
                    add_table((u32)tb2, (u16)hlv, 0);
                    stat_table_jumps++;
                }
            }
            /* Tras una llamada, la rutina pudo cambiar cualquier registro; el
             * banco se conserva (las rutinas suelen restaurarlo), salvo que sea
             * un fijador de banco */
            if (in.kind == INS_CALL || in.kind == INS_RST) {
                regs_unknown(&st);
                st.bank = new_bank;
            }
            break;
        }
        case INS_NORMAL: {
            u8 op = b[0];
            if (op == 0xC5 || op == 0xD5 || op == 0xE5) {
                int hi = ((op >> 4) & 3) * 2;
                pushed_const = pair_value(&st, hi, hi + 1);
            }
            /* El banco se escribió desde A con valor desconocido: mientras A
             * no cambie, "banco == A" */
            int bank_write = (op == 0xEA && b[2] >= 0x20 && b[2] < 0x40) ||
                             (op == 0x77 && pair_value(&st, R_H, R_L) >= 0x2000 &&
                              pair_value(&st, R_H, R_L) < 0x4000);
            if (bank_write && !IS_CONST(st.r[R_A])) bank_tied_to_a = 1;
            else if (!keeps_a(op)) bank_tied_to_a = 0;
            regs_step(&st, b);
            break;
        }
        default:
            break;
        }
        if (in.kind == INS_CALL || in.kind == INS_RST) bank_tied_to_a = 0;

        /* `cp n` + `jr/jp nz`: si no salta, A == n (y el banco también, si
         * se escribió desde A) */
        if ((in.kind == INS_JR || in.kind == INS_JP) && in.cond == 1 && last_cp >= 0) {
            st.r[R_A] = last_cp;
            if (bank_tied_to_a) st.bank = last_cp;
        }
        last_cp = b[0] == 0xFE ? b[1] : -1;

        if (sm83_ends_block(&in)) return;
        addr = (u16)(addr + in.len);
    }
}

/* Recorre desde una dirección; los push_work de fuera de un recorrido (tablas,
 * trazas, vectores) no llevan contexto de banco */
static void explore(u32 bank, u16 addr, int bank_ctx)
{
    explore_walk(bank, addr, bank_ctx);
    push_ctx = V_UNK;
}

static void drain(void)
{
    while (nwork) {
        Work w = work[--nwork];
        explore(w.bank, w.addr, w.bank_ctx);
    }
}

/*
 * Lee una tabla de punteros a código entrada por entrada. Su largo no está
 * escrito en ningún lado, así que se detiene cuando:
 *   - llega a bytes que ya se sabe que son código o destino de un salto (por
 *     eso cada entrada se explora antes de leer la siguiente: el código que
 *     suele seguir a la tabla aparece como destino de alguna entrada);
 *   - en tablas fuera de línea, llega a una dirección que el código carga
 *     como constante: ahí empieza otro objeto (otra tabla, por ejemplo). En
 *     las tablas en línea no se aplica, porque constantes que no son
 *     punteros (conteos de bytes) caen ahí por casualidad;
 *   - la entrada no apunta a ROM, apunta a la cabecera del cartucho
 *     ($0104-$014F) o apunta dentro de la tabla.
 * Si se pasa de largo, solo se descubre código de más (inofensivo); si se
 * queda corta, el resto lo resuelve la traza o el intérprete.
 */
static void read_table(u32 bank, u16 start, int inline_table)
{
    u16 pos = start;
    stat_tables++;
    for (int k = 0; k < 256; k++) {
        if (!in_region(bank, pos) || !in_region(bank, (u32)pos + 1)) break;
        u8 f0 = *flag_at(bank, pos), f1 = *flag_at(bank, (u16)(pos + 1));
        if ((f0 | f1) & (F_BODY | F_TABLE)) break;
        if (k > 0 && ((f0 | f1) & F_TARGET)) break;
        if (k > 0 && !inline_table && ((f0 | f1) & F_REF)) break;
        const u8 *p = bank_ptr(bank) + (pos - region_base(bank));
        u16 entry = (u16)(p[0] | p[1] << 8);
        if (entry >= 0x8000 || (entry >= 0x0104 && entry < 0x0150)) break;
        if (entry >= start && entry < (u32)pos + 2 && in_region(bank, entry)) break;

        *flag_at(bank, pos) |= F_TABLE;
        *flag_at(bank, (u16)(pos + 1)) |= F_TABLE;
        stat_table_entries++;
        long tb = resolve_bank(bank, entry);
        if (tb >= 0) {
            *flag_at((u32)tb, entry) |= F_TARGET;
            push_work((u32)tb, entry);
            drain();
        }
        pos = (u16)(pos + 2);
    }
    if (verbose)
        fprintf(stderr, "tabla %02X:%04X: %u entradas\n", bank, start, (unsigned)(pos - start) / 2);
}

static void discover(void)
{
    static const u16 vectors[] = {
        0x0100, 0x0040, 0x0048, 0x0050, 0x0058, 0x0060,
    };
    static size_t tables_done;
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        *flag_at(0, vectors[i]) |= F_ENTRY;
        /* Al encender, todos los MBC tienen el banco 1 en $4000-$7FFF: la
         * entrada lo sabe (un "jp $5C45" en $0101 va al banco 1). En las
         * interrupciones el banco puede ser cualquiera. */
        push_ctx = vectors[i] == 0x0100 ? 1 : V_UNK;
        push_work(0, vectors[i]);
    }
    push_ctx = V_UNK;
    drain();
    /* Las tablas se leen al final: así el código que las rodea ya se conoce */
    while (tables_done < ntables) {
        Table t = tables[tables_done++];
        read_table(t.bank, t.addr, t.inline_table);
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

static void emit_info(const char *dir, const char *sha1, const char *title, const char *name,
                      unsigned ninsns)
{
    FILE *f = open_out(dir, "gen/recomp_info.c");
    fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n\n");
    fprintf(f, "const char recomp_rom_sha1[41] = \"%s\";\n", sha1);
    fprintf(f, "const char recomp_rom_title[] = \"%s\";\n", title);
    fprintf(f, "const char recomp_name[] = \"%s\";\n", name);
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

/* Escribe el runtime y la plataforma que gbrecomp lleva adentro */
static void write_embedded(const char *dir)
{
    char to[1024];
    for (const EmbeddedFile *e = embedded_files; e->name; e++) {
        snprintf(to, sizeof to, "%s/%s", dir, e->name);
        FILE *f = fopen(to, "wb");
        if (!f) die("no se pudo crear %s", to);
        if (fwrite(e->data, 1, e->size, f) != e->size) die("no se pudo escribir %s", to);
        fclose(f);
    }
}

/* Instrucciones para compilar el proyecto generado */
static void emit_readme(const char *dir, const char *name, const char *title, const char *sha1)
{
    FILE *f = open_out(dir, "LEEME.txt");
    fprintf(f,
        "%s (%s), recompilado por gbrecomp %s\n"
        "SHA1 de la ROM: %s\n"
        "\n"
        "Este proyecto no contiene datos de la ROM. El ejecutable la pide la\n"
        "primera vez (diálogo de abrir archivo en Windows; en Linux y macOS,\n"
        "arrástrala a la ventana o pásala como argumento), valida su SHA1 y guarda\n"
        "una copia en la carpeta de datos del juego.\n"
        "\n"
        "Compilar (C99 + SDL2):\n"
        "  Windows: MSYS2 UCRT64\n"
        "      pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 make\n"
        "      make\n"
        "    El ejecutable queda enlazado de forma estática (no necesita DLLs).\n"
        "  Linux (Debian/Ubuntu):\n"
        "      sudo apt install build-essential pkg-config libsdl2-dev\n"
        "      make\n"
        "  macOS (Homebrew):\n"
        "      brew install sdl2 pkg-config\n"
        "      make\n"
        "  Con varios núcleos: make -j8\n"
        "\n"
        "Uso: %s [rom.gb] [--scale N] [--model dmg|cgb|sgb] [--palette up+a]\n"
        "            [--record archivo | --play archivo] [--interp]\n"
        "  Controles: flechas, Z = B, X = A, Enter = Start, Retroceso = Select,\n"
        "  o un control (gamepad). La partida (.sav) se guarda sola.\n"
        "  Menús (Alt o F10): reiniciar, pausa (P), estados guardados (F5 / F8,\n"
        "  cuatro ranuras), consola (Game Boy, Color o Super Game Boy), paleta\n"
        "  (F2), tamaño, pantalla completa (F11), sonido, idioma y ayuda (F1).\n"
        "\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest rom.gb --frames 6000 --seed 1\n"
        "\n"
        "El código en gen/ es una traducción del programa de la ROM: úsalo para\n"
        "jugar tu propia copia; no lo distribuyas si el juego es comercial.\n",
        title, name, GBRECOMP_VERSION, sha1, name);
    fclose(f);
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
        "SDL_LIBS := -static $(shell pkg-config --libs --static sdl2) -lcomdlg32\n"
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
        "# Plataforma: ventana, menús, controles (todo menos difftest)\n"
        "PLAT_OBJ := $(patsubst platform/%%.c,build/pl_%%.o,$(filter-out platform/difftest.c,$(wildcard platform/*.c)))\n\n"
        "build/pl_%%.o: platform/%%.c $(wildcard platform/*.h) $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) $(SDL_CFLAGS) -c -o $@ $<\n\n"
        "%s$(EXE): $(PLAT_OBJ) $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) -o $@ $^ $(SDL_LIBS)\n\n"
        "# Prueba diferencial: código recompilado contra el intérprete\n"
        "difftest$(EXE): platform/difftest.c $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^\n\n"
        "clean:\n\trm -rf build %s$(EXE) difftest$(EXE)\n",
        name, name, name);
    fclose(f);
}

/* Mapa por byte de lo descubierto: C = código, . = no descubierto (64 por línea) */
static void write_codemap(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    for (u32 b = 0; b < nbanks; b++)
        for (u32 off = 0; off < BANK_SIZE; off++) {
            fputc((flags[b][off] & F_BODY) ? 'C' : '.', f);
            if (off % 64 == 63) fputc('\n', f);
        }
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
    console_utf8();
    const char *rom_path = NULL, *out = NULL, *trace = NULL, *name = NULL, *codemap = NULL;
    const char *dis = NULL;     /* --dis BB:AAAA:N */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("%s", usage_text);
            return 0;
        }
        if (!strcmp(argv[i], "--version")) {
            printf("gbrecomp %s\n", GBRECOMP_VERSION);
            return 0;
        }
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--codemap") && i + 1 < argc) codemap = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--dis") && i + 1 < argc) dis = argv[++i];
        else if (argv[i][0] == '-') die("opción desconocida: %s (ver gbrecomp --help)", argv[i]);
        else rom_path = argv[i];
    }
    if (!rom_path || (!out && !dis)) {
        fprintf(stderr, "%s", usage_text);
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

    if (dis) {
        /* Solo desensamblar: --dis BB:AAAA:N */
        unsigned db, da, dn = 32;
        if (sscanf(dis, "%x:%x:%u", &db, &da, &dn) < 2 || db >= nbanks || !in_region(db, da))
            die("--dis espera BANCO:DIRECCIÓN[:CUÁNTAS] en hexadecimal");
        u16 a = (u16)da;
        for (unsigned k = 0; k < dn && in_region(db, a); k++) {
            u8 b[3];
            Insn in;
            char text[48];
            fetch_insn(db, a, b, &in);
            sm83_disasm(b, a, text, sizeof text);
            printf("%02X:%04X  %-24s ;", db, a, text);
            for (int j = 0; j < in.len; j++) printf(" %02X", b[j]);
            printf("\n");
            a = (u16)(a + in.len);
        }
        return 0;
    }

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

    if (codemap) write_codemap(codemap);

    /* 3. Proyecto */
    char path[1024];
    make_dir(out);
    snprintf(path, sizeof path, "%s/gen", out); make_dir(path);
    snprintf(path, sizeof path, "%s/runtime", out); make_dir(path);
    snprintf(path, sizeof path, "%s/platform", out); make_dir(path);

    emit_code(out);
    emit_dispatch(out);
    emit_info(out, sha1, title, exe_name, ninsns);
    emit_makefile(out, exe_name);
    emit_readme(out, exe_name, title, sha1);
    write_embedded(out);

    printf("ROM: %s (%s), %u bancos, SHA1 %s\n", rom_path, title, nbanks, sha1);
    printf("código: %u instrucciones (%u bytes, %.1f%% de la ROM) en %zu funciones\n",
           ninsns, nbytes, 100.0 * nbytes / rom_size, nfuncs);
    if (stat_tramp_calls)
        printf("llamadas a trampolines de banco resueltas: %u\n", stat_tramp_calls);
    if (stat_bank_setters)
        printf("llamadas a fijadores de banco: %u\n", stat_bank_setters);
    if (stat_table_jumps)
        printf("saltos por tabla con base en HL: %u\n", stat_table_jumps);
    if (stat_tables)
        printf("tablas de saltos en línea: %u, con %u entradas\n", stat_tables, stat_table_entries);
    if (stat_ram_targets)
        printf("saltos a RAM (siempre van al intérprete): %u\n", stat_ram_targets);
    if (stat_unresolved)
        printf("saltos al área de bancos sin banco conocido: %u (los resuelve la traza o el intérprete)\n",
               stat_unresolved);
    printf("proyecto generado en %s (ejecutable: %s)\n", out, exe_name);
    return 0;
}
