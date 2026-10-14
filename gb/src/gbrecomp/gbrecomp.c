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
#ifndef _WIN32
#define _DEFAULT_SOURCE     /* opendir/readdir con -std=c99 (glibc) */
#endif
#include <ctype.h>
#include <dirent.h>
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
#include "sha1.h"
#include "embedded.h"
#include "viz.h"

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
    "                     puntos de entrada, y el código que corrió desde WRAM o\n"
    "                     HRAM (líneas ram) se compila. Se puede repetir: se leen\n"
    "                     todas. Repite el ciclo hasta que no queden.\n"
    "  --codemap archivo  qué bytes se tomaron como código (para compararlo con\n"
    "                     un desensamblado)\n"
    "  --dis BB:AAAA:N    desensambla N instrucciones del banco BB en $AAAA\n"
    "  --events archivo   cada paso del descubrimiento (recorridos, instrucciones,\n"
    "                     tablas, saltos sin resolver), con de dónde vino cada uno\n"
    "  --viz              muestra el descubrimiento como un mapa animado de la ROM\n"
    "  --viz-time S       duración de la animación en segundos (por omisión 5;\n"
    "                     0 dibuja solo el resultado)\n"
    "  --color            con --viz, colorea aunque la salida no sea una terminal\n"
    "                     (lo usa la GUI; sin animación)\n"
    "  --html archivo     el descubrimiento como una página HTML sola (el visor de\n"
    "                     tools/visor.html con los eventos adentro; con --coverage,\n"
    "                     también la cobertura). La ROM, para desensamblar, se\n"
    "                     arrastra a la página\n"
    "  --coverage archivo direcciones que ejecutó el juego (formato de --uncovered);\n"
    "                     cuenta las que no se descubrieron y --viz las marca en rojo\n"
    "  --why BB:AAAA      explica por qué esa dirección se tomó como código: la\n"
    "                     cadena de saltos y tablas desde un punto de entrada\n"
    "                     (se puede repetir; sin -o no genera el proyecto)\n"
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
#define F_ARGS   0x40   /* argumentos en línea después de una llamada (datos) */

static u8 *rom;
static u32 rom_size, nbanks;
static u8 **flags;

typedef struct {
    u32 bank;
    u16 addr;
    int bank_ctx;       /* banco activo conocido al llegar (V_UNK = -1 si no se sabe) */
    u8 origin;          /* ORG_*: de dónde viene (para --events y --viz) */
    u16 from_bank, from_addr;   /* instrucción o entrada de tabla que lo agregó */
    u8 from_table;              /* 1: from_* es una entrada de tabla */
} Work;

static int push_ctx = -1;   /* contexto de banco para los push_work del recorrido actual */
/* Origen y procedencia para los push_work siguientes */
static u8 push_origin = ORG_BOOT;
static u16 push_from_bank = EV_NONE, push_from_addr;
static u8 push_from_table;
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
    work[nwork].origin = push_origin;
    work[nwork].from_bank = push_from_bank;
    work[nwork].from_addr = push_from_addr;
    work[nwork].from_table = push_from_table;
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
/* ¿Toca la pila o el SP? (push, pop, ld sp, inc/dec sp, add sp, ld [nn], sp) */
static int touches_stack(u8 op)
{
    switch (op) {
    case 0xC1: case 0xD1: case 0xE1: case 0xF1: case 0xC5: case 0xD5: case 0xE5: case 0xF5:
    case 0x31: case 0x33: case 0x3B: case 0xE8: case 0xF9: case 0x08:
        return 1;
    default:
        return 0;
    }
}

/* Rutina auxiliar del banco 0 que solo hace cuentas y regresa: pocas
 * instrucciones, sin tocar la pila ni saltar, terminada en `ret` (puede
 * tener `ret` condicionales antes). Como el `rst $18` de Street Fighter
 * Alpha (HL += A: add a, l / ld l, a / ret nc / inc h / ret) */
static int is_simple_helper(u16 addr)
{
    for (int i = 0; i < 8 && addr < 0x4000; i++) {
        u8 b[3];
        Insn in;
        if (!fetch_insn(0, addr, b, &in)) return 0;
        if (in.kind == INS_RET) {
            if (!in.cond) return 1;
        } else if (in.kind != INS_NORMAL || touches_stack(b[0])) {
            return 0;
        }
        addr = (u16)(addr + in.len);
    }
    return 0;
}

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
        /* Una llamada a una rutina auxiliar que solo hace cuentas (sumar el
         * índice a HL) no cambia nada: se sigue después de la llamada */
        if ((in.kind == INS_CALL || in.kind == INS_RST) && !in.cond && in.target < 0x4000 &&
            is_simple_helper(in.target)) {
            addr = (u16)(addr + in.len);
            continue;
        }
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
    u16 from_bank, from_addr;   /* instrucción que la encontró */
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
    tables[ntables].from_bank = push_from_bank;
    tables[ntables].from_addr = push_from_addr;
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
    u16 mem_addr[16];   /* celdas de RAM con valor conocido (p. ej. hROMBank) */
    int mem_val[16], nmem;
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
    if (s->nmem == 16) {    /* llena: se olvida la más vieja */
        memmove(s->mem_addr, s->mem_addr + 1, 15 * sizeof s->mem_addr[0]);
        memmove(s->mem_val, s->mem_val + 1, 15 * sizeof s->mem_val[0]);
        s->nmem = 15;
    }
    s->mem_addr[s->nmem] = (u16)addr;
    s->mem_val[s->nmem++] = v;
}

/* "El byte de RAM en addr", sin saber su valor: así se reconoce un puntero
 * leído de una variable (ver punteros a código en RAM) */
#define V_RAM(a)   (0x10000 + (a))
#define IS_RAM_VAL(v) ((v) >= V_RAM(0) && (v) < V_RAM(0x10000))

static int is_ram(long addr)
{
    return (addr >= 0xA000 && addr < 0xE000) || (addr >= 0xFF80 && addr < 0xFFFF);
}

static int mem_load(const Regs *s, long addr)
{
    for (int i = 0; addr >= 0 && i < s->nmem; i++)
        if (s->mem_addr[i] == addr) return s->mem_val[i];
    return is_ram(addr) ? V_RAM(addr) : V_UNK;
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


/*
 * Rutinas con argumentos en línea: quien las llama pone N bytes justo después
 * del `call`; la rutina saca la dirección de regreso con `pop hl`, lee los
 * bytes con `ld r, [hl+]` / `inc hl` y vuelve a empujar HL (ya pasados los
 * argumentos) con `push hl`. El código del que llama sigue en call + 3 + N.
 * Si además termina en `jp hl` con H, L y el banco sacados de los argumentos,
 * es una llamada a otro banco: el destino está escrito en los argumentos
 * (la rutina de $0540 de Shantae: dirección baja, alta y banco). Si salta así
 * sin haber vuelto a empujar HL, es un salto lejano que no regresa (la de
 * $056B de Shantae): después de los argumentos no sigue código.
 * Se analiza por ejecución simbólica con valores V_ARG(k): "el byte k de los
 * argumentos".
 */
#define V_ARG(k)   (0x200 + (k))
#define IS_ARG(v)  ((v) >= V_ARG(0) && (v) < V_ARG(8))
#define MAX_ARGS 8

typedef struct {
    signed char n;              /* bytes de argumentos (0: no es de este tipo) */
    signed char lo, hi, bank;   /* qué argumento es cada parte del destino (-1 si no) */
    signed char noret;          /* salto lejano: no regresa a quien la llamó */
} InlineArgs;

static InlineArgs ia_cache[0x4000];
static u8 ia_done[0x4000];
static unsigned stat_inline_calls, stat_inline_targets;

static const InlineArgs *inline_args(u16 addr)
{
    static const InlineArgs none = { 0, -1, -1, -1, 0 };
    if (addr >= 0x4000) return &none;
    InlineArgs *ia = &ia_cache[addr];
    if (ia_done[addr]) return ia;
    ia_done[addr] = 1;
    *ia = none;

    Regs s;
    regs_unknown(&s);
    int off = -1;   /* HL = dirección de regreso + off (-1: HL es otra cosa) */
    int n = -1;     /* argumentos ya saltados al hacer `push hl` */
    int read = 0;   /* bytes de argumentos leídos (aunque después se pise HL) */
    u16 pc = addr;
    for (int step = 0; step < 48 && pc < 0x4000; step++) {
        u8 b[3];
        Insn in;
        if (!fetch_insn(0, pc, b, &in)) break;
        u8 op = b[0];
        if (in.kind == INS_JP_HL || in.kind == INS_RET) {
            int dest = in.kind == INS_JP_HL && IS_ARG(s.r[R_L]) && IS_ARG(s.r[R_H]);
            if (n < 0 && dest && read > 0 && read <= MAX_ARGS) {
                n = read;           /* salto lejano: los argumentos son lo que leyó */
                ia->noret = 1;
            }
            if (n > 0 && !(in.kind == INS_RET && in.cond)) {
                ia->n = (signed char)n;
                if (dest) {
                    ia->lo = (signed char)(s.r[R_L] - V_ARG(0));
                    ia->hi = (signed char)(s.r[R_H] - V_ARG(0));
                    ia->bank = IS_ARG(s.bank) ? (signed char)(s.bank - V_ARG(0)) : -1;
                }
            }
            break;
        }
        if (in.kind == INS_JP || in.kind == INS_JR) {
            if (in.cond) break;
            pc = in.target;
            continue;
        }
        if (in.kind != INS_NORMAL) break;   /* llamadas, rst, halt... */

        /* La pila está vacía al entrar: lo de arriba es la dirección de regreso */
        if (op == 0xE1 && s.depth == 0 && n < 0 && off < 0) {
            off = 0;
            s.r[R_H] = s.r[R_L] = V_UNK;
            pc++;
            continue;
        }
        if (off >= 0) {
            int d = (op >> 3) & 7;
            if (op == 0x2A || op == 0x3A) {             /* ld a, [hl+] / [hl-] */
                s.r[R_A] = off < MAX_ARGS ? V_ARG(off) : V_UNK;
                off += op == 0x2A ? 1 : -1;
                if (off > read) read = off;
                pc++;
                continue;
            }
            if (op >= 0x40 && op < 0x80 && (op & 7) == 6 && op != 0x76 && d != R_H && d != R_L) {
                s.r[d] = off < MAX_ARGS ? V_ARG(off) : V_UNK;   /* ld r, [hl] */
                if (off + 1 > read) read = off + 1;
                pc++;
                continue;
            }
            if (op == 0x23 || op == 0x2B) {             /* inc hl / dec hl */
                off += op == 0x23 ? 1 : -1;
                pc++;
                continue;
            }
            if (op == 0xE5 && n < 0) {                  /* push hl: regreso ya ajustado */
                n = off;
                if (n < 1 || n > MAX_ARGS) break;
            } else if (overwrites_hl(op) || op == 0x09 || op == 0x19 || op == 0x29 || op == 0x39 ||
                       (op >= 0x40 && op < 0x80 && (op & 7) == 6)) {
                off = -1;
            }
        }
        if (off < 0 && n < 0 && step > 0 && s.depth == 0 && (op == 0xC1 || op == 0xD1 || op == 0xF1))
            break;      /* sacó el regreso a otro registro: no es este patrón */
        regs_step(&s, b);
        pc = (u16)(pc + in.len);
    }
    /* Ya empujó el regreso ajustado: aunque después llame a otras rutinas o
     * se ramifique (la de $0803 de Shantae llama a $0540), regresa ahí */
    if (!ia->n && n > 0 && n <= MAX_ARGS) ia->n = (signed char)n;
    if (verbose && ia->n) {
        fprintf(stderr, "%s con %d bytes en línea 00:%04X", ia->noret ? "salto lejano" : "rutina", ia->n, addr);
        if (ia->lo >= 0)
            fprintf(stderr, " (destino: argumentos %d y %d, banco: %d)", ia->lo, ia->hi, ia->bank);
        fprintf(stderr, "\n");
    }
    return ia;
}

/*
 * Filas de `jp nn` seguidos: tablas de vectores (la de $0150 de Shantae) o la
 * interfaz común con la que empiezan varios bancos (llamar a $4000 + 3n con
 * el banco que toque). Una fila cuenta si tiene al menos MIN_JP_RUN saltos
 * con destino en ROM, fuera de la cabecera.
 */
#define MIN_JP_RUN 3

static int is_jp_entry(u32 bank, long addr)
{
    if (!in_region(bank, (u32)addr) || !in_region(bank, (u32)addr + 2)) return 0;
    const u8 *p = bank_ptr(bank) + (addr - region_base(bank));
    u16 nn = (u16)(p[1] | p[2] << 8);
    return p[0] == 0xC3 && nn < 0x8000 && !(nn >= 0x0104 && nn < 0x0150);
}

/* Saltos válidos seguidos desde `addr` en pasos de `step` (+3 o -3) */
static int jp_run(u32 bank, long addr, int step)
{
    int n = 0;
    while (n < 256 && is_jp_entry(bank, addr + (long)n * step)) n++;
    return n;
}

static short iface_len[512];    /* fila de jp al inicio de cada banco (-1: sin analizar) */
static unsigned stat_jp_rows, stat_iface_calls;

static int bank_iface(u32 bank)
{
    if (bank == 0 || bank >= 512) return 0;
    if (!iface_len[bank]) iface_len[bank] = (short)(jp_run(bank, 0x4000, 3) + 1);
    return iface_len[bank] - 1 >= MIN_JP_RUN ? iface_len[bank] - 1 : 0;
}

/* Banco efectivo de un número escrito al registro de banco */
static u32 mbc_bank(int v)
{
    u32 b = (u32)v % nbanks;
    return b == 0 && nbanks > 2 ? 1 : b;   /* MBC1/3: el banco 0 se lee como 1 */
}

/*
 * Despachadores con el destino en una tabla de la ROM: rutinas del banco 0
 * que, sin ramificarse, calculan HL y el banco a partir de lo que trae quien
 * las llama y terminan en `jp hl` (el cambio de estado de Warlocked: `ld a, N`
 * + `jp $0554` lee dirección y banco del registro N de una tabla en $0521).
 * Se simulan con los registros conocidos de cada llamador, leyendo los bytes
 * de la ROM cuando la dirección es constante. Las llamadas internas se saltan
 * (dejan los registros desconocidos, pero no la pila ni la RAM conocida).
 * Si el destino depende del A del llamador, además se recorre la tabla entera
 * (A = 0, 1, 2...) hasta la primera entrada inválida: a algunos estados solo
 * se llega desde otros que el estático todavía no conoce.
 */
static signed char far_cache[0x4000];   /* 0: sin ver, 1: candidata, -1: no */
static u8 far_enumerated[0x4000];
static unsigned stat_sim_targets, stat_sim_entries;

/* Byte de la ROM en una dirección constante (V_UNK si no se sabe el banco) */
static int rom_byte(const Regs *s, long addr)
{
    if (addr < 0 || addr >= 0x8000) return V_UNK;
    if (addr < 0x4000) return rom[addr];
    if (!IS_CONST(s->bank)) return V_UNK;
    return bank_ptr(mbc_bank(s->bank))[addr - 0x4000];
}

/* 1 y el destino si se resolvió; 0 si llegó a `jp hl` sin conocerlo; -1 si
 * la rutina no termina así */
static int simulate_far_jump(u16 entry, Regs s, int via_call, u32 *tbank, u16 *taddr)
{
    s.depth = 0;
    if (via_call && s.depth < 16) {     /* la dirección de regreso */
        s.stk_hi[0] = s.stk_lo[0] = V_UNK;
        s.depth = 1;
    }
    u16 pc = entry;
    int carry = -1;     /* acarreo conocido (para `add` + `adc` de 16 bits) */
    for (int step = 0; step < 256 && pc < 0x4000; step++) {
        u8 b[3];
        Insn in;
        if (!fetch_insn(0, pc, b, &in)) return -1;
        u8 op = b[0];
        switch (in.kind) {
        case INS_JP_HL: {
            long hl = pair_value(&s, R_H, R_L);
            if (hl < 0 || hl >= 0x8000) return 0;
            if (hl >= 0x4000 && !IS_CONST(s.bank)) return 0;
            *tbank = hl < 0x4000 ? 0 : mbc_bank(s.bank);
            *taddr = (u16)hl;
            return 1;
        }
        case INS_JP: case INS_JR:
            if (in.cond) return -1;
            pc = in.target;
            continue;
        case INS_CALL: case INS_RST:
            if (in.cond) return -1;
            for (int i = 0; i < 8; i++) s.r[i] = V_UNK;
            carry = -1;
            pc = (u16)(pc + in.len);
            continue;
        case INS_NORMAL:
            break;
        default:
            return -1;
        }
        /* add / adc sobre A, llevando el acarreo: `add a, e` + `ld e, a` +
         * `ld a, d` + `adc a, 0` suma un índice a una dirección de 16 bits */
        if ((op >= 0x80 && op < 0x90) || op == 0xC6 || op == 0xCE) {
            int v = op == 0xC6 || op == 0xCE ? b[1] : (op & 7) == 6 ? V_UNK : s.r[op & 7];
            int with_carry = (op >= 0x88 && op < 0x90) || op == 0xCE;
            int a = s.r[R_A];
            if (IS_CONST(a) && IS_CONST(v) && (!with_carry || carry >= 0)) {
                int r = a + v + (with_carry ? carry : 0);
                s.r[R_A] = r & 0xFF;
                carry = r > 0xFF;
            } else {
                s.r[R_A] = V_UNK;
                carry = -1;
            }
            pc = (u16)(pc + in.len);
            continue;
        }
        /* Lo demás que toca el acarreo lo deja desconocido */
        if ((op >= 0x90 && op < 0xC0) || op == 0xD6 || op == 0xDE || op == 0xE6 || op == 0xEE ||
            op == 0xF6 || op == 0xFE || op == 0x07 || op == 0x0F || op == 0x17 || op == 0x1F ||
            op == 0x27 || op == 0x37 || op == 0x3F || op == 0xCB || op == 0x09 || op == 0x19 ||
            op == 0x29 || op == 0x39 || op == 0xE8 || op == 0xF8 || op == 0xF1)
            carry = -1;
        /* Lecturas de la ROM en direcciones constantes */
        int d = (op >> 3) & 7;
        long hl = pair_value(&s, R_H, R_L);
        if (op == 0x0A || op == 0x1A) {
            long a = op == 0x0A ? pair_value(&s, R_B, R_C) : pair_value(&s, R_D, R_E);
            if (a >= 0 && a < 0x8000) { s.r[R_A] = rom_byte(&s, a); pc++; continue; }
        } else if (op == 0xFA) {
            long a = b[1] | b[2] << 8;
            if (a < 0x8000) { s.r[R_A] = rom_byte(&s, a); pc += 3; continue; }
        } else if (hl >= 0 && hl < 0x8000 && (op == 0x2A || op == 0x3A)) {
            s.r[R_A] = rom_byte(&s, hl);
            set_pair(&s, R_H, R_L, (hl + (op == 0x2A ? 1 : -1)) & 0xFFFF);
            pc++;
            continue;
        } else if (hl >= 0 && hl < 0x8000 && op >= 0x40 && op < 0x80 && (op & 7) == 6 && op != 0x76) {
            s.r[d] = rom_byte(&s, hl);
            pc++;
            continue;
        }
        regs_step(&s, b);
        pc = (u16)(pc + in.len);
    }
    return -1;
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

/*
 * Punteros a código guardados en RAM: el juego guarda la dirección de su
 * estado actual en una variable (dos bytes) y en otro lado la carga y salta
 * a ella (`ld hl, $FFE4` / `ld a, [hl+]` / `ld h, [hl]` / `ld l, a` / `jp hl`,
 * o una llamada a una rutina que solo hace `jp hl`). Las dos partes pueden
 * descubrirse en cualquier orden: se guardan las constantes de ROM escritas
 * en cada variable y las variables usadas como puntero, y cuando coinciden
 * se explora el destino. Las constantes de variables que nunca se usan como
 * puntero (datos, gráficos) no se exploran.
 */
typedef struct { u16 var, addr; short bank; } RamPtr;   /* bank -1: no se sabe */
static RamPtr *ram_ptrs;
static size_t nram_ptrs, capram_ptrs;
static short ram_ptr_use[0x10000];      /* 0: sin usar; 1 + banco del uso; -1 usado sin banco */
static unsigned stat_ram_ptr_targets;

static void ram_ptr_explore(const RamPtr *p, int use_bank)
{
    int b = p->addr < 0x4000 ? 0 : p->bank >= 0 ? p->bank : use_bank;
    if (b < 0 || (u32)b >= nbanks || !in_region((u32)b, p->addr)) return;
    if (*flag_at((u32)b, p->addr) & F_INSN) return;
    int saved = push_ctx;
    push_ctx = b != 0 ? b : V_UNK;
    *flag_at((u32)b, p->addr) |= F_TARGET;
    push_work((u32)b, p->addr);
    push_ctx = saved;
    stat_ram_ptr_targets++;
}

/* El código escribió la dirección de ROM `target` en la variable `var` */
static void ram_ptr_store(u16 var, u16 target, int bank)
{
    if (target < 0x0100 || target >= 0x8000 || (target >= 0x0104 && target < 0x0150)) return;
    for (size_t i = 0; i < nram_ptrs; i++)
        if (ram_ptrs[i].var == var && ram_ptrs[i].addr == target && ram_ptrs[i].bank == bank) return;
    if (nram_ptrs == capram_ptrs) {
        capram_ptrs = capram_ptrs ? capram_ptrs * 2 : 64;
        ram_ptrs = realloc(ram_ptrs, capram_ptrs * sizeof *ram_ptrs);
        if (!ram_ptrs) die("sin memoria");
    }
    RamPtr *p = &ram_ptrs[nram_ptrs++];
    p->var = var;
    p->addr = target;
    p->bank = (short)bank;
    if (ram_ptr_use[var]) ram_ptr_explore(p, ram_ptr_use[var] > 0 ? ram_ptr_use[var] - 1 : -1);
}

/* El código saltó a lo que hay en la variable `var` */
static void ram_ptr_jump(u16 var, int bank)
{
    if (ram_ptr_use[var] > 0 || (ram_ptr_use[var] < 0 && bank < 0)) return;
    ram_ptr_use[var] = (short)(bank >= 0 ? bank + 1 : -1);
    for (size_t i = 0; i < nram_ptrs; i++)
        if (ram_ptrs[i].var == var) ram_ptr_explore(&ram_ptrs[i], bank);
}

/* ¿La rutina en addr (banco 0) es solo `jp hl`? Llamarla es "call hl" */
static int is_call_hl(u16 addr)
{
    if (addr >= 0x4000) return 0;
    u8 b[3];
    Insn in;
    return fetch_insn(0, addr, b, &in) && in.kind == INS_JP_HL;
}

/* `jp hl` de rutinas del banco 0 que ya se entienden (argumentos en línea,
 * despachadores, trampolines, "call hl"): no se reportan como saltos sin
 * resolver en --events / --viz */
static u8 jp_hl_explained[0x4000];

static void explain_routine(u16 entry)
{
    u16 pc = entry;
    for (int i = 0; i < 64 && pc < 0x4000; i++) {
        u8 b[3];
        Insn in;
        if (!fetch_insn(0, pc, b, &in)) return;
        if (in.kind == INS_JP_HL) jp_hl_explained[pc] = 1;
        if ((in.kind == INS_JP || in.kind == INS_JR) && !in.cond) {
            pc = in.target;
            continue;
        }
        if ((in.kind == INS_RET || in.kind == INS_JP_HL) && !in.cond) return;
        pc = (u16)(pc + in.len);
    }
}

static void explore_walk(u32 bank, u16 addr, int bank_ctx, const Work *from)
{
    int started = 0;
    /* Último `ld hl, nn` con nn en ROM dentro de este tramo lineal, y cuántas
     * instrucciones han pasado desde entonces */
    long table_base = -1;
    int since_base = 0;
    Regs st;                /* registros conocidos en este recorrido lineal */
    long pushed_const = -1; /* constante empujada a la pila, todavía arriba */
    int pushed_age = 0;     /* instrucciones desde ese push */
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
        if (*flag_at(bank, addr) & (F_INSN | F_TABLE | F_ARGS)) return;

        u8 b[3];
        Insn in;
        if (!fetch_insn(bank, addr, b, &in)) return;   /* cruza el fin del banco */

        if (!started) {
            ev_add(EV_WALK, from->origin, bank, addr, from->from_bank, from->from_addr, 0,
                   from->from_table);
            started = 1;
        }
        ev_add(EV_INSN, 0, bank, addr, 0, 0, in.len, 0);
        push_from_bank = (u16)bank;
        push_from_addr = addr;
        push_from_table = 0;
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
        int call_hl = (in.kind == INS_CALL || in.kind == INS_RST) && !in.cond && is_call_hl(in.target);
        if (call_hl) explain_routine(in.target);
        if (in.kind == INS_JP_HL || call_hl) {
            /* HL leído de una variable de RAM: puntero a código en RAM */
            int lo = st.r[R_L], hi = st.r[R_H];
            int ram_ptr = IS_RAM_VAL(lo) && IS_RAM_VAL(hi) && hi == lo + 1;
            if (ram_ptr) {
                int ub = bank != 0 ? (int)bank : IS_CONST(st.bank) ? (int)mbc_bank(st.bank) : -1;
                ram_ptr_jump((u16)(lo - V_RAM(0)), ub);
            }
            long tb = table_base >= 0 ? bank_of(bank, (u16)table_base) : -1;
            if (tb >= 0 && !ram_ptr) add_table((u32)tb, (u16)table_base, 0);
            else if (in.kind == INS_JP_HL && !ram_ptr && !(bank == 0 && jp_hl_explained[addr]))
                ev_add(EV_INDIRECT, 0, bank, addr, 0, 0, 0, 0);
        }

        /* `ld rr, nn` + `push rr` poco antes de un salto indirecto, sin nada
         * que lo saque de la pila en medio: nn es la dirección de regreso que
         * usará un `ret` (como en Bankswitch, o el `ld hl, $0564` + `push hl`
         * de la rutina de $0540 de Shantae, diez instrucciones antes) */
        if (pushed_const >= 0 && (in.kind == INS_JP_HL || in.kind == INS_RET ||
                                  (in.kind == INS_JP && in.cond == 0))) {
            long rb = bank_of(bank, (u16)pushed_const);
            if (rb >= 0) {
                *flag_at((u32)rb, (u16)pushed_const) |= F_TARGET;
                push_work((u32)rb, (u16)pushed_const);
            }
            pushed_const = -1;
        }
        /* Deja de estar arriba de la pila: un pop, otro push (lo cubre más
         * abajo) o un cambio del SP */
        if (b[0] == 0xC1 || b[0] == 0xD1 || b[0] == 0xE1 || b[0] == 0xF1 || b[0] == 0xF5 ||
            b[0] == 0x31 || b[0] == 0x33 || b[0] == 0x3B || b[0] == 0xE8 || b[0] == 0xF9 ||
            ++pushed_age > 12)
            pushed_const = -1;
        int skip = 0;   /* argumentos en línea que siguen a esta instrucción */
        int noret = 0;  /* llamó a un salto lejano: aquí termina el recorrido */

        switch (in.kind) {
        case INS_JP: case INS_JR: case INS_CALL: case INS_RST: {
            long tb;
            /* Al área de bancos con un banco escrito como constante */
            if (in.target >= 0x4000 && in.target < 0x8000 && IS_CONST(st.bank))
                tb = mbc_bank(st.bank);
            else
                tb = resolve_bank(bank, in.target);
            if (tb < 0 && in.target >= 0x4000 && in.target < 0x8000) {
                /* Sin banco conocido, pero quizá es la interfaz común de
                 * varios bancos: se explora en cada banco que la tenga */
                int found = 0;
                if ((in.target - 0x4000) % 3 == 0) {
                    int saved = push_ctx;
                    for (u32 ib = 1; ib < nbanks; ib++) {
                        int n = bank_iface(ib);
                        if (!n || in.target >= 0x4000 + 3 * n) continue;
                        push_ctx = (int)ib;
                        *flag_at(ib, in.target) |= F_TARGET;
                        push_work(ib, in.target);
                        found++;
                    }
                    push_ctx = saved;
                }
                if (found) {
                    stat_iface_calls++;
                    stat_unresolved--;      /* resolve_bank ya lo había contado */
                } else ev_add(EV_UNRESOLVED, 0, bank, addr, 0, in.target, 0, 0);
            }
            if (tb >= 0) {
                *flag_at((u32)tb, in.target) |= F_TARGET;
                push_work((u32)tb, in.target);
                /* Llamada a una rutina con argumentos en línea: se saltan y,
                 * si traen el destino, se explora */
                const InlineArgs *ia = (in.kind == INS_CALL || in.kind == INS_RST) && in.cond == 0
                                       ? inline_args(in.target) : NULL;
                if (ia && ia->n && in_region(bank, (u32)addr + in.len + ia->n)) {
                    explain_routine(in.target);
                    const u8 *arg = bank_ptr(bank) + (addr + in.len - region_base(bank));
                    skip = ia->n;
                    noret = ia->noret;
                    for (int i = 0; i < skip; i++) *flag_at(bank, (u16)(addr + in.len + i)) |= F_ARGS;
                    ev_add(EV_ARGS, 0, bank, (u16)(addr + in.len), 0, 0, skip, 0);
                    stat_inline_calls++;
                    if (ia->lo >= 0) {
                        u16 dest = (u16)(arg[(int)ia->lo] | arg[(int)ia->hi] << 8);
                        long db = dest < 0x4000 ? 0 :
                                  dest < 0x8000 && ia->bank >= 0 ? (long)mbc_bank(arg[(int)ia->bank]) : -1;
                        if (db >= 0) {
                            int saved = push_ctx;
                            push_ctx = db != 0 ? (int)db : saved;
                            *flag_at((u32)db, dest) |= F_TARGET;
                            push_work((u32)db, dest);
                            push_ctx = saved;
                            stat_inline_targets++;
                        }
                    }
                }
                /* Llamada a un despachador: lo que sigue es su tabla */
                if (!skip && (in.kind == INS_CALL || in.kind == INS_RST) && in.cond == 0 &&
                    is_table_dispatcher((u32)tb, in.target)) {
                    if (tb == 0) explain_routine(in.target);
                    add_table(bank, (u16)(addr + in.len), 1);
                    return;
                }
            }
            /* Despachador con el destino en una tabla de la ROM: se simula con
             * los registros de aquí */
            if ((in.kind == INS_CALL || in.kind == INS_RST || in.kind == INS_JP) && in.cond == 0 &&
                in.target < 0x4000 && far_cache[in.target] >= 0) {
                u32 fb;
                u16 fa;
                int r = simulate_far_jump(in.target, st, in.kind != INS_JP, &fb, &fa);
                if (!far_cache[in.target]) far_cache[in.target] = r < 0 ? -1 : 1;
                if (r > 0) explain_routine(in.target);
                int saved = push_ctx;
                if (r > 0) {
                    push_ctx = fb != 0 ? (int)fb : saved;
                    *flag_at(fb, fa) |= F_TARGET;
                    push_work(fb, fa);
                    stat_sim_targets++;
                }
                /* ¿El destino depende de A? Entonces A es un índice: la tabla
                 * entera, hasta la primera entrada que no sea código posible */
                if (r > 0 && IS_CONST(st.r[R_A]) && !far_enumerated[in.target]) {
                    Regs alt = st;
                    u32 ab;
                    u16 aa;
                    alt.r[R_A] = (st.r[R_A] + 1) & 0xFF;
                    if (simulate_far_jump(in.target, alt, in.kind != INS_JP, &ab, &aa) > 0 &&
                        (ab != fb || aa != fa)) {
                        far_enumerated[in.target] = 1;
                        for (int v = 0; v < 256; v++) {
                            alt.r[R_A] = v;
                            if (simulate_far_jump(in.target, alt, in.kind != INS_JP, &ab, &aa) <= 0) break;
                            if (ab >= nbanks || !in_region(ab, aa) || (aa >= 0x0104 && aa < 0x0150)) break;
                            push_ctx = ab != 0 ? (int)ab : V_UNK;
                            *flag_at(ab, aa) |= F_TARGET;
                            push_work(ab, aa);
                            stat_sim_entries++;
                        }
                    }
                }
                push_ctx = saved;
            }
            /* Rutinas especiales del banco 0 (ver routine_kind) */
            int breg = 0, kind = RK_NONE;
            if ((in.kind == INS_CALL || in.kind == INS_RST || in.kind == INS_JP) && in.target < 0x4000)
                kind = routine_kind(in.target, &breg);
            if (kind == RK_TRAMPOLINE || kind == RK_TABLE_JUMPER) explain_routine(in.target);
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
                pushed_age = 0;
            }
            /* El banco se escribió desde A con valor desconocido: mientras A
             * no cambie, "banco == A" */
            int bank_write = (op == 0xEA && b[2] >= 0x20 && b[2] < 0x40) ||
                             (op == 0x77 && pair_value(&st, R_H, R_L) >= 0x2000 &&
                              pair_value(&st, R_H, R_L) < 0x4000);
            if (bank_write && !IS_CONST(st.r[R_A])) bank_tied_to_a = 1;
            else if (!keeps_a(op)) bank_tied_to_a = 0;
            /* Dirección que escribe (para los punteros a código en RAM) */
            long w = -1;
            if (op == 0xE0) w = 0xFF00 | b[1];
            else if (op == 0xEA) w = b[1] | b[2] << 8;
            else if (op == 0x02) w = pair_value(&st, R_B, R_C);
            else if (op == 0x12) w = pair_value(&st, R_D, R_E);
            else if (op == 0x22 || op == 0x32 || op == 0x36 || (op >= 0x70 && op <= 0x77 && op != 0x76))
                w = pair_value(&st, R_H, R_L);
            regs_step(&st, b);
            if (w >= 0 && is_ram(w)) {
                int pb = bank != 0 ? (int)bank : IS_CONST(st.bank) ? (int)mbc_bank(st.bank) : -1;
                for (long x = w - 1; x <= w; x++) {
                    int vlo = mem_load(&st, x), vhi = mem_load(&st, x + 1);
                    if (IS_CONST(vlo) && IS_CONST(vhi) && is_ram(x) && is_ram(x + 1)) {
                        u16 t = (u16)(vlo | vhi << 8);
                        ram_ptr_store((u16)x, t, t < 0x4000 ? 0 : pb);
                    }
                }
            }
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

        /* Un `jp nn` dentro de una fila de saltos: se exploran sus vecinos
         * (cada uno revisa a los suyos, así se recorre la fila entera) */
        if (b[0] == 0xC3) {
            int back = jp_run(bank, (long)addr - 3, -3), fwd = jp_run(bank, (long)addr + 3, 3);
            if (1 + back + fwd >= MIN_JP_RUN) {
                int saved = push_ctx;
                push_ctx = bank != 0 ? (int)bank : V_UNK;
                if (back && !(*flag_at(bank, (u16)(addr - 3)) & F_INSN)) {
                    push_work(bank, (u16)(addr - 3));
                    stat_jp_rows++;
                }
                if (fwd && !(*flag_at(bank, (u16)(addr + 3)) & F_INSN)) {
                    push_work(bank, (u16)(addr + 3));
                    stat_jp_rows++;
                }
                push_ctx = saved;
            }
        }

        if (sm83_ends_block(&in) || noret) return;
        addr = (u16)(addr + in.len + skip);
    }
}

/* Recorre desde una dirección; los push_work de fuera de un recorrido (tablas,
 * trazas, vectores) no llevan contexto de banco */
static void explore(const Work *w)
{
    push_origin = w->origin;
    explore_walk(w->bank, w->addr, w->bank_ctx, w);
    push_ctx = V_UNK;
    push_from_bank = EV_NONE;
    push_from_table = 0;
}

static void drain(void)
{
    while (nwork) {
        Work w = work[--nwork];
        explore(&w);
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
static void read_table(const Table *t)
{
    u32 bank = t->bank;
    u16 start = t->addr, pos = start;
    int inline_table = t->inline_table;
    stat_tables++;
    ev_add(EV_TABLE, 0, bank, start, t->from_bank, t->from_addr, 0, inline_table);
    for (int k = 0; k < 256; k++) {
        if (!in_region(bank, pos) || !in_region(bank, (u32)pos + 1)) break;
        u8 f0 = *flag_at(bank, pos), f1 = *flag_at(bank, (u16)(pos + 1));
        if ((f0 | f1) & (F_BODY | F_TABLE | F_ARGS)) break;
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
        ev_add(EV_TENTRY, 0, bank, pos, tb >= 0 ? (unsigned)tb : EV_NONE, entry, 2, 0);
        if (tb >= 0) {
            *flag_at((u32)tb, entry) |= F_TARGET;
            push_origin = ORG_TABLE;
            push_from_bank = (u16)bank;
            push_from_addr = pos;
            push_from_table = 1;
            push_work((u32)tb, entry);
            drain();
        }
        pos = (u16)(pos + 2);
    }
    if (verbose)
        fprintf(stderr, "tabla %02X:%04X: %u entradas\n", bank, start, (unsigned)(pos - start) / 2);
}

static int trace_pass;   /* ya se cargó la traza (para nombrar las fases) */

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
        push_origin = vectors[i] == 0x0100 ? ORG_BOOT : ORG_IRQ;
        push_from_bank = EV_NONE;
        push_work(0, vectors[i]);
    }
    push_ctx = V_UNK;
    drain();
    /* Las tablas se leen al final: así el código que las rodea ya se conoce */
    if (tables_done < ntables) ev_add(EV_PHASE, 0, 0, 0, 0, 0, 0, trace_pass ? PH_TRACE_TABLES : PH_TABLES);
    while (tables_done < ntables) {
        Table t = tables[tables_done++];
        read_table(&t);
    }
}

/* Código que el juego ejecutó desde WRAM o HRAM, aprendido de la traza: una versión de una
 * instrucción por línea, "ram DIRECCION BYTES" en hexadecimal (difftest --uncovered), con ??
 * en los operandos que cambian entre ejecuciones. Se compila en emit_ram */
typedef struct { u16 addr; u8 len; u8 bytes[3]; u8 vol; } RamObs;   /* vol: bit i = el byte i es variable */
static RamObs *ram_obs;
static size_t n_ram_obs, cap_ram_obs;

/* Bytes de la instrucción que la identifican (el resto son operandos que pueden variar); ver
 * ram_key_len en difftest.c */
static unsigned ram_key_len(u8 op)
{
    switch (op) {
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0xCB: return 2;
    default: return 1;
    }
}

static void ram_trace_line(const char *s)
{
    unsigned addr;
    int used = 0;
    if (sscanf(s, "%x%n", &addr, &used) != 1) return;
    s += used;
    u8 raw[3] = { 0, 0, 0 };
    u8 vol = 0;
    unsigned len = 0;
    while (len < 3) {
        while (*s == ' ') s++;
        if (s[0] == '?' && s[1] == '?') {
            vol |= (u8)(1u << len);
            raw[len++] = 0;
            s += 2;
            continue;
        }
        unsigned v;
        if (sscanf(s, "%x%n", &v, &used) != 1) break;
        raw[len++] = (u8)v;
        s += used;
    }
    if (len < 1 || (vol & 1)) return;    /* el opcode no puede ser variable */
    int wram = addr >= 0xC000 && addr + len <= 0xE000;
    int hram = addr >= 0xFF80 && addr + len <= 0xFFFF;
    if (!wram && !hram) return;
    Insn in;
    sm83_decode(raw, (uint16_t)addr, &in);
    if (in.len != len) return;    /* los bytes no son una instrucción completa */
    for (unsigned i = 0; i < ram_key_len(raw[0]) && i < len; i++)
        if (vol & (1u << i)) return;    /* el desplazamiento de un jr o el segundo byte de CB no varían */
    if (n_ram_obs == cap_ram_obs) {
        cap_ram_obs = cap_ram_obs ? cap_ram_obs * 2 : 256;
        ram_obs = realloc(ram_obs, cap_ram_obs * sizeof *ram_obs);
        if (!ram_obs) die("sin memoria");
    }
    RamObs *o = &ram_obs[n_ram_obs++];
    o->addr = (u16)addr;
    o->len = (u8)len;
    memcpy(o->bytes, raw, sizeof o->bytes);
    o->vol = vol;
}

/* Una traza: líneas "BB:AAAA" (banco y dirección en hexadecimal) y "ram AAAA BYTES" */
static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    unsigned bank, addr, n = 0;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "ram ", 4) == 0) {
            ram_trace_line(line + 4);
            continue;
        }
        if (sscanf(line, "%x:%x", &bank, &addr) != 2) continue;
        if (bank >= nbanks || addr > 0x7FFF) continue;
        if (!in_region(bank, addr)) continue;
        ev_add(EV_TRACE, 0, bank, addr, 0, 0, 0, (*flag_at(bank, (u16)addr) & F_INSN) != 0);
        *flag_at(bank, (u16)addr) |= F_ENTRY;
        push_work(bank, (u16)addr);
        n++;
    }
    fclose(f);
    printf("traza: %u puntos de entrada\n", n);
}

/* Todas las trazas de la línea de comandos (--trace se puede repetir, como en nesrecomp): se leen
 * todas antes de descubrir. El código de RAM de todas se junta y se compila al final (emit_ram) */
static void load_traces(const char *const *paths, int n)
{
    trace_pass = 1;
    ev_add(EV_PHASE, 0, 0, 0, 0, 0, 0, PH_TRACE);
    push_origin = ORG_TRACE;
    push_from_bank = EV_NONE;
    for (int i = 0; i < n; i++) load_trace(paths[i]);
    if (n_ram_obs) printf("traza: %zu instrucciones en WRAM/HRAM\n", n_ram_obs);
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

/* Borra los gen/bank*.c de una generación anterior: si ahora hay menos
 * código, un archivo viejo que ya no se escribe se compilaría también y
 * repetiría funciones */
static void remove_old_code(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/gen", dir);
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (strncmp(e->d_name, "bank", 4) != 0 || n < 3 || strcmp(e->d_name + n - 2, ".c") != 0) continue;
        snprintf(path, sizeof path, "%s/gen/%s", dir, e->d_name);
        remove(path);
    }
    closedir(d);
}

static void emit_code(const char *dir)
{
    remove_old_code(dir);
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

/* Rutinas de DMA de OAM: el juego las copia a HRAM y las ejecuta desde ahí, porque
 * mientras el DMA corre solo se puede leer HRAM. Su dirección en HRAM no se conoce al
 * generar, así que la función compilada de cada variante calcula en ejecución dónde está
 * la copia y sirve para cualquier dirección.
 *
 * match_dma reconoce la forma de la rutina en la ROM y los bytes salen de la ROM misma, así
 * que cada juego compila solo las variantes que tiene. En ejecución se usa el código
 * compilado únicamente si los bytes de HRAM coinciden con una variante encontrada, salvo
 * los operandos de ld a,n (el byte alto del origen, el contador del bucle), que no se
 * comparan y se leen de HRAM cuando la instrucción corre. Un reconocedor laxo no arriesga
 * nada: lo peor es compilar una variante que no se usa. Una variante que aparece al final
 * de otra más larga no cuenta por separado. */
#define HRAM_MAXLEN 16
#define HRAM_MAXSIGS 4        /* variantes que se compilan por juego */
#define HRAM_MAXCANDS 32      /* variantes distintas que se consideran */
#define HRAM_MAXOCCS 256      /* apariciones que se recuerdan */

typedef struct {
    char name[8];          /* prefijo de funciones y tablas en gen/hram.c */
    u8 len;
    u8 bytes[HRAM_MAXLEN];
} HramSig;

static HramSig hram_sigs[HRAM_MAXSIGS];
static unsigned n_hram_sigs;

#define HRAM_LO 0xFF80u

typedef struct {
    unsigned base;                  /* dirección de referencia con la que se emite (HRAM_LO) */
    unsigned starts[HRAM_MAXLEN];   /* desplazamientos de inicio de instrucción en la rutina */
    unsigned nstarts;
} HramRoutine;

static int hram_is_start(const HramRoutine *r, unsigned addr)
{
    for (unsigned i = 0; i < r->nstarts; i++)
        if (r->base + r->starts[i] == addr) return 1;
    return 0;
}

/* Saltos dentro de la rutina van a su etiqueta (un desplazamiento); los demás salen al
 * despachador con la dirección que calcula el emisor, que solo es dinámica (ret) */
static void hram_jump(Emit *e, const char *expr, int is_const, uint16_t target)
{
    HramRoutine *r = e->user;
    if (is_const && hram_is_start(r, target)) emit_line(e, "goto ins%u;", target - r->base);
    else emit_line(e, "c->pc = %s; return;", expr);
}

/* Inicios de instrucción de una variante, decodificados una vez */
static unsigned hram_starts(const HramSig *s, unsigned starts[HRAM_MAXLEN])
{
    u8 pad[HRAM_MAXLEN + 3] = { 0 };    /* 3 bytes de margen para decodificar la última instrucción */
    memcpy(pad, s->bytes, s->len);
    unsigned n = 0;
    for (unsigned off = 0; off < s->len;) {
        Insn in;
        sm83_decode(pad + off, (uint16_t)off, &in);
        starts[n++] = off;
        off += in.len;
    }
    return n;
}

/* El operando de cada ld a,n (3E nn) no se compara con HRAM y se lee de ahí en ejecución: el
 * juego puede cambiarlo después de copiar la rutina (el byte alto del origen del DMA, el
 * contador del bucle), y leerlo cuando la instrucción corre es lo que hace el intérprete.
 * mask[k] = 1 si el byte k de la variante sí se compara */
static void hram_mask(const HramSig *s, u8 mask[HRAM_MAXLEN])
{
    u8 pad[HRAM_MAXLEN + 3] = { 0 };
    memcpy(pad, s->bytes, s->len);
    memset(mask, 1, HRAM_MAXLEN);
    for (unsigned off = 0; off < s->len;) {
        Insn in;
        sm83_decode(pad + off, (uint16_t)off, &in);
        if (pad[off] == 0x3E) mask[off + 1] = 0;
        off += in.len;
    }
}

/* Pone en 0 los operandos que no se comparan: la misma rutina con otro origen es una variante */
static void hram_blank_operands(HramSig *s)
{
    u8 mask[HRAM_MAXLEN];
    hram_mask(s, mask);
    for (unsigned k = 0; k < s->len; k++)
        if (!mask[k]) s->bytes[k] = 0;
}

/* ¿Hay en p una rutina de DMA de OAM? Devuelve su longitud, 0 si no. Forma:
 *   [di] [ld a,N] ldh ($46),a | ld [c],a   dispara el DMA (con ld [c],a el llamador deja C = $46)
 *   ld a,M / dec a | dec r                 espera (con dec r el llamador deja el contador en r)
 *   jr nz,-3 / [nop hasta 4] / ret | reti
 * Variantes vistas: Tetris (ld a,N), Wario Land II (sin ld a,N), Donkey Kong Land 2
 * (cuatro nop), Tony Hawk (un nop), Mega Man Xtreme (di ... reti), Tomb Raider (dec e)
 * y Metal Gear Solid (ld [c],a / dec b). */
static unsigned match_dma(const u8 *p, size_t avail)
{
#define BYTE_AT(i) ((size_t)(i) < avail ? (int)p[i] : -1)
    unsigned q = 0;
    if (BYTE_AT(q) == 0xF3) q++;                                /* di */
    if (BYTE_AT(q) == 0x3E) q += 2;                             /* ld a,N */
    if (BYTE_AT(q) == 0xE0 && BYTE_AT(q + 1) == 0x46) q += 2;   /* ldh ($46),a */
    else if (BYTE_AT(q) == 0xE2) q++;                           /* ld [c],a */
    else return 0;
    int op = BYTE_AT(q);
    if (op == 0x3E && BYTE_AT(q + 2) == 0x3D) q += 3;           /* ld a,M / dec a */
    else if (op == 0x05 || op == 0x0D || op == 0x15 || op == 0x1D || op == 0x25 || op == 0x2D)
        q++;                                                    /* dec b, c, d, e, h o l */
    else return 0;
    if (BYTE_AT(q) != 0x20 || BYTE_AT(q + 1) != 0xFD) return 0; /* jr nz,-3 */
    q += 2;
    for (unsigned n = 0; n < 4 && BYTE_AT(q) == 0x00; n++) q++; /* nop */
    if (BYTE_AT(q) != 0xC9 && BYTE_AT(q) != 0xD9) return 0;     /* ret | reti */
    return q + 1;
#undef BYTE_AT
}

/* ¿Algún byte de rom[p..p+len) es código que el análisis alcanza (se ejecuta en su sitio)? */
static int rom_has_code(size_t p, unsigned len)
{
    for (unsigned i = 0; i < len; i++) {
        size_t a = p + i;
        if (a / BANK_SIZE < nbanks && (flags[a / BANK_SIZE][a % BANK_SIZE] & F_BODY)) return 1;
    }
    return 0;
}

/* Busca en la ROM las rutinas de DMA y se queda con las variantes concretas, de la más
 * larga a la más corta, sin las que solo aparecen al final de otra */
static void find_hram_sigs(void)
{
    HramSig cand[HRAM_MAXCANDS];
    struct { unsigned cand; size_t pos; } occ[HRAM_MAXOCCS];
    unsigned ncand = 0, nocc = 0;

    for (size_t p = 0; p < rom_size; p++) {
        unsigned len = match_dma(rom + p, rom_size - p);
        if (!len) continue;

        /* ld [c],a escribe en cualquier puerto (infrarrojos, sonido...). Si esos bytes son
         * código que la ROM ejecuta en su sitio, es una función normal y no una rutina que
         * el juego copie a HRAM. Con ldh ($46),a no hace falta: solo dispara el DMA */
        int via_c = 1;
        for (unsigned k = 0; k + 1 < len; k++)
            if (rom[p + k] == 0xE0 && rom[p + k + 1] == 0x46) via_c = 0;
        if (via_c && rom_has_code(p, len)) continue;

        HramSig t = { { 0 }, (u8)len, { 0 } };
        memcpy(t.bytes, rom + p, len);
        hram_blank_operands(&t);

        unsigned c = 0;
        while (c < ncand && !(cand[c].len == len && memcmp(cand[c].bytes, t.bytes, len) == 0)) c++;
        if (c == ncand) {
            if (ncand == HRAM_MAXCANDS) continue;
            cand[ncand++] = t;
        }
        if (nocc < HRAM_MAXOCCS) {
            occ[nocc].cand = c;
            occ[nocc].pos = p;
            nocc++;
        }
    }

    unsigned order[HRAM_MAXCANDS];    /* candidatas de la más larga a la más corta */
    for (unsigned i = 0; i < ncand; i++) {
        unsigned j = i;
        while (j > 0 && cand[order[j - 1]].len < cand[i].len) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }

    for (unsigned oi = 0; oi < ncand && n_hram_sigs < HRAM_MAXSIGS; oi++) {
        const HramSig *cs = &cand[order[oi]];
        int standalone = 0;
        for (unsigned o = 0; o < nocc && !standalone; o++) {
            if (occ[o].cand != order[oi]) continue;
            /* ¿Está dentro de otra más larga que termina en el mismo byte? */
            int covered = 0;
            for (unsigned q = 0; q < nocc && !covered; q++) {
                unsigned lq = cand[occ[q].cand].len;
                covered = lq > cs->len && occ[o].pos >= lq - cs->len &&
                          occ[q].pos == occ[o].pos - (lq - cs->len);
            }
            standalone = !covered;
        }
        if (!standalone) continue;
        HramSig *s = &hram_sigs[n_hram_sigs];
        snprintf(s->name, sizeof s->name, "d%u", n_hram_sigs);
        s->len = cs->len;
        memcpy(s->bytes, cs->bytes, cs->len);
        n_hram_sigs++;
    }
}

/* La función de una variante, válida para cualquier dirección de HRAM donde esté la copia:
 * la dirección no se graba en el código. Las etiquetas son desplazamientos y CHECK usa
 * base + desplazamiento, con base calculada en ejecución (hram_base). Se emite con HRAM_LO
 * como dirección de referencia; las instrucciones de estas rutinas no llevan direcciones
 * absolutas (no hay call, rst, halt ni stop). */
static void emit_hram_sig(FILE *f, const HramSig *s)
{
    HramRoutine r = { 0 };
    r.base = HRAM_LO;
    r.nstarts = hram_starts(s, r.starts);
    u8 pad[HRAM_MAXLEN + 3] = { 0 };
    memcpy(pad, s->bytes, s->len);

    u8 mask[HRAM_MAXLEN];
    hram_mask(s, mask);

    printf("rutina de DMA en HRAM:");
    for (unsigned k = 0; k < s->len; k++) {
        if (mask[k]) printf(" %02X", s->bytes[k]);
        else printf(" ??");
    }
    printf("\n");

    fprintf(f, "static const u8 hram_%s_bytes[%u] = {", s->name, (unsigned)s->len);
    for (unsigned i = 0; i < s->len; i++) fprintf(f, "%s0x%02X", i ? ", " : " ", s->bytes[i]);
    fprintf(f, " };\n");
    fprintf(f, "static const u8 hram_%s_mask[%u] = {", s->name, (unsigned)s->len);
    for (unsigned i = 0; i < s->len; i++) fprintf(f, "%s%u", i ? ", " : " ", (unsigned)mask[i]);
    fprintf(f, " };\n");
    fprintf(f, "static const u8 hram_%s_starts[%u] = {", s->name, r.nstarts);
    for (unsigned i = 0; i < r.nstarts; i++) fprintf(f, "%s%u", i ? ", " : " ", r.starts[i]);
    fprintf(f, " };\n\n");

    Emit e = { 0 };
    e.f = f;
    e.recomp = 1;
    e.jump = hram_jump;
    e.user = &r;
    fprintf(f, "static void hram_%s(GB *g)\n{\n    Cpu *c = &g->cpu;\n", s->name);
    fprintf(f, "    unsigned base = hram_base(g, c->pc, hram_%s_bytes, hram_%s_mask, %u, hram_%s_starts, %u);\n",
            s->name, s->name, (unsigned)s->len, s->name, r.nstarts);
    fprintf(f, "    switch (c->pc - base) {\n");
    for (unsigned i = 0; i < r.nstarts; i++)
        fprintf(f, "    case %u: goto ins%u;\n", r.starts[i], r.starts[i]);
    fprintf(f, "    default: gb_interp_step(g); return;\n    }\n");

    for (unsigned i = 0; i < r.nstarts; i++) {
        unsigned off = r.starts[i], addr = HRAM_LO + off;
        Insn in;
        char text[48], operand[48];
        sm83_decode(pad + off, (uint16_t)addr, &in);
        sm83_disasm(pad + off, (uint16_t)addr, text, sizeof text);
        memcpy(e.b, pad + off, 3);
        e.addr = (uint16_t)addr;

        /* ld a,n: el operando se lee de HRAM cuando la instrucción corre */
        e.imm8_expr = NULL;
        if (pad[off] == 0x3E) {
            snprintf(operand, sizeof operand, "g->hram[base + %u - 0xFF80]", off + 1);
            e.imm8_expr = operand;
            snprintf(text, sizeof text, "ld a, [operando en HRAM]");
        }

        fprintf(f, "ins%u: /* %s */\n", off, text);
        fprintf(f, "    CHECK(base + %u);\n", off);
        fprintf(f, "    {\n");
        e.indent = "        ";
        emit_insn(&e, e.b[0]);
        fprintf(f, "    }\n");

        if (in.kind == INS_HALT || in.kind == INS_STOP || sm83_ends_block(&in)) continue;
        unsigned next = addr + in.len;
        if (!hram_is_start(&r, next)) fprintf(f, "    c->pc = base + %u; return;\n", next - HRAM_LO);
    }
    fprintf(f, "}\n\n");
}

/* gen/hram.c: las funciones de cada variante presente y el lookup que las elige. Sin
 * ninguna, el lookup devuelve NULL y HRAM sigue en el intérprete. */
static void emit_hram(const char *dir)
{
    FILE *f = open_out(dir, "gen/hram.c");
    fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n#include <string.h>\n\n");

    find_hram_sigs();
    if (!n_hram_sigs) {
        printf("rutina de DMA en HRAM: no\n");
        fprintf(f, "RecompFn hram_lookup(GB *g, u16 pc)\n{\n    (void)g;\n    (void)pc;\n    return NULL;\n}\n");
        fclose(f);
        return;
    }

    /* Dónde está la copia que contiene pc: pc - inicio de instrucción, si esos bytes de HRAM
     * siguen siendo los reconocidos (salvo los operandos que se leen en ejecución, mask = 0).
     * Una copia distinta (o con otro código) cae al intérprete */
    fprintf(f,
        "static unsigned hram_base(GB *g, u16 pc, const u8 *bytes, const u8 *mask, unsigned len,\n"
        "                          const u8 *starts, unsigned nstarts)\n"
        "{\n"
        "    for (unsigned i = 0; i < nstarts; i++) {\n"
        "        u32 base = (u32)pc - starts[i];\n"
        "        if (base < 0xFF80 || base + len > 0xFFFF) continue;\n"
        "        const u8 *h = g->hram + (base - 0xFF80);\n"
        "        unsigned k = 0;\n"
        "        while (k < len && (!mask[k] || h[k] == bytes[k])) k++;\n"
        "        if (k == len) return base;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n");

    for (unsigned i = 0; i < n_hram_sigs; i++) emit_hram_sig(f, &hram_sigs[i]);

    fprintf(f, "RecompFn hram_lookup(GB *g, u16 pc)\n{\n");
    for (unsigned i = 0; i < n_hram_sigs; i++) {
        const HramSig *s = &hram_sigs[i];
        unsigned starts[HRAM_MAXLEN];
        unsigned n = hram_starts(s, starts);
        fprintf(f, "    if (hram_base(g, pc, hram_%s_bytes, hram_%s_mask, %u, hram_%s_starts, %u)) return hram_%s;\n",
                s->name, s->name, (unsigned)s->len, s->name, n, s->name);
    }
    fprintf(f, "    return NULL;\n}\n");
    fclose(f);
}

/* Código que el juego copia a WRAM o HRAM y ejecuta desde ahí (manejadores de interrupción,
 * trampolines de cambio de banco, saltos por vectores en RAM). No está en la ROM donde corre,
 * así que se aprende de la traza (ram_obs). Cada versión de una instrucción es un nodo. Los
 * nodos que van uno detrás de otro y no tienen ambigüedad forman un bloque, y cada bloque se
 * compila con sus direcciones, como el código de ROM. Donde una dirección tiene varias versiones
 * el bloque termina y el despachador elige la variante en ejecución. Un bloque se usa solo si
 * los bytes de RAM siguen siendo los suyos (ram_match); los operandos marcados como variables no
 * se comparan y se leen de RAM cuando la instrucción corre. Si el juego cambió el código, cae al
 * intérprete. */
#define RAM_MAXCHAIN 1024    /* instrucciones por bloque */
#define RAM_MAXVER 8         /* versiones de una dirección que se aceptan; más es código generado */

typedef struct {
    size_t *node;            /* índices en ram_obs, en orden de dirección */
    size_t n;
    unsigned start, len;     /* primera dirección y bytes que cubre */
} RamBlock;

/* Orden por dirección y por lo que identifica la instrucción (opcode; en jr y CB, el segundo byte) */
static int ram_obs_cmp(const void *a, const void *b)
{
    const RamObs *x = a, *y = b;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    if (x->bytes[0] != y->bytes[0]) return x->bytes[0] < y->bytes[0] ? -1 : 1;
    if (ram_key_len(x->bytes[0]) == 2 && x->bytes[1] != y->bytes[1]) return x->bytes[1] < y->bytes[1] ? -1 : 1;
    return 0;
}

/* La misma instrucción vista en corridas distintas: los operandos que difieren pasan a ser variables */
static void ram_merge(RamObs *into, const RamObs *from)
{
    for (unsigned i = ram_key_len(into->bytes[0]); i < into->len; i++)
        if (into->bytes[i] != from->bytes[i]) into->vol |= (u8)(1u << i);
    into->vol |= from->vol;
}

/* Primer nodo con dirección >= addr */
static size_t ram_lower(size_t n, unsigned addr)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (ram_obs[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int ram_block_has(const RamBlock *b, unsigned addr)
{
    for (size_t i = 0; i < b->n; i++)
        if (ram_obs[b->node[i]].addr == addr) return 1;
    return 0;
}

/* Saltos dentro del bloque van a su etiqueta; los demás salen al despachador */
static void ram_jump(Emit *e, const char *expr, int is_const, uint16_t target)
{
    const RamBlock *b = e->user;
    if (is_const && ram_block_has(b, target)) emit_line(e, "goto L_%04X;", target);
    else emit_line(e, "c->pc = %s; return;", expr);
}

static void emit_ram_block(FILE *f, size_t bi, const RamBlock *b)
{
    /* Los bytes de la guarda y cuáles se comparan (los operandos variables no) */
    fprintf(f, "static const u8 ram_b%zu_bytes[%u] = {", bi, b->len);
    unsigned k = 0;
    for (size_t i = 0; i < b->n; i++) {
        const RamObs *o = &ram_obs[b->node[i]];
        for (unsigned x = 0; x < o->len; x++, k++)
            fprintf(f, "%s0x%02X", k % 16 ? ", " : k ? ",\n    " : " ", o->bytes[x]);
    }
    fprintf(f, " };\n");
    fprintf(f, "static const u8 ram_b%zu_mask[%u] = {", bi, b->len);
    k = 0;
    for (size_t i = 0; i < b->n; i++) {
        const RamObs *o = &ram_obs[b->node[i]];
        for (unsigned x = 0; x < o->len; x++, k++)
            fprintf(f, "%s%u", k % 32 ? ", " : k ? ",\n    " : " ", (o->vol >> x & 1) ? 0u : 1u);
    }
    fprintf(f, " };\n\n");

    Emit e = { 0 };
    e.f = f;
    e.recomp = 1;
    e.jump = ram_jump;
    e.user = (void *)b;
    fprintf(f, "static void ram_b%zu(GB *g)\n{\n    Cpu *c = &g->cpu;\n    switch (c->pc) {\n", bi);
    for (size_t i = 0; i < b->n; i++) {
        unsigned a = ram_obs[b->node[i]].addr;
        fprintf(f, "    case 0x%04X: goto L_%04X;\n", a, a);
    }
    fprintf(f, "    default: gb_interp_step(g); return;\n    }\n");

    for (size_t i = 0; i < b->n; i++) {
        const RamObs *o = &ram_obs[b->node[i]];
        Insn in;
        char text[48], operand[160], lo[48], hi[48];
        sm83_decode(o->bytes, o->addr, &in);
        sm83_disasm(o->bytes, o->addr, text, sizeof text);
        memcpy(e.b, o->bytes, sizeof e.b);
        e.addr = o->addr;

        /* Operandos variables: se leen de RAM cuando la instrucción corre */
        e.imm8_expr = e.imm16_expr = NULL;
        if (o->len == 2 && (o->vol & 2)) {
            snprintf(operand, sizeof operand, "ram_peek(g, 0x%04X)", o->addr + 1);
            e.imm8_expr = operand;
        } else if (o->len == 3 && (o->vol & 6)) {
            if (o->vol & 2) snprintf(lo, sizeof lo, "ram_peek(g, 0x%04X)", o->addr + 1);
            else snprintf(lo, sizeof lo, "0x%02X", o->bytes[1]);
            if (o->vol & 4) snprintf(hi, sizeof hi, "ram_peek(g, 0x%04X)", o->addr + 2);
            else snprintf(hi, sizeof hi, "0x%02X", o->bytes[2]);
            snprintf(operand, sizeof operand, "(u16)(%s | %s << 8)", lo, hi);
            e.imm16_expr = operand;
        }

        fprintf(f, "L_%04X: /* %s */\n", o->addr, text);
        fprintf(f, "    CHECK(0x%04X);\n", o->addr);
        fprintf(f, "    {\n");
        e.indent = "        ";
        emit_insn(&e, e.b[0]);
        fprintf(f, "    }\n");

        if (in.kind == INS_HALT || in.kind == INS_STOP || sm83_ends_block(&in)) continue;
        if (i + 1 == b->n) fprintf(f, "    c->pc = 0x%04X; return;\n", (unsigned)(o->addr + o->len));
    }
    fprintf(f, "}\n\n");
}

/* gen/ram.c: los bloques aprendidos de la traza y el lookup que los elige. Sin trazas de RAM,
 * el lookup devuelve NULL y la RAM sigue en el intérprete. */
static void emit_ram(const char *dir)
{
    FILE *f = open_out(dir, "gen/ram.c");
    fprintf(f, "/* Generado por gbrecomp: no editar. */\n#include \"recomp.h\"\n#include <string.h>\n\n");

    /* Una misma instrucción vista en varias corridas se fusiona (ram_merge). Las direcciones con
     * demasiadas versiones son código que el juego genera con estructuras distintas y no se
     * aprenden */
    qsort(ram_obs, n_ram_obs, sizeof *ram_obs, ram_obs_cmp);
    size_t kept = 0, generated = 0;
    for (size_t i = 0; i < n_ram_obs;) {
        size_t j = i + 1;
        while (j < n_ram_obs && ram_obs_cmp(&ram_obs[i], &ram_obs[j]) == 0) ram_merge(&ram_obs[i], &ram_obs[j++]);
        for (unsigned x = 1; x < ram_obs[i].len; x++)
            if (ram_obs[i].vol >> x & 1) ram_obs[i].bytes[x] = 0;
        ram_obs[kept++] = ram_obs[i];
        i = j;
    }
    n_ram_obs = kept;
    kept = 0;
    for (size_t i = 0; i < n_ram_obs;) {
        size_t j = i + 1;
        while (j < n_ram_obs && ram_obs[j].addr == ram_obs[i].addr) j++;
        if (j - i <= RAM_MAXVER) for (size_t x = i; x < j; x++) ram_obs[kept++] = ram_obs[x];
        else generated += j - i;
        i = j;
    }
    n_ram_obs = kept;
    size_t n = n_ram_obs;

    /* Bloques: cadenas de nodos que van uno detrás de otro sin ambigüedad, esto es, cada uno tiene
     * un solo sucesor y ese sucesor un solo predecesor. No se cruza el límite entre los bancos de
     * WRAM ($D000), que no son contiguos en memoria */
    size_t *next = malloc((n ? n : 1) * sizeof *next);
    size_t *succ = malloc((n ? n : 1) * sizeof *succ);
    unsigned *succ_n = calloc(n ? n : 1, sizeof *succ_n);
    unsigned *pred_n = calloc(n ? n : 1, sizeof *pred_n);
    unsigned char *has_prev = calloc(n ? n : 1, 1), *seen = calloc(n ? n : 1, 1);
    RamBlock *blocks = calloc(n ? n : 1, sizeof *blocks);
    if (!next || !succ || !succ_n || !pred_n || !has_prev || !seen || !blocks) die("sin memoria");
    for (size_t i = 0; i < n; i++) {
        unsigned end = ram_obs[i].addr + ram_obs[i].len;
        succ[i] = ram_lower(n, end);
        while (succ[i] + succ_n[i] < n && ram_obs[succ[i] + succ_n[i]].addr == end) {
            pred_n[succ[i] + succ_n[i]]++;
            succ_n[i]++;
        }
    }
    for (size_t i = 0; i < n; i++) {
        next[i] = (size_t)-1;
        if (succ_n[i] != 1 || pred_n[succ[i]] != 1) continue;
        if ((ram_obs[i].addr < 0xD000) != (ram_obs[succ[i]].addr < 0xD000)) continue;
        next[i] = succ[i];
        has_prev[succ[i]] = 1;
    }
    size_t nblocks = 0;
    for (int pass = 0; pass < 2; pass++)    /* la segunda recoge lo que quedó tras cortar una cadena larga */
        for (size_t i = 0; i < n; i++) {
            if (seen[i] || (pass == 0 && has_prev[i])) continue;
            RamBlock *b = &blocks[nblocks++];
            b->node = malloc(RAM_MAXCHAIN * sizeof *b->node);
            if (!b->node) die("sin memoria");
            for (size_t cur = i; cur != (size_t)-1 && !seen[cur] && b->n < RAM_MAXCHAIN; cur = next[cur]) {
                b->node[b->n++] = cur;
                seen[cur] = 1;
            }
            const RamObs *first = &ram_obs[b->node[0]], *last = &ram_obs[b->node[b->n - 1]];
            b->start = first->addr;
            b->len = last->addr + last->len - first->addr;
        }

    if (!nblocks) {
        fprintf(f, "RecompFn ram_lookup(GB *g, u16 pc)\n{\n    (void)g;\n    (void)pc;\n    return NULL;\n}\n");
        fclose(f);
        free(next); free(succ); free(succ_n); free(pred_n); free(has_prev); free(seen); free(blocks);
        return;
    }
    size_t nvol = 0;
    for (size_t i = 0; i < n; i++) nvol += ram_obs[i].vol != 0;
    printf("código en WRAM/HRAM aprendido de la traza: %zu instrucciones (%zu con operandos variables) en %zu bloques",
           n, nvol, nblocks);
    if (generated) printf(" (%zu descartadas por cambiar de estructura)", generated);
    printf("\n");

    fprintf(f,
        "static inline const u8 *ram_ptr(GB *g, unsigned a)\n"
        "{\n"
        "    if (a >= 0xFF80) return g->hram + (a - 0xFF80);\n"
        "    if (a < 0xD000) return g->wram + (a - 0xC000);\n"
        "    return g->wram + g->wram_bank * 0x1000 + (a - 0xD000);\n"
        "}\n\n"
        "/* Un operando que el juego cambia: se lee cuando la instrucción corre */\n"
        "static inline u8 ram_peek(GB *g, unsigned a)\n"
        "{\n"
        "    return *ram_ptr(g, a);\n"
        "}\n\n"
        "/* ¿Siguen en RAM los bytes del bloque? Los marcados con mask = 0 son operandos variables */\n"
        "static int ram_match(GB *g, unsigned start, const u8 *bytes, const u8 *mask, unsigned len)\n"
        "{\n"
        "    const u8 *p = ram_ptr(g, start);\n"
        "    for (unsigned i = 0; i < len; i++)\n"
        "        if (mask[i] && p[i] != bytes[i]) return 0;\n"
        "    return 1;\n"
        "}\n\n");

    for (size_t bi = 0; bi < nblocks; bi++) emit_ram_block(f, bi, &blocks[bi]);

    fprintf(f, "static const struct { u16 start; u16 len; const u8 *bytes; const u8 *mask; RecompFn fn; } ram_blocks[%zu] = {\n", nblocks);
    for (size_t bi = 0; bi < nblocks; bi++)
        fprintf(f, "    { 0x%04X, %u, ram_b%zu_bytes, ram_b%zu_mask, ram_b%zu },\n", blocks[bi].start, blocks[bi].len, bi, bi, bi);
    fprintf(f, "};\n\n");

    /* Lookup: por cada dirección, los bloques que tienen una instrucción allí (varios si hay
     * versiones distintas); el primero cuya guarda coincide es el que corre */
    typedef struct { unsigned addr; size_t block; } AddrBlock;
    size_t npairs = 0;
    for (size_t bi = 0; bi < nblocks; bi++) npairs += blocks[bi].n;
    AddrBlock *pairs = malloc(npairs * sizeof *pairs);
    if (!pairs) die("sin memoria");
    npairs = 0;
    for (size_t bi = 0; bi < nblocks; bi++)
        for (size_t i = 0; i < blocks[bi].n; i++) {
            pairs[npairs].addr = ram_obs[blocks[bi].node[i]].addr;
            pairs[npairs++].block = bi;
        }
    for (size_t i = 1; i < npairs; i++) {    /* inserción: por dirección y luego por bloque */
        AddrBlock t = pairs[i];
        size_t j = i;
        while (j > 0 && (pairs[j - 1].addr > t.addr || (pairs[j - 1].addr == t.addr && pairs[j - 1].block > t.block))) {
            pairs[j] = pairs[j - 1];
            j--;
        }
        pairs[j] = t;
    }
    /* Listas de candidatos distintas */
    size_t *list_start = malloc(npairs * sizeof *list_start), *list_len = malloc(npairs * sizeof *list_len);
    size_t *addr_list = malloc(npairs * sizeof *addr_list), nlists = 0, naddr = 0;
    if (!list_start || !list_len || !addr_list) die("sin memoria");
    for (size_t i = 0; i < npairs;) {
        size_t j = i;
        while (j < npairs && pairs[j].addr == pairs[i].addr) j++;
        size_t l;
        for (l = 0; l < nlists; l++) {
            if (list_len[l] != j - i) continue;
            size_t x = 0;
            while (x < j - i && pairs[list_start[l] + x].block == pairs[i + x].block) x++;
            if (x == j - i) break;
        }
        if (l == nlists) {
            list_start[nlists] = i;
            list_len[nlists++] = j - i;
        }
        addr_list[naddr++] = l;
        i = j;
    }
    fprintf(f, "static const short ram_cands[%zu][%d] = {\n", nlists, RAM_MAXVER + 1);
    for (size_t l = 0; l < nlists; l++) {
        fprintf(f, "    {");
        for (size_t x = 0; x < list_len[l]; x++) fprintf(f, " %zu,", pairs[list_start[l] + x].block);
        fprintf(f, " -1 },\n");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "RecompFn ram_lookup(GB *g, u16 pc)\n{\n    unsigned k;\n    switch (pc) {");
    for (size_t l = 0; l < nlists; l++) {
        unsigned col = 0;
        for (size_t i = 0, ai = 0; i < npairs; ai++) {
            size_t j = i;
            while (j < npairs && pairs[j].addr == pairs[i].addr) j++;
            if (addr_list[ai] == l) fprintf(f, "%s    case 0x%04X:", col++ % 8 ? "" : "\n", pairs[i].addr);
            i = j;
        }
        fprintf(f, "\n        k = %zu;\n        break;", l);
    }
    fprintf(f,
        "\n    default:\n"
        "        return NULL;\n"
        "    }\n"
        "    for (const short *p = ram_cands[k]; *p >= 0; p++)\n"
        "        if (ram_match(g, ram_blocks[*p].start, ram_blocks[*p].bytes, ram_blocks[*p].mask, ram_blocks[*p].len))\n"
        "            return ram_blocks[*p].fn;\n"
        "    return NULL;\n"
        "}\n");
    fclose(f);
    free(pairs); free(list_start); free(list_len); free(addr_list);
    for (size_t bi = 0; bi < nblocks; bi++) free(blocks[bi].node);
    free(next); free(succ); free(succ_n); free(pred_n); free(has_prev); free(seen); free(blocks);
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

    fprintf(f, "RecompFn hram_lookup(GB *g, u16 pc);\nRecompFn ram_lookup(GB *g, u16 pc);\n\n");
    fprintf(f,
        "RecompFn recomp_lookup(GB *g, u16 pc)\n"
        "{\n"
        "    const RecompFn *const *table;\n"
        "    u32 off;\n"
        "    if (pc >= 0xFF80 && pc < 0xFFFF) {\n"
        "        RecompFn f = hram_lookup(g, pc);\n"
        "        return f ? f : ram_lookup(g, pc);\n"
        "    }\n"
        "    if (pc >= 0xC000 && pc < 0xE000) return ram_lookup(g, pc);\n"
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
        if (!strncmp(e->name, "tools/", 6)) continue;     /* lo usa gbrecomp, no el proyecto */
        snprintf(to, sizeof to, "%s/%s", dir, e->name);
        FILE *f = fopen(to, "wb");
        if (!f) die("no se pudo crear %s", to);
        if (fwrite(e->data, 1, e->size, f) != e->size) die("no se pudo escribir %s", to);
        fclose(f);
    }
}

/* Texto dentro de un <script type="text/plain">: todo menos '<', que podría cerrarlo */
static void put_script_text(FILE *f, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) if (s[i] != '<') fputc(s[i], f);
}

/* --html: el visor (tools/visor.html, que gbrecomp lleva adentro) con los eventos
 * y, si se dio, la cobertura adentro; se cargan solos al abrir la página. Como el
 * de nesrecomp --html: una página sola, sin servidor. */
static void write_html(const char *path, const char *title, const char *coverage)
{
    const EmbeddedFile *v = NULL;
    for (const EmbeddedFile *e = embedded_files; e->name; e++)
        if (!strcmp(e->name, "tools/visor.html")) v = e;
    if (!v) die("gbrecomp se compiló sin tools/visor.html");
    const char *html = (const char *)v->data;
    const char *end = strstr(html, "</body>");
    if (!end) die("tools/visor.html no tiene </body>");

    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    fwrite(html, 1, (size_t)(end - html), f);
    fputs("<script type=\"text/plain\" id=\"gbrecomp-eventos\">\n", f);
    ev_write_file(f);
    fputs("</script>\n", f);
    if (coverage) {
        FILE *c = fopen(coverage, "rb");
        if (!c) die("no se pudo leer %s", coverage);
        fputs("<script type=\"text/plain\" id=\"gbrecomp-cobertura\">\n", f);
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, c)) > 0) put_script_text(f, buf, n);
        fclose(c);
        fputs("</script>\n", f);
    }
    fputs("<script>\n"
          "cargarEventos(document.getElementById('gbrecomp-eventos').textContent, '", f);
    for (const char *p = title; *p; p++) fputc(*p == '<' || *p == '\'' ? '?' : *p, f);
    fputs("');\n", f);
    if (coverage)
        fputs("cargarCobertura(document.getElementById('gbrecomp-cobertura').textContent, 'cobertura');\n", f);
    fputs("</script>\n", f);
    fputs(end, f);
    if (fclose(f)) die("no se pudo escribir %s", path);
}

/* Ícono del .exe en Windows: el Game Boy Color para los juegos de color, el
 * Game Boy para los demás (la ventana cambia de ícono según la consola) */
static void emit_icon_rc(const char *dir)
{
    FILE *f = open_out(dir, "platform/icono.rc");
    fprintf(f, "/* Generado por gbrecomp: ícono del ejecutable en Windows */\n"
               "1 ICON \"%s\"\n", (rom[0x143] & 0x80) ? "icono_gbc.ico" : "icono_gb.ico");
    fclose(f);
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
        "      pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 make\n"
        "      make\n"
        "    El ejecutable queda enlazado de forma estática (no necesita DLLs); make dist lo deja en dist/.\n"
        "  Linux (Debian/Ubuntu):\n"
        "      sudo apt install build-essential pkg-config libsdl2-dev\n"
        "      make\n"
        "  macOS (Homebrew):\n"
        "      brew install sdl2 pkg-config\n"
        "      make\n"
        "  Con varios núcleos: make -j8\n"
        "\n"
        "Uso: %s [rom.gb] [--scale N] [--model dmg|cgb|sgb] [--palette up+a]\n"
        "            [--record archivo | --play archivo] [--interp] [--link]\n"
        "  Controles: flechas, Z = B, X = A, Enter = Start, Retroceso o Shift\n"
        "  derecho = Select, o un control (gamepad). La partida (.sav) se guarda\n"
        "  sola.\n"
        "  Jugador 2: WASD, G = A, F = B, E = Start, Q = Select, o un segundo\n"
        "  control. Cable link para 2 jugadores en el menú Juego (o --link);\n"
        "  en el Super Game Boy, los controles 2 a 4 para los juegos que los usan.\n"
        "  Menús (Alt o F10): reiniciar, pausa (P), estados guardados (F5 / F8,\n"
        "  cuatro ranuras), consola (Game Boy, Color o Super Game Boy), paleta\n"
        "  (F2), tamaño, pantalla completa (F11), sonido, exportar música y\n"
        "  gráficos (Ctrl+G, Ctrl+T, F12), idioma y ayuda (F1).\n"
        "\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest rom.gb --frames 6000 --seed 1\n"
        "\n"
        "El código en gen/ es una traducción del programa de la ROM: úsalo para\n"
        "jugar tu propia copia; no lo distribuyas si el juego es comercial.\n"
        "\n"
        "Íconos (platform/iconos.c, platform/icono_*.ico): Retro game console\n"
        "icons de KyleBing, https://github.com/KyleBing/retro-game-console-icons,\n"
        "con licencia CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/);\n"
        "recortados, centrados y escalados.\n",
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
        "# MSYS2 sin \"cc\" (por ejemplo MINGW64): se usa gcc\n"
        "ifeq ($(origin CC),default)\n"
        "CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)\n"
        "endif\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen\n\n"
        "# Windows (MSYS2): .exe, carpeta temporal en build/ (el make de MSYS2 no le pasa\n"
        "# TMP a gcc), SDL2 enlazado de forma estática (el .exe no pide SDL2.dll) e ícono\n"
        "ifeq ($(OS),Windows_NT)\n"
        "EXE := .exe\n"
        "ifneq ($(shell command -v cygpath 2>/dev/null),)\n"
        "TMPDIR_WIN := $(shell mkdir -p build/tmp && cygpath -m $(CURDIR)/build/tmp)\n"
        "export TMP  := $(TMPDIR_WIN)\n"
        "export TEMP := $(TMPDIR_WIN)\n"
        "endif\n"
        "SDL_LIBS := -static $(shell pkg-config --libs --static sdl2) -lcomdlg32\n"
        "ICON_OBJ := build/icono.o\n"
        "else\n"
        "SDL_LIBS := $(shell pkg-config --libs sdl2)\n"
        "ICON_OBJ :=\n"
        "endif\n"
        "SDL_CFLAGS := $(shell pkg-config --cflags sdl2)\n\n"
        "RT_OBJ  := $(patsubst runtime/%%.c,build/rt_%%.o,$(wildcard runtime/*.c))\n"
        "GEN_OBJ := $(patsubst gen/%%.c,build/gen_%%.o,$(wildcard gen/*.c))\n\n"
        ".PHONY: all clean dist\n"
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
        "# Windows: el ícono del .exe (platform/icono.rc)\n"
        "build/icono.o: platform/icono.rc $(wildcard platform/*.ico) | build\n"
        "\twindres -I platform $< -O coff -o $@\n\n"
        "%s$(EXE): $(PLAT_OBJ) $(RT_OBJ) $(GEN_OBJ) $(ICON_OBJ)\n"
        "\t$(CC) -o $@ $^ $(SDL_LIBS)\n\n"
        "# dist/: el ejecutable (y, si en Windows no quedó estático, las DLL de MSYS2 que pida); se puede copiar a otro equipo\n"
        "dist: %s$(EXE)\n"
        "\tmkdir -p dist\n"
        "\tcp $< dist/\n"
        "ifeq ($(OS),Windows_NT)\n"
        "\tldd $< | awk '/=> \\/(ucrt64|mingw64|mingw32|clang64|clangarm64)\\//{print $$3}' | xargs -r -I{} cp {} dist/\n"
        "endif\n\n"
        "# Prueba diferencial: código recompilado contra el intérprete\n"
        "difftest$(EXE): platform/difftest.c $(RT_OBJ) $(GEN_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^\n\n"
        "clean:\n\trm -rf build dist %s$(EXE) difftest$(EXE)\n",
        name, name, name, name);
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

/* ------------------------------------------------------------------ */
/* Cobertura (--coverage) y explicaciones (--why)                      */
/* ------------------------------------------------------------------ */

/* Lee una lista "BB:AAAA" y llama a `fn` con cada dirección de ROM válida */
static unsigned read_addr_list(const char *path, void (*fn)(u32 bank, u16 addr))
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir %s", path);
    unsigned bank, addr, n = 0;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%x:%x", &bank, &addr) != 2) continue;
        if (bank >= nbanks || addr > 0x7FFF || !in_region(bank, addr)) continue;
        fn(bank, (u16)addr);
        n++;
    }
    fclose(f);
    return n;
}

static unsigned cov_missing;
static char cov_list[10][16];    /* "BB:AAAA"; con 8 bytes se truncaba en ROMs de más de 4 MB (bancos desde $100) */

static void coverage_addr(u32 bank, u16 addr)
{
    viz_want(bank, addr);
    if (*flag_at(bank, addr) & F_INSN) return;
    if (cov_missing < 10) snprintf(cov_list[cov_missing], sizeof cov_list[0], "%02X:%04X", bank, addr);
    cov_missing++;
}

/* Instrucción en `bank:start`, desensamblada */
static void disasm_at(u32 bank, u16 start, char *text, size_t size)
{
    u8 b[3];
    Insn in;
    fetch_insn(bank, start, b, &in);
    sm83_disasm(b, start, text, size);
}

static const char *const why_origin[ORG_COUNT] = { "arranque", "interrupción", "tabla", "traza" };

/*
 * Sigue hacia atrás el registro de eventos: la instrucción que cubre la
 * dirección, el recorrido que la decodificó, quién agregó ese recorrido (una
 * instrucción o una entrada de tabla, y entonces quién encontró la tabla), y
 * así hasta un punto de entrada. Cada paso va a eventos anteriores, así que
 * termina.
 */
static void explain(u32 bank, u16 addr)
{
    size_t n;
    const Event *ev = ev_list(&n);
    printf("¿Por qué %02X:%04X es código?\n", bank, addr);
    size_t limit = n;
    char text[48];
    for (int step = 0;; step++) {
        size_t i = 0;
        for (; i < limit; i++)
            if (ev[i].type == EV_INSN && ev[i].bank == bank && ev[i].addr <= addr &&
                addr < ev[i].addr + ev[i].len)
                break;
        if (i == limit) {
            if (step == 0) {
                printf("  no se descubrió como código");
                if (in_region(bank, addr) && (*flag_at(bank, addr) & F_TABLE)) printf(" (es una entrada de tabla de punteros)");
                printf(".\n");
            } else {
                printf("  (no se encontró la instrucción %02X:%04X en el registro)\n", bank, addr);
            }
            return;
        }
        size_t w = i;
        while (w > 0 && ev[w].type != EV_WALK) w--;
        const Event *in = &ev[i], *walk = &ev[w];
        disasm_at(bank, in->addr, text, sizeof text);
        printf("  %02X:%04X  %-22s en el recorrido %02X:%04X\n", bank, in->addr, text, walk->bank, walk->addr);
        if (walk->bank2 == EV_NONE) {
            printf("  %02X:%04X  <- punto de entrada (%s)\n", walk->bank, walk->addr, why_origin[walk->origin]);
            return;
        }
        if (walk->aux) {
            /* Llegó por una entrada de tabla: buscar la tabla y quién la encontró */
            size_t t = w;
            while (t > 0 && !(ev[t].type == EV_TENTRY && ev[t].bank == walk->bank2 && ev[t].addr == walk->addr2)) t--;
            while (t > 0 && ev[t].type != EV_TABLE) t--;
            printf("  %02X:%04X  <- entrada %02X:%04X de la tabla %02X:%04X (%s)\n", walk->bank, walk->addr,
                   walk->bank2, walk->addr2, ev[t].bank, ev[t].addr, ev[t].aux ? "en línea" : "fuera de línea");
            if (ev[t].type != EV_TABLE || ev[t].bank2 == EV_NONE) {
                printf("  (no se sabe qué instrucción encontró la tabla)\n");
                return;
            }
            bank = ev[t].bank2;
            addr = ev[t].addr2;
            limit = t;
        } else {
            printf("  %02X:%04X  <- salto desde %02X:%04X\n", walk->bank, walk->addr, walk->bank2, walk->addr2);
            bank = walk->bank2;
            addr = walk->addr2;
            limit = w;
        }
    }
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
    const char *rom_path = NULL, *out = NULL, *name = NULL, *codemap = NULL;
    const char *traces[32];     /* --trace, se puede repetir */
    int ntraces = 0;
    const char *dis = NULL;     /* --dis BB:AAAA:N */
    const char *events = NULL, *coverage = NULL, *html = NULL;
    const char *why[16];
    int nwhy = 0;
    int viz = 0;
    double viz_time = 5;

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
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            if (ntraces == 32) die("--trace: como mucho 32 archivos");
            traces[ntraces++] = argv[++i];
        }
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--codemap") && i + 1 < argc) codemap = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--events") && i + 1 < argc) events = argv[++i];
        else if (!strcmp(argv[i], "--viz")) viz = 1;
        else if (!strcmp(argv[i], "--color")) viz_force_color = 1;
        else if (!strcmp(argv[i], "--html") && i + 1 < argc) html = argv[++i];
        else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) coverage = argv[++i];
        else if (!strcmp(argv[i], "--why") && i + 1 < argc) {
            if (nwhy == 16) die("--why: como mucho 16 direcciones");
            why[nwhy++] = argv[++i];
        }
        else if (!strcmp(argv[i], "--viz-time") && i + 1 < argc) viz = 1, viz_time = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dis") && i + 1 < argc) dis = argv[++i];
        else if (argv[i][0] == '-') die("opción desconocida: %s (ver gbrecomp --help)", argv[i]);
        else rom_path = argv[i];
    }
    /* Sin -o basta con pedir algo que no sea el proyecto (como en nesrecomp) */
    if (!rom_path || (!out && !dis && !nwhy && !viz && !events && !html && !codemap && !coverage)) {
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
    ev_on = events || viz || nwhy || html;
    ev_add(EV_PHASE, 0, 0, 0, 0, 0, 0, PH_VECTORS);
    discover();
    if (ntraces) {
        load_traces(traces, ntraces);
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
    if (events && !ev_write(events)) die("no se pudo escribir %s", events);
    if (html) {
        write_html(html, title, coverage);
        printf("visor escrito en %s\n", html);
    }
    unsigned cov_total = coverage ? read_addr_list(coverage, coverage_addr) : 0;
    if (viz) viz_play(nbanks, title, viz_time);
    if (coverage) {
        printf("cobertura: %u direcciones ejecutadas, %u sin descubrir", cov_total, cov_missing);
        for (unsigned k = 0; k < cov_missing && k < 10; k++) printf(k ? ", %s" : " (%s", cov_list[k]);
        printf(cov_missing > 10 ? ", ...)\n" : cov_missing ? ")\n" : "\n");
    }
    for (int k = 0; k < nwhy; k++) {
        unsigned wb, wa;
        if (sscanf(why[k], "%x:%x", &wb, &wa) != 2 || wb >= nbanks || !in_region(wb, wa))
            die("--why espera BANCO:DIRECCIÓN en hexadecimal (%s)", why[k]);
        explain(wb, (u16)wa);
    }
    if (!out) return 0;

    /* 3. Proyecto */
    char path[1024];
    make_dir(out);
    snprintf(path, sizeof path, "%s/gen", out); make_dir(path);
    snprintf(path, sizeof path, "%s/runtime", out); make_dir(path);
    snprintf(path, sizeof path, "%s/platform", out); make_dir(path);

    emit_code(out);
    emit_hram(out);
    emit_ram(out);
    emit_dispatch(out);
    emit_info(out, sha1, title, exe_name, ninsns);
    emit_makefile(out, exe_name);
    emit_icon_rc(out);
    emit_readme(out, exe_name, title, sha1);
    write_embedded(out);

    printf("ROM: %s (%s), %u bancos, SHA1 %s\n", rom_path, title, nbanks, sha1);
    printf("código: %u instrucciones (%u bytes, %.2f%% de la ROM) en %zu funciones\n",
           ninsns, nbytes, 100.0 * nbytes / rom_size, nfuncs);
    if (stat_tramp_calls)
        printf("llamadas a trampolines de banco resueltas: %u\n", stat_tramp_calls);
    if (stat_bank_setters)
        printf("llamadas a fijadores de banco: %u\n", stat_bank_setters);
    if (stat_table_jumps)
        printf("saltos por tabla con base en HL: %u\n", stat_table_jumps);
    if (stat_jp_rows)
        printf("saltos en filas de jp (tablas de vectores): %u\n", stat_jp_rows);
    if (stat_iface_calls)
        printf("llamadas a la interfaz común de varios bancos: %u\n", stat_iface_calls);
    if (stat_ram_ptr_targets)
        printf("destinos de punteros a código guardados en RAM: %u\n", stat_ram_ptr_targets);
    if (stat_sim_targets)
        printf("saltos resueltos simulando un despachador con tabla en la ROM: %u (y %u entradas de sus tablas)\n",
               stat_sim_targets, stat_sim_entries);
    if (stat_inline_calls)
        printf("llamadas con argumentos en línea: %u (%u con destino en los argumentos)\n",
               stat_inline_calls, stat_inline_targets);
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
