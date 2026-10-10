/*
 * Opciones comunes de las herramientas sin ventana (nestrace, nesshot, nesaudio, nesbench, difftest,
 * bench), las mismas que tienen gbtrace, gbshot, gbaudio, gbbench y difftest en GBRecomp:
 *   --frames N       cuadros a correr
 *   --input ARCHIVO  entradas grabadas (inputs.h); las de --record de la ventana sirven
 *   --seed S         suma un mando aleatorio reproducible con esa semilla
 *   --repeat N       corridas (nesbench): se reporta la más rápida
 *   --uncovered F    (difftest) escribe lo que el intérprete tuvo que ejecutar
 * Siguen valiendo los argumentos de antes: el número de cuadros como argumento suelto y los
 * botones sostenidos `boton:desde-hasta` (a b select start up down left right).
 */
#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nes.h"
#include "inputs.h"

typedef struct { int btn, from, to; } CliHold;

typedef struct {
    const char *pos[64]; int npos;      /* argumentos sueltos que no son botones ni opciones */
    long frames; int frames_given;
    const char *input; InputScript script;
    int use_seed; unsigned rng; u8 now; long until;
    int repeat;
    const char *uncovered;
    CliHold h[64]; int nh;
    const char *extra[64]; int nextra;  /* sueltos que no se reconocieron (p. ej. zapper:X,Y,a-b) */
} Cli;

static inline unsigned cli_rng(Cli *c)
{
    c->rng ^= c->rng << 13; c->rng ^= c->rng >> 17; c->rng ^= c->rng << 5;
    return c->rng;
}

/* Mando aleatorio: mantiene una combinación entre 4 y 43 cuadros. Start es poco frecuente
 * (en muchos juegos pausa la partida); A, B y la derecha pesan más para avanzar en el juego. */
static inline u8 cli_random_pad(Cli *c, long frame)
{
    if (frame == 0) { c->now = 0; c->until = 0; }
    if (frame >= c->until) {
        unsigned r = cli_rng(c);
        c->until = frame + 4 + (long)(r % 40);
        if ((r >> 24) % 24 == 0) c->now = 0x08;                        /* start */
        else switch ((r >> 8) % 6) {
            case 0: c->now = 0; break;
            case 1: c->now = 0x80; break;                              /* derecha */
            case 2: c->now = 0x01; break;                              /* A */
            case 3: c->now = 0x80 | 0x01; break;
            case 4: c->now = 0x40 | ((r >> 16) & 1 ? 0x01 : 0x02); break;   /* izquierda + A o B */
            default: c->now = (u8)(1u << ((r >> 16) % 8)) & (u8)~0x04; break;   /* sin select */
        }
    }
    return c->now;
}

static inline int cli_is_number(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

/*
 * Lee los argumentos. `nreq` es cuántos sueltos son obligatorios (rom, salida...): si le sigue un
 * número, es el cuadro final de la sintaxis de antes. Devuelve 0, o 2 si hay un error ya impreso.
 */
static inline int cli_parse(Cli *c, int argc, char **argv, int nreq, long default_frames)
{
    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    memset(c, 0, sizeof *c);
    c->frames = default_frames;
    c->repeat = 1;
    c->rng = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        char name[16]; int f0, f1;
        if (!strcmp(a, "--frames") && i + 1 < argc) { c->frames = atol(argv[++i]); c->frames_given = 1; }
        else if (!strcmp(a, "--input") && i + 1 < argc) c->input = argv[++i];
        else if (!strcmp(a, "--seed") && i + 1 < argc) { c->rng = (unsigned)strtoul(argv[++i], NULL, 0); c->use_seed = 1; if (!c->rng) c->rng = 1; }
        else if (!strcmp(a, "--repeat") && i + 1 < argc) c->repeat = atoi(argv[++i]);
        else if (!strcmp(a, "--uncovered") && i + 1 < argc) c->uncovered = argv[++i];
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "opción desconocida: %s\n", a); return 2; }
        else if (sscanf(a, "%15[a-z]:%d-%d", name, &f0, &f1) == 3) {
            int ok = 0;
            for (int k = 0; k < 8 && c->nh < 64; k++)
                if (!strcmp(name, nm[k])) { c->h[c->nh++] = (CliHold){ 1 << k, f0, f1 }; ok = 1; }
            if (!ok && c->nextra < 64) c->extra[c->nextra++] = a;
        }
        else if (!strncmp(a, "zapper:", 7)) { if (c->nextra < 64) c->extra[c->nextra++] = a; }
        else if (c->npos < 64) c->pos[c->npos++] = a;
    }
    if (c->npos > nreq && !c->frames_given && cli_is_number(c->pos[nreq])) {
        c->frames = atol(c->pos[nreq]); c->frames_given = 1;
        for (int k = nreq; k + 1 < c->npos; k++) c->pos[k] = c->pos[k + 1];
        c->npos--;
    }
    if (c->npos < nreq) return 2;       /* el llamador imprime el uso */
    if (c->npos > nreq) { fprintf(stderr, "argumento de más: %s\n", c->pos[nreq]); return 2; }
    if (c->frames <= 0) { fprintf(stderr, "los cuadros deben ser un número positivo\n"); return 2; }
    if (c->repeat <= 0) { fprintf(stderr, "--repeat debe ser positivo\n"); return 2; }
    if (c->input) {
        const char *err = inputs_load(&c->script, c->input);
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
    }
    return 0;
}

/* Botones del jugador `player` (0 o 1) en el cuadro `frame`; los cuadros deben pedirse en orden
 * (en cada corrida nueva hay que llamar a cli_rewind) */
static inline u8 cli_pad(Cli *c, long frame, int player)
{
    u8 pad = 0;
    if (c->input) pad = inputs_pad(&c->script, frame, player);
    if (player == 0) {
        for (int i = 0; i < c->nh; i++) if (frame >= c->h[i].from && frame <= c->h[i].to) pad |= (u8)c->h[i].btn;
        if (c->use_seed) pad |= cli_random_pad(c, frame);
    }
    return pad;
}

static inline void cli_rewind(Cli *c) { c->script.pos = 0; }

#endif
