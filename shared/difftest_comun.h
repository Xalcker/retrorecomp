/*
 * difftest: la prueba diferencial del código recompilado, común a los cinco recompiladores (retrorecomp/shared). Corre el juego en
 * dos máquinas con las mismas entradas, B (el código recompilado, o el intérprete donde no hay) y A (solo el intérprete), y las
 * compara; termina en la primera diferencia. Es el difftest de cada proyecto generado.
 *   difftest rom [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--screens carpeta [--every N]] [--bench]
 *                [--executed archivo] [--estricto] [--save archivo] [--save-out archivo] [boton:desde-hasta ...]
 *   --frames       cuadros a comparar; con --input y sin --frames, hasta 10 segundos después del último cambio de botones.
 *   --uncovered f  escribe, en formato de traza ("BANCO:DIRECCION" y las líneas "ram"), las instrucciones de ROM que B tuvo que
 *                  interpretar: sirven como --trace para recompilar y cubrirlas.
 *   --screens d    guarda una captura PNG (cuadro_NNNNNN.png) cada --every N cuadros (600), para ver hasta dónde llegó.
 *   --bench        mide la velocidad del recompilado y del intérprete, sin comparar.
 *   --executed f   escribe qué bytes del programa se ejecutaron (los registra la máquina A): una letra por byte, C = ejecutado,
 *                  64 por línea.
 *   --estricto     falla (código 1) en la primera instrucción de la ROM que B corre con el intérprete porque no está
 *                  recompilada ("ESTRICTO: código que falta en ..."). El código en RAM sí pasa. Al final, "Intérprete por
 *                  motivo: ..." cuenta las entradas al intérprete (metrica.h). Con --uncovered, también al fallar escribe lo
 *                  que B interpretó hasta ahí, el código que falta incluido: la traza del hueco, para --trace o el almacén.
 *   --save f       empieza las dos máquinas con esa partida guardada (la RAM con batería, la EEPROM o la tarjeta); --save-out f
 *                  guarda la de B al terminar.
 * Sin diferencias termina con "difftest: N cuadros iguales (L llamadas, R a código recompilado)", la línea común "Métrica: ..."
 * (metrica.h) y una última línea RESUMEN {json}.
 *
 * Cada difftest.c define sus dos máquinas y lo propio de la consola (qué se compara, cómo avanza B y A hasta un mismo punto, las
 * capturas, las trazas) y llama a difftest_main con un DiffOps. El esqueleto es éste: opciones, carga, bench, el bucle de cuadros, las
 * capturas y todo el final.
 */
#ifndef DIFFTEST_COMUN_H
#define DIFFTEST_COMUN_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli.h"
#include "console.h"
#include "metrica.h"

#define DT_ZONAS 6

/* Lo que comparten el esqueleto y los ganchos durante una corrida */
typedef struct {
    Cli *c;
    long cuadros;                       /* los que se comparan */
    unsigned long llamadas, recompiladas;
    u64 zona[DT_ZONAS];                 /* ciclos del intérprete de B por zona del PC (ver DiffOps.zonas) */
    u64 cyc0;                           /* ciclos de B al empezar */
    struct DtGolpe *golpes;             /* cuántas veces interpretó B cada instrucción de ROM (dt_golpe) */
    size_t ngolpes, capgolpes;
} DiffCtx;

/* Las instrucciones de ROM que B corrió con el intérprete, con su cuenta: las más ejecutadas salen al final. `clave` es la dirección
 * (o el desplazamiento) que la consola sabe mostrar con DiffOps.golpe */
typedef struct DtGolpe { u32 clave, n; } DtGolpe;

static inline void dt_golpe(DiffCtx *x, u32 clave)
{
    if ((x->ngolpes + 1) * 2 > x->capgolpes) {      /* tabla abierta: se agranda al llenarse la mitad */
        size_t nueva = x->capgolpes ? x->capgolpes * 2 : 1024;
        DtGolpe *t = calloc(nueva, sizeof *t);
        if (!t) return;
        for (size_t i = 0; i < x->capgolpes; i++)
            if (x->golpes[i].n) {
                size_t h = (x->golpes[i].clave * 2654435761u) & (nueva - 1);
                while (t[h].n) h = (h + 1) & (nueva - 1);
                t[h] = x->golpes[i];
            }
        free(x->golpes);
        x->golpes = t;
        x->capgolpes = nueva;
    }
    size_t h = (clave * 2654435761u) & (x->capgolpes - 1);
    while (x->golpes[h].n && x->golpes[h].clave != clave) h = (h + 1) & (x->capgolpes - 1);
    if (!x->golpes[h].n) x->ngolpes++;
    x->golpes[h].clave = clave;
    x->golpes[h].n++;
}

/* Resultado del hook cuadro */
enum { DT_IGUAL = 0, DT_DIFERENCIA = 1, DT_ESTRICTO = 2 };

typedef struct {
    const char *titulo;                 /* recomp_name, para el encabezado */
    const char *uso_rom, *uso_extra;    /* «rom.bin», «juego»...; opciones propias de la consola para el uso ("" si no hay) */
    long cuadros;                       /* por omisión */
    double fps;                         /* cuadros por segundo de la consola real */
    const char *unidad;                 /* «ROM» o «PRG»: «Instrucciones de ROM interpretadas» */
    const char *zonas[DT_ZONAS];        /* nombres de las zonas de DiffCtx.zona, terminados con NULL */
    int sin_leer;                       /* el juego no es un archivo que se lea entero (Neo Geo): cargar recibe solo la ruta */

    /* carga: A y B (y lo suyo); NULL si todo bien, o el motivo */
    const char *(*cargar)(const u8 *rom, size_t len, const Cli *c);
    const char *(*sha1)(const u8 *rom, size_t len);                     /* el SHA1 de lo que se cargó, para compararlo con el recompilado */
    const char *(*preparar)(const Cli *c);                              /* paleta, qué compara bench... (puede ser NULL) */
    int (*cargar_partida)(const char *ruta);                            /* --save en A y B: 1 si se cargó (puede ser NULL: no tiene) */
    int (*guardar_partida)(const char *ruta);                           /* --save-out de B: 1 si se guardó */
    void (*trazas)(const Cli *c);                                       /* --uncovered: la traza de B; --executed: la de A */
    void (*reiniciar)(void);                                            /* metrica_reiniciar(&B) y las estadísticas de la consola */

    /* --bench: B y A por separado */
    void *(*maquina)(int k);                                            /* 0: B (recompilado), 1: A (intérprete) */
    void (*modo)(int k);                                                /* fuerza el intérprete en k = 1 (puede ser NULL) */
    void (*pad)(void *g, unsigned p1, unsigned p2);
    void (*cuadro_solo)(void *g);
    void (*vaciar)(void *g);

    /* la comparación */
    int (*cuadro)(DiffCtx *x, long f);                                  /* compara el cuadro f; DT_* */
    u64 (*ciclos)(void);                                                /* los ciclos de B */
    int (*captura)(const char *ruta);                                   /* PNG del cuadro de B */

    /* el final */
    void (*extra)(void);                                                /* líneas propias tras la zona (puede ser NULL) */
    size_t (*uncovered)(const char *ruta, int *ok);                     /* escribe la traza de B; cuántas instrucciones; ok = 0 si falló */
    void (*ram)(unsigned *fijas, unsigned *variables, unsigned *varias, unsigned *perdidas);
    int (*executed)(const char *ruta);                                  /* escribe lo que ejecutó A */
    void (*golpe)(char *out, size_t size, u32 clave);                   /* cómo se muestra la clave de dt_golpe: «$001A2C», «PRG $0123» (puede ser NULL) */
    void (*informe)(DiffCtx *x);                                        /* detalle propio al terminar (puede ser NULL) */
} DiffOps;

static inline u8 *dt_leer(const char *ruta, size_t *len)
{
    FILE *f = fopen(ruta, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz > 0 ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = (size_t)sz;
    return b;
}

/* Compara un campo de dos máquinas A y B (los difftest.c que la usan definen A y B) */
#define DT_CMP(field, what) do { if (memcmp(&A.field, &B.field, sizeof A.field)) { puts("  difiere: " what); bad = 1; } } while (0)

static inline int difftest_main(int argc, char **argv, const DiffOps *o)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, o->cuadros)) {
        fprintf(stderr, "uso: difftest %s [--frames N] [--input archivo] [--seed S] [--uncovered archivo]%s\n"
                        "          [--screens carpeta [--every N]] [--bench] [--executed archivo] [--estricto]\n"
                        "          [--save archivo] [--save-out archivo] [boton:desde-hasta ...]\n", o->uso_rom, o->uso_extra);
        return 2;
    }
    long frames = c.frames;
    if (c.input && !c.frames_given) frames = inputs_last_frame(&c.script) + 600;    /* hasta 10 s después del último cambio */
    size_t len = 0;
    u8 *rom = NULL;
    if (!o->sin_leer) {
        rom = dt_leer(c.pos[0], &len);
        if (!rom) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    }
    const char *err = o->cargar(rom, len, &c);
    if (err) { fprintf(stderr, "%s\n", err); return 2; }
    const char *sha = o->sha1(rom, len);
    if (strcmp(sha, recomp_rom_sha1)) {
        fprintf(stderr, "este juego no es el que se recompiló (SHA1 %s, esperaba %s)\n", sha, recomp_rom_sha1);
        return 2;
    }
    if (o->preparar && (err = o->preparar(&c))) { fprintf(stderr, "%s\n", err); return 2; }
    if (c.save) {
        /* La misma partida en las dos máquinas */
        if (!o->cargar_partida || !o->cargar_partida(c.save)) {
            fprintf(stderr, "no se pudo cargar %s (¿este juego no guarda partida?)\n", c.save);
            return 2;
        }
    }
    if (c.save_out && !o->guardar_partida) { fprintf(stderr, "este juego no guarda partida: --save-out no vale\n"); return 2; }
    cli_rewind(&c);
    printf("%s: %ld cuadros, semilla %u\n", o->titulo, frames, c.seed);

    if (c.bench) {
        /* Velocidad: cada máquina por separado, con las mismas entradas */
        const char *nombre[2] = { "recompilado", "intérprete" };
        for (int k = 0; k < 2; k++) {
            void *m = o->maquina(k);
            cli_rewind(&c);
            if (o->modo) o->modo(k);
            clock_t t0 = clock();
            for (long f = 0; f < frames; f++) {
                o->pad(m, cli_pad(&c, f, 0), cli_pad(&c, f, 1));
                o->cuadro_solo(m);
                o->vaciar(m);
            }
            double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
            printf("  %-12s %.2f s  (%.1fx tiempo real)\n", nombre[k], s, s > 0 ? (double)frames / o->fps / s : 0.0);
        }
        if (o->modo) o->modo(0);
        if (o->extra) o->extra();
        return 0;
    }
    if (o->trazas) o->trazas(&c);

    static DiffCtx x;
    x.c = &c;
    x.cuadros = frames;
    x.cyc0 = o->ciclos();
    o->reiniciar();
    cli_rewind(&c);
    for (long f = 0; f < frames; f++) {
        int r = o->cuadro(&x, f);
        if (r) {
            if (r == DT_ESTRICTO && c.uncovered) {
                /* La traza del hueco: lo que B interpretó hasta aquí, con el código que falta */
                int ok = 1;
                size_t cnt = o->uncovered(c.uncovered, &ok);
                if (!ok) fprintf(stderr, "no pude escribir %s\n", c.uncovered);
                else printf("ESTRICTO: la traza del hueco (%zu instrucciones de %s interpretadas, la que falta incluida) quedó en %s\n",
                            cnt, o->unidad, c.uncovered);
            }
            metrica_resumen_falla(f, r == DT_ESTRICTO);
            return 1;
        }
        if (c.screens && c.every > 0 && (f + 1) % c.every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/cuadro_%06ld.png", c.screens, f + 1);
            if (!o->captura(path)) fprintf(stderr, "no pude escribir %s\n", path);
        }
    }
    u64 interp = 0;
    int nz = 0;
    while (nz < DT_ZONAS && o->zonas[nz]) interp += x.zona[nz++];
    if (interp) {
        printf("Intérprete por zona: ");
        for (int k = 0; k < nz; k++) printf("%s%s %.1f%%", k ? ", " : "", o->zonas[k], 100.0 * (double)x.zona[k] / (double)interp);
        printf("\n");
    }
    if (o->extra) o->extra();
    printf("difftest: %ld cuadros iguales (%lu llamadas, %lu a código recompilado)\n", frames, x.llamadas, x.recompiladas);
    u64 ciclos = o->ciclos() - x.cyc0;
    metrica_imprimir(&metrica, ciclos);
    char res[200];
    int nres = snprintf(res, sizeof res, "\"cuadros\":%ld,\"llamadas\":%lu,\"recompilado\":%lu", frames, x.llamadas, x.recompiladas);
    metrica_imprimir_motivos(&metrica);
    if (c.uncovered) {
        int ok = 1;
        size_t cnt = o->uncovered(c.uncovered, &ok);
        if (!ok) { fprintf(stderr, "no pude escribir %s\n", c.uncovered); return 2; }
        printf("Instrucciones de %s interpretadas: %zu\n", o->unidad, cnt);
        snprintf(res + nres, sizeof res - (size_t)nres, ",\"interpretadas\":%zu", cnt);
        unsigned fijas, variables, varias, perdidas;
        o->ram(&fijas, &variables, &varias, &perdidas);
        if (fijas || variables || perdidas)
            printf("Código en RAM: %u instrucciones fijas, %u con operandos variables, %u direcciones con varias versiones, "
                   "%u que cambian demasiado\n", fijas, variables, varias, perdidas);
    }
    if (x.ngolpes) {
        /* Las 20 más ejecutadas por el intérprete: más veces primero; en empate, la dirección menor */
        enum { TOP = 20 };
        DtGolpe top[TOP];
        unsigned ntop = 0;
        for (size_t i = 0; i < x.capgolpes; i++) {
            DtGolpe g = x.golpes[i];
            if (!g.n) continue;
            if (ntop == TOP && !(g.n > top[TOP - 1].n || (g.n == top[TOP - 1].n && g.clave < top[TOP - 1].clave))) continue;
            if (ntop < TOP) ntop++;
            unsigned k = ntop - 1;
            while (k > 0 && (g.n > top[k - 1].n || (g.n == top[k - 1].n && g.clave < top[k - 1].clave))) { top[k] = top[k - 1]; k--; }
            top[k] = g;
        }
        printf("Las más ejecutadas por el intérprete:\n");
        for (unsigned i = 0; i < ntop; i++) {
            char dir[32];
            if (o->golpe) o->golpe(dir, sizeof dir, top[i].clave);
            else snprintf(dir, sizeof dir, "$%X", top[i].clave);
            printf("  interpretada en %s: %s (%u veces)\n", o->unidad, dir, top[i].n);
        }
    }
    if (o->informe) o->informe(&x);
    if (c.executed && !o->executed(c.executed)) { fprintf(stderr, "no pude escribir %s\n", c.executed); return 2; }
    if (c.save_out && !o->guardar_partida(c.save_out)) fprintf(stderr, "no se pudo escribir %s\n", c.save_out);
    metrica_resumen("difftest", 1, res, &metrica, ciclos);
    return 0;
}

#endif
