/*
 * Esqueleto común de las herramientas *run (genrun, neorun, smsrun, gbrun, nesrun): corre un juego sin ventana con el
 * intérprete y resume lo que pasó. Cada consola da una tabla RunOps con lo suyo (cargar, correr un cuadro, el audio, la
 * captura, el resumen del hardware); aquí está todo lo demás, igual en las cinco:
 *
 *   --frames N        cuántos cuadros (600 por omisión)
 *   --input ARCHIVO   la entrada de un archivo (el de cli.h); también boton:desde-hasta, --seed
 *   --watch DIR       (hexadecimal, en la RAM de la consola) imprime el byte cada vez que cambia; se puede repetir
 *   --sram ARCHIVO    la partida (SRAM, EEPROM o tarjeta): la carga si existe y la guarda al terminar si cambió
 *                     (--card es lo mismo; --save carga y --save-out guarda, como en el difftest)
 *   --shot A.png      la imagen del último cuadro; --screens CARPETA --every N, una cada N cuadros
 *   --wav A.wav       el sonido y su nivel por segundo
 *
 * Al final: el resumen de la consola, los PC más frecuentes del último cuadro (los bucles de espera), el sonido y la imagen.
 * La consola cuenta los pasos del último cuadro llamando a run_paso(r, pc) tras cada instrucción.
 *
 * Común (retrorecomp/shared). Se incluye después de los encabezados de la consola (inputs.h pide input_host.h).
 */
#ifndef RUN_COMUN_H
#define RUN_COMUN_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "console.h"
#include "wav.h"

#define RUN_HN 8192
#define RUN_MAX_WATCH 16

typedef struct {
    int ultimo;                         /* este es el último cuadro: hay que llamar a run_paso */
    u64 pasos;                          /* instrucciones del último cuadro */
    u32 hpc[RUN_HN], hcount[RUN_HN];    /* histograma de PC (direccionamiento abierto) */
} RunCtx;

/* Una instrucción más en el último cuadro, en el PC dado */
static inline void run_paso(RunCtx *r, u32 pc)
{
    if (!r->ultimo) return;
    u32 i = (pc * 2654435761u) % RUN_HN;
    while (r->hcount[i] && r->hpc[i] != pc) i = (i + 1) % RUN_HN;
    r->hpc[i] = pc; r->hcount[i]++; r->pasos++;
}

/* Lee un archivo entero (NULL si no se puede) */
static inline u8 *run_leer(const char *path, size_t *len)
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

typedef struct {
    const char *prog, *uso_rom;        /* "genrun", "rom.bin" */
    const char *uso_extra;              /* las opciones propias en el uso (una línea por salto: "\n              ...") */
    unsigned ram_base, ram_tam;         /* la RAM de --watch: dirección de la consola y tamaño (potencia de 2) */
    int dir_dig;                        /* dígitos hexadecimales de la dirección al imprimir */
    int pc_dig;                         /* ídem para el PC */
    unsigned rate; int canales;         /* del audio que entrega audio() */

    /* opciones propias de la consola: 0 si no es suya, 1 si es una bandera, 2 si lleva valor (v) */
    int (*opcion)(const char *a, const char *v);
    /* crea la máquina y la carga: NULL si todo bien, si no el motivo */
    const char *(*cargar)(void **g, const Cli *c);
    void (*inicio)(void *g);            /* tras cargar (y la partida): lo propio de las opciones */
    void (*liberar)(void *g);
    void (*pad)(void *g, unsigned p1, unsigned p2);
    void (*cuadro)(void *g, RunCtx *r); /* un cuadro entero; en el último, run_paso tras cada instrucción */
    const char *(*parado)(void *g);     /* el motivo si el CPU se detuvo y no hay más que correr (NULL si sigue) */
    int (*audio)(void *g, int16_t *dst, int max);   /* saca hasta max cuadros de muestras; devuelve cuántos */
    int (*captura)(void *g, const char *path, int *w, int *h);
    int (*ram)(const void *g, unsigned off);        /* un byte de la RAM */
    u64 (*cuadros)(const void *g);
    u64 (*ciclos)(const void *g);
    void (*resumen)(void *g, const char *rom);      /* las líneas propias del hardware, tras cuadros y ciclos */
    const char *(*nota_pc)(unsigned pc);            /* anota un PC de la lista (por ejemplo "(BIOS)") o NULL */
    void (*final)(void *g);                         /* tras el sonido: lo propio (--fix en NEO) */
    int (*partida_cargar)(void *g, const char *path);
    int (*partida_guardar)(void *g, const char *path);
    int (*partida_sucia)(const void *g);
    const char *partida_nombre;                     /* "partida", "tarjeta" */
} RunOps;

static inline int run_main(int argc, char **argv, const RunOps *o)
{
    console_utf8();
    const char *shot = NULL, *wav = NULL, *sram = NULL;
    unsigned watch[RUN_MAX_WATCH];
    int nwatch = 0;
    char **rest = malloc(sizeof(char *) * (size_t)(argc + 1));
    int nrest = 0;
    rest[nrest++] = argv[0];
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--shot") && v) shot = argv[++i];
        else if (!strcmp(a, "--wav") && v) wav = argv[++i];
        else if ((!strcmp(a, "--sram") || !strcmp(a, "--card")) && v) sram = argv[++i];
        else if (!strcmp(a, "--watch") && v && nwatch < RUN_MAX_WATCH) {
            if (*v == '$') v++;
            watch[nwatch++] = (unsigned)strtoul(v, NULL, 16);
            i++;
        } else if (o->opcion && a[0] == '-' && a[1] == '-') {
            int r = o->opcion(a, v);
            if (r == 2) i++;
            else if (!r) rest[nrest++] = argv[i];
        } else rest[nrest++] = argv[i];
    }
    static Cli c;
    if (cli_parse(&c, nrest, rest, 1, 600) || !c.npos || c.frames <= 0) {
        fprintf(stderr, "uso: %s %s [--frames N] [--input ARCHIVO] [--seed S] [--watch DIR ...] [--sram ARCHIVO]%s\n"
                        "%*s[--shot ARCHIVO.png] [--screens CARPETA [--every N]] [--wav ARCHIVO.wav] [boton:desde-hasta ...]\n"
                        "  botones: ",
                o->prog, o->uso_rom, o->uso_extra ? o->uso_extra : "", (int)strlen(o->prog) + 7, "");
        static const char *const nombres[] = { INPUT_NAMES };
        for (int k = 0; k < INPUT_BUTTONS; k++) {
            for (const char *p = nombres[k]; *p; p++) fputc(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p, stderr);
            fputc(k + 1 < INPUT_BUTTONS ? ' ' : '\n', stderr);
        }
        free(rest);
        return 2;
    }
    free(rest);
    const char *rom = c.pos[0];
    void *g = NULL;
    const char *err = o->cargar(&g, &c);
    if (err) { fprintf(stderr, "%s: %s\n", rom, err); return 1; }
    const char *cargar_de = c.save ? c.save : sram;
    const char *guardar_en = c.save_out ? c.save_out : sram;
    if (cargar_de && o->partida_cargar && o->partida_cargar(g, cargar_de)) printf("%s: %s\n", o->partida_nombre, cargar_de);
    if (o->inicio) o->inicio(g);

    FILE *wf = NULL;
    uint32_t wav_bytes = 0;
    double sec_sum = 0;
    long sec_n = 0, sec = 0;
    if (wav) {
        wf = fopen(wav, "wb");
        if (!wf) { fprintf(stderr, "no se pudo crear %s\n", wav); return 1; }
        wav_put_header(wf, o->rate, o->canales, 0);      /* la de verdad, al final */
    }
    static RunCtx r;
    int wlast[RUN_MAX_WATCH];
    for (int i = 0; i < nwatch; i++) { watch[i] = (watch[i] >= o->ram_base ? watch[i] - o->ram_base : watch[i]) & (o->ram_tam - 1); wlast[i] = -1; }
    static int16_t snd[4096 * 2];
    for (long f = 0; f < c.frames; f++) {
        r.ultimo = f == c.frames - 1;
        o->pad(g, cli_pad(&c, f, 0), cli_pad(&c, f, 1));
        o->cuadro(g, &r);
        int n = o->audio(g, snd, 4096);
        if (wf) {
            fwrite(snd, 2 * (size_t)o->canales, (size_t)n, wf);
            wav_bytes += (uint32_t)n * 2 * (uint32_t)o->canales;
            for (int k = 0; k < n * o->canales; k++) sec_sum += (double)snd[k] * snd[k];
            sec_n += n * o->canales;
            if (sec_n >= (long)o->rate * o->canales * 2) {
                printf("audio, segundo %3ld: nivel %5.0f\n", sec++, sqrt(sec_sum / (double)sec_n));
                sec_sum = 0; sec_n = 0;
            }
        }
        if (c.screens && c.every > 0 && (f + 1) % c.every == 0) {
            char path[1024];
            int w, h;
            snprintf(path, sizeof path, "%s/%06ld.png", c.screens, f + 1);
            if (!o->captura(g, path, &w, &h)) fprintf(stderr, "no se pudo escribir %s\n", path);
        }
        for (int i = 0; i < nwatch; i++) {
            int b = o->ram(g, watch[i]);
            if (b != wlast[i]) {
                wlast[i] = b;
                printf("cuadro %5ld: [$%0*X] = $%02X\n", f, o->dir_dig, o->ram_base + watch[i], b);
            }
        }
        const char *motivo = o->parado ? o->parado(g) : NULL;
        if (motivo) { printf("%s (cuadro %ld)\n", motivo, f); break; }
    }

    u64 cuadros = o->cuadros(g);
    printf("cuadros:       %llu\n", (unsigned long long)cuadros);
    printf("instrucciones: %llu en el último cuadro\n", (unsigned long long)r.pasos);
    printf("ciclos:        %llu\n", (unsigned long long)o->ciclos(g));
    if (o->resumen) o->resumen(g, rom);
    printf("PC más frecuentes en el último cuadro:\n");
    for (int k = 0; k < 8; k++) {
        u32 best = 0, bi = 0;
        for (u32 i = 0; i < RUN_HN; i++) if (r.hcount[i] > best) { best = r.hcount[i]; bi = i; }
        if (!best) break;
        const char *nota = o->nota_pc ? o->nota_pc(r.hpc[bi]) : NULL;
        printf("  $%0*X  %u%s%s\n", o->pc_dig, r.hpc[bi], best, nota ? "  " : "", nota ? nota : "");
        r.hcount[bi] = 0;
    }
    if (wf) {
        wav_put_header(wf, o->rate, o->canales, wav_bytes);
        fclose(wf);
        printf("sonido en %s (%.1f s)\n", wav, wav_bytes / (2.0 * o->canales) / o->rate);
    }
    if (o->final) o->final(g);
    if (shot) {
        int w, h;
        if (o->captura(g, shot, &w, &h)) printf("imagen en %s (%dx%d)\n", shot, w, h);
        else printf("no se pudo escribir %s\n", shot);
    }
    if (guardar_en && o->partida_guardar && o->partida_sucia && o->partida_sucia(g)) {
        if (o->partida_guardar(g, guardar_en)) printf("%s guardada en %s\n", o->partida_nombre, guardar_en);
        else printf("no se pudo guardar la %s en %s\n", o->partida_nombre, guardar_en);
    }
    o->liberar(g);
    return 0;
}

#endif
