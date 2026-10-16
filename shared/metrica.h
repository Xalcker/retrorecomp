/*
 * La métrica común de los cinco recompiladores: qué parte del programa corre como código nativo (recompilado) y en
 * qué se va el resto. La imprimen difftest y el bench del proyecto generado, en una línea:
 *   Métrica: 97.3% de las instrucciones nativas (1234567 de 1268800); ciclos: 80.1% nativo, 2.0% intérprete en ROM,
 *   0.5% intérprete en RAM, 1.2% entrada a interrupciones, 16.0% espera (HALT/STOP), 0.2% otros
 * (todo en una sola línea). Es del CPU principal (el 68000 en Genesis y Neo Geo; su Z80 tiene la línea "Z80:").
 * Las instrucciones nativas son las que ejecutó el código recompilado; las del intérprete, las que no se
 * encontraron recompiladas (en ROM o en RAM). Los ciclos se reparten por motivo:
 *   nativo           dentro del código recompilado
 *   intérprete ROM   el intérprete con el PC en la ROM (código que el recompilador no encontró)
 *   intérprete RAM   el intérprete con el PC en RAM (código que se copia o se genera al correr)
 *   interrupciones   la entrada a una interrupción (empujar el PC y saltar al vector)
 *   espera           el CPU dormido en HALT o STOP hasta la siguiente interrupción
 *   otros            lo que queda (DMA que detiene al CPU fuera de una instrucción, el CPU detenido...)
 *
 * Los contadores van fuera de la máquina (y de los estados guardados, que la copian entera, y de la comparación de
 * difftest). Solo cuentan los de la máquina `metrica_de` (la recompilada en difftest; la única en el bench y en la
 * ventana): METRICA(g, campo, n) suma n si g es esa máquina. Cada sistema define las dos variables con
 * METRICA_DEFINIR en un .c de su runtime.
 */
#ifndef METRICA_H
#define METRICA_H

#include <stdio.h>

typedef struct {
    unsigned long long ins_nat, ins_int;    /* instrucciones: del código recompilado y del intérprete */
    unsigned long long nat;                 /* ciclos: en código recompilado */
    unsigned long long rom, ram;            /* ciclos: el intérprete con el PC en ROM / en RAM */
    unsigned long long irq;                 /* ciclos: entrada a interrupciones */
    unsigned long long espera;              /* ciclos: HALT / STOP */
} Metrica;

extern Metrica metrica;
extern const void *metrica_de;

#define METRICA_DEFINIR Metrica metrica; const void *metrica_de;
#define METRICA(g, campo, n) do { if ((const void *)(g) == metrica_de) metrica.campo += (n); } while (0)

static inline void metrica_reiniciar(const void *maquina)
{
    Metrica cero = { 0, 0, 0, 0, 0, 0, 0 };
    metrica = cero;
    metrica_de = maquina;
}

static inline double metrica_pct(unsigned long long a, unsigned long long t) { return t ? 100.0 * (double)a / (double)t : 0.0; }

/* La línea "Métrica: ..." con `total` ciclos del CPU principal en la corrida (lo que no tiene motivo va a "otros") */
static inline void metrica_imprimir(const Metrica *m, unsigned long long total)
{
    unsigned long long ins = m->ins_nat + m->ins_int;
    unsigned long long conocidos = m->nat + m->rom + m->ram + m->irq + m->espera;
    unsigned long long otros = total > conocidos ? total - conocidos : 0;
    if (total < conocidos) total = conocidos;
    printf("Métrica: %.1f%% de las instrucciones nativas (%llu de %llu); ciclos: %.1f%% nativo, %.1f%% intérprete en ROM, "
           "%.1f%% intérprete en RAM, %.1f%% entrada a interrupciones, %.1f%% espera (HALT/STOP), %.1f%% otros\n",
           metrica_pct(m->ins_nat, ins), m->ins_nat, ins, metrica_pct(m->nat, total), metrica_pct(m->rom, total),
           metrica_pct(m->ram, total), metrica_pct(m->irq, total), metrica_pct(m->espera, total), metrica_pct(otros, total));
}

#endif
