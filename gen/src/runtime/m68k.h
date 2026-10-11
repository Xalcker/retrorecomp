/*
 * 68000: estado del CPU y lo que el CPU necesita de la consola (el bus).
 *
 * La semántica de cada instrucción está en m68k_ops.h, la única fuente de verdad: el intérprete
 * (cpu68k.c) la usa con valores de tiempo de ejecución y el código generado con constantes.
 */
#ifndef M68K_H
#define M68K_H

#include <stdint.h>

struct Gen;

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
    uint8_t  tas;               /* 1 durante la escritura de TAS (el Genesis la ignora en la RAM) */
} M68k;

/* Bus: lo implementa la consola (o m68ktest). Cada acceso avanza 4 ciclos del 68000. Las
 * direcciones llegan con 24 bits; las de palabra son pares (el CPU ya levantó el error de
 * dirección si no). */
uint16_t m68k_fetch(struct Gen *g, uint32_t addr);          /* lectura de programa */
uint8_t  m68k_rd8(struct Gen *g, uint32_t addr);
uint16_t m68k_rd16(struct Gen *g, uint32_t addr);
void     m68k_wr8(struct Gen *g, uint32_t addr, uint8_t v);
void     m68k_wr16(struct Gen *g, uint32_t addr, uint16_t v);
void     m68k_idle(struct Gen *g, uint32_t cycles);         /* ciclos sin acceso al bus */
uint16_t m68k_peek16(struct Gen *g, uint32_t addr);         /* sin efectos ni ciclos (prefetch al salir del código generado) */
void     m68k_reset_line(struct Gen *g);                    /* instrucción RESET: reinicia los periféricos */
void     m68k_int_ack(struct Gen *g, int level);            /* el CPU atiende una interrupción de ese nivel */

/* Intérprete (cpu68k.c) */
void     m68k_reset(struct Gen *g);                         /* reset del CPU: SSP y PC de los vectores 0 y 1 */
int      m68k_step(struct Gen *g);                          /* una instrucción (o la interrupción pendiente); ciclos */
void     m68k_run(struct Gen *g, int (*after)(struct Gen *));   /* instrucciones hasta que after() devuelva != 0 */
uint16_t m68k_get_sr(const M68k *c);
void     m68k_set_sr(M68k *c, uint16_t sr);
int      m68k_decode(uint16_t op);                          /* K_* de la instrucción (K_ILLEGAL si no es válida) */
int      m68k_ext_words(uint16_t op);                       /* palabras de extensión que siguen al opcode */

#endif
