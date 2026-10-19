/*
 * El almacén de trazas por SHA1 (F4.4 de docs/PLAN-ORDEN.md; Xalcker/retrorecomp#16). Una carpeta con las trazas de cada juego,
 * que los recompiladores suman a las suyas:
 *
 *   DIR/<sistema>/<SHA1>/ARCHIVO.trace   las trazas de ese juego (las de *trace, --uncovered o el sondeo; el formato es
 *                                     el de trace_line.h, una instrucción por línea)
 *   DIR/<SHA1>/ARCHIVO.trace             lo mismo, sin la carpeta del sistema
 *
 * El SHA1 es el de la ROM (cuarenta dígitos hexadecimales), así que no importa cómo se llame el archivo de cada quien, y una
 * traza no lleva nada de la ROM: solo desplazamientos y direcciones. Generaliza la caché de la BIOS de NEORecomp
 * (~/.neorecomp/bios/<SHA1>/semillas.txt), que guarda las semillas de cada versión de la BIOS.
 *
 * La carpeta sale de --traces-dir, o de la variable de entorno RETROCOMP_TRAZAS. Una traza de más no rompe nada (a lo más
 * agrega puntos de entrada), pero una de otro juego sí agregaría basura: por eso se entra por el SHA1.
 */
#ifndef TRAZAS_H
#define TRAZAS_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAZAS_ENV "RETROCOMP_TRAZAS"

/* ¿Son cuarenta dígitos hexadecimales? (el SHA1 forma parte de una ruta: nada de «..» ni de barras) */
static inline int trazas_sha1_valido(const char *s)
{
    size_t n = 0;
    for (; s[n]; n++)
        if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f') || (s[n] >= 'A' && s[n] <= 'F'))) return 0;
    return n == 40;
}

/* La carpeta del almacén: la que se dijo, o la de la variable de entorno, o NULL */
static inline const char *trazas_dir(const char *dado)
{
    if (dado && *dado) return dado;
    const char *e = getenv(TRAZAS_ENV);
    return e && *e ? e : NULL;
}

static int trazas_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Llama a fn(ruta, ctx) con cada *.trace de una carpeta, en orden alfabético (así el resultado no depende del sistema de
 * archivos). Devuelve cuántas */
static inline unsigned trazas_carpeta(const char *carpeta, void (*fn)(const char *ruta, void *ctx), void *ctx)
{
    DIR *d = opendir(carpeta);
    if (!d) return 0;
    char **nombres = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 7 || strcmp(e->d_name + l - 6, ".trace")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **nuevo = realloc(nombres, cap * sizeof *nombres);
            if (!nuevo) break;
            nombres = nuevo;
        }
        nombres[n] = strdup(e->d_name);
        if (nombres[n]) n++;
    }
    closedir(d);
    if (n) qsort(nombres, n, sizeof *nombres, trazas_cmp);
    for (size_t i = 0; i < n; i++) {
        char ruta[1024];
        snprintf(ruta, sizeof ruta, "%s/%s", carpeta, nombres[i]);
        fn(ruta, ctx);
        free(nombres[i]);
    }
    free(nombres);
    return (unsigned)n;
}

/* Las trazas del almacén para ese sistema y ese SHA1 (primero DIR/<sistema>/<SHA1>, después DIR/<SHA1>). 0 si no hay, si el
 * SHA1 no es válido o si no se dio carpeta */
static inline unsigned trazas_del_juego(const char *dir, const char *sistema, const char *sha1,
                                        void (*fn)(const char *ruta, void *ctx), void *ctx)
{
    if (!dir || !*dir || !trazas_sha1_valido(sha1)) return 0;
    char carpeta[1024];
    snprintf(carpeta, sizeof carpeta, "%s/%s/%s", dir, sistema, sha1);
    unsigned n = trazas_carpeta(carpeta, fn, ctx);
    snprintf(carpeta, sizeof carpeta, "%s/%s", dir, sha1);
    return n + trazas_carpeta(carpeta, fn, ctx);
}

/* Para los recompiladores que juntan las rutas de --trace en una lista antes de leerlas: trazas_del_juego(..., trazas_a_lista,
 * &lista) agrega una copia de cada ruta del almacén mientras quepa (las que no caben se cuentan en `sobran`) */
typedef struct { const char **v; int n, max; unsigned sobran; } TrazasLista;

static inline void trazas_a_lista(const char *ruta, void *ctx)
{
    TrazasLista *l = ctx;
    char *copia = l->n < l->max ? strdup(ruta) : NULL;
    if (copia) l->v[l->n++] = copia;
    else l->sobran++;
}

#endif
