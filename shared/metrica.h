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
 * Cada entrada al intérprete lleva además un motivo (metrica_motivo, en el despachador de cada sistema): el código que
 * falta (una instrucción de la ROM que el recompilador no encontró: lo que difftest --estricto no deja pasar), código
 * en RAM, el intérprete forzado (a propósito: tras EI o un bug de HALT en GB, con --interp...) o el CPU detenido. Las
 * interrupciones y las esperas no son entradas al intérprete: van en sus ciclos.
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
    unsigned long long motivo[4];           /* entradas al intérprete por motivo (MOTIVO_...) */
    unsigned long falta_pc;                 /* el PC del primer código que falta (si motivo[MOTIVO_FALTA]) */
} Metrica;

/* Por qué el despachador usó el intérprete */
enum { MOTIVO_FALTA, MOTIVO_RAM, MOTIVO_FORZADO, MOTIVO_DETENIDO, MOTIVOS };
static const char *const metrica_motivos[MOTIVOS] = { "código que falta", "RAM", "intérprete forzado", "CPU detenido" };

extern Metrica metrica;
extern const void *metrica_de;

#define METRICA_DEFINIR Metrica metrica; const void *metrica_de;
#define METRICA(g, campo, n) do { if ((const void *)(g) == metrica_de) metrica.campo += (n); } while (0)

static inline void metrica_reiniciar(const void *maquina)
{
    static const Metrica cero;
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

/* El despachador de `g` usó el intérprete en `pc` por `motivo` (solo cuenta si g es metrica_de) */
static inline void metrica_motivo(const void *g, int motivo, unsigned long pc)
{
    if (g != metrica_de) return;
    if (motivo == MOTIVO_FALTA && !metrica.motivo[MOTIVO_FALTA]) metrica.falta_pc = pc;
    metrica.motivo[motivo]++;
}

/* difftest --estricto: si ya hubo código que falta, lo dice (con `cifras` hexadecimales en el PC y, si no es NULL,
 * `detalle` entre paréntesis) y devuelve 1 */
static inline int metrica_estricto(long cuadro, int cifras, const char *detalle)
{
    if (!metrica.motivo[MOTIVO_FALTA]) return 0;
    printf("ESTRICTO: código que falta en $%0*lX%s%s%s, cuadro %ld: el intérprete ejecutó una instrucción de la ROM que "
           "no está recompilada\n", cifras, metrica.falta_pc, detalle ? " (" : "", detalle ? detalle : "",
           detalle ? ")" : "", cuadro);
    return 1;
}

/* "Intérprete por motivo: ..." (nada si no se anotó ningún motivo) */
static inline void metrica_imprimir_motivos(const Metrica *m)
{
    unsigned long long t = 0;
    for (int i = 0; i < MOTIVOS; i++) t += m->motivo[i];
    if (!t) return;
    printf("Intérprete por motivo:");
    for (int i = 0, n = 0; i < MOTIVOS; i++)
        if (m->motivo[i]) printf("%s %llu %s", n++ ? "," : "", m->motivo[i], metrica_motivos[i]);
    printf("\n");
}

/*
 * La línea "RESUMEN {json}" de difftest y del bench: lo mismo que las líneas de arriba, para la GUI y los scripts, que
 * así no leen el texto. Es la última línea de la salida (en una sola línea):
 *   RESUMEN {"herramienta":"difftest","ok":true,"cuadros":1500,"llamadas":40,"recompilado":38,
 *            "metrica":{"nativas_pct":97.3,"nativas":1234567,"instrucciones":1268800,
 *                       "ciclos":{"nativo":80.1,"rom":2.0,"ram":0.5,"irq":1.2,"espera":16.0,"otros":0.2},
 *                       "motivos":{"falta":0,"ram":3,"forzado":0,"detenido":0}}}
 * `campos` son los propios de la herramienta, ya en JSON y sin llaves (pueden ir vacíos); sin `m`, sin "metrica".
 */
static inline void metrica_resumen(const char *herramienta, int ok, const char *campos, const Metrica *m,
                                   unsigned long long total)
{
    printf("RESUMEN {\"herramienta\":\"%s\",\"ok\":%s%s%s", herramienta, ok ? "true" : "false",
           campos && *campos ? "," : "", campos ? campos : "");
    if (m) {
        unsigned long long ins = m->ins_nat + m->ins_int;
        unsigned long long conocidos = m->nat + m->rom + m->ram + m->irq + m->espera;
        unsigned long long otros = total > conocidos ? total - conocidos : 0;
        if (total < conocidos) total = conocidos;
        printf(",\"metrica\":{\"nativas_pct\":%.1f,\"nativas\":%llu,\"instrucciones\":%llu,\"ciclos\":{\"nativo\":%.1f,"
               "\"rom\":%.1f,\"ram\":%.1f,\"irq\":%.1f,\"espera\":%.1f,\"otros\":%.1f},\"motivos\":{\"falta\":%llu,"
               "\"ram\":%llu,\"forzado\":%llu,\"detenido\":%llu}}",
               metrica_pct(m->ins_nat, ins), m->ins_nat, ins, metrica_pct(m->nat, total), metrica_pct(m->rom, total),
               metrica_pct(m->ram, total), metrica_pct(m->irq, total), metrica_pct(m->espera, total),
               metrica_pct(otros, total), m->motivo[MOTIVO_FALTA], m->motivo[MOTIVO_RAM], m->motivo[MOTIVO_FORZADO],
               m->motivo[MOTIVO_DETENIDO]);
    }
    printf("}\n");
}

/* El RESUMEN de un difftest que falló en `cuadro`: por una diferencia o, con --estricto, por código que falta */
static inline void metrica_resumen_falla(long cuadro, int estricto)
{
    char c[96];
    snprintf(c, sizeof c, "\"cuadro\":%ld,\"motivo\":\"%s\"", cuadro, estricto ? "estricto" : "diferencia");
    metrica_resumen("difftest", 0, c, NULL, 0);
}

#endif
