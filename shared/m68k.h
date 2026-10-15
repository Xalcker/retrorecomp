/*
 * 68000: estado del CPU y lo que el CPU necesita de la consola (el bus).
 *
 * La semántica de cada instrucción está en m68k_ops.h, la única fuente de verdad: el intérprete
 * (cpu68k.c) la usa con valores de tiempo de ejecución y el código generado con constantes.
 *
 * Es común a GENRecomp y NEORecomp (retrorecomp/shared). Cada consola pone su m68k_host.h:
 *   M68K_HOST         el tipo de la consola (Gen, Neo): struct M68K_HOST y typedef con el mismo nombre
 *   M68K_HOST_HEADER  su cabecera ("gen.h"), que define ese tipo, con los campos cpu (M68k), cyc,
 *                     jb, trace y exc_count[], y m68k_idle(g, ciclos) en línea
 *   M68K_TRACE(g, pc) con la traza activa (g->trace), anota la instrucción que el intérprete va a
 *                     ejecutar en pc (en ROM, o en RAM con m68k_ram_trace)
 */
#ifndef M68K_H
#define M68K_H

#include <stdint.h>

#include "m68k_host.h"

struct M68K_HOST;

typedef struct {
    uint32_t d[8], a[8];        /* a[7] es la pila activa (SSP en supervisor, USP en usuario) */
    uint32_t osp;               /* la otra pila: USP en supervisor, SSP en usuario */
    uint32_t pc;                /* dirección de la instrucción que está en ir */
    uint16_t ir, irc;           /* cola de prefetch: la instrucción actual y la palabra siguiente */
    uint8_t  s, t, ipm;         /* supervisor, traza, máscara de interrupciones (0-7) */
    uint8_t  fx, fn, fz, fv, fc;/* banderas X N Z V C (0 o 1) */
    uint8_t  stopped;           /* STOP: espera una interrupción */
    uint8_t  halted;            /* doble falta: el CPU se detiene hasta el reset */
    uint8_t  ipl;               /* nivel de interrupción que pide la consola (0-7) */
    uint8_t  nmi_edge;          /* el nivel 7 subió (el 7 no se enmascara: es por flanco) */
    uint8_t  tas;               /* 1 durante la escritura de TAS (la consola decide si la hace; el Genesis la ignora en la RAM) */
} M68k;

/* Bus: lo implementa la consola (o m68ktest). Cada acceso avanza 4 ciclos del 68000. Las
 * direcciones llegan con 24 bits; las de palabra son pares (el CPU ya levantó el error de
 * dirección si no). */
uint16_t m68k_fetch(struct M68K_HOST *g, uint32_t addr);          /* lectura de programa */
uint8_t  m68k_rd8(struct M68K_HOST *g, uint32_t addr);
uint16_t m68k_rd16(struct M68K_HOST *g, uint32_t addr);
void     m68k_wr8(struct M68K_HOST *g, uint32_t addr, uint8_t v);
void     m68k_wr16(struct M68K_HOST *g, uint32_t addr, uint16_t v);
/* m68k_idle (ciclos sin acceso al bus) está en la cabecera de la consola, en línea: g->cyc += ciclos */
uint16_t m68k_peek16(struct M68K_HOST *g, uint32_t addr);         /* sin efectos ni ciclos (prefetch al salir del código generado) */
void     m68k_reset_line(struct M68K_HOST *g);                    /* instrucción RESET: reinicia los periféricos */
void     m68k_int_ack(struct M68K_HOST *g, int level);            /* el CPU atiende una interrupción de ese nivel */

/* ¿Puede seguir el código recompilado? No con una interrupción pendiente, el modo traza, STOP o el
 * CPU detenido: eso lo atiende el intérprete */
static inline int m68k_native_ok(const M68k *c)
{
    return !(c->halted | c->stopped | c->t | c->nmi_edge) && c->ipl <= c->ipm;
}

/* Intérprete (cpu68k.c) */
void     m68k_reset(struct M68K_HOST *g);                         /* reset del CPU: SSP y PC de los vectores 0 y 1 */
int      m68k_step(struct M68K_HOST *g);                          /* una instrucción (o la interrupción pendiente); ciclos */
void     m68k_run(struct M68K_HOST *g, int (*after)(struct M68K_HOST *));   /* instrucciones hasta que after() devuelva != 0 */
void     m68k_run_hybrid(struct M68K_HOST *g, int (*after)(struct M68K_HOST *), int (*native)(struct M68K_HOST *));   /* con código recompilado */
uint16_t m68k_get_sr(const M68k *c);
void     m68k_set_sr(M68k *c, uint16_t sr);
int      m68k_decode(uint16_t op);                          /* K_* de la instrucción (K_ILLEGAL si no es válida) */
int      m68k_ext_words(uint16_t op);                       /* palabras de extensión que siguen al opcode */
extern void (*m68k_ram_trace)(struct M68K_HOST *g, uint32_t pc);   /* NULL, o quien anota el código que corre desde RAM */

#endif
