/*
 * neorecomp: recompilador estático de juegos de Neo Geo a C99.
 *
 * Uso: neorecomp juego -o salida/ [--trace traza.txt] [--name nombre] [--bios archivo]
 *      neorecomp juego [--info] [--bios archivo] [--neo salida.neo]
 *
 * Genera un proyecto completo (código recompilado del juego y de la BIOS, runtime, plataforma y
 * Makefile). El proyecto no contiene datos del juego ni de la BIOS: el ejecutable los carga y valida
 * sus SHA1.
 *
 * Hay dos espacios de identidad, y el mismo análisis corre en cada uno:
 *   - el juego: el desplazamiento en la imagen P normalizada (el primer MB es $000000-$0FFFFF; el MB
 *     n + 1, el banco n de la ventana $200000-$2FFFFF)
 *   - la BIOS: el desplazamiento dentro de sus 128 KB ($C00000-$CFFFFF, con espejos); en las trazas, el
 *     banco FF
 * Los saltos de uno al otro (el juego llamando a la BIOS, la BIOS saltando a la cabecera del juego) no
 * se siguen: salen al despachador. La BIOS se recompila una vez por versión en una caché del usuario
 * (~/.neorecomp/bios/<sha1>/, o $NEO_BIOS_CACHE), que crece con las líneas de la BIOS de las trazas de
 * cada juego; cada proyecto lleva una copia.
 *
 * Pasos, en cada espacio:
 *   1. Descubrimiento: recorrido desde los puntos de entrada (juego: sus vectores y las entradas de la
 *      cabecera, $122 USER, $128 PLAYER_START, $12E DEMO_END, $134 COIN_SOUND; BIOS: sus vectores y su
 *      tabla de saltos $C00402-$C004DA) siguiendo Bcc, BRA, BSR, DBcc y los JMP/JSR con destino
 *      constante, más las heurísticas de tablas de saltos y punteros de GENRecomp y las trazas
 *      (--trace: las escriben neotrace, la ventana con --trace y difftest --uncovered).
 *   2. Partición en funciones: tramos contiguos de ROM que no cruzan una ventana de 1 MB.
 *   3. Emisión de C: cada instrucción llama a su k_* de m68k_ops.h (la misma semántica que el
 *      intérprete) con el opcode, las palabras de extensión y el PC constantes.
 *
 * Lo que no se descubrió, o está en RAM, lo ejecuta el intérprete. Descubrir de más no rompe nada (lo
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
#include "neodb.h"
#include "sha1.h"
#include "console.h"
#include "trace_line.h"
#include "embedded.h"
#include "dis68k.h"

#ifndef NEORECOMP_VERSION
#define NEORECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "neorecomp " NEORECOMP_VERSION ": recompila un juego de Neo Geo a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: neorecomp juego -o salida/ [opciones]\n"
    "     neorecomp juego [--info]\n"
    "  juego              un .neo, un .zip al estilo MAME (mslug.zip) o una carpeta con los chips\n"
    "  -o carpeta         dónde escribir el proyecto (se crea si no existe)\n"
    "  --name nombre      nombre del ejecutable (por omisión, el del juego)\n"
    "  --bios archivo     la BIOS del AES que se recompila con el juego: neo-epo.bin, neo-po.bin, el\n"
    "                     aes.zip de MAME o la propia, neobios.bin (se puede repetir; por omisión,\n"
    "                     NEO_BIOS o la que se encuentre junto al juego o en roms/: la propia primero)\n"
    "  --trace archivo    instrucciones que ejecutó el juego (las escriben neotrace, la ventana con\n"
    "                     --trace y difftest --uncovered); se agregan como entradas (se puede repetir).\n"
    "                     Las de la BIOS (banco FF) se suman a la caché de su versión\n"
    "  --coverage archivo compara lo descubierto sin traza con las instrucciones que ejecutó el juego\n"
    "                     (una traza): cuántas se encontraron y cuáles faltan\n"
    "  --why BB:DIRECCION explica por qué esa instrucción (banco y dirección como en la traza, o su\n"
    "                     desplazamiento hexadecimal en P) se tomó como código: la cadena de saltos y\n"
    "                     tablas desde un punto de entrada (se puede repetir; sin -o no genera el proyecto)\n"
    "  --viz              mapa del programa en la terminal, coloreado por cómo se descubrió (con\n"
    "                     --coverage, en rojo lo que el juego ejecutó y no se encontró)\n"
    "  --color            con --viz, colorea aunque la salida no sea una terminal (lo usa la GUI)\n"
    "  --html archivo     el descubrimiento como una página HTML sola: mapa, instrucciones por celda y\n"
    "                     la cadena de \"por qué\" al hacer clic (con --coverage marca lo que falta)\n"
    "  --events archivo   cada instrucción descubierta con su origen (texto: desplazamiento, dirección,\n"
    "                     cómo se llegó, de dónde, origen, desensamblado)\n"
    "  --codemap archivo  qué bytes del programa se tomaron como código: una letra por byte (C = código),\n"
    "                     64 por línea; el mismo formato que difftest --executed\n"
    "  --no-tables        sin las heurísticas de tablas de saltos (para comparar)\n"
    "  --no-spec          sin los punteros sueltos: inmediatos y tablas que ningún código delata\n"
    "  --dis OFF:DIR:N    desensambla N instrucciones desde ese desplazamiento de P, con esa dirección\n"
    "  --neo archivo      escribe el juego como .neo (el formato de Geolith y de otros emuladores)\n"
    "  --info             formato, regiones, SHA1, cabecera y vectores (lo que hace sin otras opciones)\n"
    "  --verbose          detalles del descubrimiento\n"
    "  --version, --help\n";

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "neorecomp: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
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

static const char *format_name(int f)
{
    return f == ROMFMT_NEO ? ".neo" : f == ROMFMT_ZIP ? "zip al estilo MAME" : "carpeta con los chips";
}

/* Tamaño legible: KB o MB */
static void size_text(char *out, size_t n, size_t bytes)
{
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) snprintf(out, n, "%zu MB", bytes >> 20);
    else snprintf(out, n, "%zu KB", (bytes + 1023) >> 10);
}

static void print_info(const char *path, const Cart *c, const Bios *b, const char *bios_err, const u8 *l0)
{
    const NeoHeader *h = &c->hdr;
    char sz[32];
    size_text(sz, sizeof sz, c->size);
    printf("%s: %s, %s\n", path, format_name(c->format), sz);
    if (c->title[0] || c->set[0]) {
        printf("juego:       %s", c->title[0] ? c->title : c->set);
        if (c->set[0] || c->maker[0] || c->year) {
            printf(" (");
            const char *sep = "";
            if (c->set[0]) { printf("%s", c->set); sep = ", "; }
            if (c->maker[0]) { printf("%s%s", sep, c->maker); sep = " "; }
            if (c->year) printf("%s%u", c->maker[0] ? " " : sep, c->year);
            printf(")");
        }
        printf("%s\n", c->db ? "" : " (no está en la base de datos)");
    } else if (c->format != ROMFMT_NEO) printf("juego:       no está en la base de datos (armado por los nombres de los archivos)\n");
    printf("SHA1:        %s\n", c->sha1);
    printf("regiones:\n");
    for (int r = 0; r < REG_COUNT; r++) {
        size_text(sz, sizeof sz, c->len[r]);
        printf("  %-4s %8s  %s", neo_region_name[r], c->len[r] ? sz : "-", c->len[r] ? c->reg_sha1[r] : "");
        if (r == REG_VB && !c->len[r]) printf("(ADPCM-B usa el bus A)");
        if (c->files[r][0]) printf("  %s", c->files[r]);
        if (r == REG_P && c->p1_swapped) printf(" (mitades cambiadas)");
        printf("\n");
    }
    if (!h->neo) printf("aviso: no hay \"NEO-GEO\" en $100\n");
    else {
        size_text(sz, sizeof sz, h->p_size);
        printf("cabecera:    NEO-GEO, NGH $%04X, programa de %s (declarado)%s\n", h->ngh, sz,
               h->p_size != c->len[REG_P] ? ", distinto del real" : "");
        if (c->p_banks > 1) printf("P2:          %d bancos de 1 MB en $200000\n", c->p_banks);
        else if (c->p_banks) printf("P2:          fijo en $200000 (sin bancos)\n");
        printf("respaldo:    $%06X, %u bytes\n", h->backup_ptr, h->backup_size);
        static const char *const eye[] = { "la hace la BIOS", "la hace el juego", "ninguna" };
        printf("logo de SNK: %s\n", h->eyecatch < 3 ? eye[h->eyecatch] : "valor desconocido");
        printf("DIP:         Japón $%06X, EE. UU. $%06X, Europa $%06X\n", h->dip[0], h->dip[1], h->dip[2]);
        printf("entradas:\n");
        for (int i = 0; i < ENTRY_COUNT; i++)
            if (h->entry[i]) printf("  %-13s $%06X\n", neo_entry_name[i], h->entry[i]);
            else printf("  %-13s (sin JMP)\n", neo_entry_name[i]);
    }
    printf("vectores:\n");
    for (int n = 0; n < VEC_COUNT; n++) {
        const char *name = cart_vector_name(n);
        char trap[16];
        if (!name && n >= VEC_TRAP0 && n < VEC_TRAP0 + 16) { snprintf(trap, sizeof trap, "TRAP #%d", n - VEC_TRAP0); name = trap; }
        if (name) printf("  %2d %s%*s $%06X\n", n, name, 24 - utf8_width(name), "", c->vec[n]);
    }
    int fatal;
    const char *u = cart_unsupported(c, &fatal);
    if (u) printf("%s %s\n", fatal ? "no se puede:" : "aviso:      ", u);
    if (b->ver) {
        /* $400: 0 AES, $80 MVS; $401: región que ven los juegos (no hay BIOS del AES de EE. UU.) */
        static const char *const region[] = { "Japón", "EE. UU.", "Europa" };
        u8 reg = b->rom[0x401 ^ 1];
        printf("BIOS:        %s, %s, región %s (SHA1 %s)\n", b->path, b->ver->desc, reg < 3 ? region[reg] : "desconocida", b->sha1);
    }
    else printf("BIOS:        %s\n", bios_err);
    char l0sha[41];
    sha1_hex(l0, L0_SIZE, l0sha);
    printf("tabla L0:    calculada, SHA1 %s (%s)%s\n", l0sha, strcmp(l0sha, L0_SHA1) ? "NO es la del chip" : "la del chip",
           b->l0_checked > 0 ? "; igual a la 000-lo.lo de la BIOS" : b->l0_checked < 0 ? "; DISTINTA de la 000-lo.lo de la BIOS" : "");
}


/* ------------------------------------------------------------------ */
/* El espacio que se analiza                                           */
/* ------------------------------------------------------------------ */

enum { SPACE_GAME, SPACE_BIOS };

static Neo *neo;                    /* el juego y la BIOS cargados */
static int space;                   /* el espacio del análisis en curso */
static const u8 *rom;               /* su imagen (palabras con los bytes invertidos, como los chips) */
static size_t rom_size;
static u32 bank_size;               /* ventana que el código no cruza: 1 MB en el juego, la BIOS entera */
static int p_banks;                 /* bancos de la ventana P2 del juego (más de 1: cambian) */
static int verbose;
static int use_tables = 1;          /* --no-tables: sin las heurísticas de tablas de saltos */
static int use_spec = 1;            /* --no-spec: sin los punteros sueltos (inmediatos y tablas sin código que las use) */

/* Cómo se llegó a una instrucción (para --why, --viz, --events y --html) */
enum { HOW_VECTOR, HOW_TRACE, HOW_LINEAR, HOW_JMP, HOW_JSR, HOW_BRANCH, HOW_TABLE, HOW_RUN, HOW_IMM, HOW_AFTER, HOW_ENTRY, HOW_COUNT };
static const char *const how_name[HOW_COUNT] = { "vector", "traza", "sigue", "salto", "llamada", "bifurcación", "tabla",
                                                 "tabla suelta", "puntero inmediato", "tras un final", "entrada" };
enum { ORG_FLOW, ORG_TRACE, ORG_TABLE, ORG_SPEC, ORG_COUNT };
static const char *const org_name[ORG_COUNT] = { "flujo desde los puntos de entrada", "traza", "tabla de saltos",
                                                 "especulativo (punteros sueltos)" };
#define NO_PARENT 0xFFFFFFFFu
#define MULTI (-2)                  /* un destino en la ventana P2 visto desde fuera: puede ser cualquier banco */

/* Por palabra de ROM (desplazamiento / 2) */
static u8  *is_ins;                 /* empieza una instrucción ahí */
static u32 *ins_addr;               /* con qué dirección del CPU se descubrió */
static u32 *parent;                 /* de qué instrucción se llegó (desplazamiento) o NO_PARENT */
static u8  *how_at, *origin;
static u8  *missing_mark;           /* --coverage: ejecutadas y no descubiertas */

static unsigned stat_insns, stat_bytes, stat_seeds, stat_trace, stat_ind, stat_unresolved;
static unsigned stat_tables, stat_table_entries, stat_runs, stat_run_entries, stat_imm, stat_after;

typedef struct { u32 off, addr, from; u8 how; } Work;
typedef struct { Work *w; size_t n, cap; } Queue;
static Queue sure, spec;            /* lo especulativo se recorre cuando ya no queda nada seguro */
static u32 cur_from = NO_PARENT;    /* instrucción que está haciendo el descubrimiento */
static int cur_how = HOW_JMP;

static u16 romw(size_t off) { return off + 1 < rom_size ? (u16)(rom[off + 1] << 8 | rom[off]) : 0xFFFF; }
static u32 roml(size_t off) { return (u32)romw(off) << 16 | romw(off + 2); }
static int ins_len(size_t off) { return 2 + 2 * m68k_ext_words(romw(off)); }

/* Empieza el análisis de un espacio: libera lo del anterior y pone todo en cero */
static void space_begin(int sp, const u8 *img, size_t size)
{
    space = sp;
    rom = img;
    rom_size = size;
    bank_size = sp == SPACE_GAME ? P_BANK_SIZE : BIOS_SIZE;
    free(is_ins); free(ins_addr); free(parent); free(how_at); free(origin); free(missing_mark);
    size_t words = rom_size / 2 + 1;
    is_ins = calloc(words, 1);
    ins_addr = calloc(words, sizeof *ins_addr);
    parent = malloc(words * sizeof *parent);
    how_at = calloc(words, 1);
    origin = calloc(words, 1);
    missing_mark = NULL;
    if (!is_ins || !ins_addr || !parent || !how_at || !origin) die("sin memoria");
    for (size_t i = 0; i < words; i++) parent[i] = NO_PARENT;
    stat_insns = stat_bytes = stat_seeds = stat_trace = stat_ind = stat_unresolved = 0;
    stat_tables = stat_table_entries = stat_runs = stat_run_entries = stat_imm = stat_after = 0;
    sure.n = spec.n = 0;
    cur_from = NO_PARENT;
}

/* Texto de la instrucción en off (con la dirección dada) */
static void disasm_at(size_t off, u32 addr, char *out, size_t size)
{
    u16 w[5];
    for (int i = 0; i < 5; i++) w[i] = romw(off + 2 * (size_t)i);
    dis68k(w, addr, out, size);
}

/* Desplazamiento en el espacio actual de la dirección `addr`, visto desde la instrucción en from_off
 * (-1 si no se sabe): el juego ve P1 en $000000 (con espejos) y P2 en $200000 (desde fuera de la
 * ventana, con varios bancos, MULTI); la BIOS se ve en $C00000-$CFFFFF. -1 si no es de este espacio */
static long space_offset(u32 addr, long from_off)
{
    addr &= 0xFFFFFF;
    if (addr & 1) return -1;
    if (space == SPACE_BIOS) return addr >= 0xC00000 && addr < 0xD00000 ? (long)(addr & (BIOS_SIZE - 1)) : -1;
    if (addr < 0x100000) {
        size_t p1 = rom_size < P_BANK_SIZE ? rom_size : P_BANK_SIZE;
        return (long)(addr % p1);
    }
    if (addr >= 0x200000 && addr < 0x300000 && p_banks) {
        size_t base;
        if (p_banks == 1) base = P_BANK_SIZE;
        else if (from_off >= (long)P_BANK_SIZE) base = (size_t)from_off & ~(size_t)(P_BANK_SIZE - 1);
        else return MULTI;
        size_t n = rom_size - base < P_BANK_SIZE ? rom_size - base : P_BANK_SIZE;
        return (long)(base + (addr & 0xFFFFF) % n);
    }
    return -1;
}

/* La instrucción en `off` del banco b de la ventana P2 */
static long bank_offset(int b, u32 addr)
{
    size_t base = P_BANK_SIZE * (1 + (size_t)b);
    size_t n = rom_size - base < P_BANK_SIZE ? rom_size - base : P_BANK_SIZE;
    return (long)(base + (addr & 0xFFFFF) % n);
}

/* ¿Una dirección donde puede haber código? (el programa, la RAM, la BIOS) */
static int code_addr(u32 a)
{
    a &= 0xFFFFFF;
    return a < 0x300000 || (a >= 0xC00000 && a < 0xD00000);
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

/* Desplazamiento de un destino `target` visto desde la instrucción (off, addr); -1 si no es de este
 * espacio (MULTI si puede ser cualquier banco de P2) */
static long target_offset(u32 off, u32 addr, u32 target)
{
    (void)addr;
    return space_offset(target, (long)off);
}

static int plausible(u32 off, u32 addr, int strict);

static void follow(u32 off, u32 addr, u32 target)
{
    long t = target_offset(off, addr, target);
    if (t >= 0) push_work((u32)t, target);
    else if (t == MULTI) {
        /* Un salto a la ventana P2 desde fuera: cualquier banco puede ser el que esté; se toman los que
         * tienen pinta de código (como en NESRecomp) */
        for (int b = 0; b < p_banks; b++) {
            long o = bank_offset(b, target);
            if (is_ins[o >> 1] || plausible((u32)o, target & 0xFFFFFF, 0)) push_work((u32)o, target);
        }
    } else {
        stat_unresolved++;
        if (verbose) printf("  $%06X: destino $%06X fuera de este espacio\n", addr, target & 0xFFFFFF);
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

/* ¿Una dirección absoluta que un programa del Neo Geo usaría? (el programa, la RAM, la E/S, el LSPC, la
 * paleta, la tarjeta, la BIOS y la RAM de respaldo del MVS en $D00000, que la BIOS también mira) */
static int sane_abs(u32 a, int is_long)
{
    if (is_long && (a >> 24) != 0x00 && (a >> 24) != 0xFF) return 0;
    a &= 0xFFFFFF;
    return a < 0x300000 || (a >= 0x300000 && a < 0x3E0000) || (a >= 0x400000 && a < 0x800000) ||
           (a >= 0x800000 && a < 0xC00000) || (a >= 0xC00000 && a < 0xD10000);
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

/* Los primeros bytes de cada espacio no son código: los vectores y la cabecera ($000-$1FF) del juego,
 * los vectores de la BIOS ($000-$0FF) */
static int below_code(u32 off) { return off < (space == SPACE_GAME ? 0x200u : 0x100u); }

/*
 * ¿Lo que hay en (off, addr) parece el comienzo de una rutina? Se decodifica en línea recta, sin
 * guardar nada: opcodes válidos (ni ILLEGAL, ni línea A/F, ni ori.b #0,d0: ceros), direcciones absolutas
 * razonables y destinos de saltos dentro del espacio, hasta un RTS/RTE/RTR/BRA/JMP (o código ya
 * descubierto). Sin `strict` basta con llegar a ese final o con 64 instrucciones válidas seguidas.
 * Con `strict` (punteros sueltos, sin un código que los use como tabla) el final tiene que llegar en
 * menos de 64 instrucciones; las rutinas largas (de inicialización, de estados) valen si dan señales
 * de código: un final antes de 160 instrucciones con algo de evidencia, o 160 instrucciones con mucha.
 * La evidencia: llamadas a rutinas ya descubiertas, saltos que caen justo en una de las instrucciones
 * decodificadas y accesos absolutos a la RAM, la E/S, la paleta o la BIOS. Los datos al azar casi
 * nunca pasan.
 */
static int abs_evidence(size_t off, u16 op, int kind);

#define PLAUSIBLE_SHORT 64
#define PLAUSIBLE_LONG  160

static int plausible(u32 off, u32 addr, int strict)
{
    if ((off & 1) || (addr & 1) || below_code(off)) return 0;
    int evidence = 0, nstart = 0, nfwd = 0, n, max = strict ? PLAUSIBLE_LONG : PLAUSIBLE_SHORT, ended = 0;
    u32 starts[PLAUSIBLE_LONG], fwd[PLAUSIBLE_LONG];
    for (n = 0; n < max && !ended; n++) {
        if (off + 2 > rom_size) return 0;
        if (is_ins[off >> 1]) { ended = 1; break; }    /* se une a código ya descubierto */
        starts[nstart++] = addr;
        u16 op = romw(off);
        int kind = m68k_decode(op), len = ins_len(off);
        if ((op == 0x0000 && romw(off + 2) == 0x0000) || kind == K_ILLEGAL || kind == K_LINEA || kind == K_LINEF) return 0;
        if (off + (u32)len > rom_size || !sane_operands(off, op, kind)) return 0;
        switch (kind) {
        case K_BCC: case K_DBCC: {
            u32 d8 = op & 0xFF;
            u32 to = addr + 2 + (kind == K_BCC && d8 ? sx8(d8) : sx16(romw(off + 2)));
            if (target_offset(off, addr, to) == -1) return 0;
            if (kind == K_BCC && (op >> 8 & 15) == 0) { ended = 1; break; }     /* BRA */
            /* un salto corto hacia atrás o adelante que cae justo en una de estas instrucciones */
            if (to < addr) { for (int k = 0; k < nstart; k++) if (starts[k] == to) { evidence += 3; break; } }
            else if (to - addr < 0x400 && nfwd < PLAUSIBLE_LONG) fwd[nfwd++] = to;
            break; }
        case K_JMP: case K_JSR: {
            long t = jump_target(off, addr, op);
            if (t >= 0 && !code_addr((u32)t)) return 0;
            if (kind == K_JMP) { ended = 1; break; }
            if (t >= 0) {                               /* llamada a una rutina ya descubierta */
                long to = target_offset(off, addr, (u32)t);
                if (to >= 0 && is_ins[to >> 1]) evidence += 3;
            }
            break; }
        case K_RTS: case K_RTE: case K_RTR: ended = 1; break;
        default: evidence += abs_evidence(off, op, kind); break;
        }
        off += (u32)len;
        addr += (u32)len;
        if ((off ^ (off - (u32)len)) & ~(bank_size - 1)) return 0;     /* cruzó de ventana */
    }
    if (n <= PLAUSIBLE_SHORT && (ended || !strict)) return 1;
    for (int j = 0; j < nfwd; j++)
        for (int k = 0; k < nstart; k++) if (starts[k] == fwd[j]) { evidence += 3; break; }
    return ended ? evidence >= 4 : evidence >= 8;
}

/* ¿Una dirección absoluta larga a la RAM ($100000), la E/S y el LSPC ($300000-$3DFFFF), la paleta o
 * la BIOS? Solo en los operandos de MOVE y de las operaciones de un operando, como sane_operands */
static int abs_target(u32 a)
{
    a &= 0xFFFFFF;
    return (a >= 0x100000 && a < 0x110000) || (a >= 0x300000 && a < 0x3E0000) || (a >= 0x400000 && a < 0x402000) ||
           (a >= 0xC00000 && a < 0xC20000);
}

static int abs_evidence(size_t off, u16 op, int kind)
{
    int i = 1, e = 0;
    int ea[2][2] = { { op >> 3 & 7, op & 7 }, { op >> 6 & 7, op >> 9 & 7 } }, n = 0;
    switch (kind) {
    case K_MOVE: case K_MOVEA: n = 2; break;
    case K_TST: case K_UNARY: case K_ALU: case K_QUICK: n = 1; break;
    default: return 0;
    }
    int sz = kind == K_MOVE || kind == K_MOVEA ? ((op >> 12 & 3) == 1 ? 0 : (op >> 12 & 3) == 3 ? 1 : 2) : op >> 6 & 3;
    for (int k = 0; k < n; k++) {
        int mode = ea[k][0], reg = ea[k][1];
        if (mode == 7 && reg == 1 && abs_target(roml(off + 2 * (size_t)i))) e++;
        sane_ea(off, mode, reg, sz, &i);
    }
    return e;
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

/* Tabla de desplazamientos de 16 bits relativos a la base (move.w tbl(pc,d0.w),d1 / jmp tbl(pc,d1.w)).
 * Termina donde empieza la primera rutina o en la primera entrada que no apunta a algo con pinta de
 * código. Lo especulativo no la corta: "tras un final" pudo tomar la tabla, que sigue a un BRA, por
 * código. */
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
        if (p + 2 > end || (i && is_ins[p >> 1] && origin[p >> 1] != ORG_SPEC)) break;
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
static int scan_branch_table(u32 src_off, u32 src_addr, u32 base)
{
    long tb = target_offset(src_off, src_addr, base);
    if (tb < 0) return 0;
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
        if (to < 0) {
            if (s == 6 && code_addr(t) && to == -1) { push_work((u32)p, a); good++; continue; }  /* JMP al otro espacio */
            break;
        }
        if (!(is_ins[to >> 1] || plausible((u32)to, t & 0xFFFFFF, 0))) break;
        push_work((u32)p, a);
        good++;
    }
    if (verbose) printf("  $%06X: tabla de saltos en $%06X, %d entradas\n", src_addr, base, good);
    if (good) { stat_tables++; stat_table_entries += (unsigned)good; }
    cur_how = saved;
    return good;
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
        if (p + 4 > (long)rom_size || (i && is_ins[p >> 1] && origin[p >> 1] != ORG_SPEC)) break;
        u32 t = roml((size_t)p);
        if (t >> 24) break;
        long to = space_offset(t, (long)src_off);
        if (to < 0 || !(is_ins[to >> 1] || plausible((u32)to, t, 0))) break;
        push_work((u32)to, t);
        good++;
    }
    if (verbose) printf("  $%06X: tabla de punteros en $%06X, %d entradas\n", src_addr, base, good);
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
        u32 base, tbase, k;
        /* movea.l tbls(pc,d0.w),a0 / move.w 0(a0,d0.w),d1 / jmp 0(a0,d1.w): cada puntero de tbls es una
         * tabla de desplazamientos relativos a ella misma */
        if (mode == 6 && !known_areg(h, reg, &k) && loaded_from_table(h, 1, reg, &tbase) == 2) {
            long pt = target_offset(off, addr, tbase);
            for (int i = 0; pt >= 0 && i < 64 && pt + 4 * (long)i + 4 <= (long)rom_size; i++) {
                u32 t = roml((size_t)pt + 4 * (size_t)i);
                if (t >> 24 || (t & 1) || target_offset(off, addr, t) < 0) break;
                int dup = 0;
                for (int j = 0; j < i && !dup; j++) dup = roml((size_t)pt + 4 * (size_t)j) == t;
                if (!dup) scan_offset_table(off, addr, t);
            }
            return;
        }
        if (!indexed_base(h, mode, reg, x, addr + 2, &base)) return;
        int sz = loaded_from_table(h, (x & 0x8000) != 0, x >> 12 & 7, &tbase);
        if (sz == 1 && tbase == base) scan_offset_table(off, addr, base);
        else if (!scan_branch_table(off, addr, base)) {
            /* jmp blk(pc,d4.w) sin tabla: se entra a la mitad de un bloque desenrollado (move.l d7,(a6) /
             * add.l d3,d7 ...); el bloque es código desde blk */
            long tb = target_offset(off, addr, base);
            if (tb >= 0 && !is_ins[tb >> 1] && plausible((u32)tb, base, 0)) {
                int saved = cur_how;
                cur_how = HOW_TABLE;
                push_work((u32)tb, base);
                cur_how = saved;
            }
        }
        return;
    }
    if (mode == 2 || mode == 5) {
        u32 d = mode == 5 ? sx16(x) : 0, base, k;
        int sz = loaded_from_table(h, 1, reg, &base);
        if (sz == 2) { scan_ptr_table(off, addr, base); return; }
        /* lea tbl,a0 / move.w 0(a0,d0.w),d0 / lea 0(a0,d0.w),a0 / jsr (a0): desplazamientos desde tbl */
        for (int i = h - 1; !sz && i >= 0 && i >= h - 10; i--) {
            u32 o = hist[i];
            if (!writes_areg(o, reg)) continue;
            u16 lop = romw(o);
            int lm = lop >> 3 & 7, lr = lop & 7;
            if (m68k_decode(lop) == K_LEA && (lm == 6 || (lm == 7 && lr == 3))) {
                u16 lx = romw(o + 2);
                u32 b, tb;
                if (indexed_base(i, lm, lr, lx, ins_addr[o >> 1] + 2, &b) &&
                    loaded_from_table(i, (lx & 0x8000) != 0, lx >> 12 & 7, &tb) == 1 && tb == b) {
                    scan_offset_table(off, addr, b);
                    return;
                }
            }
            break;
        }
        if (!sz && known_areg(h, reg, &k)) {                       /* lea rutina,a0 / jmp (a0) */
            int saved = cur_how;
            cur_how = HOW_TABLE;
            follow(off, addr, k + d);
            cur_how = saved;
        }
    }
}

/* MOVE.L #rutina,... / MOVEA.L #rutina,An / PEA rutina / LEA rutina,An: un puntero a código que se
 * guarda para llamarlo después */
static void immediate_pointer(u32 off, u32 addr, u16 op)
{
    u32 t;
    if ((op >> 12) == 2 && (op & 0x3F) == 0x3C && (op >> 6 & 7) != 0) t = roml(off + 2);     /* no a un Dn */
    else if (op == 0x4879) t = roml(off + 2);
    else if (op == 0x487A) t = addr + 2 + sx16(romw(off + 2));
    else if ((op & 0xF1FF) == 0x41FA) t = addr + 2 + sx16(romw(off + 2));     /* LEA d16(PC),An */
    else if ((op & 0xF1FF) == 0x41F9) t = roml(off + 2);                     /* LEA (xxx).l,An */
    else return;
    if (t >> 24 || (t & 1)) return;
    long to = space_offset(t, (long)off);
    /* move.l #siguiente,estado(a6) / rts / [unos datos] / siguiente: (el próximo estado de un objeto,
     * justo después): basta con que parezca código */
    u32 len = (u32)ins_len(off);
    int next_state = (op >> 12) == 2 && off + len + 2 <= rom_size && romw(off + len) == 0x4E75 &&
                     t >= addr + len + 2 && t <= addr + len + 2 + 16;
    if (to < 0 || below_code((u32)to) || is_ins[to >> 1] || !plausible((u32)to, t, !next_state)) return;
    int saved = cur_how;
    cur_how = HOW_IMM;
    push_work((u32)to, t);
    stat_imm++;
    cur_how = saved;
}

/* Tablas de punteros que ningún patrón de código delata: cuatro o más largos con el mismo paso (4, 6 u 8
 * bytes: tablas de punteros sueltos o de registros con un puntero, como las de objetos y estados de
 * SNK) que apuntan, todos, a algo con pinta de rutina. Solo donde la dirección dice el desplazamiento:
 * P1 en el juego, la BIOS entera en la BIOS */
static void scan_pointer_runs(void)
{
    cur_how = HOW_RUN;
    cur_from = NO_PARENT;
    u32 base = space == SPACE_GAME ? 0 : 0xC00000;
    size_t lim = rom_size < bank_size ? rom_size : bank_size;
    size_t first = below_code(0) ? 0x200 : 0x100;
    /* ok[o / 2]: el largo en o apunta a algo con pinta de rutina (0 sin calcular, 1 sí, 2 no) */
    u8 *ok = calloc(lim / 2 + 1, 1);
    if (!ok) die("sin memoria");
    for (int stride = 4; stride <= 8; stride += 2) {
        for (size_t start = first; start < first + (size_t)stride && start + 4 <= lim; start += 2) {
            int run = 0;
            for (size_t o = start; o + 4 <= lim; o += (size_t)stride) {
                if (!ok[o >> 1]) {
                    u32 v = roml(o);
                    long to = -1;
                    if (v >= base && v - base < lim && !(v & 1) && !below_code(v - base)) to = (long)(v - base);
                    ok[o >> 1] = to >= 0 && (is_ins[to >> 1] || plausible((u32)to, v, 1)) ? 1 : 2;
                }
                if (ok[o >> 1] != 1) { run = 0; continue; }
                run++;
                if (run < 4) continue;
                if (run == 4) stat_runs++;
                for (int k = run == 4 ? 3 : 0; k >= 0; k--) {
                    u32 kv = roml(o - (size_t)k * (size_t)stride);
                    push_work(kv - base, kv);
                    stat_run_entries++;
                }
            }
        }
    }
    free(ok);
}

/* Anclas de máquinas de estados: move.l #X,(An) o move.l #X,d16(An) donde X es la instrucción que
 * sigue (o la que sigue a un RTS inmediato): el objeto guarda su próximo estado, que empieza justo
 * ahí. Que unos datos valgan justo su propia dirección más 6 u 8 es casi imposible: se toman como
 * código, en todo el programa (en cada banco de P2, con la dirección de la ventana) */
static void scan_state_anchors(void)
{
    cur_how = HOW_IMM;
    cur_from = NO_PARENT;
    for (size_t o = below_code(0) ? 0x200 : 0x100; o + 8 <= rom_size; o += 2) {
        u16 op = romw(o);
        u32 len;
        if ((op & 0xF1FF) == 0x20BC) len = 6;                           /* move.l #imm,(An) */
        else if ((op & 0xF1FF) == 0x217C) len = 8;                      /* move.l #imm,d16(An) */
        else continue;
        u32 addr = space == SPACE_BIOS ? 0xC00000 | (u32)o :
                   o < P_BANK_SIZE ? (u32)o : 0x200000 | ((u32)o & (P_BANK_SIZE - 1));
        u32 t = roml(o + 2);
        int ok = t == addr + len;
        if (!ok && o + len + 2 <= rom_size && romw(o + len) == 0x4E75) ok = t >= addr + len + 2 && t <= addr + len + 2 + 16;
        if (!ok || is_ins[o >> 1]) continue;
        stat_imm++;
        push_work((u32)o, addr);
    }
}

/* Después de un final de rutina (RTS, JMP, BRA...) suele empezar otra: las rutinas van una detrás
 * de otra aunque solo se llegue a ellas por saltos calculados. Si lo que sigue parece código, se toma. */
static void after_end(u32 off, u32 addr, int len)
{
    if (!use_spec) return;
    u32 n = off + (u32)len, a = addr + (u32)len;
    if (n + 2 > rom_size || (n ^ off) & ~(bank_size - 1) || is_ins[n >> 1]) return;
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
        /* La siguiente, a continuación en la misma ventana */
        u32 t = off + (u32)len;
        if ((t ^ off) & ~(bank_size - 1)) return;
        off = t;
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
    int runs_done = 0, anchors_done = 0;
    for (;;) {
        drain();
        unsigned before = stat_insns;
        if (use_spec && use_tables && !runs_done) { runs_done = 1; scan_pointer_runs(); }
        if (use_spec && !anchors_done) { anchors_done = 1; scan_state_anchors(); }
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

/* Los puntos de entrada del espacio: los vectores (y la cabecera del juego o la tabla de la BIOS) */
static void seed_entries(void)
{
    if (space == SPACE_GAME) {
        for (int v = 1; v < VEC_COUNT; v++) {
            u32 a = neo->cart.vec[v] & 0xFFFFFF;
            long off = space_offset(a, -1);
            /* los vectores sin usar suelen valer 0: debajo de $200 están los vectores y la cabecera */
            if (off < 0 || below_code((u32)off)) continue;
            if (verbose) printf("vector %2d = $%06X\n", v, a);
            seed((u32)off, a, HOW_VECTOR);
        }
        /* La BIOS salta a los JMP de la cabecera: USER, PLAYER_START, DEMO_END y COIN_SOUND */
        for (u32 a = 0x122; a <= 0x134; a += 6)
            if (romw(a) == 0x4EF9) seed(a, a, HOW_ENTRY);
    } else {
        for (int v = 1; v < VEC_COUNT; v++) {
            u32 a = roml(4 * (size_t)v) & 0xFFFFFF;
            long off = space_offset(a, -1);
            if (off < 0 || below_code((u32)off)) continue;
            seed((u32)off, a, HOW_VECTOR);
        }
        /* La tabla de saltos de la BIOS: lo que llaman los juegos */
        for (u32 o = 0x402; o <= 0x4DA; o += 6) seed(o, 0xC00000 | o, HOW_ENTRY);
    }
}

/* --why: "BB:DIRECCION" como en la traza, o el desplazamiento en P */
static u32 why_arg(const char *arg)
{
    unsigned long off, addr;
    return (u32)(trace_line_read(arg, TRACE_SHIFT, &off, &addr) ? off : strtoul(arg, NULL, 16));
}

/* Las instrucciones de las trazas, de los dos espacios: las del juego se siembran en su análisis; las de
 * la BIOS (banco FF) se guardan para la caché de la BIOS */
typedef struct { u32 off, addr; } TraceEntry;
static TraceEntry *bios_trace;
static size_t nbios_trace, bios_trace_cap;

static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    char line[128];
    unsigned long off, addr;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr) || (off & 1)) continue;
        if (off >= BIOS_SPACE) {
            if (off - BIOS_SPACE >= BIOS_SIZE) continue;
            if (nbios_trace == bios_trace_cap) {
                bios_trace_cap = bios_trace_cap ? bios_trace_cap * 2 : 1024;
                bios_trace = realloc(bios_trace, bios_trace_cap * sizeof *bios_trace);
                if (!bios_trace) die("sin memoria");
            }
            bios_trace[nbios_trace++] = (TraceEntry){ (u32)(off - BIOS_SPACE), (u32)addr & 0xFFFFFF };
            continue;
        }
        if (off >= rom_size) continue;
        if (!is_ins[off >> 1]) stat_trace++;
        seed((u32)off, (u32)addr & 0xFFFFFF, HOW_TRACE);
    }
    fclose(f);
}

/* --coverage: de las instrucciones del juego de una traza, cuántas encontró el análisis sin ayuda */
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
            printf("  falta: P $%06lX = $%06lX  %s\n", off, addr, d);
            shown++;
        }
    }
    fclose(f);
    printf("cobertura: %u de %u instrucciones ejecutadas descubiertas sin traza (%.1f%%); descubiertas en total %u\n",
           found, total, total ? 100.0 * found / total : 100.0, stat_insns);
}

/* --coverage con la BIOS: lo que encuentra su análisis sin semillas (sin la caché, como para un usuario
 * nuevo) de las instrucciones de la BIOS de la traza (banco FF) */
static void bios_coverage_report(const Bios *b, const char *path)
{
    space_begin(SPACE_BIOS, b->rom, BIOS_SIZE);
    seed_entries();
    discover();
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir %s", path);
    char line[128];
    unsigned long off, addr;
    unsigned total = 0, found = 0;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, TRACE_SHIFT, &off, &addr) || off < BIOS_SPACE || (off & 1)) continue;
        off -= BIOS_SPACE;
        if (off >= BIOS_SIZE) continue;
        total++;
        if (is_ins[off >> 1]) found++;
        else if (verbose) {
            char d[96];
            disasm_at((u32)off, (u32)addr, d, sizeof d);
            printf("  falta en la BIOS: $%05lX = $%06lX  %s\n", off, addr, d);
        }
    }
    fclose(f);
    printf("cobertura de la BIOS: %u de %u instrucciones ejecutadas descubiertas sin traza (%.1f%%); descubiertas en total %u\n",
           found, total, total ? 100.0 * found / total : 100.0, stat_insns);
}

/* ------------------------------------------------------------------ */
/* --why, --viz, --events, --codemap, --html, --dis                    */
/* ------------------------------------------------------------------ */

static void explain(u32 off)
{
    if (off >= rom_size || (off & 1) || !is_ins[off >> 1]) {
        printf("P $%06X: no se descubrió como código\n", off);
        return;
    }
    printf("P $%06X ($%06X) se tomó como código así:\n", off, ins_addr[off >> 1]);
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
    printf("\nMapa del programa (una celda = %zu bytes; filas de %zu KB):\n", cell, row_size / 1024);
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
    printf("\n%u de %u celdas con código (%.1f%% del programa en bytes)\n", code_cells, cells, 100.0 * stat_bytes / (double)rom_size);
}

/* --codemap: qué bytes del programa se tomaron como código (C) y cuáles no (.), 64 por línea. El mismo
 * formato que difftest --executed y el --codemap de los otros recompiladores */
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
    free(funcs); free(fn_of);
    funcs = NULL;
    nfuncs = 0;
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
        while (j < rom_size && j / bank_size == i / bank_size && f->n < FUNC_MAX_INS) {
            if (is_ins[j >> 1]) {
                fn_of[j >> 1] = (u32)nfuncs;
                f->n++;
                f->hi = (u32)j;
                j += 2;
            } else {
                /* un hueco: se acepta si es corto y sigue código en la misma ventana */
                size_t k = j;
                while (k < rom_size && !is_ins[k >> 1] && k - j < FUNC_MAX_GAP && k / bank_size == i / bank_size) k += 2;
                if (k >= rom_size || !is_ins[k >> 1] || k / bank_size != i / bank_size) break;
                j = k;
            }
        }
        i = j;
    }
}

/* ------------------------------------------------------------------ */
/* Emisión                                                             */
/* ------------------------------------------------------------------ */

static char fn_prefix[32];          /* "f_" en el juego, "b<sha1 corto>_" en la BIOS */

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

/* ¿La instrucción en `target` (vista desde off) es de la misma función? */
static long in_func(u32 off, u32 target)
{
    long t = space_offset(target, (long)off);
    if (t < 0 || (u32)t / bank_size != off / bank_size) return -1;
    if (!is_ins[t >> 1] || ins_addr[t >> 1] != (target & 0xFFFFFF) || fn_of[t >> 1] != fn_of[off >> 1]) return -1;
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

/* ¿Las funciones de esta ventana dependen del banco elegido? (la ventana P2 de un juego con bancos) */
static int banked(u32 off) { return space == SPACE_GAME && p_banks > 1 && off >= P_BANK_SIZE; }

/* ¿El Bcc en off cierra un bucle de espera? Las mismas reglas que m68k_idle_skip (neo.c): de head al Bcc,
 * sin saltos, solo TST, CMPI, BTST, CMP <ea>,Dn y MOVE <ea>,Dn con operandos Dn, inmediatos, (An),
 * d16(An), absolutos o d16(PC); cada Dn que se lee se escribió antes en la vuelta o el bucle no lo toca;
 * la condición sale de la última que puso las banderas (BTST solo Z: BEQ/BNE). Devuelve el
 * desplazamiento de head o -1 */
static int idle_ea_ok(u32 *o, int mode, int reg, int size, u32 *rd)
{
    switch (mode) {
    case 0: *rd |= 1u << reg; return 1;
    case 2: return 1;
    case 5: *o += 2; return 1;
    case 7:
        if (reg == 0 || reg == 2) { *o += 2; return 1; }
        if (reg == 1) { *o += 4; return 1; }
        if (reg == 4) { *o += size == 2 ? 4 : 2; return 1; }
        return 0;
    default: return 0;
    }
}

static long idle_site(u32 off)
{
    u16 op = romw(off);
    int cc = op >> 8 & 15;
    if (m68k_decode(op) != K_BCC || cc == 1) return -1;
    u32 addr = ins_addr[off >> 1], d8 = op & 0xFF;
    u32 to = (addr + 2 + (d8 ? sx8(d8) : sx16(romw(off + 2)))) & 0xFFFFFF;
    if (to >= addr || addr - to > 32) return -1;
    long head = in_func(off, to);
    if (head < 0) return -1;
    u32 o = (u32)head, rd[8], wr[8], all = 0, done = 0;
    int n = 0, flags = 0;
    while (o < off) {
        if (n == 6 || !is_ins[o >> 1]) return -1;
        u16 w = romw(o);
        int mode = w >> 3 & 7, reg = w & 7;
        u32 r = 0, wv = 0, o0 = o;
        o += 2;
        if ((w & 0xFF00) == 0x4A00 && (w & 0xC0) != 0xC0) { if (!idle_ea_ok(&o, mode, reg, w >> 6 & 3, &r)) return -1; flags = 1; }
        else if ((w & 0xFF00) == 0x0C00 && (w & 0xC0) != 0xC0) {
            o += (w >> 6 & 3) == 2 ? 4 : 2;
            if (!idle_ea_ok(&o, mode, reg, w >> 6 & 3, &r)) return -1;
            flags = 1;
        } else if ((w & 0xFFC0) == 0x0800 || ((w & 0xF1C0) == 0x0100 && mode != 1)) {
            if (w & 0x0100) r |= 1u << (w >> 9 & 7); else o += 2;
            if (!idle_ea_ok(&o, mode, reg, mode ? 0 : 2, &r)) return -1;
            flags = 2;
        } else if ((w & 0xF000) == 0xB000 && (w >> 6 & 7) <= 2) {
            r |= 1u << (w >> 9 & 7);
            if (mode == 1 || !idle_ea_ok(&o, mode, reg, w >> 6 & 3, &r)) return -1;
            flags = 1;
        } else if ((w >> 12) >= 1 && (w >> 12) <= 3 && (w >> 6 & 7) == 0) {
            int sz = (w >> 12) == 1 ? 0 : (w >> 12) == 3 ? 1 : 2;
            if (mode == 1 || !idle_ea_ok(&o, mode, reg, sz, &r)) return -1;
            wv = 1u << (w >> 9 & 7);
            flags = 1;
        } else return -1;
        if (o - o0 != (u32)ins_len(o0)) return -1;
        rd[n] = r; wr[n] = wv; n++;
    }
    if (o != off) return -1;
    if (cc >= 2 && (flags == 0 || (flags == 2 && cc != 6 && cc != 7))) return -1;
    for (int i = 0; i < n; i++) all |= wr[i];
    for (int i = 0; i < n; i++) { if (rd[i] & all & ~done) return -1; done |= wr[i]; }
    return head;
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
    if (banked(off)) fprintf(f, "    MAPCHK(npc);\n");

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
    /* El Bcc de un bucle de espera: si se toma, IDLE68; si no, el bucle terminó */
    long idle = idle_site(off);
    if (idle >= 0) {
        fprintf(f, "    IDLE68(0x%06X, 0x%06X, L_%06X);\n", (u32)succ[0] & 0xFFFFFF, addr, (u32)idle);
        fprintf(f, "    idle_head = 0;\n");
    }
    for (int i = 0; i < ns; i++) {
        long t = in_func(off, (u32)succ[i]);
        if (t >= 0) fprintf(f, "    if (npc == 0x%06X) goto L_%06X;\n", (u32)succ[i] & 0xFFFFFF, (u32)t);
    }
    fprintf(f, "    LEAVE(npc);\n");
}

static void emit_function(FILE *f, size_t idx)
{
    const Func *fn = &funcs[idx];
    fprintf(f, "/* %s $%06X-$%06X (%u instrucciones) */\n", space == SPACE_GAME ? "P" : "BIOS", fn->lo, fn->hi, fn->n);
    fprintf(f, "int %s%06X(Neo *g)\n{\n    M68k *c = &g->cpu;\n    u32 npc;\n", fn_prefix, fn->lo);
    if (banked(fn->lo)) fprintf(f, "    u32 ver0 = g->map_ver;\n");
    for (u32 o = fn->lo; o <= fn->hi; o += 2)
        if (is_ins[o >> 1] && fn_of[o >> 1] == idx + 1 && idle_site(o) >= 0) {
            fprintf(f, "    u32 idle_head = 0;\n    u64 idle_at = 0;\n");   /* bucles de espera (IDLE68) */
            break;
        }
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

/* Las funciones del espacio en dir: `header` las declara y `code` es el prefijo de los archivos */
static void emit_code(const char *dir, const char *header, const char *code)
{
    FILE *h = open_out(dir, header);
    fprintf(h, "/* Generado por neorecomp: no editar. */\n#ifndef GEN_%s_H\n#define GEN_%s_H\n#include \"m68k_ops.h\"\n#include \"recomp.h\"\n\n",
            fn_prefix, fn_prefix);
    for (size_t i = 0; i < nfuncs; i++) fprintf(h, "int %s%06X(Neo *g);\n", fn_prefix, funcs[i].lo);
    fprintf(h, "\n#endif\n");
    fclose(h);

    FILE *f = NULL;
    unsigned in_file = 0, part = 0;
    for (size_t i = 0; i < nfuncs; i++) {
        if (!f || in_file >= INS_PER_FILE) {
            if (f) fclose(f);
            char name[96];
            snprintf(name, sizeof name, "%s%03u.c", code, part++);
            f = open_out(dir, name);
            fprintf(f, "/* Generado por neorecomp: no editar. */\n#include \"%s\"\n\n"
                       "static const u16 noext[1];     /* instrucciones sin palabras de extensión */\n\n", header);
            in_file = 0;
        }
        emit_function(f, i);
        in_file += funcs[i].n;
    }
    if (f) fclose(f);
}

/* La tabla de búsqueda: las funciones ordenadas por desplazamiento y, por KB, la primera candidata.
 * `lookup` es el nombre de la función que busca por desplazamiento */
static void emit_table(FILE *f, const char *lookup)
{
    size_t nf = nfuncs ? nfuncs : 1;
    fprintf(f, "#define NFUNCS %zu\n\n", nfuncs);
    fprintf(f, "static const u32 f_lo[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%X,", i % 8 ? "" : "\n    ", funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const u32 f_hi[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s0x%X,", i % 8 ? "" : "\n    ", funcs[i].hi);
    if (!nfuncs) fprintf(f, "0");
    fprintf(f, "\n};\nstatic const RecompFn f_fn[%zu] = {", nf);
    for (size_t i = 0; i < nfuncs; i++) fprintf(f, "%s%s%06X,", i % 4 ? "" : "\n    ", fn_prefix, funcs[i].lo);
    if (!nfuncs) fprintf(f, "0");
    size_t pages = (rom_size + 1023) >> 10;
    fprintf(f, "\n};\n\n/* Por KB: la primera función cuya última instrucción está en él o después */\n");
    fprintf(f, "static const u32 page_first[%zu] = {", pages);
    size_t fi = 0;
    for (size_t p = 0; p < pages; p++) {
        while (fi < nfuncs && funcs[fi].hi < p * 1024) fi++;
        fprintf(f, "%s%zu,", p % 16 ? "" : "\n    ", fi);
    }
    fprintf(f, "\n};\n\n"
               "RecompFn %s(u32 off)\n{\n"
               "    if (off >= %zu) return 0;\n"
               "    u32 i = page_first[off >> 10];\n"
               "    while (i < NFUNCS && f_hi[i] < off) i++;\n"
               "    return (i < NFUNCS && f_lo[i] <= off) ? f_fn[i] : 0;\n}\n", lookup, rom_size);
}

/* El despacho del juego: la BIOS va a su tabla (bios/bios.c) */
static void emit_dispatch(const char *dir)
{
    FILE *f = open_out(dir, "gen/dispatch.c");
    fprintf(f, "/* Generado por neorecomp: no editar. */\n#include \"gen.h\"\n\n");
    emit_table(f, "game_lookup");
    fprintf(f, "\nRecompFn recomp_lookup(Neo *g, u32 pc)\n{\n"
               "    long off = neo_code_offset(g, pc);\n"
               "    if (off < 0) return 0;\n"
               "    if (off >= (long)BIOS_SPACE) return recomp_bios_lookup(g, (u32)(off - (long)BIOS_SPACE));\n"
               "    return game_lookup((u32)off);\n}\n");
    fclose(f);
}

static void emit_info(const char *dir, const char *sha1, const char *name, unsigned insns)
{
    FILE *f = open_out(dir, "gen/info.c");
    fprintf(f, "/* Generado por neorecomp: no editar. */\n#include \"recomp.h\"\n\n");
    fprintf(f, "const char recomp_rom_sha1[41] = \"%s\";\n", sha1);
    fprintf(f, "const char recomp_name[] = \"%s\";\n", name);
    fprintf(f, "const unsigned recomp_instructions = %u;\n", insns);
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
        "# Generado por neorecomp. Compila con cualquier gcc/clang C99 con SDL2 (make -j para usar\n"
        "# todos los núcleos). Windows: MSYS2 UCRT64.\n\n"
        "CC     ?= cc\n"
        "# MSYS2 sin \"cc\" (por ejemplo MINGW64): se usa gcc\n"
        "ifeq ($(origin CC),default)\n"
        "CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)\n"
        "endif\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen -Ibios -Iplatform -DNEO_RECOMP\n\n"
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
        "RT_OBJ   := $(patsubst runtime/%%.c,build/rt_%%.o,$(wildcard runtime/*.c))\n"
        "GEN_OBJ  := $(patsubst gen/%%.c,build/gen_%%.o,$(wildcard gen/*.c))\n"
        "BIOS_OBJ := $(patsubst bios/%%.c,build/bios_%%.o,$(wildcard bios/*.c))\n"
        "CODE_OBJ := $(GEN_OBJ) $(BIOS_OBJ)\n\n"
        ".PHONY: all clean dist\n"
        "all: %s$(EXE) difftest$(EXE) bench$(EXE)\n\n"
        "build:\n\tmkdir -p build\n\n"
        "build/rt_%%.o: runtime/%%.c $(wildcard runtime/*.h) | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "build/gen_%%.o: gen/%%.c $(wildcard runtime/*.h) gen/gen.h | build\n"
        "\t$(CC) $(CFLAGS) $(INC) -c -o $@ $<\n\n"
        "build/bios_%%.o: bios/%%.c $(wildcard runtime/*.h) $(wildcard bios/*.h) | build\n"
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
        "%s$(EXE): $(PLAT_OBJ) $(RT_OBJ) $(CODE_OBJ) $(WINRES)\n"
        "\t$(CC) -o $@ $^ $(SDL_LIBS) -lm\n\n"
        "# dist/: el ejecutable (y, si en Windows no quedó estático, las DLL de MSYS2 que pida); se puede copiar a otro equipo\n"
        "dist: %s$(EXE)\n"
        "\tmkdir -p dist\n"
        "\tcp $< dist/\n"
        "ifeq ($(OS),Windows_NT)\n"
        "\tldd $< | awk '/=> \\/(ucrt64|mingw64|mingw32|clang64|clangarm64)\\//{print $$3}' | xargs -r -I{} cp {} dist/\n"
        "endif\n\n"
        "# Prueba diferencial: código recompilado contra el intérprete\n"
        "difftest$(EXE): platform/difftest.c platform/png.c $(RT_OBJ) $(CODE_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm\n\n"
        "bench$(EXE): platform/bench.c $(RT_OBJ) $(CODE_OBJ)\n"
        "\t$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm\n\n"
        "clean:\n\trm -rf build dist %s$(EXE) difftest$(EXE) bench$(EXE)\n",
        name, name, name, name);
    fclose(f);
}

static void emit_readme(const char *dir, const char *name, const char *sha1)
{
    FILE *f = open_out(dir, "LEEME.txt");
    fprintf(f,
        "Proyecto generado por neorecomp.\n\n"
        "Compilar (necesita un compilador C99, make y SDL2):\n"
        "  macOS:   brew install sdl2 pkg-config\n"
        "  Debian:  sudo apt install build-essential pkg-config libsdl2-dev\n"
        "  Windows: MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 make\n"
        "           (el Makefile usa gcc si no hay cc; el .exe lleva el icono)\n"
        "           el .exe enlaza SDL2 de forma estática (no pide DLLs); make dist lo deja en dist/\n\n"
        "  make -j\n"
        "  ./%s juego.zip\n\n"
        "El proyecto no contiene el juego ni la BIOS. El ejecutable pide el juego (.neo, .zip de MAME o\n"
        "una carpeta con los chips) y comprueba que el SHA1 de su imagen normalizada sea\n"
        "%s.\n"
        "Si se ejecuta sin argumentos busca en la carpeta actual un juego con ese SHA1. La BIOS del AES\n"
        "(la propia, neobios.bin, y si no la de SNK) se busca en NEO_BIOS, junto al juego o en roms/, o\n"
        "se pide una vez. El código de la BIOS va recompilado para las versiones de bios/; con otra\n"
        "versión aceptada, la BIOS corre en el intérprete.\n\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest juego.zip 600\n"
        "Lo que el intérprete tuvo que ejecutar (para recompilar de nuevo con neorecomp --trace):\n"
        "  ./difftest juego.zip 3600 --seed 1 --uncovered faltante.txt\n", name, sha1);
    fclose(f);
}

/* El código de una corrida anterior se borra: puede haber menos archivos ahora */
static void remove_old_code(const char *dir, const char *code)
{
    for (unsigned part = 0; part < 100000; part++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s%03u.c", dir, code, part);
        if (remove(p) != 0) break;
    }
}

/* Nombre del ejecutable: letras, números, - y _ */
static void sanitize(char *s)
{
    for (char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') *p = '_';
}

/* ------------------------------------------------------------------ */
/* La BIOS: una vez por versión, en la caché                           */
/* ------------------------------------------------------------------ */

/* Sube cuando cambia lo que se genera (el análisis, la emisión): la caché vieja se rehace aunque la versión
 * de neorecomp sea la misma (un árbol con cambios sin commit). 2: las heurísticas de la fase 7; 3: los
 * bucles de espera (IDLE68) de la fase 8 */
#define BIOS_CACHE_FORMAT "3"

static void bios_cache_root(char *out, size_t n)
{
    const char *e = getenv("NEO_BIOS_CACHE");
    if (e && *e) { snprintf(out, n, "%s", e); return; }
    const char *h = getenv("HOME");
    if (!h || !*h) h = getenv("USERPROFILE");
    snprintf(out, n, "%s/.neorecomp/bios", h && *h ? h : ".");
}

static int copy_file(const char *from, const char *to)
{
    size_t n;
    u8 *d = neo_read_file(from, &n);
    if (!d) return 0;
    FILE *f = fopen(to, "wb");
    int ok = f && fwrite(d, 1, n, f) == n;
    if (f && fclose(f)) ok = 0;
    free(d);
    return ok;
}

static int file_exists(const char *path) { struct stat st; return !stat(path, &st); }

typedef struct { char sha1[41], sha8[9]; unsigned insns; int reused; } BiosOut;

/* Genera (o toma de la caché) el código de una versión de la BIOS y lo copia a out/bios */
static BiosOut bios_code(const Bios *b, const char *out)
{
    BiosOut r;
    memset(&r, 0, sizeof r);
    snprintf(r.sha1, sizeof r.sha1, "%s", b->sha1);
    snprintf(r.sha8, sizeof r.sha8, "%.8s", b->sha1);
    char root[900], dir[1000], path[1200];
    bios_cache_root(root, sizeof root);
    snprintf(dir, sizeof dir, "%s/%s", root, b->sha1);
    make_dir(dir);

    /* Lo que ya tiene la caché: la versión de neorecomp y las instrucciones de las trazas */
    char want[128], have[128] = "";
    snprintf(want, sizeof want, "%s formato %s\n", NEORECOMP_VERSION, BIOS_CACHE_FORMAT);
    snprintf(path, sizeof path, "%s/version.txt", dir);
    FILE *f = fopen(path, "r");
    if (f) { if (!fgets(have, sizeof have, f)) have[0] = 0; fclose(f); }
    TraceEntry *seeds = NULL;
    size_t nseeds = 0, cap = 0;
    snprintf(path, sizeof path, "%s/semillas.txt", dir);
    f = fopen(path, "r");
    if (f) {
        unsigned long o, a;
        char line[64];
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "%lx %lx", &o, &a) == 2 && o < BIOS_SIZE) {
                if (nseeds == cap) { cap = cap ? cap * 2 : 1024; seeds = realloc(seeds, cap * sizeof *seeds); }
                seeds[nseeds++] = (TraceEntry){ (u32)o, (u32)a };
            }
        fclose(f);
    }
    u8 *known = calloc(BIOS_SIZE / 2, 1);
    for (size_t i = 0; i < nseeds; i++) known[seeds[i].off >> 1] = 1;
    size_t added = 0;
    for (size_t i = 0; i < nbios_trace; i++)
        if (!known[bios_trace[i].off >> 1]) {
            known[bios_trace[i].off >> 1] = 1;
            if (nseeds == cap) { cap = cap ? cap * 2 : 1024; seeds = realloc(seeds, cap * sizeof *seeds); }
            seeds[nseeds++] = bios_trace[i];
            added++;
        }
    free(known);
    snprintf(path, sizeof path, "%s/%s.h", dir, r.sha8);
    r.reused = !strcmp(have, want) && !added && file_exists(path);

    char header[32], code[32], disp[48];
    snprintf(header, sizeof header, "%s.h", r.sha8);
    snprintf(code, sizeof code, "%s_code_", r.sha8);
    snprintf(disp, sizeof disp, "%s_dispatch.c", r.sha8);
    if (!r.reused) {
        space_begin(SPACE_BIOS, b->rom, BIOS_SIZE);
        snprintf(fn_prefix, sizeof fn_prefix, "b%s_", r.sha8);
        seed_entries();
        for (size_t i = 0; i < nseeds; i++) seed(seeds[i].off, seeds[i].addr, HOW_TRACE);
        discover();
        partition();
        remove_old_code(dir, code);
        emit_code(dir, header, code);
        f = open_out(dir, disp);
        fprintf(f, "/* Generado por neorecomp: no editar. */\n#include \"%s\"\n\n", header);
        char lookup[48];
        snprintf(lookup, sizeof lookup, "bios_%s_lookup", r.sha8);
        emit_table(f, lookup);
        fclose(f);
        f = open_out(dir, "semillas.txt");
        for (size_t i = 0; i < nseeds; i++) fprintf(f, "%05X %06X\n", seeds[i].off, seeds[i].addr);
        fclose(f);
        f = open_out(dir, "instrucciones.txt");
        fprintf(f, "%u\n", stat_insns);
        fclose(f);
        f = open_out(dir, "version.txt");
        fputs(want, f);
        fclose(f);
        r.insns = stat_insns;
    } else {
        snprintf(path, sizeof path, "%s/instrucciones.txt", dir);
        f = fopen(path, "r");
        if (f) { if (fscanf(f, "%u", &r.insns) != 1) r.insns = 0; fclose(f); }
    }
    free(seeds);

    /* La copia al proyecto */
    char bdir[1100], from[1200], to[1300];
    snprintf(bdir, sizeof bdir, "%s/bios", out);
    make_dir(bdir);
    remove_old_code(bdir, code);
    const char *fixed[2] = { header, disp };
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof from, "%s/%s", dir, fixed[i]);
        snprintf(to, sizeof to, "%s/%s", bdir, fixed[i]);
        if (!copy_file(from, to)) die("no se pudo copiar %s", from);
    }
    for (unsigned part = 0; part < 100000; part++) {
        snprintf(from, sizeof from, "%s/%s%03u.c", dir, code, part);
        snprintf(to, sizeof to, "%s/%s%03u.c", bdir, code, part);
        if (!file_exists(from)) break;
        if (!copy_file(from, to)) die("no se pudo copiar %s", from);
    }
    return r;
}

/* bios/bios.c: las versiones recompiladas y cuál es la de la BIOS cargada */
static void emit_bios_table(const char *out, const BiosOut *v, int n)
{
    char bdir[1100];
    snprintf(bdir, sizeof bdir, "%s/bios", out);
    make_dir(bdir);
    FILE *f = open_out(bdir, "bios.c");
    fprintf(f, "/* Generado por neorecomp: no editar. Las versiones de la BIOS recompiladas en este proyecto. */\n"
               "#include <string.h>\n\n#include \"recomp.h\"\n\n");
    for (int i = 0; i < n; i++) fprintf(f, "RecompFn bios_%s_lookup(u32 off);\n", v[i].sha8);
    fprintf(f, "\nconst unsigned recomp_bios_count = %d;\n", n);
    fprintf(f, "const char *const recomp_bios_sha1[%d] = {", n ? n : 1);
    for (int i = 0; i < n; i++) fprintf(f, " \"%s\",", v[i].sha1);
    if (!n) fprintf(f, " 0");
    fprintf(f, " };\n");
    if (n) {
        fprintf(f, "static RecompFn (*const lookups[%d])(u32) = {", n);
        for (int i = 0; i < n; i++) fprintf(f, " bios_%s_lookup,", v[i].sha8);
        fprintf(f, " };\n\n");
    }
    fprintf(f, "RecompFn recomp_bios_lookup(Neo *g, u32 off)\n{\n");
    if (n) {
        fprintf(f, "    if (g->bios.code == -2) {\n"
                   "        g->bios.code = -1;\n"
                   "        for (unsigned i = 0; i < recomp_bios_count; i++)\n"
                   "            if (!strcmp(g->bios.sha1, recomp_bios_sha1[i])) g->bios.code = (int)i;\n"
                   "    }\n"
                   "    return g->bios.code >= 0 ? lookups[g->bios.code](off) : 0;\n");
    } else fprintf(f, "    (void)g; (void)off;\n    return 0;\n");
    fprintf(f, "}\n");
    fclose(f);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *out = NULL, *name_arg = NULL, *coverage = NULL, *dis_arg = NULL, *neo_out = NULL;
    const char *events_path = NULL, *html_path = NULL, *codemap_path = NULL;
    const char *traces[64], *bios_paths[8];
    u32 why_list[32];
    int ntraces = 0, nbios = 0, nwhy = 0, info = 0, viz = 0, force_color = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("neorecomp %s\n", NEORECOMP_VERSION); return 0; }
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
            if (!strcmp(a, "--neo")) { neo_out = argv[++i]; continue; }
            if (!strcmp(a, "--bios")) {
                if (nbios == 8) die("demasiadas --bios");
                bios_paths[nbios++] = argv[++i];
                continue;
            }
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

    neo = calloc(1, sizeof *neo);
    if (!neo) die("sin memoria");
    const char *err = NULL;
    if (cart_load_path(&neo->cart, rom_path, &err)) { fprintf(stderr, "%s: %s\n", rom_path, err); return 1; }
    l0_generate(neo->l0);
    /* Las BIOS: las de --bios o la que se encuentre */
    static Bios bios[8];
    int nb = 0;
    const char *bios_err = NULL;
    for (int i = 0; i < nbios; i++) {
        if (bios_load(&bios[nb], bios_paths[i], neo->l0, &bios_err)) die("%s", bios_err);
        nb++;
    }
    if (!nbios && !bios_find(&bios[0], rom_path, neo->l0, &bios_err)) nb = 1;
    if (nb) neo->bios = bios[0];
    if (neo_out) {
        if (!cart_write_neo(&neo->cart, neo_out)) die("no se pudo escribir %s", neo_out);
        printf("%s escrito (SHA1 de la imagen %s)\n", neo_out, neo->cart.sha1);
        return 0;
    }
    if (info || (!out && !analysis)) {
        print_info(rom_path, &neo->cart, &neo->bios, bios_err, neo->l0);
        return 0;
    }
    int fatal;
    const char *u = cart_unsupported(&neo->cart, &fatal);
    if (u && fatal) die("%s: %s", rom_path, u);
    if (u) fprintf(stderr, "aviso: %s\n", u);

    /* --- El juego --- */
    const Cart *cart = &neo->cart;
    p_banks = cart->p_banks;
    if (coverage && nb) bios_coverage_report(&neo->bios, coverage);
    space_begin(SPACE_GAME, cart->data + cart->off[REG_P], cart->len[REG_P]);
    printf("%s: P %zu KB%s, SHA1 %s\n", rom_path, rom_size / 1024,
           p_banks > 1 ? " (con bancos en $200000)" : "", cart->sha1);
    /* Primero sin traza (lo que mide --coverage) y después con las trazas */
    seed_entries();
    discover();
    if (coverage) coverage_report(coverage);
    unsigned base_insns = stat_insns;
    for (int i = 0; i < ntraces; i++) load_trace(traces[i]);
    discover();
    printf("código: %u instrucciones (%u bytes, %.2f%% de P)", stat_insns, stat_bytes, 100.0 * stat_bytes / (double)rom_size);
    if (ntraces) printf(", %u por la traza (%u puntos de entrada)", stat_insns - base_insns, stat_trace);
    printf("\n");
    if (verbose)
        printf("saltos indirectos: %u, destinos fuera de este espacio: %u; tablas: %u (%u entradas), tablas sueltas: %u (%u entradas), "
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

    char name[128];
    if (name_arg) snprintf(name, sizeof name, "%s", name_arg);
    else if (cart->set[0]) snprintf(name, sizeof name, "%s", cart->set);
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
    remove_old_code(gdir, "code_");
    partition();
    snprintf(fn_prefix, sizeof fn_prefix, "f_");
    emit_code(gdir, "gen.h", "code_");
    emit_dispatch(out);
    emit_info(out, cart->sha1, name, stat_insns);
    size_t game_funcs = nfuncs;

    /* --- La BIOS --- */
    static BiosOut bout[8];
    for (int i = 0; i < nb; i++) {
        bout[i] = bios_code(&bios[i], out);
        printf("BIOS %s (%.8s): %u instrucciones%s\n", bios[i].ver->desc, bios[i].sha1, bout[i].insns,
               bout[i].reused ? " (de la caché)" : "");
    }
    if (!nb) printf("sin BIOS (%s): la BIOS correrá en el intérprete\n", bios_err ? bios_err : "no se encontró");
    emit_bios_table(out, bout, nb);

    write_embedded(out);
    emit_makefile(out, name);
    emit_readme(out, name, cart->sha1);
    printf("%zu funciones en %s/gen; el ejecutable se llamará %s\n", game_funcs, out, name);
    printf("compilar: make -C %s -j\n", out);
    cart_free(&neo->cart);
    free(neo);
    return 0;
}
