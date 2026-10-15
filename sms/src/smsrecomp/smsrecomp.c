/*
 * smsrecomp: recompilador estático de juegos de Sega Master System y Game Gear a C99.
 *
 * Uso: smsrecomp juego.sms -o salida/ [--trace traza.txt] [--name nombre] [--model sms|gg]
 *      smsrecomp juego.sms [--info]
 *
 * Genera un proyecto completo (código recompilado, runtime, plataforma y Makefile). El proyecto no contiene datos
 * de la ROM: el ejecutable carga la ROM del usuario y valida su SHA1.
 *
 * El Z80 se compila con el recompilador común (z80_recomp.h, en tools/retrorecomp/shared), por vistas: una página
 * de 16 KB de la ROM en una de las tres ranuras del mapper ($0000, $4000, $8000), con los bytes que ve el Z80 ahí
 * (en el mapper Sega, la de la ranura 0 lleva el primer KB de la página 0, que no se mueve). Como el mapper puede
 * poner una página en cualquier ranura, el código de una página se compila para la ranura donde corre:
 *   1. Descubrimiento: desde las semillas ($0000, los RST, $0038 y $0066, en las vistas de la ranura 0) y las
 *      instrucciones de la traza (--trace: las escriben smstrace, la ventana con --trace y difftest --uncovered;
 *      cada línea BANCO:DIRECCION dice la página y la ranura), siguiendo el flujo con z80_decode. Un salto a otra
 *      ranura siembra la vista de la página que está ahí al encender (lo demás lo dice la traza).
 *   2. Emisión: cada vista en gen/zpPPsS*.c (z80rc_emit) y el despachador, gen/views.c: recomp_lookup elige la
 *      vista con el PC y el mapeo de ahora.
 *   3. Código en RAM: las líneas "ram" de la traza (ram_code.h) dan los bytes de lo que corrió desde la RAM; cada
 *      versión se compila aparte con una guarda (gen/zramN*.c): si los bytes de una instrucción no son los
 *      compilados, la ejecuta el intérprete.
 *
 * Las instrucciones se identifican por su desplazamiento en la ROM (página × $4000 + dirección & $3FFF): lo que no
 * se descubrió lo ejecuta el intérprete. Descubrir de más no rompe nada (lo que se compila es lo que hay ahí, y se
 * entra por el PC exacto); de menos, tampoco.
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

#include "sms.h"
#include "console.h"
#include "trace_line.h"
#include "ram_code.h"
#include "z80_recomp.h"
#include "disz80.h"
#include "embedded.h"

#ifndef SMSRECOMP_VERSION
#define SMSRECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "smsrecomp " SMSRECOMP_VERSION ": recompila una ROM de Sega Master System o Game Gear a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: smsrecomp juego.sms -o salida/ [opciones]\n"
    "     smsrecomp juego.sms [--info]\n"
    "  -o carpeta         dónde escribir el proyecto (se crea si no existe)\n"
    "  --name nombre      nombre del ejecutable (por omisión, el de la ROM)\n"
    "  --trace archivo    instrucciones que ejecutó el juego (las escriben smstrace, la ventana con --trace y\n"
    "                     difftest --uncovered): se agregan como entradas, en la página y la ranura donde\n"
    "                     corrieron, y sus líneas \"ram\" dan el código de la RAM (se puede repetir)\n"
    "  --coverage archivo compara lo descubierto sin traza con las instrucciones que ejecutó el juego\n"
    "                     (una traza): cuántas se encontraron y cuáles faltan\n"
    "  --why BB:DIRECCION explica por qué esa instrucción (página y dirección como en la traza, o su\n"
    "                     desplazamiento hexadecimal en la ROM) se tomó como código: la cadena de saltos\n"
    "                     desde un punto de entrada (se puede repetir; sin -o no genera el proyecto)\n"
    "  --viz              mapa de la ROM en la terminal, coloreado por cómo se descubrió (con\n"
    "                     --coverage, en rojo lo que el juego ejecutó y no se encontró)\n"
    "  --color            con --viz, colorea aunque la salida no sea una terminal (lo usa la GUI)\n"
    "  --html archivo     el descubrimiento como una página HTML sola: mapa, instrucciones por celda y\n"
    "                     la cadena de \"por qué\" al hacer clic (con --coverage marca lo que falta)\n"
    "  --events archivo   cada instrucción descubierta con su origen (texto: desplazamiento, dirección,\n"
    "                     cómo se llegó, de dónde, origen, desensamblado)\n"
    "  --codemap archivo  qué bytes de la ROM se tomaron como código: una letra por byte (C = código),\n"
    "                     64 por línea; el mismo formato que difftest --executed\n"
    "  --dis OFF:DIR:N    desensambla N instrucciones desde ese desplazamiento de la ROM, con esa dirección\n"
    "  --no-tables        sin las heurísticas: tablas de saltos (JP (HL) con un LD HL,nn antes) ni\n"
    "                     direcciones apiladas (LD rr,nn y PUSH rr); para comparar\n"
    "  --model M          sms (Master System II) o gg (Game Gear); por omisión, el de la extensión (.sms, .gg)\n"
    "                     o el de la región de la cabecera (el proyecto lo usa igual)\n"
    "  --info             lo que se sabe del cartucho (lo que hace sin otras opciones)\n"
    "  --verbose          detalles del descubrimiento\n"
    "  --version, --help\n";

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "smsrecomp: ");
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

static void print_size(size_t bytes)
{
    if (bytes % 1024) printf("%zu bytes", bytes);
    else printf("%zu KB", bytes / 1024);
}

static void print_info(const char *path, const Sms *s)
{
    const Cart *c = &s->cart;
    printf("%s: ", path);
    print_size(c->rom_size);
    printf(", %s\n", c->copier ? "con cabecera de copiador (512 bytes, quitada)" : "binario");
    printf("SHA1:        %s\n", c->sha1);
    printf("CRC32:       %08X\n", c->crc32);
    if (c->hdr.found) {
        const SmsHeader *h = &c->hdr;
        u32 declared = size_code_bytes(h->size_code);
        printf("cabecera:    TMR SEGA en $%04X\n", h->at);
        printf("producto:    %s\n", h->product);
        printf("versión:     %u\n", h->version);
        printf("región:      %u (%s)\n", h->region, region_name(h->region));
        printf("tamaño:      $%X (", h->size_code);
        if (declared) print_size(declared); else printf("la BIOS no lo admite");
        printf(")\n");
        if (h->checksum == 0 && h->checksum_calc != 0)
            printf("suma:        sin suma ($0000; la BIOS del Game Gear y la japonesa no la comprueban)\n");
        else if (h->checksum_ok)
            printf("suma:        $%04X (calculada $%04X, %s)\n", h->checksum, h->checksum_calc,
                   h->checksum == h->checksum_calc ? "coincide" : "no coincide");
        else
            printf("suma:        $%04X (no se puede comprobar con ese tamaño)\n", h->checksum);
    } else {
        printf("cabecera:    no hay \"TMR SEGA\" (en $7FF0, $3FF0 ni $1FF0)\n");
    }
    if (c->cm.found)
        printf("Codemasters: %u páginas, %02X/%02X/%02X %02X:%02X, suma $%04X (calculada $%04X, %s)\n",
               c->cm.banks, c->cm.day, c->cm.month, c->cm.year, c->cm.hour, c->cm.minute, c->cm.checksum,
               c->cm.checksum_calc, c->cm.checksum == c->cm.checksum_calc ? "coincide" : "no coincide");
    if (c->sdsc.found)
        printf("SDSC:        %s%s%s, versión %X.%02X, %02X/%02X/%04X\n", c->sdsc.name[0] ? c->sdsc.name : "(sin nombre)",
               c->sdsc.author[0] ? " de " : "", c->sdsc.author, c->sdsc.major, c->sdsc.minor,
               c->sdsc.day, c->sdsc.month, c->sdsc.year);
    printf("mapper:      %s\n", mapper_name(c->mapper));
    printf("modelo:      %s (%s%s)\n", model_name(s->model), s->model == MODEL_GG ? "Game Gear" : "Master System II",
           s->model == MODEL_GG && s->sms_mode ? ", en su modo SMS" : "");
    printf("juego:       %s\n", c->db ? c->db->name : "no está en la base de datos (se usa lo que dicen la cabecera y el tamaño)");
}

/* ------------------------------------------------------------------ */
/* Vistas: una página en una ranura                                    */
/* ------------------------------------------------------------------ */

/* Cómo se llegó a una instrucción: los primeros tres son los de z80_recomp.h (Z80RC_SEED, Z80RC_FLOW,
 * Z80RC_TRACE); HOW_CROSS, un salto desde otra ranura. Para los informes, el de Z80RC_FLOW se detalla mirando la
 * instrucción de la que vino (sigue, salto, llamada, bifurcación) */
enum { HOW_VECTOR = Z80RC_SEED, HOW_FLOW = Z80RC_FLOW, HOW_TRACE = Z80RC_TRACE, HOW_CROSS, HOW_TABLE, HOW_PUSHED };
enum { SHOW_VECTOR, SHOW_TRACE, SHOW_NEXT, SHOW_JUMP, SHOW_CALL, SHOW_BRANCH, SHOW_CROSS, SHOW_TABLE, SHOW_PUSHED, SHOW_COUNT };
static const char *const how_name[SHOW_COUNT] = { "vector", "traza", "sigue", "salto", "llamada", "bifurcación",
                                                  "otra_ranura", "tabla", "apilada" };
/* El origen (lo que colorea el mapa): las categorías de la GUI (retrorecomp_gui.py, clase SMS) */
enum { ORG_VECTORS, ORG_TRACE, ORG_TABLE, ORG_SPEC, ORG_COUNT };
static const char *const org_name[ORG_COUNT] = { "flujo desde los vectores", "traza", "tabla de saltos",
                                                 "especulativo (punteros sueltos)" };

#define MAX_PAGES 256
#define NO_PARENT 0xFFFFFFFFu

typedef struct {
    int page, slot;
    u8 *img;                    /* los 16 KB que ve el Z80 en la ranura con esa página */
    Z80Rc *r;
    u32 *xsrc;                  /* por dirección de la ranura: 1 + el desplazamiento de la instrucción que la delató
                                 * desde otra ranura (HOW_CROSS) o con una heurística (HOW_TABLE, HOW_PUSHED) */
} View;

static Sms *sms;                    /* la ROM cargada (normalizada) y su mapeo al encender */
static const u8 *rom;
static size_t rom_size;
static int sega, verbose;
static int use_tables = 1;          /* --no-tables: sin las tablas de saltos ni las direcciones apiladas */
static unsigned stat_tables, stat_table_entries, stat_pushed;
static View *views;
static int nviews;
static int view_at[MAX_PAGES][3];   /* 1 + la vista de esa página en esa ranura, o 0 */

/* Por desplazamiento de la ROM: lo descubierto (en cualquier vista; la primera que lo encontró manda) */
static u8  *is_ins, *how_at, *origin, *ins_len;
static u16 *ins_addr;
static u32 *parent;
static u8  *missing_mark;           /* --coverage: ejecutadas y no descubiertas */
static unsigned stat_insns, stat_bytes, stat_trace, stat_views_trace;

static u32 off_of(const View *v, u16 a)
{
    u32 off = (sega && v->slot == 0 && a < 0x400) ? a : (u32)v->page * SMS_PAGE_SIZE + (a & 0x3FFF);
    return (u32)(off % rom_size);
}

static int default_page(int slot) { return (int)(sms->map.slot[slot] / SMS_PAGE_SIZE); }

static void seed_vectors(View *v)
{
    if (v->slot) return;
    for (unsigned a = 0; a <= 0x38; a += 8) z80rc_seed(v->r, (u16)a, HOW_VECTOR);
    z80rc_seed(v->r, 0x66, HOW_VECTOR);
}

static View *get_view(int page, int slot)
{
    if (page < 0 || page >= (int)sms->cart.pages || slot < 0 || slot > 2) return NULL;
    if (view_at[page][slot]) return &views[view_at[page][slot] - 1];
    View *nv = realloc(views, (size_t)(nviews + 1) * sizeof *views);
    if (!nv) die("sin memoria");
    views = nv;
    View *v = &views[nviews++];
    v->page = page;
    v->slot = slot;
    v->img = malloc(SMS_PAGE_SIZE);
    v->r = malloc(sizeof *v->r);
    v->xsrc = calloc(SMS_PAGE_SIZE, sizeof *v->xsrc);
    if (!v->img || !v->r || !v->xsrc) die("sin memoria");
    u16 lo = (u16)(slot * SMS_PAGE_SIZE);
    for (u32 i = 0; i < SMS_PAGE_SIZE; i++)
        v->img[i] = (sega && slot == 0 && i < 0x400) ? sms->cart.image[i] : sms->cart.image[(size_t)page * SMS_PAGE_SIZE + i];
    char prefix[16];
    snprintf(prefix, sizeof prefix, "p%02Xs%d_", page, slot);
    z80rc_init(v->r, prefix, lo, (u16)(lo + SMS_PAGE_SIZE - 1), v->img);
    view_at[page][slot] = nviews;
    seed_vectors(v);
    return v;
}

/* Siembra a en la vista que corresponde: la misma si a cae en su ranura; si no, la de la página que está ahí al
 * encender. from: el desplazamiento de la instrucción que la delató. 1 si es nueva */
static int seed_at(int vi, u16 a, int how, u32 from)
{
    View *v = &views[vi];
    if (a >= 0xC000) return 0;
    if (!z80rc_in(v->r, a)) {
        int ts = a >> 14;
        v = get_view(default_page(ts), ts);
        if (!v) return 0;
        if (how == HOW_FLOW) how = HOW_CROSS;
    }
    if (v->r->queued[a]) return 0;
    z80rc_seed(v->r, a, how);
    v->xsrc[a & 0x3FFF] = from + 1;
    return 1;
}

/* El byte que ve el Z80 en a con la vista vi en su ranura (y lo del encendido en las otras), o -1 en la RAM */
static int peek_view(int vi, u32 a)
{
    if (a >= 0xC000) return -1;
    if (z80rc_in(views[vi].r, a)) return views[vi].img[a & 0x3FFF];
    int ts = (int)(a >> 14);
    if (sega && ts == 0 && a < 0x400) return sms->cart.image[a];
    return sms->cart.image[(size_t)default_page(ts) * SMS_PAGE_SIZE + (a & 0x3FFF)];
}

/* ¿Lo que hay en t parece código? Se decodifica hasta el primer salto incondicional (o 24 instrucciones) y se
 * descarta si aparece algo que en un juego casi nunca es código: RST $38 ($FF, lo que hay en la ROM vacía), un ED sin
 * definir, un prefijo DD/FD seguido de otro prefijo, o cuatro NOP seguidos. Para las heurísticas: con eso, una
 * entrada que apunta a datos no arrastra miles de instrucciones de mentira */
static int plausible(int vi, u16 t)
{
    int nops = 0;
    for (int i = 0; i < 24; i++) {
        u8 b[4];
        for (int k = 0; k < 4; k++) {
            int c = peek_view(vi, (u32)t + (u32)k);
            if (c < 0) return 0;
            b[k] = (u8)c;
        }
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
        t = (u16)(t + in.len);
    }
    return 1;
}

/* Heurísticas (las de GBRecomp llevadas al Z80; descubrir de más no rompe nada: lo que se compila son los bytes que
 * hay, y se entra por el PC exacto):
 *   tablas de saltos   un JP (HL), (IX) o (IY) con un LD HL/IX/IY,nn poco antes (24 bytes) y, entre los dos, el
 *                      ADD HL/IX/IY,rr que la indexa: nn es una tabla de direcciones de 16 bits; se toman
 *                      mientras caigan en la ROM ($0008-$BFFF), parezcan código (plausible) y la tabla no llegue
 *                      a código descubierto ni a una dirección a la que apunta ella misma, hasta 128, y se
 *                      siembran si son dos o más
 *   direcciones apiladas  LD rr,nn seguido de PUSH rr y, en los 24 bytes que siguen, un JP, JR o JP (HL)/(IX)/(IY)
 *                      sin un CALL ni un RET antes: nn es la vuelta de un RET (en el código de SDCC, un PUSH de un
 *                      inmediato es casi siempre un argumento: por eso se pide el salto) */
static int heuristics(int vi)
{
    int added = 0;
    View *v = &views[vi];
    Z80Rc *r = v->r;
    for (u32 a = r->lo; a <= r->hi; a++) {
        if (!r->ins[a]) continue;
        u8 b0 = v->img[a & 0x3FFF];
        u8 b1 = a + 1 <= r->hi ? v->img[(a + 1) & 0x3FFF] : 0;
        int ix = (b0 == 0xDD || b0 == 0xFD) && b1 == 0xE9;
        if (b0 == 0xE9 || ix) {                     /* JP (HL) / JP (IX) / JP (IY) */
            u8 want0 = ix ? b0 : 0x21;
            u32 base = 0x10000;
            int indexed = 0;
            for (u32 x = a > r->lo + 24 ? a - 24 : r->lo; x < a; x++) {
                if (!r->ins[x]) continue;
                u8 o0 = v->img[x & 0x3FFF], o1 = v->img[(x + 1) & 0x3FFF];
                if (!ix && o0 == 0x21 && x + 2 < a) {
                    base = (u32)(v->img[(x + 1) & 0x3FFF] | v->img[(x + 2) & 0x3FFF] << 8);
                    indexed = 0;
                } else if (ix && o0 == want0 && x + 3 < a && o1 == 0x21) {
                    base = (u32)(v->img[(x + 2) & 0x3FFF] | v->img[(x + 3) & 0x3FFF] << 8);
                    indexed = 0;
                } else if (!ix && (o0 & 0xCF) == 0x09) indexed = 1;                 /* ADD HL,rr */
                else if (ix && o0 == want0 && (o1 & 0xCF) == 0x09) indexed = 1;     /* ADD IX/IY,rr */
            }
            if (base >= 0xC000 || !indexed) continue;
            u32 from = off_of(v, (u16)a);
            u16 entry[128];
            unsigned n = 0;
            for (; n < 128; n++) {
                u32 at = base + 2 * n;
                if (n && z80rc_in(r, at) && (r->ins[at] || r->ins[at + 1])) break;     /* la tabla llegó a código */
                int lo = peek_view(vi, at), hi = peek_view(vi, at + 1);
                if (lo < 0 || hi < 0) break;
                u32 t = (u32)(lo | hi << 8);
                if (t >= 0xC000 || t < 0x0008 || (t >= base && t <= at + 1) || !plausible(vi, (u16)t)) break;
                entry[n] = (u16)t;
            }
            if (n < 2) continue;
            for (unsigned k = 0; k < n; k++) {
                added += seed_at(vi, entry[k], HOW_TABLE, from);
                v = &views[vi];
                r = v->r;
            }
            stat_tables++;
            stat_table_entries += n;
        } else if ((b0 & 0xCF) == 0x01 && b0 != 0x31) {   /* LD BC/DE/HL,nn y después PUSH del mismo par */
            u32 nn = (u32)(b1 | (a + 2 <= r->hi ? v->img[(a + 2) & 0x3FFF] : 0) << 8);
            u8 push = (u8)(0xC5 | (b0 & 0x30));
            int jumps = 0;
            for (u32 x = a + 4; x <= r->hi && x < a + 28 && !jumps; x++) {
                if (!r->ins[x]) continue;
                u8 o0 = v->img[x & 0x3FFF], o1 = x + 1 <= r->hi ? v->img[(x + 1) & 0x3FFF] : 0;
                if (o0 == 0xC3 || o0 == 0x18 || o0 == 0xE9 || ((o0 == 0xDD || o0 == 0xFD) && o1 == 0xE9)) jumps = 1;
                else if (o0 == 0xCD || o0 == 0xC9 || (o0 & 0xC7) == 0xC4 || (o0 & 0xC7) == 0xC0 || (o0 & 0xC7) == 0xC7) break;
            }
            if (jumps && a + 3 <= r->hi && v->img[(a + 3) & 0x3FFF] == push && nn < 0xC000 && nn >= 0x0008 &&
                plausible(vi, (u16)nn) && seed_at(vi, (u16)nn, HOW_PUSHED, off_of(v, (u16)a))) {
                added++;
                stat_pushed++;
                v = &views[vi];
                r = v->r;
            }
        }
    }
    return added;
}

/* Descubre en todas las vistas, y los saltos a otra ranura siembran la vista de la página que está ahí al
 * encender, hasta que no quede nada nuevo */
static void discover_all(void)
{
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < nviews; i++) z80rc_discover(views[i].r);
        if (use_tables)
            for (int i = 0; i < nviews; i++) changed |= heuristics(i) != 0;
        for (int i = 0; i < nviews; i++) {
            View *v = &views[i];
            for (u32 a = v->r->lo; a <= v->r->hi; a++) {
                if (!v->r->ins[a]) continue;
                u8 b[4];
                Z80Insn in;
                z80rc_bytes(v->r, (u16)a, b);
                z80_decode(b, (u16)a, &in);
                u16 s[2];
                int n = z80rc_succ((u16)a, &in, s);
                for (int k = 0; k < n; k++) {
                    if (z80rc_in(v->r, s[k]) || s[k] >= 0xC000) continue;
                    int ts = s[k] >> 14;
                    View *tv = get_view(default_page(ts), ts);
                    v = &views[i];          /* get_view puede mover la lista */
                    if (!tv || tv->r->queued[s[k]]) continue;
                    z80rc_seed(tv->r, s[k], HOW_CROSS);
                    tv->xsrc[s[k] & 0x3FFF] = off_of(v, (u16)a) + 1;
                    changed = 1;
                }
            }
        }
    }
}

/* Junta lo descubierto en las vistas por desplazamiento de la ROM, con su origen */
static void collect(void)
{
    memset(is_ins, 0, rom_size);
    stat_insns = stat_bytes = 0;
    for (int i = 0; i < nviews; i++) {
        const View *v = &views[i];
        for (u32 a = v->r->lo; a <= v->r->hi; a++) {
            if (!v->r->ins[a]) continue;
            u32 off = off_of(v, (u16)a);
            if (is_ins[off]) continue;
            u8 b[4];
            Z80Insn in;
            z80rc_bytes(v->r, (u16)a, b);
            z80_decode(b, (u16)a, &in);
            is_ins[off] = 1;
            ins_addr[off] = (u16)a;
            ins_len[off] = in.len;
            int how = v->r->how[a];
            parent[off] = NO_PARENT;
            if (how == HOW_FLOW) {
                u16 from = v->r->from[a];
                u8 fb[4];
                Z80Insn fi;
                z80rc_bytes(v->r, from, fb);
                z80_decode(fb, from, &fi);
                parent[off] = off_of(v, from);
                how_at[off] = (u16)(from + fi.len) == a && fi.kind != Z80_INS_JP && fi.kind != Z80_INS_JR ? SHOW_NEXT
                            : fi.cond || fi.kind == Z80_INS_DJNZ ? SHOW_BRANCH
                            : fi.kind == Z80_INS_CALL || fi.kind == Z80_INS_RST ? SHOW_CALL : SHOW_JUMP;
            } else if (how == HOW_CROSS || how == HOW_TABLE || how == HOW_PUSHED) {
                how_at[off] = how == HOW_CROSS ? SHOW_CROSS : how == HOW_TABLE ? SHOW_TABLE : SHOW_PUSHED;
                if (v->xsrc[a & 0x3FFF]) parent[off] = v->xsrc[a & 0x3FFF] - 1;
            } else how_at[off] = how == HOW_TRACE ? SHOW_TRACE : SHOW_VECTOR;
            stat_insns++;
            stat_bytes += in.len;
        }
    }
    /* El origen: la primera heurística de la cadena hacia atrás, o el punto de entrada (un vector o la traza) */
    for (size_t off = 0; off < rom_size; off++) {
        if (!is_ins[off]) continue;
        u32 cur = (u32)off;
        int steps = 0, org = -1;
        for (;;) {
            if (how_at[cur] == SHOW_TABLE) { org = ORG_TABLE; break; }
            if (how_at[cur] == SHOW_PUSHED) { org = ORG_SPEC; break; }
            if (parent[cur] == NO_PARENT || !is_ins[parent[cur]] || steps++ > 100000) break;
            cur = parent[cur];
        }
        origin[off] = (u8)(org >= 0 ? org : how_at[cur] == SHOW_TRACE ? ORG_TRACE : ORG_VECTORS);
    }
}

/* ------------------------------------------------------------------ */
/* Trazas                                                              */
/* ------------------------------------------------------------------ */

static RcList ram_list;             /* las instrucciones de la RAM de las trazas (líneas "ram") */

static void load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("no se pudo abrir la traza %s", path);
    char line[256];
    unsigned long off, addr;
    RcIns in;
    while (fgets(line, sizeof line, f)) {
        if (trace_line_read(line, SMS_TRACE_SHIFT, &off, &addr)) {
            if (off >= rom_size || addr >= 0xC000) continue;
            int slot = (int)(addr >> 14), page = (int)(off / SMS_PAGE_SIZE);
            if (sega && slot == 0 && addr < 0x400) page = default_page(0);
            int had = view_at[page < MAX_PAGES ? page : 0][slot];
            View *v = get_view(page, slot);
            if (!v) continue;
            if (!had) stat_views_trace++;
            if (!v->r->queued[addr]) stat_trace++;
            z80rc_seed(v->r, (u16)addr, HOW_TRACE);
        } else if (rc_parse(line, &in)) {
            in.key = (uint8_t)sms_rc_key(in.bytes);
            if (in.addr >= 0x8000 && in.addr <= 0xFFFF && !rc_list_add(&ram_list, &in)) die("sin memoria");
        }
    }
    fclose(f);
}

/* --coverage: de las instrucciones de una traza, cuántas encontró el análisis sin ayuda */
static void coverage_report(const char *path)
{
    missing_mark = calloc(rom_size, 1);
    FILE *f = fopen(path, "r");
    if (!f || !missing_mark) die("no se pudo abrir %s", path);
    char line[256];
    unsigned long off, addr;
    unsigned total = 0, found = 0, shown = 0;
    while (fgets(line, sizeof line, f)) {
        if (!trace_line_read(line, SMS_TRACE_SHIFT, &off, &addr) || off >= rom_size) continue;
        total++;
        if (is_ins[off]) { found++; continue; }
        missing_mark[off] = 1;
        if (shown < 12 || verbose) {
            u8 b[4];
            char d[64];
            for (int i = 0; i < 4; i++) b[i] = off + (size_t)i < rom_size ? rom[off + (size_t)i] : 0;
            disz80(b, (u16)addr, d, sizeof d);
            printf("  falta: ROM $%06lX = $%04lX  %s\n", off, addr, d);
            shown++;
        }
    }
    fclose(f);
    printf("cobertura: %u de %u instrucciones ejecutadas descubiertas sin traza (%.1f%%); descubiertas en total %u\n",
           found, total, total ? 100.0 * found / total : 100.0, stat_insns);
}

/* ------------------------------------------------------------------ */
/* --why, --viz, --events, --codemap, --html, --dis                    */
/* ------------------------------------------------------------------ */

static void dis_off(u32 off, char *d, size_t size)
{
    u8 b[4];
    for (int i = 0; i < 4; i++) b[i] = off + (u32)i < rom_size ? rom[off + (u32)i] : 0;
    disz80(b, ins_addr[off], d, size);
}

/* BB:DIRECCION (como en la traza) o un desplazamiento hexadecimal */
static u32 why_arg(const char *arg)
{
    unsigned long off, addr;
    if (strchr(arg, ':') && trace_line_read(arg, SMS_TRACE_SHIFT, &off, &addr)) return (u32)off;
    return (u32)strtoul(arg, NULL, 16);
}

static void explain(u32 off)
{
    if (off >= rom_size || !is_ins[off]) {
        printf("ROM $%06X: no se descubrió como código\n", off);
        return;
    }
    printf("ROM $%06X ($%04X) se tomó como código así:\n", off, ins_addr[off]);
    char d[64], d0[64];
    u32 cur = off;
    int steps = 0;
    while (cur != NO_PARENT && steps++ < 4000) {
        dis_off(cur, d, sizeof d);
        u32 par = parent[cur], first = cur;
        int how = how_at[cur], run = 0;
        /* las instrucciones seguidas se agrupan: solo se cuenta cuántas hubo */
        while (how == SHOW_NEXT && par != NO_PARENT) { cur = par; par = parent[cur]; how = how_at[cur]; run++; }
        if (run) {
            dis_off(cur, d0, sizeof d0);
            printf("  $%06X $%04X  %-24s  ...%d instrucciones seguidas hasta  $%06X $%04X  %s\n", first, ins_addr[first], d,
                   run, cur, ins_addr[cur], d0);
        } else {
            printf("  $%06X $%04X  %s\n", cur, ins_addr[cur], d);
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
    for (size_t i = 0; i < rom_size; i++) {
        if (!is_ins[i]) continue;
        char d[64];
        dis_off((u32)i, d, sizeof d);
        if (parent[i] == NO_PARENT)
            fprintf(f, "%06zX %04X %s - %s %s\n", i, ins_addr[i], how_name[how_at[i]], org_name[origin[i]], d);
        else
            fprintf(f, "%06zX %04X %s %06X %s %s\n", i, ins_addr[i], how_name[how_at[i]], parent[i], org_name[origin[i]], d);
    }
    fclose(f);
}

/* Bytes por celda del mapa: 256 (64 celdas por fila de 16 KB, una página) */
static size_t viz_cell(void) { return 256; }

static void viz_map(int color)
{
    static const int org_color[ORG_COUNT] = { 46, 51, 226, 201 };        /* verde, cian, amarillo, magenta */
    size_t cell = viz_cell(), row_size = cell * 64;
    printf("\nMapa de la ROM (una celda = %zu bytes; una fila por página de %zu KB):\n", cell, row_size / 1024);
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
            for (size_t i = c0; i < c0 + cell && i < rom_size; i++) {
                if (is_ins[i]) { bytes += ins_len[i]; cnt[origin[i]] += ins_len[i]; }
                if (missing_mark && missing_mark[i]) miss = 1;
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

/* --codemap: qué bytes de la ROM se tomaron como código (C) y cuáles no (.), 64 por línea. El mismo formato que
 * difftest --executed y el --codemap de los otros recompiladores */
static void write_codemap(const char *path)
{
    u8 *code = calloc(rom_size, 1);
    FILE *f = fopen(path, "w");
    if (!code || !f) die("no se pudo crear %s", path);
    for (size_t i = 0; i < rom_size; i++)
        if (is_ins[i])
            for (int k = 0; k < ins_len[i] && i + (size_t)k < rom_size; k++) code[i + (size_t)k] = 1;
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

#define VISOR_NAME "SMSRecomp"     /* el título de la página (visor_html.h, común en tools/retrorecomp/shared) */
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
    for (int i = 0; i < SHOW_COUNT; i++) { if (i) fputc(',', f); json_str(f, how_name[i]); }
    fputs("],\"missing\":[", f);
    int first = 1;
    if (missing_mark)
        for (size_t i = 0; i < rom_size; i++)
            if (missing_mark[i]) { fprintf(f, first ? "%zu" : ",%zu", i); first = 0; }
    fputs("],\"ins\":[\n", f);
    first = 1;
    for (size_t i = 0; i < rom_size; i++) {
        if (!is_ins[i]) continue;
        char d[64];
        dis_off((u32)i, d, sizeof d);
        fprintf(f, "%s[%zu,%u,%d,%ld,%d,", first ? "" : ",\n", i, ins_addr[i], how_at[i],
                parent[i] == NO_PARENT ? -1L : (long)parent[i], origin[i]);
        json_str(f, d);
        fprintf(f, ",%d]", ins_len[i]);
        first = 0;
    }
    fputs("\n]}", f);
    fputs(html_tail, f);
    fclose(f);
}

static void dis_range(const char *arg)
{
    unsigned long off, addr, n;
    if (sscanf(arg, "%lx:%lx:%lu", &off, &addr, &n) != 3) die("--dis espera OFFSET:DIRECCION:N (hexadecimal, hexadecimal, decimal)");
    for (unsigned long k = 0; k < n && off < rom_size; k++) {
        u8 b[4];
        char d[64];
        for (int i = 0; i < 4; i++) b[i] = off + (unsigned long)i < rom_size ? rom[off + (unsigned long)i] : 0;
        int len = disz80(b, (u16)addr, d, sizeof d);
        printf("%06lX  $%04lX  ", off, addr & 0xFFFF);
        for (int i = 0; i < 4; i++) {
            if (i < len) printf("%02X ", b[i]);
            else printf("   ");
        }
        printf(" %s%s\n", d, is_ins[off] ? "" : "   ; (no descubierta)");
        off += (unsigned long)len;
        addr += (unsigned long)len;
    }
}

/* ------------------------------------------------------------------ */
/* El código de la RAM                                                 */
/* ------------------------------------------------------------------ */

#define RAM_LO 0x8000u
#define RAM_SIZE 0x8000u
static Z80Rc *ram_rc[RC_MAXVER];
static u8 ram_img[RC_MAXVER][RAM_SIZE];
static int nram;
static unsigned ram_insns, ram_dropped;

/* Una imagen por versión: la versión k de cada dirección que tiene k + 1 o más, y la primera de las demás (para
 * que el flujo siga); los bytes variables quedan en 0 (la guarda los compara y cae al intérprete). Se compila con
 * guarda, sembrada con las direcciones que tienen esa versión */
static void build_ram(void)
{
    if (!ram_list.n) return;
    ram_dropped = (unsigned)rc_list_finish(&ram_list);
    int maxver = 0;
    for (size_t i = 0, j; i < ram_list.n; i = j) {
        for (j = i; j < ram_list.n && ram_list.ins[j].addr == ram_list.ins[i].addr; j++) {}
        if ((int)(j - i) > maxver) maxver = (int)(j - i);
    }
    if (maxver > RC_MAXVER) maxver = RC_MAXVER;
    for (int k = 0; k < maxver; k++) {
        memset(ram_img[k], 0, RAM_SIZE);
        for (size_t i = 0, j; i < ram_list.n; i = j) {
            for (j = i; j < ram_list.n && ram_list.ins[j].addr == ram_list.ins[i].addr; j++) {}
            const RcIns *in = &ram_list.ins[(size_t)k < j - i ? i + (size_t)k : i];
            for (unsigned b = 0; b < in->len && in->addr + b < RAM_LO + RAM_SIZE; b++)
                ram_img[k][in->addr + b - RAM_LO] = in->vol >> b & 1 ? 0 : in->bytes[b];
        }
        Z80Rc *r = ram_rc[k] = malloc(sizeof *r);
        if (!r) die("sin memoria");
        char prefix[16];
        snprintf(prefix, sizeof prefix, "r%d_", k);
        z80rc_init(r, prefix, RAM_LO, RAM_LO + RAM_SIZE - 1, ram_img[k]);
        r->guard = 1;
        for (size_t i = 0, j; i < ram_list.n; i = j) {
            for (j = i; j < ram_list.n && ram_list.ins[j].addr == ram_list.ins[i].addr; j++) {}
            if ((size_t)k < j - i) z80rc_seed(r, (u16)ram_list.ins[i].addr, HOW_TRACE);
        }
        z80rc_discover(r);
        ram_insns += r->ninsns;
        nram++;
    }
}

/* ------------------------------------------------------------------ */
/* El proyecto                                                         */
/* ------------------------------------------------------------------ */

/* Crea la carpeta y las que falten antes (como mkdir -p) */
static void make_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return;
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            if (stat(tmp, &st) != 0) MKDIR(tmp);
            *p = c;
        }
    if (MKDIR(path) != 0 && stat(path, &st) != 0) die("no se pudo crear %s", path);
}

static FILE *open_out(const char *dir, const char *name)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) die("no se pudo crear %s", path);
    return f;
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

/* El código de una corrida anterior (gen/z*.c, gen/z*.h): puede haber otras vistas ahora */
static void remove_old_code(const char *gdir)
{
    DIR *d = opendir(gdir);
    if (!d) return;
    struct dirent *e;
    char names[4096][64];
    int n = 0;
    while ((e = readdir(d)) && n < 4096)
        if (e->d_name[0] == 'z' && strlen(e->d_name) < sizeof names[0]) snprintf(names[n++], sizeof names[0], "%s", e->d_name);
    closedir(d);
    for (int i = 0; i < n; i++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s", gdir, names[i]);
        remove(p);
    }
}

static unsigned compiled_insns;

static void emit_views(const char *gdir)
{
    for (int i = 0; i < nviews; i++) {
        char base[32];
        snprintf(base, sizeof base, "zp%02Xs%d", views[i].page, views[i].slot);
        if (!z80rc_emit(views[i].r, gdir, base, "smsrecomp")) die("no se pudo escribir el código en %s", gdir);
        compiled_insns += views[i].r->ninsns;
    }
    for (int k = 0; k < nram; k++) {
        char base[32];
        snprintf(base, sizeof base, "zram%d", k);
        if (!z80rc_emit(ram_rc[k], gdir, base, "smsrecomp")) die("no se pudo escribir el código en %s", gdir);
        compiled_insns += ram_rc[k]->ninsns;
    }
}

static void emit_dispatch(const char *dir)
{
    FILE *f = open_out(dir, "gen/views.c");
    unsigned pages = sms->cart.pages;
    fprintf(f, "/* Generado por smsrecomp: no editar. El despachador: la vista de la página que está en la ranura del PC\n"
               " * (recomp.h), o el código aprendido de la RAM, con su guarda. */\n#include \"recomp.h\"\n\n");
    for (int i = 0; i < nviews; i++) fprintf(f, "Z80RecompFn %slookup(uint16_t pc);\n", views[i].r->prefix);
    for (int k = 0; k < nram; k++) fprintf(f, "Z80RecompFn %slookup(uint16_t pc);\n", ram_rc[k]->prefix);
    fprintf(f, "\ntypedef Z80RecompFn (*ViewLookup)(uint16_t pc);\n\n/* [página][ranura] */\nstatic const ViewLookup views[%u][3] = {\n", pages);
    for (unsigned p = 0; p < pages; p++) {
        fprintf(f, "    {");
        for (int sl = 0; sl < 3; sl++) {
            int v = p < MAX_PAGES ? view_at[p][sl] : 0;
            if (v) fprintf(f, " %slookup,", views[v - 1].r->prefix);
            else fprintf(f, " 0,");
        }
        fprintf(f, " },\n");
    }
    fprintf(f, "};\n\n");
    if (nram) {
        fprintf(f, "static const ViewLookup ram_views[%d] = {", nram);
        for (int k = 0; k < nram; k++) fprintf(f, " %slookup,", ram_rc[k]->prefix);
        fprintf(f, " };\n\n"
                   "/* Las versiones del código de la RAM, en orden: la primera cuya guarda pasa (0: ninguna, el intérprete) */\n"
                   "static int ram_any(Sms *g, uint64_t target)\n{\n"
                   "    for (int k = 0; k < %d; k++) {\n"
                   "        Z80RecompFn fn = ram_views[k](g->z80.pc);\n"
                   "        if (fn) { int r = fn(g, target); if (r) return r; }\n"
                   "    }\n    return 0;\n}\n\n", nram);
    }
    fprintf(f, "Z80RecompFn recomp_lookup(Sms *g, u16 pc)\n{\n"
               "    if (pc < 0xC000 && bus_rom_offset(g, pc) >= 0) {\n"
               "        unsigned slot = pc >> 14, page = (unsigned)(g->map.slot[slot] / SMS_PAGE_SIZE);\n"
               "        ViewLookup v = page < %uu ? views[page][slot] : 0;\n"
               "        return v ? v(pc) : 0;\n"
               "    }\n", pages);
    if (nram) {
        fprintf(f, "    if (pc >= 0x%04X)\n        for (int k = 0; k < %d; k++)\n            if (ram_views[k](pc)) return ram_any;\n", RAM_LO, nram);
    }
    fprintf(f, "    return 0;\n}\n");
    fclose(f);
}

static void emit_info(const char *dir, const char *sha1, const char *name)
{
    FILE *f = open_out(dir, "gen/info.c");
    fprintf(f, "/* Generado por smsrecomp: no editar. */\n#include \"recomp.h\"\n\n");
    fprintf(f, "const char recomp_rom_sha1[41] = \"%s\";\n", sha1);
    fprintf(f, "const char recomp_name[] = \"%s\";\n", name);
    fprintf(f, "const unsigned recomp_instructions = %u;\n", compiled_insns);
    fclose(f);
}

static void emit_makefile(const char *dir, const char *name, const char *ext)
{
    FILE *f = open_out(dir, "Makefile");
    fprintf(f,
        "# Generado por smsrecomp. Compila con cualquier gcc/clang C99 con SDL2 (make -j para usar\n"
        "# todos los núcleos). Windows: MSYS2 UCRT64.\n\n"
        "CC     ?= cc\n"
        "# MSYS2 sin \"cc\" (por ejemplo MINGW64): se usa gcc\n"
        "ifeq ($(origin CC),default)\n"
        "CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)\n"
        "endif\n"
        "CFLAGS ?= -std=c99 -O2 -Wall -Wno-unused-label\n"
        "INC    := -Iruntime -Igen -Iplatform -DSMS_RECOMP\n\n"
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
        "build/gen_%%.o: gen/%%.c $(wildcard runtime/*.h) | build\n"
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
        "# Navegador (WebAssembly): make web, con Emscripten o con Docker (ver platform/web.mk). La copia de la ROM\n"
        "# lleva la extensión del juego: con ella el runtime elige el modelo (.gg es la Game Gear)\n"
        "WEB_NAME   := %s\n"
        "WEB_TITLE  := %s\n"
        "WEB_PREF   := /libsdl/SMSRecomp/%s/\n"
        "WEB_CACHE  := rom%s\n"
        "WEB_ACCEPT := .sms,.gg,.sg\n"
        "include platform/web.mk\n\n"
        "# Android: make apk, con el SDK de Android o con Docker (ver platform/android/android.mk)\n"
        "APK_NAME   := %s\n"
        "APK_TITLE  := %s\n"
        "APK_SYSTEM := smsrecomp\n"
        "APK_CACHE  := rom%s\n"
        "APK_ICON   := platform/icono.h\n"
        "include platform/android/android.mk\n\n"
        "clean:\n\trm -rf build dist web apk %s$(EXE) difftest$(EXE) bench$(EXE)\n",
        name, name, name, name, name, name, ext, name, name, ext, name);
    fclose(f);
}

static void emit_readme(const char *dir, const char *name, const char *sha1, const char *ext)
{
    FILE *f = open_out(dir, "LEEME.txt");
    fprintf(f,
        "Proyecto generado por smsrecomp.\n\n"
        "Compilar (necesita un compilador C99, make y SDL2):\n"
        "  macOS:   brew install sdl2 pkg-config\n"
        "  Debian:  sudo apt install build-essential pkg-config libsdl2-dev\n"
        "  Windows: MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 make\n"
        "           (el Makefile usa gcc si no hay cc; el .exe lleva el icono)\n"
        "           el .exe enlaza SDL2 de forma estática (no pide DLLs); make dist lo deja en dist/\n\n"
        "  make -j\n"
        "  ./%s juego%s\n\n"
        "El proyecto no contiene la ROM. El ejecutable la pide y comprueba que su SHA1 sea\n"
        "%s.\n"
        "Si se ejecuta sin argumentos busca en la carpeta actual una ROM con ese SHA1.\n\n"
        "Prueba diferencial (código recompilado contra el intérprete):\n"
        "  ./difftest juego%s 600\n"
        "Lo que el intérprete tuvo que ejecutar (para recompilar de nuevo con smsrecomp --trace):\n"
        "  ./difftest juego%s 3600 --seed 1 --uncovered faltante.txt\n"
        "Velocidad:\n"
        "  ./bench juego%s\n\n"
        "En el navegador (WebAssembly; necesita Emscripten o Docker):\n"
        "  make web\n"
        "  python3 -m http.server -d web     y abrir http://localhost:8000/%s.html\n"
        "La página pide la ROM (no va en el build) y la guarda en el navegador con la partida.\n\n"
        "En Android (necesita el SDK de Android y un JDK 17, o Docker):\n"
        "  make apk                       y luego  adb install -r apk/%s-debug.apk\n"
        "La primera vez el juego pide la ROM con el selector de archivos del teléfono y la guarda.\n"
        "make apk ROM=juego%s la mete en la APK (solo para uso personal: no compartas esa APK).\n"
        "En una pantalla táctil, el mando en pantalla es el jugador 1.\n", name, ext, sha1, ext, ext, ext, name, name, ext);
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
    const char *rom_path = NULL, *out = NULL, *name_arg = NULL, *coverage = NULL, *dis_arg = NULL;
    const char *events_path = NULL, *html_path = NULL, *codemap_path = NULL;
    const char *traces[64];
    u32 why_list[32];
    int ntraces = 0, nwhy = 0, info = 0, viz = 0, force_color = 0, model = MODEL_AUTO;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("smsrecomp %s\n", SMSRECOMP_VERSION); return 0; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "--info")) { info = 1; continue; }
        if (!strcmp(a, "--verbose")) { verbose = 1; continue; }
        if (!strcmp(a, "--viz")) { viz = 1; continue; }
        if (!strcmp(a, "--color")) { force_color = 1; continue; }
        if (!strcmp(a, "--no-tables")) { use_tables = 0; continue; }
        if (i + 1 < argc) {
            if (!strcmp(a, "-o")) { out = argv[++i]; continue; }
            if (!strcmp(a, "--name")) { name_arg = argv[++i]; continue; }
            if (!strcmp(a, "--coverage")) { coverage = argv[++i]; continue; }
            if (!strcmp(a, "--dis")) { dis_arg = argv[++i]; continue; }
            if (!strcmp(a, "--events")) { events_path = argv[++i]; continue; }
            if (!strcmp(a, "--html")) { html_path = argv[++i]; continue; }
            if (!strcmp(a, "--codemap")) { codemap_path = argv[++i]; continue; }
            if (!strcmp(a, "--model")) {
                model = model_from_name(argv[++i]);
                if (model < MODEL_AUTO) die("modelo desconocido: %s (sms o gg)", argv[i]);
                continue;
            }
            if (!strcmp(a, "--trace")) {
                if (ntraces < 64) traces[ntraces++] = argv[++i];
                else die("demasiadas trazas (64 como mucho)");
                continue;
            }
            if (!strcmp(a, "--why")) {
                if (nwhy < 32) why_list[nwhy++] = why_arg(argv[++i]);
                else die("demasiados --why (32 como mucho)");
                continue;
            }
        }
        if (a[0] == '-' && a[1]) { fprintf(stderr, "opción desconocida o sin valor: %s\n\n%s", a, usage_text); return 2; }
        if (rom_path) die("sobra un argumento: %s", a);
        rom_path = a;
    }
    if (!rom_path) { fputs(usage_text, stderr); return 2; }
    int analysis = coverage || dis_arg || nwhy || viz || events_path || html_path || codemap_path;

    size_t len;
    u8 *data = read_file(rom_path, &len);
    if (!data) die("no se pudo leer %s", rom_path);
    sms = calloc(1, sizeof *sms);
    const char *err = NULL;
    if (!sms || sms_load(sms, data, len, rom_path, model, &err)) die("%s: %s", rom_path, err ? err : "sin memoria");
    free(data);
    if (info || (!out && !analysis)) {
        print_info(rom_path, sms);
        return 0;
    }

    const Cart *cart = &sms->cart;
    rom = cart->rom;
    rom_size = cart->rom_size;
    sega = cart->mapper == MAPPER_SEGA;
    if (cart->pages > MAX_PAGES) die("la ROM tiene %u páginas (%d como mucho)", cart->pages, MAX_PAGES);
    is_ins = calloc(rom_size, 1);
    how_at = calloc(rom_size, 1);
    origin = calloc(rom_size, 1);
    ins_len = calloc(rom_size, 1);
    ins_addr = calloc(rom_size, sizeof *ins_addr);
    parent = malloc(rom_size * sizeof *parent);
    if (!is_ins || !how_at || !origin || !ins_len || !ins_addr || !parent) die("sin memoria");
    printf("%s: %zu KB, mapper %s, SHA1 %s\n", rom_path, rom_size / 1024, mapper_name(cart->mapper), cart->sha1);

    /* Primero sin traza (lo que mide --coverage): las vistas del mapeo al encender */
    for (int s = 0; s < 3; s++) get_view(default_page(s), s);
    discover_all();
    collect();
    if (coverage) coverage_report(coverage);
    unsigned base_insns = stat_insns;
    for (int i = 0; i < ntraces; i++) load_trace(traces[i]);
    discover_all();
    collect();
    build_ram();
    printf("código: %u instrucciones (%u bytes, %.2f%% de la ROM)", stat_insns, stat_bytes, 100.0 * stat_bytes / (double)rom_size);
    if (ntraces) printf(", %u por la traza (%u puntos de entrada, %u vistas más)", stat_insns - base_insns, stat_trace, stat_views_trace);
    printf("; %d vistas (página y ranura)", nviews);
    if (nram) printf("; código de la RAM: %u instrucciones en %d versiones%s", ram_insns, nram,
                     ram_dropped ? " (y direcciones con demasiadas versiones, que quedan en el intérprete)" : "");
    printf("\n");
    if (verbose)
        printf("tablas de saltos: %u (%u entradas), direcciones apiladas: %u\n", stat_tables, stat_table_entries, stat_pushed);
    if (verbose)
        for (int i = 0; i < nviews; i++)
            printf("  página %02X en la ranura %d ($%04X): %u instrucciones\n", views[i].page, views[i].slot,
                   views[i].slot * SMS_PAGE_SIZE, views[i].r->ninsns);

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
    emit_views(gdir);
    emit_dispatch(out);
    emit_info(out, cart->sha1, name);
    /* La extensión de la ROM: la de la copia que guarda el juego (rom.sms o rom.gg, como en main_sdl.c) */
    const char *dot = strrchr(rom_path, '.');
    const char *ext = dot && (!strcmp(dot, ".gg") || !strcmp(dot, ".GG")) ? ".gg" : ".sms";
    emit_makefile(out, name, ext);
    emit_readme(out, name, cart->sha1, ext);
    printf("%u instrucciones compiladas en %s/gen (%d vistas%s); el ejecutable se llamará %s\n", compiled_insns, out, nviews,
           nram ? " y el código de la RAM" : "", name);
    return 0;
}
