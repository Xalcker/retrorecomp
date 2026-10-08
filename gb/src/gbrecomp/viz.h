/*
 * Registro de eventos del descubrimiento (--events) y su visualización
 * animada en la terminal (--viz).
 *
 * gbrecomp anota cada paso (recorrido que empieza, instrucción descubierta,
 * tabla leída, salto sin resolver...) y al terminar el registro se escribe a
 * un archivo o se reproduce como un mapa de la ROM que se va encendiendo.
 */
#ifndef VIZ_H
#define VIZ_H

#include <stddef.h>
#include <stdint.h>

/* De dónde viene un recorrido (lo heredan los destinos que descubre) */
enum {
    ORG_BOOT,       /* desde $0100 */
    ORG_IRQ,        /* desde un vector de interrupción */
    ORG_TABLE,      /* desde una entrada de una tabla de punteros */
    ORG_TRACE,      /* desde una dirección de la traza (--trace) */
    ORG_COUNT
};

/* Fases del descubrimiento */
enum { PH_VECTORS, PH_TABLES, PH_TRACE, PH_TRACE_TABLES };

enum {
    EV_PHASE,       /* aux: PH_* */
    EV_WALK,        /* empieza un recorrido lineal en bank:addr, llegando desde bank2:addr2;
                     * aux: 1 si bank2:addr2 es una entrada de tabla, 0 si es una instrucción */
    EV_INSN,        /* instrucción de `len` bytes en bank:addr */
    EV_TABLE,       /* tabla de punteros en bank:addr, encontrada por la instrucción en
                     * bank2:addr2; aux: 1 en línea, 0 fuera de línea */
    EV_TENTRY,      /* entrada de tabla en bank:addr que apunta a bank2:addr2 */
    EV_UNRESOLVED,  /* salto en bank:addr a addr2, en el área de bancos, sin banco conocido */
    EV_INDIRECT,    /* jp hl en bank:addr sin tabla reconocida */
    EV_TRACE,       /* punto de entrada de la traza; aux: 1 si ya se conocía */
    EV_ARGS         /* `len` bytes de argumentos en línea en bank:addr, después de una llamada */
};

#define EV_NONE 0xFFFF  /* bank2 sin valor (no hay origen o banco desconocido) */

typedef struct {
    uint8_t type, origin, len, aux;
    uint16_t bank, addr, bank2, addr2;
} Event;

extern int ev_on;   /* solo se anota si se pidió --events o --viz */

void ev_add(int type, int origin, unsigned bank, unsigned addr, unsigned bank2, unsigned addr2,
            int len, int aux);
int ev_write(const char *path);
const Event *ev_list(size_t *n);

/* Dirección que el juego ejecutó (--coverage): --viz la marca hasta que se descubra */
void viz_want(unsigned bank, unsigned addr);
void viz_play(unsigned nbanks, const char *title, double seconds);

#endif
