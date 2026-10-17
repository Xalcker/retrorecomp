/*
 * El Z80 en las herramientas de análisis de GENRecomp y NEORecomp (--why z80:..., --dis z80:..., --events y --html con --z80): lo
 * que descubrió el recompilador común (z80_recomp.h) de cada tramo de código del Z80 (en GEN, cada versión del driver que el 68000
 * sube a la RAM del Z80; en NEO, la M1), puesto en el mismo formato que el 68000 y en el mismo mapa de la página (visor_html.h).
 *
 * Cada tramo es una «vista» con su nombre y un desplazamiento propio en el mapa, a continuación de la ROM: el desplazamiento de una
 * instrucción es base + dirección. Común (retrorecomp/shared); lo incluye el recompilador de la consola, después de z80_recomp.h y
 * z80_dis.h.
 */
#ifndef Z80_VISTA_H
#define Z80_VISTA_H

#include <stdio.h>
#include <string.h>

#include "z80_recomp.h"
#include "z80_dis.h"

#define Z80V_MAX 8

typedef struct {
    char nombre[40];        /* «Z80, versión 0», «Z80, M1»... */
    const Z80Rc *r;         /* ya descubierto */
    size_t base;            /* desplazamiento de su dirección lo en el mapa */
} Z80Vista;

typedef struct {
    Z80Vista v[Z80V_MAX];
    int n;
    size_t sig;             /* el desplazamiento de la próxima vista */
} Z80Vistas;

/* Empieza las vistas en el desplazamiento `base0` (el final de la ROM, alineado a la celda del mapa) */
static inline void z80v_init(Z80Vistas *z, size_t base0)
{
    memset(z, 0, sizeof *z);
    z->sig = base0;
}

static inline void z80v_add(Z80Vistas *z, const char *nombre, const Z80Rc *r)
{
    if (z->n == Z80V_MAX) return;
    Z80Vista *v = &z->v[z->n++];
    snprintf(v->nombre, sizeof v->nombre, "%s", nombre);
    v->r = r;
    v->base = z->sig - r->lo;       /* base + lo = donde empieza */
    z->sig += (size_t)r->hi - r->lo + 1;
}

/* Cuánto mide el mapa con las vistas (para D.mapa de la página) */
static inline size_t z80v_total(const Z80Vistas *z, size_t rom_size) { return z->n ? z->sig : rom_size; }

/* De dónde salió una instrucción: los nombres que se escriben en --events y en la página */
static inline const char *z80v_como(int como)
{
    return como == Z80RC_SEED ? "semilla del Z80" : como == Z80RC_TRACE ? "traza del Z80" : "flujo del Z80";
}

/* El origen (índice del org_name de la consola): la semilla y el flujo son «flujo desde los vectores», la traza es «traza» */
static inline int z80v_origen(int como) { return como == Z80RC_TRACE ? 1 : 0; }

static inline int z80v_largo(const Z80Rc *r, uint16_t a)
{
    uint8_t b[4];
    Z80Insn in;
    z80rc_bytes(r, a, b);
    z80_decode(b, a, &in);
    return in.len == 1 && (b[0] == 0xDD || b[0] == 0xFD) ? 2 : in.len;
}

static inline void z80v_texto(const Z80Rc *r, uint16_t a, char *out, size_t size)
{
    uint8_t b[4];
    z80rc_bytes(r, a, b);
    z80_dis(b, a, out, size);
}

/* --events: las mismas columnas que las del 68000 (desplazamiento, dirección, cómo se llegó, de dónde, origen, texto) */
static inline void z80v_events(FILE *f, const Z80Vistas *z, const char *const *org_name)
{
    for (int k = 0; k < z->n; k++) {
        const Z80Vista *v = &z->v[k];
        for (uint32_t a = v->r->lo; a <= v->r->hi; a++) {
            if (!v->r->ins[a]) continue;
            char d[96];
            z80v_texto(v->r, (uint16_t)a, d, sizeof d);
            int como = v->r->ins[a];
            if (como == Z80RC_FLOW)
                fprintf(f, "%06zX %04X %s %06zX %s %s\n", v->base + a, a, z80v_como(como), v->base + v->r->from[a], org_name[z80v_origen(como)], d);
            else
                fprintf(f, "%06zX %04X %s - %s %s\n", v->base + a, a, z80v_como(como), org_name[z80v_origen(como)], d);
        }
    }
}

/* --html: las instrucciones, como entradas [desplazamiento, dirección, cómo, padre, origen, texto, largo] de la página. `como0` es el
 * índice del primer nombre de z80v_como en la lista de formas de llegar de la página (los tres seguidos); first: 1 si todavía no
 * se escribió ninguna entrada. `json` escribe una cadena JSON */
static inline void z80v_html(FILE *f, const Z80Vistas *z, int como0, int *first, void (*json)(FILE *, const char *))
{
    for (int k = 0; k < z->n; k++) {
        const Z80Vista *v = &z->v[k];
        for (uint32_t a = v->r->lo; a <= v->r->hi; a++) {
            if (!v->r->ins[a]) continue;
            char d[96];
            z80v_texto(v->r, (uint16_t)a, d, sizeof d);
            int como = v->r->ins[a];
            long padre = como == Z80RC_FLOW ? (long)(v->base + v->r->from[a]) : -1L;
            fprintf(f, "%s[%zu,%u,%d,%ld,%d,", *first ? "" : ",\n", v->base + a, a, como0 + como - 1, padre, z80v_origen(como));
            json(f, d);
            fprintf(f, ",%d]", z80v_largo(v->r, (uint16_t)a));
            *first = 0;
        }
    }
}

/* --html: los nombres de las vistas y dónde empiezan, para "regiones" de la página: [[desde, hasta, "nombre"], ...] */
static inline void z80v_regiones(FILE *f, const Z80Vistas *z, void (*json)(FILE *, const char *))
{
    fputc('[', f);
    for (int k = 0; k < z->n; k++) {
        const Z80Vista *v = &z->v[k];
        fprintf(f, "%s[%zu,%zu,", k ? "," : "", v->base + v->r->lo, v->base + v->r->hi + 1);
        json(f, v->nombre);
        fputc(']', f);
    }
    fputc(']', f);
}

/* --why z80:V:DIRECCION: la cadena de instrucciones desde un punto de entrada */
static inline void z80v_why(const Z80Vistas *z, int k, uint16_t addr)
{
    if (k < 0 || k >= z->n) { printf("Z80: no hay vista %d\n", k); return; }
    const Z80Vista *v = &z->v[k];
    const Z80Rc *r = v->r;
    if (!z80rc_in(r, addr) || !r->ins[addr]) { printf("%s $%04X: no se descubrió como código\n", v->nombre, addr); return; }
    printf("%s $%04X se tomó como código así:\n", v->nombre, addr);
    uint32_t cur = addr;
    for (int pasos = 0; pasos++ < 4000;) {
        char d[96];
        z80v_texto(r, (uint16_t)cur, d, sizeof d);
        printf("  $%04X  %s\n", cur, d);
        int como = r->ins[cur];
        if (como != Z80RC_FLOW) { printf("  └ punto de entrada: %s\n", z80v_como(como)); return; }
        printf("  └ %s desde\n", z80v_como(como));
        cur = r->from[cur];
    }
}

/* --dis z80:V:DIRECCION:N: N instrucciones desde esa dirección, con sus bytes */
static inline void z80v_dis(const Z80Vistas *z, int k, uint16_t addr, unsigned long n)
{
    if (k < 0 || k >= z->n) { printf("Z80: no hay vista %d\n", k); return; }
    const Z80Vista *v = &z->v[k];
    uint32_t a = addr;
    for (unsigned long i = 0; i < n && z80rc_in(v->r, a); i++) {
        char d[96];
        z80v_texto(v->r, (uint16_t)a, d, sizeof d);
        int len = z80v_largo(v->r, (uint16_t)a);
        uint8_t b[4];
        z80rc_bytes(v->r, (uint16_t)a, b);
        printf("%06zX  $%04X  ", v->base + a, a);
        for (int j = 0; j < 4; j++) { if (j < len) printf("%02X ", b[j]); else printf("   "); }
        printf(" %s%s\n", d, v->r->ins[a] ? "" : "   ; (no descubierta)");
        a += (uint32_t)len;
    }
}

#endif
