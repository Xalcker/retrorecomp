/*
 * Código que el juego copia a RAM y ejecuta desde ahí, para el ciclo de trazas (--uncovered →
 * recompilador --trace). Común a los recompiladores; GBRecomp lo hizo primero (su ramtrace.h y
 * «Código que el juego copia a WRAM o HRAM» en su docs/ARQUITECTURA.md, con la lista de lo que
 * hay que replicar en otra consola).
 *
 * Por qué de la traza: lo que corre desde RAM no está en la ROM en esa dirección, así que el
 * descubrimiento estático no llega, y deducir qué bytes habrá exigiría emular la copia. El
 * intérprete sí los ve: lee de RAM lo que ejecuta. Se anotan aquí y el recompilador compila
 * bloques con una guarda que comprueba, al entrar, que la RAM siga teniendo esos bytes; si no,
 * cae al intérprete.
 *
 * Formato, una línea por versión de cada instrucción, en el mismo archivo que la traza común:
 *   ram DIRECCION B0 B1 ...      (hexadecimal; ?? en los bytes que cambiaron entre ejecuciones)
 * No empieza con un dígito hexadecimal, así que trace_line_read la ignora: los lectores de trazas
 * "BANCO:DIRECCION" de cualquier consola siguen funcionando igual.
 *
 * Cada consola dice cuántos bytes mide una instrucción y cuántos la identifican (la clave: el
 * opcode, y lo que cambie lo que hace). El resto son operandos que pueden variar sin que sea otra
 * instrucción (un valor que el juego parcha, el destino de un salto). Por dirección se guardan
 * hasta RC_VERSIONS claves distintas; con más, es código que el juego genera con estructuras
 * distintas cada vez y se queda en el intérprete.
 *
 * Dos mitades: RcTable, para quien ejecuta (el runtime, difftest, la herramienta de traza y el
 * ejecutable del juego: todos escriben las mismas líneas), y RcList / rc_blocks, para el
 * recompilador, que une lo visto en varias corridas y arma bloques.
 */
#ifndef RAM_CODE_H
#define RAM_CODE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RC_MAXLEN   10      /* bytes de la instrucción más larga (68000: opcode y 4 palabras de extensión) */
#define RC_VERSIONS 4       /* claves distintas por dirección al anotar */
#define RC_MAXVER   8       /* al compilar (lo visto en varias corridas): más es código generado */
#define RC_MAXCHAIN 1024    /* instrucciones por bloque */

/* Una versión de una instrucción */
typedef struct {
    uint32_t addr;
    uint32_t n;                  /* ejecuciones */
    uint8_t len, key;            /* bytes de la instrucción y cuántos la identifican */
    uint16_t vol;                /* bit i: el byte i cambió entre ejecuciones (solo i >= key) */
    uint8_t bytes[RC_MAXLEN];
} RcIns;

static inline int rc_same_key(const RcIns *a, const RcIns *b)
{
    return a->len == b->len && a->key == b->key && memcmp(a->bytes, b->bytes, a->key) == 0;
}

/* La misma instrucción vista otra vez: los operandos que difieren pasan a ser variables */
static inline void rc_merge(RcIns *into, const RcIns *from)
{
    for (unsigned i = into->key; i < into->len; i++)
        if (into->bytes[i] != from->bytes[i]) into->vol |= (uint16_t)(1u << i);
    into->vol |= from->vol;
    into->n += from->n;
}

/* --- Anotar (quien ejecuta) --- */

typedef struct {
    uint32_t n;
    uint8_t nver, overflow;      /* overflow: más claves que RC_VERSIONS; no se aprende */
    RcIns v[RC_VERSIONS];
} RcSlot;

typedef struct {
    uint32_t base, size;         /* región de RAM: [base, base + size) */
    RcSlot **slot;               /* uno por byte, creado la primera vez que se ejecuta ahí */
} RcTable;

static inline int rc_init(RcTable *t, uint32_t base, uint32_t size)
{
    t->base = base;
    t->size = size;
    t->slot = (RcSlot **)calloc(size, sizeof *t->slot);
    return t->slot != NULL;
}

static inline void rc_add(RcTable *t, const RcIns *in)
{
    if (!t->slot || in->addr - t->base >= t->size || in->len > RC_MAXLEN || in->key > in->len) return;
    RcSlot **sp = &t->slot[in->addr - t->base];
    if (!*sp && !(*sp = (RcSlot *)calloc(1, sizeof **sp))) return;
    RcSlot *s = *sp;
    s->n += in->n;
    for (unsigned i = 0; i < s->nver; i++)
        if (rc_same_key(&s->v[i], in)) { rc_merge(&s->v[i], in); return; }
    if (s->nver < RC_VERSIONS) s->v[s->nver++] = *in;
    else s->overflow = 1;
}

/* El intérprete va a ejecutar la instrucción de len bytes en addr; key bytes la identifican */
static inline void rc_observe(RcTable *t, uint32_t addr, const uint8_t *bytes, unsigned len, unsigned key)
{
    RcIns in;
    memset(&in, 0, sizeof in);
    in.addr = addr;
    in.n = 1;
    in.len = (uint8_t)len;
    in.key = (uint8_t)key;
    memcpy(in.bytes, bytes, len <= RC_MAXLEN ? len : RC_MAXLEN);
    rc_add(t, &in);
}

/* Las líneas "ram"; addr_digits: los de las direcciones de la consola */
static inline void rc_write(const RcTable *t, FILE *f, int addr_digits)
{
    if (!t->slot) return;
    for (uint32_t i = 0; i < t->size; i++) {
        const RcSlot *s = t->slot[i];
        if (!s || s->overflow) continue;
        for (unsigned k = 0; k < s->nver; k++) {
            const RcIns *v = &s->v[k];
            fprintf(f, "ram %0*lX", addr_digits, (unsigned long)v->addr);
            for (unsigned b = 0; b < v->len; b++)
                if (v->vol >> b & 1) fprintf(f, " ??");
                else fprintf(f, " %02X", v->bytes[b]);
            fputc('\n', f);
        }
    }
}

/* Lee una línea "ram"; deja la dirección, los bytes y los variables (key queda en 0: lo pone la
 * consola según el opcode). 0 si la línea es de otro tipo o está mal */
static inline int rc_parse(const char *line, RcIns *in)
{
    char *end;
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "ram ", 4) != 0) return 0;
    memset(in, 0, sizeof *in);
    const char *p = line + 4;
    unsigned long a = strtoul(p, &end, 16);
    if (end == p) return 0;
    in->addr = (uint32_t)a;
    in->n = 1;
    p = end;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n' || *p == '\r') break;
        if (in->len == RC_MAXLEN) return 0;
        if (p[0] == '?' && p[1] == '?') {
            in->vol |= (uint16_t)(1u << in->len);
            in->len++;
            p += 2;
            continue;
        }
        unsigned long b = strtoul(p, &end, 16);
        if (end != p + 2 || b > 0xFF) return 0;
        in->bytes[in->len++] = (uint8_t)b;
        p = end;
    }
    return in->len > 0;
}

/* Para el resumen: instrucciones fijas, con operandos variables, direcciones con varias versiones y
 * direcciones que cambian demasiado (no se aprenden) */
typedef struct { unsigned fixed, vol, multi, lost; } RcSummary;

static inline RcSummary rc_summary(const RcTable *t)
{
    RcSummary r = { 0, 0, 0, 0 };
    if (!t->slot) return r;
    for (uint32_t i = 0; i < t->size; i++) {
        const RcSlot *s = t->slot[i];
        if (!s) continue;
        if (s->overflow) { r.lost++; continue; }
        if (s->nver > 1) r.multi++;
        for (unsigned k = 0; k < s->nver; k++)
            if (s->v[k].vol) r.vol++;
            else r.fixed++;
    }
    return r;
}

/* --- Compilar (el recompilador) --- */

typedef struct { RcIns *ins; size_t n, cap; } RcList;

static inline int rc_list_add(RcList *l, const RcIns *in)
{
    if (l->n == l->cap) {
        size_t cap = l->cap ? 2 * l->cap : 256;
        RcIns *p = (RcIns *)realloc(l->ins, cap * sizeof *p);
        if (!p) return 0;
        l->ins = p;
        l->cap = cap;
    }
    l->ins[l->n++] = *in;
    return 1;
}

/* Orden por dirección y por clave */
static inline int rc_cmp(const void *a, const void *b)
{
    const RcIns *x = (const RcIns *)a, *y = (const RcIns *)b;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return memcmp(x->bytes, y->bytes, x->key);
}

/* Une las versiones iguales vistas en varias corridas, deja en 0 los bytes variables y descarta las
 * direcciones con más de RC_MAXVER versiones; devuelve cuántas instrucciones se descartaron. Las
 * claves (key) ya deben estar puestas */
static inline size_t rc_list_finish(RcList *l)
{
    size_t kept = 0, dropped = 0;
    if (l->n) qsort(l->ins, l->n, sizeof *l->ins, rc_cmp);
    for (size_t i = 0; i < l->n;) {
        size_t j = i + 1;
        while (j < l->n && rc_cmp(&l->ins[i], &l->ins[j]) == 0) rc_merge(&l->ins[i], &l->ins[j++]);
        for (unsigned b = 0; b < l->ins[i].len; b++)
            if (l->ins[i].vol >> b & 1) l->ins[i].bytes[b] = 0;
        l->ins[kept++] = l->ins[i];
        i = j;
    }
    l->n = kept;
    kept = 0;
    for (size_t i = 0; i < l->n;) {
        size_t j = i + 1;
        while (j < l->n && l->ins[j].addr == l->ins[i].addr) j++;
        if (j - i <= RC_MAXVER) for (size_t x = i; x < j; x++) l->ins[kept++] = l->ins[x];
        else dropped += j - i;
        i = j;
    }
    l->n = kept;
    return dropped;
}

/* Un bloque: instrucciones de la lista que van una detrás de otra sin ambigüedad */
typedef struct {
    size_t *node;                /* índices en la lista, en orden de dirección */
    size_t n;
    uint32_t start, len;         /* primera dirección y bytes que cubre */
} RcBlock;

static inline int rc_block_has(const RcList *l, const RcBlock *b, uint32_t addr)
{
    for (size_t i = 0; i < b->n; i++)
        if (l->ins[b->node[i]].addr == addr) return 1;
    return 0;
}

/* Primera instrucción con dirección >= addr (la lista está ordenada) */
static inline size_t rc_lower(const RcList *l, uint32_t addr)
{
    size_t lo = 0, hi = l->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (l->ins[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Arma los bloques: cadenas en las que cada instrucción tiene un solo sucesor en la lista y ese
 * sucesor un solo predecesor. joins(a, b), si no es NULL, dice si dos direcciones seguidas pueden
 * ir en el mismo bloque (por ejemplo, no cruzar entre bancos de RAM que no son contiguos).
 * Devuelve cuántos bloques dejó en *out (o (size_t)-1 sin memoria) */
static inline size_t rc_blocks(const RcList *l, RcBlock **out, int (*joins)(uint32_t a, uint32_t b))
{
    size_t n = l->n, nb = 0, m = n ? n : 1;
    size_t *next = (size_t *)malloc(m * sizeof *next), *succ = (size_t *)malloc(m * sizeof *succ);
    unsigned *succ_n = (unsigned *)calloc(m, sizeof *succ_n), *pred_n = (unsigned *)calloc(m, sizeof *pred_n);
    unsigned char *has_prev = (unsigned char *)calloc(m, 1), *seen = (unsigned char *)calloc(m, 1);
    RcBlock *blocks = (RcBlock *)calloc(m, sizeof *blocks);
    if (!next || !succ || !succ_n || !pred_n || !has_prev || !seen || !blocks) { nb = (size_t)-1; goto out; }
    for (size_t i = 0; i < n; i++) {
        uint32_t end = l->ins[i].addr + l->ins[i].len;
        succ[i] = rc_lower(l, end);
        while (succ[i] + succ_n[i] < n && l->ins[succ[i] + succ_n[i]].addr == end) {
            pred_n[succ[i] + succ_n[i]]++;
            succ_n[i]++;
        }
    }
    for (size_t i = 0; i < n; i++) {
        next[i] = (size_t)-1;
        if (succ_n[i] != 1 || pred_n[succ[i]] != 1) continue;
        if (joins && !joins(l->ins[i].addr, l->ins[succ[i]].addr)) continue;
        next[i] = succ[i];
        has_prev[succ[i]] = 1;
    }
    for (int pass = 0; pass < 2; pass++)    /* la segunda recoge lo que quedó tras cortar una cadena larga */
        for (size_t i = 0; i < n; i++) {
            if (seen[i] || (pass == 0 && has_prev[i])) continue;
            RcBlock *b = &blocks[nb++];
            if (!(b->node = (size_t *)malloc(RC_MAXCHAIN * sizeof *b->node))) { nb = (size_t)-1; goto out; }
            for (size_t cur = i; cur != (size_t)-1 && !seen[cur] && b->n < RC_MAXCHAIN; cur = next[cur]) {
                b->node[b->n++] = cur;
                seen[cur] = 1;
            }
            const RcIns *first = &l->ins[b->node[0]], *last = &l->ins[b->node[b->n - 1]];
            b->start = first->addr;
            b->len = last->addr + last->len - first->addr;
        }
out:
    free(next); free(succ); free(succ_n); free(pred_n); free(has_prev); free(seen);
    if (nb == (size_t)-1) { free(blocks); return nb; }
    *out = blocks;
    return nb;
}

#endif
