/*
 * Opciones comunes de las herramientas sin ventana (nestrace, gentrace, neotrace... *shot, *audio,
 * *bench, *ramdump, difftest y bench del proyecto generado), las mismas en los cinco recompiladores:
 *   --frames N       cuadros a correr
 *   --input ARCHIVO  entradas grabadas (inputs.h); las de --record de la ventana sirven
 *   --seed S         suma un mando aleatorio reproducible con esa semilla
 *   --model M        (consolas con más de un modelo: GB, SMS) el modelo; lo resuelve CLI_HOST_DONE
 *   --repeat N       corridas (*bench): se reporta la más rápida
 *   --profile         (genbench, neobench y su bench) en qué se va el tiempo: Z80, sonido, video y lo demás (prof.h)
 *   --save F, --save-out F   (difftest) empieza con esa partida guardada / guarda al final la del recompilado
 *   --uncovered F    (difftest) escribe lo que el intérprete tuvo que ejecutar
 *   --counts         (con --uncovered) cada línea lleva al final cuántas veces lo ejecutó, " xN" (solo SMSRecomp por ahora)
 *   --screens DIR    (difftest) guarda una captura PNG cada --every N cuadros (600 si no se dice)
 *   --bench          (difftest) mide la velocidad del recompilado y del intérprete, sin comparar
 *   --executed F     (difftest) escribe qué bytes del programa se ejecutaron (formato de --codemap)
 *   --estricto       (difftest) falla en la primera instrucción de la ROM que corre el intérprete
 *                    porque no está recompilada (el motivo «código que falta» de metrica.h); con --uncovered
 *                    deja también la traza del hueco
 *   --expect PNG     (*shot) pasa si la pantalla final es idéntica al PNG, como gbtest --expect
 *   --hash           (*shot) imprime un hash de todos los cuadros y de todo el audio, como gbtest --hash
 * También valen el número de cuadros como argumento suelto y los botones sostenidos
 * `boton:desde-hasta` (los nombres de INPUT_NAMES, en minúsculas o mayúsculas).
 *
 * Común (retrorecomp/shared); lo propio de cada consola viene de su input_host.h (ver inputs.h). Si la
 * consola tiene opciones de más, las agrega con CLI_HOST_FIELDS (campos de Cli), CLI_HOST_OPTION(c, a)
 * (1 si reconoció la opción a), CLI_HOST_VALUE(c, a, v) (1 si reconoció la opción a con el valor v, el
 * argumento que la sigue, como --palette en GB), CLI_HOST_EXTRA(a) (1 si el suelto a va a extra, como zapper:
 * en NES), CLI_HAS_MODEL (la consola acepta --model: queda en c->model_arg) y CLI_HOST_DONE(c) (después de leer --input; 0, o 2 si hay un error ya impreso).
 */
#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "inputs.h"
#include "random_pad.h"     /* el mismo mando aleatorio en los recompiladores */

#ifndef CLI_HOST_FIELDS
#define CLI_HOST_FIELDS
#endif
#ifndef CLI_HOST_OPTION
#define CLI_HOST_OPTION(c, a) 0
#endif
#ifndef CLI_HOST_VALUE
#define CLI_HOST_VALUE(c, a, v) 0
#endif
#ifndef CLI_HOST_EXTRA
#define CLI_HOST_EXTRA(a) 0
#endif
#ifndef CLI_HOST_DONE
#define CLI_HOST_DONE(c) 0
#endif


/* El nombre del programa para el "uso:", de argv[0] sin la carpeta ni .exe (así sirve el mismo bench en el de la
 * consola y en el del proyecto generado) */
static inline const char *cli_prog(const char *argv0)
{
    static char buf[64];
    const char *b = argv0 ? argv0 : "bench";
    for (const char *p = b; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    snprintf(buf, sizeof buf, "%s", b);
    size_t n = strlen(buf);
    if (n > 4 && !strcmp(buf + n - 4, ".exe")) buf[n - 4] = 0;
    return buf;
}
typedef struct { int btn, from, to; } CliHold;

typedef struct {
    const char *pos[64]; int npos;      /* argumentos sueltos que no son botones ni opciones */
    long frames; int frames_given;
    const char *input; InputScript script;
    const char *model_arg;              /* --model (solo las consolas con CLI_HAS_MODEL; la resuelve CLI_HOST_DONE) */
    int use_seed; unsigned seed; RandomPad rp;
    int repeat, profile;
    const char *uncovered;
    int counts;                         /* --counts: --uncovered con la cuenta de cada instrucción */
    const char *save, *save_out;        /* difftest: la partida con que empiezan las dos máquinas y dónde guardar la de B */
    const char *screens, *executed; long every; int bench, estricto;     /* solo difftest */
    const char *expect; int hash;                               /* solo *shot */
    CliHold h[64]; int nh;
    const char *extra[64]; int nextra;  /* sueltos con forma de botón que no se reconocieron */
    CLI_HOST_FIELDS
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
        else if (!strcmp(a, "--profile") || !strcmp(a, "--perfil")) c->profile = 1;
        else if (!strcmp(a, "--model") && i + 1 < argc) c->model_arg = argv[++i];
        else if (!strcmp(a, "--uncovered") && i + 1 < argc) c->uncovered = argv[++i];
        else if (!strcmp(a, "--counts")) c->counts = 1;
        else if (!strcmp(a, "--save") && i + 1 < argc) c->save = argv[++i];
        else if (!strcmp(a, "--save-out") && i + 1 < argc) c->save_out = argv[++i];
        else if (!strcmp(a, "--screens") && i + 1 < argc) c->screens = argv[++i];
        else if (!strcmp(a, "--every") && i + 1 < argc) c->every = atol(argv[++i]);
        else if (!strcmp(a, "--executed") && i + 1 < argc) c->executed = argv[++i];
        else if (!strcmp(a, "--bench")) c->bench = 1;
        else if (!strcmp(a, "--estricto")) c->estricto = 1;
        else if (!strcmp(a, "--expect") && i + 1 < argc) c->expect = argv[++i];
        else if (!strcmp(a, "--hash")) c->hash = 1;
        else if (CLI_HOST_OPTION(c, a)) {}
        else if (i + 1 < argc && CLI_HOST_VALUE(c, a, argv[i + 1])) i++;
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "opción desconocida: %s\n", a); return 2; }
        else if (sscanf(a, "%15[a-z]:%d-%d", name, &f0, &f1) == 3) {
            InputPad b = inputs_button(name);
            if (b && c->nh < 64) c->h[c->nh++] = (CliHold){ b, f0, f1 };
            else if (!b && c->nextra < 64) c->extra[c->nextra++] = a;
        }
        else if (CLI_HOST_EXTRA(a)) { if (c->nextra < 64) c->extra[c->nextra++] = a; }
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
#ifndef CLI_HAS_MODEL
    if (c->model_arg) { fprintf(stderr, "opción desconocida: --model\n"); return 2; }
#endif
    if (c->input) {
        const char *err = inputs_load(&c->script, c->input);
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
    }
    if (CLI_HOST_DONE(c)) return 2;
    random_pad_init(&c->rp, c->seed);
    return 0;
}

/* Botones del jugador `player` (0 o 1) en el cuadro `frame`; los cuadros deben pedirse en orden
 * (en cada corrida nueva hay que llamar a cli_rewind) */
static inline InputPad cli_pad(Cli *c, long frame, int player)
{
    InputPad pad = 0;
    if (c->input) pad = inputs_pad(&c->script, frame, player);
    if (player == 0) {
        for (int i = 0; i < c->nh; i++) if (frame >= c->h[i].from && frame <= c->h[i].to) pad |= (InputPad)c->h[i].btn;
        if (c->use_seed) pad |= input_from_rp(random_pad(&c->rp, frame));
    }
    return pad;
}

static inline void cli_rewind(Cli *c) { c->script.pos = 0; random_pad_init(&c->rp, c->seed); }

#endif
