/*
 * nesrecomp: recompilador estático de ROMs de NES a C99.
 *
 * Uso: nesrecomp juego.nes -o salida/ [--trace traza.txt] [--name nombre]
 *
 * Genera un proyecto completo (código recompilado, runtime, plataforma y
 * Makefile). El proyecto no contiene datos de la ROM: el ejecutable carga la
 * ROM del usuario y valida su SHA1.
 *
 * Pasos:
 *   1. Descubrimiento: recorrido recursivo desde los vectores (RESET, NMI, IRQ)
 *      siguiendo saltos, llamadas y bifurcaciones con destino constante. Las
 *      instrucciones que ejecutó el juego (--trace, ver nestrace) se agregan
 *      como puntos de entrada: así se resuelven los saltos indirectos y los
 *      bancos intercambiables.
 *   2. Partición en funciones: tramos contiguos de PRG que no cruzan una
 *      ventana de banco.
 *   3. Emisión de C. Cada instrucción hace los mismos accesos al bus, en el
 *      mismo orden, que el intérprete; las operaciones son las de cpu_ops.h.
 *
 * Las instrucciones se identifican por su desplazamiento en la ROM, no por la
 * dirección del CPU: lo que no se descubrió lo ejecuta el intérprete.
 * Descubrir de más no rompe nada; de menos, tampoco. El código que el juego
 * copia a la RAM (la interna y la del cartucho) se aprende de las líneas
 * "ram" de la traza y va a gen/ram.c con una guarda (emit_ram).
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include <ctype.h>
#include <dirent.h>
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

#include "../runtime/cpu_ops.h"
#include "../runtime/console.h"
#include "sha1.h"
#include "trace_line.h"
#include "ram_code.h"
#include "embedded.h"

#ifndef NESRECOMP_VERSION
#define NESRECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "nesrecomp " NESRECOMP_VERSION ": recompila una ROM de NES a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: nesrecomp juego.nes -o salida/ [opciones]\n"
    "  -o carpeta         dónde escribir el proyecto (se crea si no existe)\n"
    "  --name nombre      nombre del ejecutable (por omisión, el de la ROM)\n"
    "  --trace archivo    instrucciones que ejecutó el juego (las escribe nestrace o\n"
    "                     la ventana con NES_TRACE=archivo); se agregan como entradas\n"
    "                     (se puede repetir)\n"
    "  --coverage archivo compara lo descubierto con las instrucciones que ejecutó el juego\n"
    "                     (una traza): cuántas se encontraron sin ayuda y cuáles faltan\n"
    "  --why BB:DIRECCION explica por qué esa instrucción (banco y dirección como en la traza, o su\n"
    "                     desplazamiento hexadecimal en el PRG) se tomó como código: la cadena de saltos,\n"
    "                     tablas y bancos desde un punto de entrada (se puede repetir; sin -o no genera el\n"
    "                     proyecto)\n"
    "  --viz              mapa del PRG en la terminal: una celda por 256 bytes, coloreada por cómo se\n"
    "                     descubrió (con --coverage, en rojo lo que el juego ejecutó y no se encontró)\n"
    "  --color            con --viz, colorea aunque la salida no sea una terminal (lo usa la GUI)\n"
    "  --html archivo     el descubrimiento como una página HTML sola: mapa del PRG, instrucciones por\n"
    "                     celda y la cadena de \"por qué\" al hacer clic (con --coverage marca lo que falta)\n"
    "  --events archivo   cada instrucción descubierta con su origen (texto: desplazamiento, dirección,\n"
    "                     cómo se llegó, de dónde, origen, desensamblado)\n"
    "  --codemap archivo  qué bytes del PRG se tomaron como código: una letra por byte (C = código),\n"
    "                     64 por línea; el mismo formato que difftest --executed y gbrecomp --codemap\n"
    "  --no-tables        sin las heurísticas de tablas de saltos (para comparar)\n"
    "  --no-spec          sin el descubrimiento especulativo en todos los bancos (para comparar)\n"
    "  --dis OFFSET:DIR:N desensambla N instrucciones desde ese byte de PRG\n"
    "  --verbose          detalles del descubrimiento\n"
    "  --version, --help\n";

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    u16 addr;           /* dirección del CPU con la que se decodificó */
    u8  state;          /* 0 = sin descubrir, 1 = instrucción */
    u8  opc;
    u8  seed;           /* punto de entrada (vector o traza) */
} Site;

static NES nes;                 /* solo para la cabecera y las consultas del mapper */
static Cart *cart;
static u8 *rom_file;
static size_t rom_file_size;
static Site *site;
static size_t prg_size;
static int verbose;
static int use_tables = 1;
static int use_spec = 1;

/* Cómo se llegó a una instrucción (para --why, --viz y --events) */
enum { HOW_VECTOR, HOW_TRACE, HOW_LINEAR, HOW_JMP, HOW_JSR, HOW_BRANCH, HOW_TABLE, HOW_SPEC, HOW_PAIR, HOW_RUN, HOW_COUNT };
static const char *const how_name[HOW_COUNT] = { "vector", "traza", "sigue", "jmp", "jsr", "bifurcación", "tabla", "especulativo",
                                                 "par de cargas", "tabla suelta" };
enum { ORG_FLOW, ORG_TRACE, ORG_TABLE, ORG_SPEC, ORG_COUNT };
static const char *const org_name[ORG_COUNT] = { "flujo desde los vectores", "traza", "tabla de saltos", "especulativo (todos los bancos)" };
#define NO_PARENT ((size_t)-1)
static size_t cur_from = NO_PARENT;      /* instrucción que está haciendo el descubrimiento */
static int cur_how = HOW_JMP;
static size_t *parent;                   /* por desplazamiento: de qué instrucción se llegó */
static u8 *how_at, *origin;

typedef struct { size_t off; u16 addr; size_t from; u8 how; } Work;
static Work *work;
static size_t nwork, capwork;

static unsigned stat_bytes, stat_insns, stat_unresolved, stat_ind, stat_conflicts, stat_seeds, stat_trace;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "nesrecomp: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Opcodes oficiales (los NOP y el SBC $EB no oficiales cuentan como no oficiales) */
static int opc_official(u8 opc)
{
    int op = ins_tab[opc].op;
    if (op > TYA) return 0;
    if (op == NOP && opc != 0xEA) return 0;
    if (op == SBC && opc == 0xEB) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Descubrimiento                                                      */
/* ------------------------------------------------------------------ */

static void push_work(size_t off, u16 addr)
{
    if (off >= prg_size) return;
    if (site[off].state) return;
    if (nwork == capwork) {
        capwork = capwork ? capwork * 2 : 1024;
        work = realloc(work, capwork * sizeof *work);
    }
    work[nwork++] = (Work){ off, addr, cur_from, (u8)cur_how };
}

/* Desplazamiento en PRG de un destino 'target' visto desde la instrucción en (src_off, src_addr):
 * en la misma ventana por aritmética; en otra, solo si es fija. -1 si no se puede saber. */
static long resolve(size_t src_off, u16 src_addr, u16 target)
{
    PrgWin w, tw;
    target = mapper_prg_canon(cart, target);
    if (target < 0x8000) return -1;
    if (!mapper_prg_window(cart, src_addr, &w)) return -1;
    if (target >= w.start && (u32)(target - w.start) < w.size)
        return (long)src_off + ((int)target - (int)src_addr);
    if (!mapper_prg_window(cart, target, &tw) || !tw.fixed) return -1;
    return (long)(tw.off + (target - tw.start));
}

/* ¿Esta dirección de E/S o RAM es una que un programa usaría? (filtra operandos al azar) */
static int sane_data_addr(u16 a)
{
    if (a < 0x2000) return 1;                    /* RAM y espejos */
    if (a < 0x4000) return 1;                    /* PPU */
    if (a < 0x4018) return 1;                    /* APU y controles */
    if (a < 0x6000) return 0;                    /* no mapeado (salvo mappers raros) */
    return 1;                                    /* RAM del cartucho y ROM */
}

/* Decodifica desde (off, addr) sin guardar nada: ¿parece código de verdad? Se exige una secuencia
 * de opcodes oficiales, con destinos de salto que caigan en la ROM, que termine en un RTS/RTI/JMP o
 * dure lo bastante. Los datos al azar casi nunca pasan. */
static int is_inline_dispatcher(size_t off, u16 addr);

static int plausible(size_t off, u16 addr)
{
    PrgWin w;
    if (!mapper_prg_window(cart, addr, &w)) return 0;
    for (int n = 0; n < 48; n++) {
        if (off >= prg_size) return 0;
        u8 opc = cart->prg[off];
        const Ins *in = &ins_tab[opc];
        int len = mode_len[in->mode];
        if (!opc_official(opc) || in->op == JAM || in->op == BRK) return 0;
        if ((u32)(addr - w.start) + len > w.size || off + len > prg_size) return 0;
        u16 imm = (len > 1 ? cart->prg[off + 1] : 0) | (len > 2 ? cart->prg[off + 2] << 8 : 0);
        switch (in->mode) {
        case ABS: case ABX: case ABY:
            if (in->op == JMP || in->op == JSR) { if (imm < 0x8000) return 0; }
            else if (!sane_data_addr(imm)) return 0;
            break;
        case IND: if (!sane_data_addr(imm) || imm >= 0x8000) return 0; break;   /* el puntero está en RAM */
        case REL: {
            u16 t = (u16)(addr + 2 + (int8_t)cart->prg[off + 1]);
            if (t < w.start || (u32)(t - w.start) >= w.size) return 0;
            break; }
        }
        if (in->op == RTS || in->op == RTI) return 1;
        if (in->op == JMP) return in->mode == ABS || n >= 1;     /* un JMP solo (vector de entrada) sí cuenta */
        if (in->op == JSR && use_tables) {           /* una llamada a un despachador: lo que sigue es una tabla */
            long t = resolve(off, addr, imm);
            if (t >= 0 && is_inline_dispatcher((size_t)t, mapper_prg_canon(cart, imm))) return 1;
        }
        off += len; addr += len;
    }
    return 1;
}

static unsigned stat_spec, stat_spec_hits;
static u8 spec_done[0x10000 / 8];

/* Un destino en una ventana intercambiable al que se llega desde otra ventana: no se sabe qué banco
 * habrá. Se prueban todos y se queda con los que parecen código. */
static void spec_target(u16 target)
{
    PrgWin tw;
    target = mapper_prg_canon(cart, target);
    if (!use_spec || target < 0x8000 || !mapper_prg_window(cart, target, &tw) || tw.fixed) return;
    if (spec_done[target >> 3] & (1u << (target & 7))) return;
    spec_done[target >> 3] |= (u8)(1u << (target & 7));
    stat_spec++;
    int save_how = cur_how;
    cur_how = HOW_SPEC;
    for (size_t base = 0; base + tw.size <= prg_size; base += tw.size) {
        size_t off = base + (target - tw.start);
        if (off < prg_size && !site[off].state && plausible(off, target)) {
            push_work(off, target);
            stat_spec_hits++;
        }
    }
    cur_how = save_how;
}

static void follow(size_t off, u16 addr, u16 target)
{
    long t = resolve(off, addr, target);
    if (t < 0) { stat_unresolved++; spec_target(target); return; }
    push_work(t, mapper_prg_canon(cart, target));
}

/* ------------------------------------------------------------------ */
/* Tablas de saltos                                                    */
/* ------------------------------------------------------------------ */

/* Las últimas instrucciones del recorrido actual: sirven para reconocer cómo se arma un salto
 * indirecto (una tabla indexada, el truco de apilar la dirección y hacer RTS, etc.) */
static size_t hist[16];
static int nhist;
static unsigned stat_tables, stat_table_entries, stat_inline;

static void hist_push(size_t off)
{
    if (nhist == 16) { memmove(hist, hist + 1, 15 * sizeof *hist); nhist = 15; }
    hist[nhist++] = off;
}

static int ins_op(size_t off) { return ins_tab[site[off].opc].op; }
static int ins_mode(size_t off) { return ins_tab[site[off].opc].mode; }
static u16 ins_operand(size_t off)
{
    int len = mode_len[ins_mode(off)];
    return (len > 1 && off + 1 < prg_size ? cart->prg[off + 1] : 0) |
           (len > 2 && off + 2 < prg_size ? cart->prg[off + 2] << 8 : 0);
}

/* Un destino de tabla: ¿tiene pinta de ser el inicio de una rutina? Resuelve el desplazamiento. */
static int table_target(size_t tab_off, u16 tab_addr, u16 v, long *t)
{
    if (v < 0x8000) return 0;
    *t = resolve(tab_off, tab_addr, v);
    if (*t < 0) return 1;                        /* en otra ventana: se prueba en todos los bancos */
    if ((size_t)*t >= prg_size) return 0;
    u8 opc = cart->prg[*t];
    if (!opc_official(opc) || ins_tab[opc].op == JAM || ins_tab[opc].op == BRK) return 0;
    return plausible((size_t)*t, mapper_prg_canon(cart, v));
}

/* Lee una tabla de direcciones. Interleaved: palabras de 16 bits seguidas. Si no, un arreglo de bajos y
 * otro de altos. plus1: son direcciones menos uno (se usan con RTS). 'max' limita la cuenta. */
static void scan_table(size_t src_off, u16 src_addr, u16 lo_base, u16 hi_base, int interleaved, int plus1, int max)
{
    cur_how = HOW_TABLE;
    long lo_o = resolve(src_off, src_addr, lo_base);
    long hi_o = interleaved ? lo_o + 1 : resolve(src_off, src_addr, hi_base);
    if (lo_o < 0 || hi_o < 0) return;
    if (!interleaved && hi_base != lo_base) {
        int span = hi_base > lo_base ? hi_base - lo_base : lo_base - hi_base;
        if (span < max) max = span;               /* los dos arreglos suelen ir pegados */
    }
    int step = interleaved ? 2 : 1, good = 0;
    for (int i = 0; i < max; i++) {
        size_t a = (size_t)lo_o + (size_t)i * step, b = (size_t)hi_o + (size_t)i * step;
        if (a >= prg_size || b >= prg_size) break;
        u16 v = (u16)((cart->prg[a] | (cart->prg[b] << 8)) + plus1);
        long t;
        if (!table_target((size_t)lo_o, lo_base, v, &t)) break;
        if (t >= 0) push_work(t, mapper_prg_canon(cart, v)); else spec_target(v);
        good++;
    }
    if (good) { stat_tables++; stat_table_entries += good; }
}

/* ¿Es esta rutina un "despachador con tabla en línea"? Saca de la pila la dirección de retorno (dos PLA),
 * la usa como puntero (LDA (zp),Y) y salta con JMP (ind). Las llamadas a ella van seguidas de la tabla. */
static signed char disp_cache[0x10000 / 1];       /* por dirección canónica de entrada: 0 sin ver, 1 sí, -1 no */

static int is_inline_dispatcher(size_t off, u16 addr)
{
    if (disp_cache[addr]) return disp_cache[addr] > 0;
    int pla = 0, izy = 0, found = 0;
    PrgWin w;
    size_t o = off; u16 a = addr;
    for (int n = 0; n < 40 && mapper_prg_window(cart, a, &w) && o < prg_size; n++) {
        u8 opc = cart->prg[o];
        const Ins *in = &ins_tab[opc];
        int len = mode_len[in->mode];
        if (!opc_official(opc) || (u32)(a - w.start) + len > w.size) break;
        if (in->op == PLA) pla++;
        if (in->op == LDA && in->mode == IZY) izy++;
        if (in->op == JMP) { found = in->mode == IND; break; }
        if (in->op == RTS || in->op == RTI) break;
        o += len; a += len;
    }
    int yes = found && pla >= 2 && izy >= 1;
    disp_cache[addr] = yes ? 1 : -1;
    return yes;
}

/* La tabla que sigue a un JSR a un despachador con tabla en línea */
static void scan_inline_table(size_t jsr_off, u16 jsr_addr)
{
    cur_how = HOW_TABLE;
    size_t o = jsr_off + 3;
    u16 a = (u16)(jsr_addr + 3);
    int good = 0;
    for (int i = 0; i < 128 && o + 1 < prg_size; i++, o += 2, a += 2) {
        if (site[o].state) break;                  /* ya es código: terminó la tabla */
        u16 v = cart->prg[o] | (cart->prg[o + 1] << 8);
        long t;
        if (!table_target(o, a, v, &t)) break;
        if (t >= 0) push_work(t, mapper_prg_canon(cart, v)); else spec_target(v);
        good++;
    }
    if (good) { stat_inline++; stat_table_entries += good; }
}

/* JMP (ptr): ¿de dónde sale el puntero? Busca "LDA tabla,X / STA ptr / LDA tabla2,X / STA ptr+1". */
static u8 ind_ptr_seen[0x10000 / 8];     /* operandos de JMP (ind) vistos */
static int ind_ptr(u16 m) { return (ind_ptr_seen[m >> 3] >> (m & 7)) & 1; }

static void analyze_ind_jump(size_t off, u16 addr, u16 ptr)
{
    ind_ptr_seen[ptr >> 3] |= (u8)(1u << (ptr & 7));
    int i_lo = -1, i_hi = -1;
    for (int i = nhist - 2; i >= 0; i--) {         /* de lo más reciente a lo más antiguo (hist[nhist-1] es el JMP) */
        size_t h = hist[i];
        if (ins_op(h) != STA || (ins_mode(h) != ZP && ins_mode(h) != ABS)) continue;
        u16 m = ins_operand(h);
        if (m == ptr && i_lo < 0) i_lo = i;
        if (m == (u16)(ptr + 1) && i_hi < 0) i_hi = i;
    }
    if (i_lo < 1 || i_hi < 1) return;
    size_t l = hist[i_lo - 1], h = hist[i_hi - 1];
    int lm = ins_mode(l), hm = ins_mode(h);
    if (ins_op(l) != LDA || ins_op(h) != LDA) return;
    if ((lm == ABX || lm == ABY) && (hm == ABX || hm == ABY)) {
        u16 lb = ins_operand(l), hb = ins_operand(h);
        if (hb == (u16)(lb + 1)) scan_table(off, addr, lb, hb, 1, 0, 128);
        else scan_table(off, addr, lb, hb, 0, 0, 128);
    }
}

/* RTS tras apilar una dirección: "LDA alto,X / PHA / LDA bajo,X / PHA / RTS" (la dirección va menos uno) */
static void analyze_rts_trick(size_t off, u16 addr)
{
    cur_how = HOW_TABLE;
    int p2 = -1, p1 = -1;
    for (int i = nhist - 2; i >= 1 && i >= nhist - 10; i--) {
        if (ins_op(hist[i]) != PHA) continue;
        if (p2 < 0) p2 = i; else { p1 = i; break; }
    }
    if (p1 < 1 || p2 < 1) return;
    size_t lo = hist[p2 - 1], hi = hist[p1 - 1];
    if (ins_op(lo) != LDA || ins_op(hi) != LDA) return;
    int lm = ins_mode(lo), hm = ins_mode(hi);
    if ((lm == ABX || lm == ABY) && (hm == ABX || hm == ABY)) {
        scan_table(off, addr, ins_operand(lo), ins_operand(hi), 0, 1, 128);
    } else if (lm == IMM && hm == IMM) {
        u16 v = (u16)((ins_operand(lo) | (ins_operand(hi) << 8)) + 1);
        long t;
        if (table_target(off, addr, v, &t)) { if (t >= 0) push_work(t, mapper_prg_canon(cart, v)); else spec_target(v); }
    }
}

/* Pares de cargas inmediatas que arman un puntero para un JMP (ind):
 *   LDA #<rutina / STA ptr / LDA #>rutina / STA ptr+1   (en cualquier orden de las mitades) */
static unsigned stat_pairs;

static void scan_immediate_pairs(void)
{
    cur_how = HOW_PAIR;
    for (size_t i = 0; i < prg_size; i++) {
        if (!site[i].state || ins_op(i) != LDA || ins_mode(i) != IMM) continue;
        size_t b = i + 2;
        if (b >= prg_size || !site[b].state || ins_op(b) != STA || (ins_mode(b) != ZP && ins_mode(b) != ABS)) continue;
        size_t c = b + mode_len[ins_mode(b)];
        if (c >= prg_size || !site[c].state || ins_op(c) != LDA || ins_mode(c) != IMM) continue;
        size_t d = c + 2;
        if (d >= prg_size || !site[d].state || ins_op(d) != STA || (ins_mode(d) != ZP && ins_mode(d) != ABS)) continue;
        u16 m1 = ins_operand(b), m2 = ins_operand(d);
        u16 v1 = ins_operand(i), v2 = ins_operand(c);
        u16 v;
        if (m2 == (u16)(m1 + 1) && ind_ptr(m1)) v = (u16)((v1 & 0xFF) | (v2 << 8));
        else if (m1 == (u16)(m2 + 1) && ind_ptr(m2)) v = (u16)((v2 & 0xFF) | (v1 << 8));
        else continue;
        long t;
        cur_from = i;
        if (!table_target(i, site[i].addr, v, &t)) continue;
        if (t >= 0) { if (!site[t].state) { push_work(t, mapper_prg_canon(cart, v)); stat_pairs++; } }
        else spec_target(v);
    }
}

/* Tablas de direcciones que ningún patrón de código delata: cuatro o más palabras seguidas que apuntan, todas,
 * a algo con pinta de rutina. Los datos al azar casi nunca lo cumplen. */
static unsigned stat_runs, stat_run_entries;

static void scan_pointer_runs(void)
{
    cur_how = HOW_RUN; cur_from = NO_PARENT;
    size_t gran = mapper_prg_granule(cart);
    for (u32 s = 0x8000; s < 0x10000; s += (u32)gran) {
        PrgWin w;
        if (!mapper_prg_window(cart, (u16)s, &w) || w.start != s) continue;
        for (size_t base = w.fixed ? w.off : 0; base + w.size <= prg_size; base += w.size) {
            for (int parity = 0; parity < 2; parity++) {
                int run = 0;
                size_t run_start = 0;
                for (size_t o = base + parity; o + 1 < base + w.size; o += 2) {
                    u16 v = cart->prg[o] | (cart->prg[o + 1] << 8);
                    u16 a = (u16)(s + (o - base));
                    long t;
                    if (table_target(o, a, v, &t)) {
                        if (run == 0) run_start = o;
                        run++;
                        if (run == 4) {                          /* se confirma: se agregan las cuatro */
                            stat_runs++;
                            for (int k = 0; k < 4; k++) {
                                size_t ko = run_start + (size_t)k * 2;
                                u16 kv = cart->prg[ko] | (cart->prg[ko + 1] << 8);
                                long kt;
                                u16 ka = (u16)(s + (ko - base));
                                if (table_target(ko, ka, kv, &kt)) {
                                    if (kt >= 0) push_work(kt, mapper_prg_canon(cart, kv)); else spec_target(kv);
                                    stat_run_entries++;
                                }
                            }
                        } else if (run > 4) {
                            if (t >= 0) push_work(t, mapper_prg_canon(cart, v)); else spec_target(v);
                            stat_run_entries++;
                        }
                    } else run = 0;
                    if (w.fixed && 0) break;
                }
            }
            if (w.fixed) break;
        }
    }
}

static int runs_done;

static void resolve_tables(void)
{
    if (!runs_done) { runs_done = 1; scan_pointer_runs(); }
    scan_immediate_pairs();
    cur_from = NO_PARENT;
}

/* Recorre desde (off, addr) hasta encontrar un final de camino */
static void walk(Work wk)
{
    PrgWin w;
    size_t off = wk.off, prev = NO_PARENT;
    u16 addr = wk.addr;
    nhist = 0;
    for (;;) {
        if (off >= prg_size) return;
        if (site[off].state) {
            if (site[off].addr != addr) stat_conflicts++;
            return;
        }
        if (!mapper_prg_window(cart, addr, &w)) return;
        u8 opc = cart->prg[off];
        const Ins *in = &ins_tab[opc];
        int len = mode_len[in->mode];
        if (!opc_official(opc) && !site[off].seed) return;   /* datos, casi seguro */
        if (in->op == JAM) return;
        if ((u32)(addr - w.start) + len > w.size || off + len > prg_size) return;
        site[off] = (Site){ addr, 1, opc, site[off].seed };
        stat_insns++; stat_bytes += len;
        if (prev == NO_PARENT) {
            parent[off] = wk.from; how_at[off] = wk.how;
            origin[off] = wk.from != NO_PARENT ? (wk.how == HOW_TABLE ? ORG_TABLE : wk.how == HOW_SPEC ? ORG_SPEC :
                          wk.how == HOW_PAIR || wk.how == HOW_RUN ? ORG_TABLE : origin[wk.from])
                          : (wk.how == HOW_TRACE ? ORG_TRACE : wk.how == HOW_SPEC ? ORG_SPEC : wk.how == HOW_RUN ? ORG_TABLE : ORG_FLOW);
        } else {
            parent[off] = prev; how_at[off] = HOW_LINEAR; origin[off] = origin[prev];
        }
        prev = off;
        cur_from = off;
        hist_push(off);

        u16 imm = (len > 1 ? cart->prg[off + 1] : 0) | (len > 2 ? cart->prg[off + 2] << 8 : 0);
        switch (in->op) {
        case JMP:
            if (in->mode == ABS) { cur_how = HOW_JMP; follow(off, addr, imm); }
            else { stat_ind++; if (use_tables) analyze_ind_jump(off, addr, imm); }
            return;
        case JSR: {
            cur_how = HOW_JSR;
            follow(off, addr, imm);
            long t = use_tables ? resolve(off, addr, imm) : -1;
            if (t >= 0 && is_inline_dispatcher((size_t)t, mapper_prg_canon(cart, imm))) {
                scan_inline_table(off, addr);      /* tras la llamada va una tabla, no código */
                return;
            }
            break; }
        case RTS:
            if (use_tables) analyze_rts_trick(off, addr);
            return;
        case RTI: case BRK: return;
        case BCC: case BCS: case BEQ: case BMI: case BNE: case BPL: case BVC: case BVS:
            cur_how = HOW_BRANCH;
            follow(off, addr, (u16)(addr + 2 + (int8_t)cart->prg[off + 1]));
            break;
        }
        off += len; addr += len;
    }
}

static void drain(void)
{
    while (nwork) {
        Work w = work[--nwork];
        walk(w);
    }
}

static void resolve_tables(void);

/* Recorre todo lo pendiente; las heurísticas de tablas pueden agregar más y se repite */
static void discover(void)
{
    for (;;) {
        drain();
        unsigned before = stat_insns;
        if (use_tables) resolve_tables();
        drain();
        if (stat_insns == before) break;
    }
}

static void seed(size_t off, u16 addr, int how)
{
    cur_from = NO_PARENT; cur_how = how;
    if (off >= prg_size) return;
    addr = mapper_prg_canon(cart, addr);
    site[off].seed = 1;
    stat_seeds++;
    push_work(off, addr);
}

static void seed_vectors(void)
{
    static const u16 vec[3] = { 0xFFFA, 0xFFFC, 0xFFFE };
    static const char *nm[3] = { "NMI", "RESET", "IRQ/BRK" };
    for (int i = 0; i < 3; i++) {
        PrgWin w;
        u16 va = mapper_prg_canon(cart, vec[i]);
        if (!mapper_prg_window(cart, va, &w)) continue;
        size_t vo = w.off + (va - w.start);
        u16 t = cart->prg[vo] | (cart->prg[vo + 1] << 8);
        if (verbose) printf("vector %-8s = $%04X\n", nm[i], t);
        PrgWin tw; t = mapper_prg_canon(cart, t);
        if (t < 0x8000 || !mapper_prg_window(cart, t, &tw)) continue;   /* apunta a RAM: no es código de ROM */
        seed(tw.off + (t - tw.start), t, HOW_VECTOR);
    }
}

#define TRACE_SHIFT 13      /* bancos de la traza: ventanas de 8 KB del PRG (NES_TRACE_SHIFT del runtime) */

/* --why: "BB:DIRECCION" como en la traza, o el desplazamiento en el PRG */
static size_t why_arg(const char *arg)
{
    unsigned long off, addr;
    return trace_line_read(arg, TRACE_SHIFT, &off, &addr) ? off : strtoul(arg, NULL, 16);
}

static RcList ram_list;              /* instrucciones de RAM de las trazas (líneas "ram") */

/* Una línea "ram" válida: en la RAM interna ($0000-$1FFF) o en la del cartucho ($6000-$7FFF), sin cruzar su final,
 * con la longitud que pide su opcode y sin variar lo que identifica la instrucción (el opcode; en los saltos
 * relativos, también el desplazamiento) */
static void add_ram_line(RcIns *in)
{
    const Ins *ins = &ins_tab[in->bytes[0]];
    in->key = ins->mode == REL ? 2 : 1;
    int inside = in->addr + in->len <= 0x2000 || (in->addr >= 0x6000 && in->addr + in->len <= 0x8000);
    if (!inside || in->len != mode_len[ins->mode] || (in->vol & ((1u << in->key) - 1))) return;
    if (!rc_list_add(&ram_list, in)) die("sin memoria");
}

/* Traza "BANCO:DIRECCION" (trace_line.h; también el formato viejo "DESPLAZAMIENTO:DIRECCION"), más las líneas
 * "ram" del código que corrió desde la RAM */
static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir %s", path);
    char line[64];
    unsigned long off, addr;
    RcIns in;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr)) {
            if (rc_parse(line, &in)) add_ram_line(&in);
            continue;
        }
        if (off >= prg_size || addr < 0x8000) continue;
        seed(off, (u16)addr, HOW_TRACE);
        stat_trace++;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Funciones                                                           */
/* ------------------------------------------------------------------ */

typedef struct { size_t lo, hi; unsigned n; int fixed; } Func;   /* hi: último byte de la última instrucción */
static Func *funcs;
static size_t nfuncs;
static u32 *fn_of;      /* por desplazamiento: índice de función + 1 */

static int ins_len(size_t off) { return mode_len[ins_tab[site[off].opc].mode]; }

static void partition(void)
{
    size_t gran = mapper_prg_granule(cart);
    size_t cap = 0;
    fn_of = calloc(prg_size, sizeof *fn_of);
    size_t i = 0;
    while (i < prg_size) {
        if (!site[i].state) { i++; continue; }
        if (nfuncs == cap) { cap = cap ? cap * 2 : 256; funcs = realloc(funcs, cap * sizeof *funcs); }
        Func *f = &funcs[nfuncs++];
        PrgWin w;
        mapper_prg_window(cart, site[i].addr, &w);
        *f = (Func){ i, i + ins_len(i) - 1, 1, w.fixed };
        fn_of[i] = (u32)nfuncs;
        size_t last = i;
        size_t j = i + ins_len(i);
        while (j < prg_size && j / gran == i / gran && f->n < 1500) {
            if (site[j].state) {
                f->hi = j + ins_len(j) - 1; f->n++; fn_of[j] = (u32)nfuncs; last = j;
                j += ins_len(j);
            } else {
                size_t k = j;
                while (k < prg_size && !site[k].state && k - last < 32 && k / gran == i / gran) k++;
                if (k >= prg_size || !site[k].state || k / gran != i / gran) break;
                j = k;
            }
        }
        i = f->hi + 1;
    }
}

/* ------------------------------------------------------------------ */
/* Emisión                                                             */
/* ------------------------------------------------------------------ */

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
        if (*p == '/') { *p = 0; MKDIR(tmp); *p = '/'; }
    MKDIR(tmp);
}

static void upper(char *dst, const char *src)
{
    while (*src) *dst++ = (char)toupper((unsigned char)*src++);
    *dst = 0;
}

static void disasm_bytes(u8 opc, u16 a, u8 b1, u8 b2, char *out, size_t sz)
{
    const Ins *in = &ins_tab[opc];
    u16 w = b1 | (b2 << 8);
    const char *m = op_names[in->op];
    switch (in->mode) {
    case IMP: snprintf(out, sz, "%s", m); break;
    case ACC: snprintf(out, sz, "%s a", m); break;
    case IMM: snprintf(out, sz, "%s #$%02X", m, b1); break;
    case ZP:  snprintf(out, sz, "%s $%02X", m, b1); break;
    case ZPX: snprintf(out, sz, "%s $%02X,x", m, b1); break;
    case ZPY: snprintf(out, sz, "%s $%02X,y", m, b1); break;
    case ABS: snprintf(out, sz, "%s $%04X", m, w); break;
    case ABX: snprintf(out, sz, "%s $%04X,x", m, w); break;
    case ABY: snprintf(out, sz, "%s $%04X,y", m, w); break;
    case IND: snprintf(out, sz, "%s ($%04X)", m, w); break;
    case IZX: snprintf(out, sz, "%s ($%02X,x)", m, b1); break;
    case IZY: snprintf(out, sz, "%s ($%02X),y", m, b1); break;
    default:  snprintf(out, sz, "%s $%04X", m, (u16)(a + 2 + (int8_t)b1)); break;
    }
}

static void disasm(size_t off, char *out, size_t sz)
{
    disasm_bytes(site[off].opc, site[off].addr, cart->prg[off + 1 < prg_size ? off + 1 : off],
                 cart->prg[off + 2 < prg_size ? off + 2 : off], out, sz);
}

/* ¿El destino 'target' (visto desde la instrucción en off) es una instrucción de la misma función? */
static int in_func(size_t off, u16 target)
{
    long t = resolve(off, site[off].addr, target);
    if (t < 0 || (size_t)t >= prg_size) return 0;
    return site[t].state && site[t].addr == mapper_prg_canon(cart, target) && fn_of[t] == fn_of[off];
}

/* ¿Esta instrucción puede escribir en $8000-$FFFF (registros del mapper)? */
static int may_write_mapper(size_t off)
{
    const Ins *in = &ins_tab[site[off].opc];
    int k = op_kind(in->op);
    if (k != K_WRITE && k != K_RMW) return 0;
    u16 w = cart->prg[off + 1] | (mode_len[in->mode] > 2 ? cart->prg[off + 2] << 8 : 0);
    u16 lim = cart->low_regs ? 0x4020 : 0x8000;     /* algunos mappers tienen registros por debajo de $8000 */
    switch (in->mode) {
    case ABS: return w >= lim;
    case ABX: case ABY: return w >= lim - 0x100;
    case IZX: case IZY: return 1;
    default: return 0;
    }
}

/* Una instrucción para emitir: la de la ROM, o una de RAM aprendida de la traza (emit_ram). Los operandos van
 * como expresiones de C: literales en la ROM; en RAM, los que el juego cambia se leen al ejecutar */
typedef struct {
    u8 opc;
    u16 addr;
    u8 b1;                       /* primer byte de operando (constante) */
    u16 w;                       /* los dos (constante) */
    const char *B1, *W, *WH;     /* b1, w y el byte alto de w como expresiones */
    int w_const;                 /* w es constante: un JMP o un JSR tienen destino fijo */
    int (*here)(const void *ctx, u16 target);   /* ¿target es una instrucción de esta función o bloque? */
    const void *ctx;
} EmitIns;

/* El cuerpo de la instrucción; 1 si termina el flujo (ya salió o saltó: no sigue con la siguiente) */
static int emit_ins_body(FILE *f, const EmitIns *e)
{
    const Ins *in = &ins_tab[e->opc];
    int op = in->op, mode = in->mode, kind = op_kind(op);
    u16 addr = e->addr;
    char opn[8];
    upper(opn, op_names[op]);

    switch (op) {
    case JSR: {
        u16 ret = (u16)(addr + 2);
        fprintf(f, "    TICKS(3);\n    push(n, 0x%02X); push(n, 0x%02X);\n    TICK();\n", ret >> 8, ret & 0xFF);
        if (e->w_const) fprintf(f, "    LEAVE(0x%04X);\n", mapper_prg_canon(cart, e->w));
        else fprintf(f, "    LEAVE(%s);\n", e->W);
        return 1; }
    case RTS:
        fprintf(f, "    TICKS(3);\n"
                   "    { u16 lo = pull(n); u16 hi = pull(n); c->pc = (u16)((hi << 8) | lo); }\n"
                   "    RD(c->pc); c->pc++;\n"
                   "    if (UNLIKELY(n->prev_need_nmi | n->prev_run_irq)) nes_interrupt(n, 0);\n    return;\n");
        return 1;
    case RTI:
        fprintf(f, "    TICKS(3);\n"
                   "    c->p = (pull(n) & ~FLAG_B) | FLAG_U;\n"
                   "    { u16 lo = pull(n); u16 hi = pull(n); c->pc = (u16)((hi << 8) | lo); }\n"
                   "    if (UNLIKELY(n->prev_need_nmi | n->prev_run_irq)) nes_interrupt(n, 0);\n    return;\n");
        return 1;
    case BRK:
        fprintf(f, "    TICK();\n    c->pc = 0x%04X; nes_interrupt(n, 1);\n    return;\n", (u16)(addr + 1));
        return 1;
    case JAM:
        fprintf(f, "    TICK();\n    c->jammed = 1;\n    LEAVE(0x%04X);\n", addr);
        return 1;
    case PHA: fprintf(f, "    TICKS(2);\n    push(n, c->a);\n"); break;
    case PHP: fprintf(f, "    TICKS(2);\n    push(n, c->p | FLAG_B | FLAG_U);\n"); break;
    case PLA: fprintf(f, "    TICKS(3);\n    c->a = pull(n); set_nz(c, c->a);\n"); break;
    case PLP: fprintf(f, "    TICKS(3);\n    c->p = (pull(n) & ~FLAG_B) | FLAG_U;\n"); break;
    case BCC: case BCS: case BEQ: case BMI: case BNE: case BPL: case BVC: case BVS: {
        u16 tgt = (u16)(addr + 2 + (int8_t)e->b1);       /* el desplazamiento es parte de la instrucción: constante */
        int cross = ((addr + 2) ^ tgt) & 0xFF00;
        fprintf(f, "    TICKS(2);\n    if (op_branch_taken(c, %s)) {\n", opn);
        if (!cross) fprintf(f, "        if (UNLIKELY(n->run_irq & !n->prev_run_irq)) n->run_irq = 0;   /* ignora un IRQ recién llegado */\n");
        fprintf(f, "        TICK();%s\n", cross ? " TICK();" : "");
        if (e->here(e->ctx, tgt)) fprintf(f, "        ICHK(0x%04X);\n        goto L_%04X;\n    }\n", tgt, tgt);
        else fprintf(f, "        LEAVE(0x%04X);\n    }\n", tgt);
        break; }
    case JMP:
        fprintf(f, "    TICKS(3);\n");
        if (mode == ABS && e->w_const) {
            u16 t = mapper_prg_canon(cart, e->w);
            if (e->here(e->ctx, t)) fprintf(f, "    ICHK(0x%04X);\n    goto L_%04X;\n", t, t);
            else fprintf(f, "    LEAVE(0x%04X);\n", t);
        } else if (mode == ABS) {
            fprintf(f, "    LEAVE(%s);\n", e->W);
        } else {
            fprintf(f, "    { u16 p = %s; u16 ea = RD(p); ea |= RD((p & 0xFF00) | ((p + 1) & 0xFF)) << 8;\n"
                       "      LEAVE(ea); }\n", e->W);
        }
        return 1;
    default: {
        /* Direccionamiento */
        int store_unstable = op == AHX || op == TAS || op == SHY || op == SHX;
        fprintf(f, "    {\n");
        int has_ea = 1;
        switch (mode) {
        case IMP: case ACC: fprintf(f, "        TICKS(2);\n"); has_ea = 0; break;
        case IMM: fprintf(f, "        TICKS(2);\n"); has_ea = 0; break;
        case ZP:  fprintf(f, "        TICKS(2);\n        u16 ea = %s;\n", e->B1); break;
        case ZPX: case ZPY:
            fprintf(f, "        TICKS(3);\n        u16 ea = (u8)(%s + c->%c);\n", e->B1, mode == ZPX ? 'x' : 'y');
            break;
        case ABS: fprintf(f, "        TICKS(3);\n        n->bus = %s;                  /* bus abierto: lo último que se leyó fue el byte alto */\n        u16 ea = %s;\n", e->WH, e->W); break;
        case ABX: case ABY:
            fprintf(f, "        TICKS(3);\n"
                       "        u16 ea = (u16)(%s + c->%c);\n"
                       "        int cross = (%s ^ ea) & 0xFF00;\n", e->W, mode == ABX ? 'x' : 'y', e->W);
            if (kind == K_READ) fprintf(f, "        if (cross) RD((%s & 0xFF00) | (ea & 0xFF)); else n->bus = %s;\n", e->W, e->WH);
            else fprintf(f, "        RD((%s & 0xFF00) | (ea & 0xFF));\n", e->W);
            if (!store_unstable) fprintf(f, "        (void)cross;\n");
            break;
        case IZX:
            fprintf(f, "        TICKS(3);\n"
                       "        u8 z = (u8)(%s + c->x);\n"
                       "        u16 ea = RD(z); ea |= RD((u8)(z + 1)) << 8;\n", e->B1);
            break;
        case IZY:
            fprintf(f, "        TICKS(2);\n"
                       "        u16 base = RD(%s); base |= RD((u8)(%s + 1)) << 8;\n"
                       "        u16 ea = (u16)(base + c->y);\n"
                       "        int cross = (base ^ ea) & 0xFF00;\n", e->B1, e->B1);
            if (kind == K_READ) fprintf(f, "        if (cross) RD((base & 0xFF00) | (ea & 0xFF));\n");
            else fprintf(f, "        RD((base & 0xFF00) | (ea & 0xFF));\n");
            if (!store_unstable) fprintf(f, "        (void)cross; (void)base;\n");
            break;
        }
        switch (kind) {
        case K_READ:
            if (mode == IMM) fprintf(f, "        op_read(c, %s, %s);\n", opn, e->B1);
            else if (has_ea) fprintf(f, "        op_read(c, %s, RD(ea));\n", opn);
            break;
        case K_WRITE:
            if (store_unstable) fprintf(f, "        cpu_unstable_store(n, %s, %s, ea, cross);\n", opn, mode == IZY ? "base" : e->W);
            else fprintf(f, "        WR(ea, op_store(c, %s));\n", opn);
            break;
        case K_RMW:
            if (mode == ACC) fprintf(f, "        c->a = op_rmw(c, %s, c->a);\n", opn);
            else fprintf(f, "        { u8 v = RD(ea); WR(ea, v); WR(ea, op_rmw(c, %s, v)); }\n", opn);
            break;
        default:
            fprintf(f, "        op_implied(c, %s);\n", opn);
            break;
        }
        fprintf(f, "    }\n");
        break; }
    }
    return 0;
}

static int rom_here(const void *ctx, u16 target) { return in_func(*(const size_t *)ctx, target); }

static void emit_ins(FILE *f, size_t off)
{
    const Site *s = &site[off];
    const Ins *in = &ins_tab[s->opc];
    int len = mode_len[in->mode];
    u16 addr = s->addr, next = (u16)(addr + len);
    u8 b1 = len > 1 ? cart->prg[off + 1] : 0;
    u16 w = b1 | (len > 2 ? cart->prg[off + 2] << 8 : 0);
    char dis[64], B1[8], W[8], WH[8];
    disasm(off, dis, sizeof dis);
    snprintf(B1, sizeof B1, "0x%02X", b1);
    snprintf(W, sizeof W, "0x%04X", w);
    snprintf(WH, sizeof WH, "0x%02X", w >> 8);
    EmitIns e = { s->opc, addr, b1, w, B1, W, WH, 1, rom_here, &off };

    fprintf(f, "L_%04X: /* %s */\n    FRAME(0x%04X);\n", addr, dis, addr);
    if (emit_ins_body(f, &e)) return;
    fprintf(f, "    ICHK(0x%04X);\n", next);
    PrgWin w2; mapper_prg_window(cart, addr, &w2);
    if (!w2.fixed && may_write_mapper(off)) fprintf(f, "    MAPCHK(0x%04X);\n", next);
    /* ¿sigue la instrucción siguiente en esta función? */
    size_t noff = off + len;
    if (!(noff < prg_size && site[noff].state && site[noff].addr == next && fn_of[noff] == fn_of[off]))
        fprintf(f, "    c->pc = 0x%04X;\n    return;\n", next);
}

static void emit_function(FILE *f, size_t idx)
{
    const Func *fn = &funcs[idx];
    fprintf(f, "/* PRG $%06zX-$%06zX (%u instrucciones) */\n", fn->lo, fn->hi, fn->n);
    fprintf(f, "void f_%06zX(NES *n)\n{\n    Cpu *c = &n->cpu;\n    u32 ver0 = n->cart.map_ver; (void)ver0;\n    switch (c->pc) {\n", fn->lo);
    for (size_t o = fn->lo; o <= fn->hi; o++)
        if (site[o].state && fn_of[o] == idx + 1) fprintf(f, "    case 0x%04X: goto L_%04X;\n", site[o].addr, site[o].addr);
    fprintf(f, "    default: cpu_step(n); return;\n    }\n");
    for (size_t o = fn->lo; o <= fn->hi; o++)
        if (site[o].state && fn_of[o] == idx + 1) emit_ins(f, o);
    fprintf(f, "}\n\n");
}

static void emit_code(const char *dir)
{
    FILE *h = open_out(dir, "gen/gen.h");
    fprintf(h, "/* Generado por nesrecomp: no editar. */\n#ifndef GEN_H\n#define GEN_H\n#include \"recomp.h\"\n\n");
    for (size_t i = 0; i < nfuncs; i++) fprintf(h, "void f_%06zX(NES *n);\n", funcs[i].lo);
    fprintf(h, "RecompFn ram_lookup(NES *n, u16 pc);     /* gen/ram.c: el código aprendido de RAM */\n");
    fprintf(h, "\n#endif\n");
    fclose(h);

    FILE *f = NULL;
    unsigned in_file = 0, part = 0;
    for (size_t i = 0; i < nfuncs; i++) {
        if (!f || in_file >= 4000) {
            if (f) fclose(f);
            char name[64];
            snprintf(name, sizeof name, "gen/code_%03u.c", part++);
            f = open_out(dir, name);
            fprintf(f, "/* Generado por nesrecomp: no editar. */\n#include \"gen.h\"\n\n");
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
    fprintf(f, "/* Generado por nesrecomp: no editar. */\n#include \"gen.h\"\n\n");
    fprintf(f, "#define NFUNCS %zu\n\n", nfuncs);
    fprintf(f, "static const u32 f_lo[%zu] = {", nfuncs ? nfuncs : 1);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%zX,", i % 8 ? "" : "\n    ", funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const u32 f_hi[%zu] = {", nfuncs ? nfuncs : 1);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%zX,", i % 8 ? "" : "\n    ", funcs[i].hi);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const RecompFn f_fn[%zu] = {", nfuncs ? nfuncs : 1);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%sf_%06zX,", i % 4 ? "" : "\n    ", funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    size_t pages = (prg_size + 255) >> 8;
    fprintf(f, "\n};\n\n/* Por página de 256 bytes de PRG: primera función que termina en ella o después */\n");
    fprintf(f, "static const u32 page_first[%zu] = {", pages);
    size_t fi = 0;
    for (size_t p = 0; p < pages; p++) {
        while (fi < nfuncs && funcs[fi].hi < p * 256) fi++;
        fprintf(f, "%s%zu,", p % 16 ? "" : "\n    ", fi);
    }
    fprintf(f, "\n};\n\n"
               "RecompFn recomp_lookup(NES *n, u16 pc)\n{\n"
               "    long off = mapper_prg_offset(&n->cart, pc);\n"
               "    if (off < 0) return pc < 0x2000 || (pc >= 0x6000 && pc < 0x8000) ? ram_lookup(n, pc) : 0;\n"
               "    u32 i = page_first[off >> 8];\n"
               "    while (i < NFUNCS && f_hi[i] < (u32)off) i++;\n"
               "    return (i < NFUNCS && f_lo[i] <= (u32)off) ? f_fn[i] : 0;\n}\n");
    fclose(f);
}

/* Código que el juego copia a la RAM interna o a la del cartucho y ejecuta desde ahí (3-D WorldRunner espera el NMI
 * en un bucle en $0760; Gradius 2, Batman y Zelda corren rutinas desde $6000-$7FFF). No está en el PRG, así que se aprende de las líneas "ram" de la traza (ram_code.h, en retrorecomp). Las
 * instrucciones que van una detrás de otra sin ambigüedad forman un bloque, que se compila con sus direcciones
 * como el código del PRG (emit_ins_body). La guarda: ram_lookup elige el primer bloque cuyos opcodes (y
 * desplazamientos de los saltos relativos) siguen en RAM; si el juego cambió el código, cae al intérprete. Los
 * operandos que cambiaron entre ejecuciones se leen de RAM al ejecutar, como en el intérprete. La RAM del cartucho
 * se lee con mapper_ram_ptr (el banco y si está activa dependen del mapper); un bloque ahí sale al despachador si
 * una escritura cambió el mapeo (MAPCHK), para que la guarda vuelva a comprobar sus bytes */
static int ram_here(const void *ctx, u16 target) { return rc_block_has(&ram_list, ctx, target); }

static void emit_ram(const char *dir)
{
    FILE *f = open_out(dir, "gen/ram.c");
    fprintf(f, "/* Generado por nesrecomp: no editar. El código que el juego ejecuta desde la RAM, aprendido de la\n"
               " * traza (líneas \"ram\"); sin ellas, ram_lookup no tiene nada y la RAM sigue en el intérprete. */\n"
               "#include \"gen.h\"\n\n");
    size_t dropped = rc_list_finish(&ram_list);
    RcBlock *blocks = NULL;
    size_t nb = ram_list.n ? rc_blocks(&ram_list, &blocks, NULL) : 0;
    if (nb == (size_t)-1) die("sin memoria");
    if (!nb) {
        fprintf(f, "RecompFn ram_lookup(NES *n, u16 pc)\n{\n    (void)n;\n    (void)pc;\n    return 0;\n}\n");
        fclose(f);
        if (dropped) printf("código en RAM: %zu instrucciones descartadas por cambiar de estructura\n", dropped);
        return;
    }
    size_t nvol = 0;
    for (size_t i = 0; i < ram_list.n; i++) nvol += ram_list.ins[i].vol != 0;
    printf("código en RAM aprendido de la traza: %zu instrucciones (%zu con operandos variables) en %zu bloques",
           ram_list.n, nvol, nb);
    if (dropped) printf(" (%zu descartadas por cambiar de estructura)", dropped);
    printf("\n");

    fprintf(f, "/* Un byte de RAM sin efectos; -1 si en la del cartucho no hay RAM legible con el mapeo actual */\n"
               "static inline int ram_g(const NES *n, u16 a)\n{\n"
               "    if (a < 0x2000) return n->ram[a & 0x7FF];\n"
               "    const u8 *p = mapper_ram_ptr(&n->cart, a);\n"
               "    return p ? *p : -1;\n}\n"
               "static inline u8 ram_b(const NES *n, u16 a) { return (u8)ram_g(n, a); }\n\n");
    for (size_t bi = 0; bi < nb; bi++) {
        const RcBlock *b = &blocks[bi];
        fprintf(f, "/* RAM $%04X-$%04X (%zu instrucciones) */\nstatic int m_%zu(const NES *n)\n{\n    return", b->start,
                b->start + b->len - 1, b->n, bi);
        int first = 1, k3 = 0;
        for (size_t k = 0; k < b->n; k++) {
            const RcIns *in = &ram_list.ins[b->node[k]];
            for (unsigned j = 0; j < in->key; j++, k3++) {
                fprintf(f, "%s ram_g(n, 0x%04X) == 0x%02X", first ? "" : (k3 % 4 ? " &&" : " &&\n          "), in->addr + j, in->bytes[j]);
                first = 0;
            }
        }
        int cart = b->start >= 0x6000;
        fprintf(f, ";\n}\n\nstatic void r_%zu(NES *n)\n{\n    Cpu *c = &n->cpu;\n", bi);
        if (cart) fprintf(f, "    u32 ver0 = n->cart.map_ver;\n");
        fprintf(f, "    switch (c->pc) {\n");
        for (size_t k = 0; k < b->n; k++) {
            u32 a = ram_list.ins[b->node[k]].addr;
            fprintf(f, "    case 0x%04X: goto L_%04X;\n", a, a);
        }
        fprintf(f, "    default: cpu_step(n); return;\n    }\n");
        for (size_t k = 0; k < b->n; k++) {
            const RcIns *in = &ram_list.ins[b->node[k]];
            u16 a = (u16)in->addr, next = (u16)(a + in->len);
            u8 b1 = in->len > 1 ? in->bytes[1] : 0, b2 = in->len > 2 ? in->bytes[2] : 0;
            int wvar = (in->vol & 6) != 0;
            char dis[64], B1[32], W[64], WH[32];
            disasm_bytes(in->bytes[0], a, b1, b2, dis, sizeof dis);
            if (in->vol & 2) snprintf(B1, sizeof B1, "ram_b(n, 0x%04X)", (u16)(a + 1));
            else snprintf(B1, sizeof B1, "0x%02X", b1);
            if (wvar) {
                snprintf(W, sizeof W, "(u16)(ram_b(n, 0x%04X) | ram_b(n, 0x%04X) << 8)", (u16)(a + 1), (u16)(a + 2));
                snprintf(WH, sizeof WH, "ram_b(n, 0x%04X)", (u16)(a + 2));
            } else {
                snprintf(W, sizeof W, "0x%04X", (u16)(b1 | b2 << 8));
                snprintf(WH, sizeof WH, "0x%02X", b2);
            }
            EmitIns e = { in->bytes[0], a, b1, (u16)(b1 | b2 << 8), B1, W, WH, !wvar, ram_here, b };
            fprintf(f, "L_%04X: /* RAM %s%s */\n    FRAME(0x%04X);\n", a, dis, in->vol ? " (operandos variables)" : "", a);
            if (emit_ins_body(f, &e)) continue;
            fprintf(f, "    ICHK(0x%04X);\n", next);
            int kind = op_kind(ins_tab[in->bytes[0]].op);
            if (cart && (kind == K_WRITE || kind == K_RMW)) fprintf(f, "    MAPCHK(0x%04X);\n", next);
            if (k + 1 == b->n || ram_list.ins[b->node[k + 1]].addr != next) fprintf(f, "    c->pc = 0x%04X;\n    return;\n", next);
        }
        fprintf(f, "}\n\n");
    }

    /* El lookup: por cada dirección, los bloques que la tienen, en orden; el primero cuya guarda pasa */
    fprintf(f, "RecompFn ram_lookup(NES *n, u16 pc)\n{\n    switch (pc) {\n");
    for (u32 a = 0; a < 0x8000; a++) {
        if (a == 0x2000) a = 0x6000;
        int any = 0;
        for (size_t bi = 0; bi < nb; bi++)
            if (rc_block_has(&ram_list, &blocks[bi], a)) {
                if (!any) fprintf(f, "    case 0x%04X:", a);
                fprintf(f, " if (m_%zu(n)) return r_%zu;", bi, bi);
                any = 1;
            }
        if (any) fprintf(f, " break;\n");
    }
    fprintf(f, "    }\n    return 0;\n}\n");
    fclose(f);
    for (size_t bi = 0; bi < nb; bi++) free(blocks[bi].node);
    free(blocks);
}

static void emit_info(const char *dir, const char *sha1, const char *name)
{
    FILE *f = open_out(dir, "gen/info.c");
    fprintf(f, "/* Generado por nesrecomp: no editar. */\n#include \"recomp.h\"\n\n");
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
        "# Generado por nesrecomp. Compila con cualquier gcc/clang C99 con SDL2.\n"
        "# Windows: MSYS2 UCRT64.\n\n"
        "CC     ?= cc\n"
        "# MSYS2 sin \"cc\" (por ejemplo MINGW64): se usa gcc\n"
        "ifeq ($(origin CC),default)\n"
        "CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)\n"
        "endif\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen -Iplatform -DNES_RECOMP -DNES_MAPPER=%d\n\n"
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
        "WEB_PREF   := /libsdl/NESRecomp/%s/\n"
        "WEB_CACHE  := rom.nes\n"
        "WEB_ACCEPT := .nes\n"
        "include platform/web.mk\n\n"
        "# Android: make apk, con el SDK de Android o con Docker (ver platform/android/android.mk)\n"
        "APK_NAME   := %s\n"
        "APK_TITLE  := %s\n"
        "APK_SYSTEM := nesrecomp\n"
        "APK_CACHE  := rom.nes\n"
        "APK_ICON   := platform/icono.h\n"
        "include platform/android/android.mk\n\n"
        "clean:\n\trm -rf build dist web apk %s$(EXE) difftest$(EXE) bench$(EXE)\n",
        cart->mapper, name, name, name, name, name, name, name, name, name);
    fclose(f);
}

static void emit_readme(const char *dir, const char *name, const char *sha1)
{
    FILE *f = open_out(dir, "LEEME.txt");
    fprintf(f,
        "Proyecto generado por nesrecomp.\n\n"
        "Compilar (necesita un compilador C99, make y SDL2):\n"
        "  macOS:   brew install sdl2 pkg-config\n"
        "  Debian:  sudo apt install build-essential pkg-config libsdl2-dev\n"
        "  Windows: MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 make\n"
        "           (el Makefile usa gcc si no hay cc; el .exe lleva el icono del juego)\n"
        "           el .exe enlaza SDL2 de forma estática (no pide DLLs); make dist lo deja en dist/\n\n"
        "  make\n"
        "  ./%s juego.nes\n\n"
        "El proyecto no contiene la ROM. El ejecutable la pide y comprueba que su SHA1 sea\n"
        "%s.\n"
        "Si se ejecuta sin argumentos busca un .nes con ese SHA1 en la carpeta actual.\n\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest juego.nes 600\n\n"
        "En el navegador (WebAssembly; necesita Emscripten o Docker):\n"
        "  make web\n"
        "  python3 -m http.server -d web     y abrir http://localhost:8000/%s.html\n"
        "La página pide la ROM (no va en el build) y la guarda en el navegador con la partida.\n\n"
        "En Android (necesita el SDK de Android y un JDK 17, o Docker):\n"
        "  make apk                       y luego  adb install -r apk/%s-debug.apk\n"
        "La primera vez el juego pide la ROM con el selector de archivos del teléfono y la guarda.\n"
        "make apk ROM=juego.nes la mete en la APK (solo para uso personal: no compartas esa APK).\n", name, sha1, name, name);
    fclose(f);
}

/* Cuántas de las instrucciones ejecutadas (una traza) se descubrieron sin esa ayuda */
static u8 *missing_mark;

static void coverage_report(const char *path)
{
    missing_mark = calloc(prg_size, 1);
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir %s", path);
    char line[64];
    unsigned long off, addr;
    unsigned total = 0, found = 0, missing_shown = 0;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr) || off >= prg_size) continue;
        total++;
        if (site[off].state) { found++; continue; }
        missing_mark[off] = 1;
        if (missing_shown < 12 || verbose) {
            printf("  falta: PRG $%06lX = $%04lX  ", off, addr);
            Site save = site[off]; site[off].opc = cart->prg[off]; site[off].addr = (u16)addr;
            char d[64]; disasm(off, d, sizeof d); site[off] = save;
            printf("%s\n", d);
            missing_shown++;
        }
    }
    fclose(f);
    printf("cobertura: %u de %u instrucciones ejecutadas descubiertas sin traza (%.1f%%); descubiertas en total %u\n",
           found, total, total ? 100.0 * found / total : 100.0, stat_insns);
}

/* ------------------------------------------------------------------ */
/* --why, --viz, --events                                              */
/* ------------------------------------------------------------------ */

static void explain(size_t off)
{
    if (off >= prg_size || !site[off].state) {
        printf("PRG $%06zX: no se descubrió como código\n", off);
        return;
    }
    printf("PRG $%06zX ($%04X) se tomó como código así:\n", off, site[off].addr);
    char d[64];
    size_t cur = off;
    int steps = 0;
    while (cur != NO_PARENT && steps++ < 4000) {
        disasm(cur, d, sizeof d);
        size_t par = parent[cur];
        int how = how_at[cur];
        /* las instrucciones seguidas se agrupan: solo se cuenta cuántas hubo */
        size_t first = cur;
        int run = 0;
        while (how == HOW_LINEAR && par != NO_PARENT) { cur = par; par = parent[cur]; how = how_at[cur]; run++; }
        if (run) {
            char d0[64]; disasm(cur, d0, sizeof d0);
            printf("  $%06zX $%04X  %-22s  ...%d instrucciones seguidas hasta  $%06zX $%04X  %s\n", first, site[first].addr, d, run, cur,
                   site[cur].addr, d0);
            disasm(cur, d, sizeof d);
        } else {
            printf("  $%06zX $%04X  %-22s\n", cur, site[cur].addr, d);
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
    for (size_t i = 0; i < prg_size; i++) {
        if (!site[i].state) continue;
        char d[64]; disasm(i, d, sizeof d);
        if (parent[i] == NO_PARENT) fprintf(f, "%06zX %04X %s - %s %s\n", i, site[i].addr, how_name[how_at[i]], org_name[origin[i]], d);
        else fprintf(f, "%06zX %04X %s %06zX %s %s\n", i, site[i].addr, how_name[how_at[i]], parent[i], org_name[origin[i]], d);
    }
    fclose(f);
}

static void viz_map(int color)
{
    static const int org_color[ORG_COUNT] = { 46, 51, 226, 201 };        /* verde, cian, amarillo, magenta */
    printf("\nMapa del PRG (una celda = 256 bytes; filas de 16 KB):\n");
    for (int o = 0; o < ORG_COUNT; o++) {
        if (color) printf("  \033[38;5;%dm#\033[0m %s\n", org_color[o], org_name[o]);
        else printf("  %c %s\n", "#=%@"[o], org_name[o]);
    }
    if (missing_mark) printf("  %sX%s ejecutado por el juego y no descubierto\n", color ? "\033[41;97m" : "", color ? "\033[0m" : "");
    printf("  . sin código   : menos de la mitad   # la mitad o más\n\n");
    unsigned cells = 0, code_cells = 0;
    for (size_t row = 0; row < prg_size; row += 0x4000) {
        printf("%06zX ", row);
        for (size_t cell = row; cell < row + 0x4000 && cell < prg_size; cell += 256) {
            unsigned bytes = 0, cnt[ORG_COUNT] = { 0 };
            int miss = 0;
            for (size_t i = cell; i < cell + 256 && i < prg_size; i++) {
                if (site[i].state) { int l = mode_len[ins_tab[site[i].opc].mode]; bytes += l; cnt[origin[i]] += l; }
                if (missing_mark && missing_mark[i]) miss = 1;
            }
            cells++;
            if (bytes) code_cells++;
            if (miss) { printf("%s%s%s", color ? "\033[41;97m" : "", "X", color ? "\033[0m" : ""); continue; }
            if (!bytes) { putchar('.'); continue; }
            int best = 0;
            for (int o = 1; o < ORG_COUNT; o++) if (cnt[o] > cnt[best]) best = o;
            char ch = bytes >= 128 ? '#' : ':';
            if (color) printf("\033[38;5;%dm%c\033[0m", org_color[best], ch);
            else putchar(bytes >= 128 ? "#=%@"[best] : ':');
        }
        putchar('\n');
    }
    printf("\n%u de %u celdas con código (%.1f%% del PRG en bytes)\n", code_cells, cells, 100.0 * stat_bytes / prg_size);
}


/* Visor en HTML: una página sola con los datos del descubrimiento (mapa del PRG, instrucciones por celda y la cadena "por qué") */
static void json_str(FILE *f, const char *t)
{
    fputc('"', f);
    for (; *t; t++) { if (*t == '"' || *t == '\\') fputc('\\', f); fputc(*t, f); }
    fputc('"', f);
}

static const char html_head[] =     "<!doctype html>\n"
    "<html lang=\"es\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>Descubrimiento NESRecomp</title>\n"
    "<style>\n"
    ":root{--bg:#fff;--fg:#1b1f24;--mut:#6a737d;--line:#d7dce1;--card:#f6f8fa;--c0:#1a9e3a;--c1:#0b8fb5;--c2:#c79a00;--c3:#c2188f;--miss:#d1242f;--sel:#0b5fff}\n"
    "@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--mut:#8b949e;--line:#30363d;--card:#161b22;--c0:#3fb950;--c1:#39c5cf;--c2:#e3b341;--c3:#f778ba;--miss:#ff7b72;--sel:#58a6ff}}\n"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}\n"
    "header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;flex-wrap:wrap;gap:8px 20px;align-items:baseline}\n"
    "h1{font-size:16px;margin:0}.mut{color:var(--mut)}\n"
    "main{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(0,1fr);gap:16px;padding:16px}\n"
    "@media(max-width:900px){main{grid-template-columns:1fr}}\n"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px;min-width:0}\n"
    ".legend{display:flex;flex-wrap:wrap;gap:6px 14px;margin:0 0 10px;font-size:13px}\n"
    ".legend label{cursor:pointer;display:flex;gap:5px;align-items:center}\n"
    ".sw{width:12px;height:12px;border-radius:3px;display:inline-block}\n"
    "#map{display:block;width:100%;image-rendering:pixelated;border:1px solid var(--line);border-radius:4px;cursor:crosshair;background:var(--bg)}\n"
    ".row{display:flex;gap:8px;margin:8px 0;flex-wrap:wrap}\n"
    "input[type=text]{font:13px ui-monospace,monospace;padding:5px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);width:11em}\n"
    "button{font:13px system-ui;padding:5px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);cursor:pointer}\n"
    "button:hover{border-color:var(--sel)}\n"
    "table{border-collapse:collapse;width:100%;font:12.5px ui-monospace,Menlo,monospace}\n"
    "td{padding:1px 6px;white-space:nowrap}tr.ins:hover{background:var(--line);cursor:pointer}tr.sel{background:color-mix(in srgb,var(--sel) 22%,transparent)}\n"
    ".list{max-height:46vh;overflow:auto;border:1px solid var(--line);border-radius:6px;background:var(--bg)}\n"
    ".chain li{margin:2px 0;font:12.5px ui-monospace,Menlo,monospace}.chain{padding-left:18px;margin:6px 0}\n"
    ".tag{font:11px system-ui;padding:0 6px;border-radius:9px;border:1px solid var(--line);color:var(--mut)}\n"
    "code{font:12.5px ui-monospace,monospace}\n"
    "</style></head><body>\n"
    "<header><h1>Descubrimiento de código: <span id=\"rom\"></span></h1><span class=\"mut\" id=\"stats\"></span></header>\n"
    "<main>\n"
    "<section class=\"card\">\n"
    " <div class=\"legend\" id=\"legend\"></div>\n"
    " <canvas id=\"map\"></canvas>\n"
    " <div class=\"row\"><input id=\"goto\" type=\"text\" placeholder=\"PRG hex, p. ej. 1C2A\"><button id=\"go\">Ir</button><span class=\"mut\" id=\"hint\">Clic en una celda (256 bytes) para ver sus instrucciones.</span></div>\n"
    "</section>\n"
    "<section class=\"card\">\n"
    " <div id=\"cellhead\" class=\"mut\">Sin selección</div>\n"
    " <div class=\"list\"><table id=\"ins\"></table></div>\n"
    " <h3 style=\"margin:14px 0 4px;font-size:14px\">¿Por qué es código?</h3>\n"
    " <ol class=\"chain\" id=\"chain\"><li class=\"mut\">Elige una instrucción.</li></ol>\n"
    "</section>\n"
    "</main>\n"
    "<script>\n"
    "const D=";
static const char html_tail[] =     ";\n"
    "const ORG=D.orgs, HOW=D.hows, COL=['--c0','--c1','--c2','--c3'];\n"
    "const I=D.ins; // [off, addr, how, parent(-1), origin, texto]\n"
    "const byOff=new Map(); I.forEach((x,k)=>byOff.set(x[0],k));\n"
    "const miss=new Set(D.missing);\n"
    "const CELL=256, W=64, rows=Math.ceil(D.prg/(CELL*W));\n"
    "const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();\n"
    "const show=[true,true,true,true];\n"
    "document.getElementById('rom').textContent=D.name;\n"
    "document.getElementById('stats').textContent=`mapper ${D.mapper} · PRG ${D.prg/1024} KB · ${I.length} instrucciones · ${D.bytes} bytes (${(100*D.bytes/D.prg).toFixed(1)} %)`+(miss.size?` · ${miss.size} ejecutadas sin descubrir`:'');\n"
    "const lg=document.getElementById('legend');\n"
    "ORG.forEach((n,i)=>{const l=document.createElement('label');l.innerHTML=`<input type=\"checkbox\" checked><span class=\"sw\" style=\"background:var(${COL[i]})\"></span>${n}`;l.firstChild.onchange=e=>{show[i]=e.target.checked;draw()};lg.appendChild(l)});\n"
    "if(miss.size){const s=document.createElement('span');s.innerHTML=`<span class=\"sw\" style=\"background:var(--miss)\"></span> ejecutado y no descubierto`;lg.appendChild(s)}\n"
    "const cv=document.getElementById('map'),ctx=cv.getContext('2d');\n"
    "// por celda: bytes de código por origen y si falta algo\n"
    "const ncell=Math.ceil(D.prg/CELL), cnt=Array.from({length:ncell},()=>[0,0,0,0]), cm=new Uint8Array(ncell);\n"
    "I.forEach(x=>{cnt[x[0]/CELL|0][x[4]]+=x[6]}); miss.forEach(o=>cm[o/CELL|0]=1);\n"
    "let sel=-1;\n"
    "function draw(){\n"
    " const S=12,G=1; cv.width=W*(S+G); cv.height=rows*(S+G);\n"
    " for(let c=0;c<ncell;c++){\n"
    "  const x=(c%W)*(S+G), y=(c/W|0)*(S+G); let tot=0,best=-1,bv=0;\n"
    "  for(let o=0;o<4;o++){ if(!show[o])continue; tot+=cnt[c][o]; if(cnt[c][o]>bv){bv=cnt[c][o];best=o} }\n"
    "  if(cm[c]){ctx.fillStyle=css('--miss');ctx.fillRect(x,y,S,S);continue}\n"
    "  ctx.fillStyle=css('--line'); ctx.fillRect(x,y,S,S);\n"
    "  if(best>=0){ctx.globalAlpha=Math.min(1,.25+tot/256*.75);ctx.fillStyle=css(COL[best]);ctx.fillRect(x,y,S,S);ctx.globalAlpha=1}\n"
    "  if(c===sel){ctx.strokeStyle=css('--sel');ctx.lineWidth=2;ctx.strokeRect(x+1,y+1,S-2,S-2)}\n"
    " }\n"
    "}\n"
    "function cellAt(ev){const r=cv.getBoundingClientRect(),S=13*(r.width/cv.width);return ((ev.clientY-r.top)/S|0)*W+((ev.clientX-r.left)/S|0)}\n"
    "function hex(n,w){return n.toString(16).toUpperCase().padStart(w,'0')}\n"
    "function pick(c){\n"
    " if(c<0||c>=ncell)return; sel=c; draw();\n"
    " const t=document.getElementById('ins');t.innerHTML='';\n"
    " let n=0;for(let o=c*CELL;o<c*CELL+CELL;o++){const k=byOff.get(o);if(k===undefined)continue;n++;\n"
    "  const x=I[k],tr=document.createElement('tr');tr.className='ins';tr.dataset.k=k;\n"
    "  tr.innerHTML=`<td class=\"mut\">$${hex(x[0],6)}</td><td>$${hex(x[1],4)}</td><td>${x[5]}</td><td><span class=\"tag\">${HOW[x[2]]}</span></td>`;\n"
    "  tr.onclick=()=>why(k);t.appendChild(tr)}\n"
    " document.getElementById('cellhead').textContent=`Celda $${hex(c*CELL,6)}-$${hex(c*CELL+CELL-1,6)}: ${n} instrucciones`+(cm[c]?' · contiene código ejecutado que no se descubrió':'');\n"
    "}\n"
    "function why(k){\n"
    " document.querySelectorAll('tr.sel').forEach(e=>e.classList.remove('sel'));\n"
    " const row=document.querySelector(`tr[data-k=\"${k}\"]`);if(row)row.classList.add('sel');\n"
    " const ol=document.getElementById('chain');ol.innerHTML='';let cur=k,guard=0;\n"
    " while(cur!==undefined&&guard++<5000){\n"
    "  const x=I[cur];let run=0,first=cur;\n"
    "  while(x&&I[cur][2]===2&&I[cur][3]>=0){cur=byOff.get(I[cur][3]);run++;if(cur===undefined)break}\n"
    "  const a=I[first],b=I[cur]||a,li=document.createElement('li');\n"
    "  li.innerHTML=run?`<code>$${hex(a[0],6)} ${a[5]}</code> <span class=\"mut\">…${run} seguidas desde</span> <code>$${hex(b[0],6)} ${b[5]}</code>`:`<code>$${hex(a[0],6)} $${hex(a[1],4)} ${a[5]}</code>`;\n"
    "  ol.appendChild(li);\n"
    "  const how=b[2],par=b[3];\n"
    "  const m=document.createElement('li');m.className='mut';m.innerHTML=par<0?`↳ punto de entrada: <b>${HOW[how]}</b> (${ORG[b[4]]})`:`↳ <b>${HOW[how]}</b> desde`;ol.appendChild(m);\n"
    "  if(par<0)break; cur=byOff.get(par);\n"
    " }\n"
    "}\n"
    "cv.onclick=e=>pick(cellAt(e));\n"
    "function go(){const v=parseInt(document.getElementById('goto').value,16);if(isNaN(v))return;pick(v/CELL|0);const k=byOff.get(v);if(k!==undefined)why(k)}\n"
    "document.getElementById('go').onclick=go;document.getElementById('goto').onkeydown=e=>{if(e.key==='Enter')go()};\n"
    "matchMedia('(prefers-color-scheme: dark)').onchange=draw;\n"
    "draw();\n"
    "</script></body></html>\n";

/* --codemap: qué bytes del PRG se tomaron como código (C) y cuáles no (.), 64 por línea. El mismo
 * formato que difftest --executed y gbrecomp --codemap, para comparar lo descubierto con lo ejecutado */
static void write_codemap(const char *path)
{
    u8 *code = calloc(prg_size ? prg_size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!code || !f) die("no se pudo crear %s", path);
    for (size_t i = 0; i < prg_size; i++) {
        if (!site[i].state) continue;
        int len = mode_len[ins_tab[site[i].opc].mode];
        for (int k = 0; k < len && i + k < prg_size; k++) code[i + k] = 1;
    }
    for (size_t i = 0; i < prg_size; i++) {
        fputc(code[i] ? 'C' : '.', f);
        if (i % 64 == 63) fputc('\n', f);
    }
    free(code);
    if (fclose(f)) die("no se pudo escribir %s", path);
}

static void write_html(const char *path, const char *rom_name)
{
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    fputs(html_head, f);
    fputs("{\"name\":", f); json_str(f, rom_name);
    fprintf(f, ",\"mapper\":%d,\"prg\":%zu,\"bytes\":%u,\"orgs\":[", cart->mapper, prg_size, stat_bytes);
    for (int i = 0; i < ORG_COUNT; i++) { if (i) fputc(',', f); json_str(f, org_name[i]); }
    fputs("],\"hows\":[", f);
    for (int i = 0; i < HOW_COUNT; i++) { if (i) fputc(',', f); json_str(f, how_name[i]); }
    fputs("],\"missing\":[", f);
    int first = 1;
    if (missing_mark) for (size_t i = 0; i < prg_size; i++) if (missing_mark[i]) { fprintf(f, first ? "%zu" : ",%zu", i); first = 0; }
    fputs("],\"ins\":[\n", f);
    first = 1;
    for (size_t i = 0; i < prg_size; i++) {
        if (!site[i].state) continue;
        char d[64]; disasm(i, d, sizeof d);
        fprintf(f, "%s[%zu,%u,%d,%ld,%d,", first ? "" : ",\n", i, site[i].addr, how_at[i],
                parent[i] == NO_PARENT ? -1L : (long)parent[i], origin[i]);
        json_str(f, d);
        fprintf(f, ",%d]", mode_len[ins_tab[site[i].opc].mode]);
        first = 0;
    }
    fputs("\n]}", f);
    fputs(html_tail, f);
    fclose(f);
}

/* ------------------------------------------------------------------ */

static void sanitize(char *s)
{
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') *s = '_';
}

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *in_path = NULL, *out_dir = NULL, *name = NULL, *dis_arg = NULL;
    const char *traces[32]; int ntraces = 0;
    const char *coverage = NULL, *events_path = NULL, *html_path = NULL, *codemap_path = NULL;
    size_t why_list[32]; int nwhy = 0, viz = 0, force_color = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc && ntraces < 32) traces[ntraces++] = argv[++i];
        else if (!strcmp(argv[i], "--dis") && i + 1 < argc) dis_arg = argv[++i];
        else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) coverage = argv[++i];
        else if (!strcmp(argv[i], "--why") && i + 1 < argc && nwhy < 32) why_list[nwhy++] = why_arg(argv[++i]);
        else if (!strcmp(argv[i], "--viz")) viz = 1;
        else if (!strcmp(argv[i], "--color")) force_color = 1;
        else if (!strcmp(argv[i], "--html") && i + 1 < argc) html_path = argv[++i];
        else if (!strcmp(argv[i], "--events") && i + 1 < argc) events_path = argv[++i];
        else if (!strcmp(argv[i], "--codemap") && i + 1 < argc) codemap_path = argv[++i];
        else if (!strcmp(argv[i], "--no-tables")) use_tables = 0;
        else if (!strcmp(argv[i], "--no-spec")) use_spec = 0;
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--version")) { puts("nesrecomp " NESRECOMP_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { fputs(usage_text, stdout); return 0; }
        else if (argv[i][0] != '-' && !in_path) in_path = argv[i];
        else die("argumento desconocido: %s (ver --help)", argv[i]);
    }
    if (!in_path) { fputs(usage_text, stderr); return 2; }
    if (!out_dir && !dis_arg && !coverage && !nwhy && !viz && !events_path && !html_path && !codemap_path) die("falta -o carpeta");

    rom_file = slurp(in_path, &rom_file_size);
    if (!rom_file) die("no se pudo leer %s", in_path);
    const char *err = NULL;
    if (nes_load(&nes, rom_file, rom_file_size, &err)) die("%s: %s", in_path, err);
    cart = &nes.cart;
    prg_size = cart->prg_size;
    site = calloc(prg_size, sizeof *site);
    parent = malloc(prg_size * sizeof *parent);
    for (size_t i = 0; i < prg_size; i++) parent[i] = NO_PARENT;
    how_at = calloc(prg_size, 1); origin = calloc(prg_size, 1);
    printf("%s: mapper %d, PRG %zu KB, CHR %zu KB%s\n", in_path, cart->mapper, prg_size / 1024,
           cart->chr_is_ram ? 0 : cart->chr_size / 1024, cart->chr_is_ram ? " (RAM)" : "");

    seed_vectors();
    discover();
    if (coverage) { coverage_report(coverage); if (!out_dir && !dis_arg && !viz && !nwhy && !events_path && !html_path && !codemap_path) return 0; }
    unsigned base_insns = stat_insns;
    for (int i = 0; i < ntraces; i++) load_trace(traces[i]);
    discover();
    printf("código: %u instrucciones (%u bytes, %.2f%% del PRG)", stat_insns, stat_bytes, 100.0 * stat_bytes / prg_size);
    if (ntraces) printf(", %u por la traza (%u puntos de entrada)", stat_insns - base_insns, stat_trace);
    printf("\n");
    if (verbose) {
        printf("saltos sin resolver: %u (especulados en todos los bancos: %u destinos, %u aceptados), "
               "saltos indirectos: %u, conflictos de dirección: %u\n",
               stat_unresolved, stat_spec, stat_spec_hits, stat_ind, stat_conflicts);
        printf("tablas: %u con índice, %u en línea, %u destinos; %u pares de puntero, %u tablas sueltas (%u entradas)\n",
               stat_tables, stat_inline, stat_table_entries, stat_pairs, stat_runs, stat_run_entries);
    }

    if (events_path) write_events(events_path);
    if (codemap_path) write_codemap(codemap_path);
    if (html_path) {
        const char *b = strrchr(in_path, '/'); b = b ? b + 1 : in_path;
        write_html(html_path, b);
        printf("visor escrito en %s\n", html_path);
    }
    if (viz) viz_map(force_color || isatty(1));
    for (int i = 0; i < nwhy; i++) explain(why_list[i]);
    if (!out_dir && !dis_arg && (viz || nwhy || events_path || html_path || codemap_path)) return 0;

    if (dis_arg) {
        unsigned long off, addr, cnt;
        if (sscanf(dis_arg, "%lx:%lx:%lu", &off, &addr, &cnt) != 3) die("--dis espera OFFSET:DIR:N");
        for (unsigned long k = 0; k < cnt && off < prg_size; k++) {
            const Ins *in = &ins_tab[cart->prg[off]];
            Site tmp = site[off];
            site[off].opc = cart->prg[off]; site[off].addr = (u16)addr;
            char d[64]; disasm(off, d, sizeof d);
            printf("%06lX  $%04lX  %s\n", off, addr, d);
            int l = mode_len[in->mode]; off += l; addr += l; (void)tmp;
        }
        if (!out_dir) return 0;
    }

    partition();
    printf("%zu funciones\n", nfuncs);

    char sha1[41];
    sha1_hex(rom_file, rom_file_size, sha1);
    char nm[128];
    if (name) snprintf(nm, sizeof nm, "%s", name);
    else {
        const char *b = strrchr(in_path, '/'); b = b ? b + 1 : in_path;
        snprintf(nm, sizeof nm, "%s", b);
        char *dot = strrchr(nm, '.'); if (dot) *dot = 0;
    }
    sanitize(nm);

    make_dir(out_dir);
    char gen[1024]; snprintf(gen, sizeof gen, "%s/gen", out_dir);
    make_dir(gen);
    /* se borra el código de una corrida anterior */
    DIR *d = opendir(gen);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strncmp(e->d_name, "code_", 5)) {
                char p[1280]; snprintf(p, sizeof p, "%s/%s", gen, e->d_name); remove(p);
            }
        }
        closedir(d);
    }
    emit_code(out_dir);
    emit_dispatch(out_dir);
    emit_ram(out_dir);
    emit_info(out_dir, sha1, nm);
    write_embedded(out_dir);
    emit_makefile(out_dir, nm);
    emit_readme(out_dir, nm, sha1);
    printf("proyecto escrito en %s/ (ejecutable: %s)\n", out_dir, nm);
    return 0;
}
