/*
 * Opciones comunes de las herramientas sin ventana (gentrace, genshot, genaudio, genbench, ramdump,
 * difftest, bench), las mismas que tienen nestrace, nesshot... de NESRecomp y gbtrace, gbshot... de
 * GBRecomp:
 *   --frames N       cuadros a correr
 *   --input ARCHIVO  entradas grabadas (inputs.h); las de --record de la ventana sirven
 *   --seed S         suma un mando aleatorio reproducible con esa semilla
 *   --repeat N       corridas (genbench): se reporta la más rápida
 *   --uncovered F    (difftest) escribe lo que el intérprete tuvo que ejecutar
 *   --screens DIR    (difftest) guarda una captura PNG cada --every N cuadros (600 si no se dice)
 *   --bench          (difftest) mide la velocidad del recompilado y del intérprete, sin comparar
 *   --executed F     (difftest) escribe qué bytes de ROM se ejecutaron (formato de genrecomp --codemap)
 *   --expect PNG     (genshot) pasa si la pantalla final es idéntica al PNG, como gbtest --expect
 *   --hash           (genshot) imprime un hash de todos los cuadros y de todo el audio, como gbtest --hash
 * También valen el número de cuadros como argumento suelto y los botones sostenidos
 * `boton:desde-hasta` (a b c x y z start mode up down left right).
 */
#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gen.h"
#include "inputs.h"
#include "random_pad.h"     /* el mismo mando aleatorio que GBRecomp */

typedef struct { int btn, from, to; } CliHold;

typedef struct {
    const char *pos[64]; int npos;      /* argumentos sueltos que no son botones ni opciones */
    long frames; int frames_given;
    const char *input; InputScript script;
    int use_seed; unsigned seed; RandomPad rp;
    int repeat;
    const char *uncovered;
    const char *screens, *executed; long every; int bench;     /* solo difftest */
    const char *expect; int hash;                               /* solo genshot */
    CliHold h[64]; int nh;
    const char *extra[64]; int nextra;  /* sueltos con forma de botón que no se reconocieron */
} Cli;

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
    static const char *nm[] = { "up", "down", "left", "right", "b", "c", "a", "start", "z", "y", "x", "mode" };
    memset(c, 0, sizeof *c);
    c->frames = default_frames;
    c->repeat = 1;
    c->every = 600;
    c->seed = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        char name[16]; int f0, f1;
        if (!strcmp(a, "--frames") && i + 1 < argc) { c->frames = atol(argv[++i]); c->frames_given = 1; }
        else if (!strcmp(a, "--input") && i + 1 < argc) c->input = argv[++i];
        else if (!strcmp(a, "--seed") && i + 1 < argc) { c->seed = (unsigned)strtoul(argv[++i], NULL, 0); c->use_seed = 1; }
        else if (!strcmp(a, "--repeat") && i + 1 < argc) c->repeat = atoi(argv[++i]);
        else if (!strcmp(a, "--uncovered") && i + 1 < argc) c->uncovered = argv[++i];
        else if (!strcmp(a, "--screens") && i + 1 < argc) c->screens = argv[++i];
        else if (!strcmp(a, "--every") && i + 1 < argc) c->every = atol(argv[++i]);
        else if (!strcmp(a, "--executed") && i + 1 < argc) c->executed = argv[++i];
        else if (!strcmp(a, "--bench")) c->bench = 1;
        else if (!strcmp(a, "--expect") && i + 1 < argc) c->expect = argv[++i];
        else if (!strcmp(a, "--hash")) c->hash = 1;
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "opción desconocida: %s\n", a); return 2; }
        else if (sscanf(a, "%15[a-z]:%d-%d", name, &f0, &f1) == 3) {
            int ok = 0;
            for (int k = 0; k < 12 && c->nh < 64; k++)
                if (!strcmp(name, nm[k])) { c->h[c->nh++] = (CliHold){ 1 << k, f0, f1 }; ok = 1; }
            if (!ok && c->nextra < 64) c->extra[c->nextra++] = a;
        }
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
    random_pad_init(&c->rp, c->seed);
    return 0;
}

/* El mando aleatorio común (RP_*, los bits de la NES) en los botones del Genesis: A y B del NES
 * son A y B; Select no se usa */
static inline u16 cli_rp_to_gen(unsigned rp)
{
    u16 p = 0;
    if (rp & RP_A) p |= GEN_A;
    if (rp & RP_B) p |= GEN_B;
    if (rp & RP_START) p |= GEN_START;
    if (rp & RP_UP) p |= GEN_UP;
    if (rp & RP_DOWN) p |= GEN_DOWN;
    if (rp & RP_LEFT) p |= GEN_LEFT;
    if (rp & RP_RIGHT) p |= GEN_RIGHT;
    return p;
}

/* Botones del jugador `player` (0 o 1) en el cuadro `frame`; los cuadros deben pedirse en orden
 * (en cada corrida nueva hay que llamar a cli_rewind) */
static inline u16 cli_pad(Cli *c, long frame, int player)
{
    u16 pad = 0;
    if (c->input) pad = inputs_pad(&c->script, frame, player);
    if (player == 0) {
        for (int i = 0; i < c->nh; i++) if (frame >= c->h[i].from && frame <= c->h[i].to) pad |= (u16)c->h[i].btn;
        if (c->use_seed) pad |= cli_rp_to_gen(random_pad(&c->rp, frame));
    }
    return pad;
}

static inline void cli_rewind(Cli *c) { c->script.pos = 0; random_pad_init(&c->rp, c->seed); }

#endif
