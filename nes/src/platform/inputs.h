/*
 * Entradas grabadas: archivo de texto con una línea por cambio de botones.
 *
 *   # comentario
 *   0     -              (ningún botón desde el cuadro 0)
 *   300   START
 *   310   -
 *   400   RIGHT+A  B     (jugador 1, y opcionalmente el jugador 2)
 *
 * Los botones son A, B, SELECT, START, UP, DOWN, LEFT, RIGHT unidos con '+', o '-' para ninguno.
 * También se acepta un número hexadecimal con $ o 0x (BTN_*). El estado se mantiene hasta la
 * siguiente línea; si falta la segunda columna, el jugador 2 no pulsa nada.
 *
 * Lo usan la plataforma SDL (--record / --play) y las herramientas de prueba.
 */
#ifndef INPUTS_H
#define INPUTS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nes.h"

typedef struct {
    long *frame;
    u8 *pad[2];
    size_t n, cap, pos;
} InputScript;

static const char *const input_names[8] = {
    "A", "B", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",     /* orden de BTN_* */
};

static inline int inputs_parse_pad(const char *s, u8 *pad)
{
    *pad = 0;
    if (!strcmp(s, "-")) return 1;
    if (s[0] == '$' || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
        *pad = (u8)strtoul(s + (s[0] == '$' ? 1 : 2), NULL, 16);
        return 1;
    }
    char buf[128];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        int found = 0;
        for (int i = 0; i < 8; i++) {
            const char *a = tok, *b = input_names[i];
            while (*a && *b && toupper((unsigned char)*a) == *b) { a++; b++; }
            if (!*a && !*b) { *pad |= (u8)(1 << i); found = 1; }
        }
        if (!found) return 0;
    }
    return 1;
}

/* Devuelve NULL si todo bien, o un mensaje de error */
static inline const char *inputs_load(InputScript *s, const char *path)
{
    static char err[256];
    FILE *f = fopen(path, "r");
    memset(s, 0, sizeof *s);
    if (!f) {
        snprintf(err, sizeof err, "no se pudo abrir %s", path);
        return err;
    }
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        long frame;
        char p1[128], p2[128];
        p2[0] = 0;
        int got = sscanf(line, "%ld %127s %127s", &frame, p1, p2);
        if (got <= 0) continue;
        u8 a, b = 0;
        if (got < 2 || !inputs_parse_pad(p1, &a) || (got == 3 && !inputs_parse_pad(p2, &b)) ||
            (s->n && frame < s->frame[s->n - 1])) {
            snprintf(err, sizeof err, "%s:%d: línea inválida", path, lineno);
            fclose(f);
            return err;
        }
        if (s->n == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 256;
            s->frame = realloc(s->frame, s->cap * sizeof *s->frame);
            s->pad[0] = realloc(s->pad[0], s->cap);
            s->pad[1] = realloc(s->pad[1], s->cap);
        }
        s->frame[s->n] = frame;
        s->pad[0][s->n] = a;
        s->pad[1][s->n] = b;
        s->n++;
    }
    fclose(f);
    return NULL;
}

/* Botones del jugador `player` (0 o 1) en el cuadro `frame` (los cuadros deben pedirse en orden) */
static inline u8 inputs_pad(InputScript *s, long frame, int player)
{
    while (s->pos + 1 < s->n && s->frame[s->pos + 1] <= frame) s->pos++;
    if (!s->n || s->frame[s->pos] > frame) return 0;
    return s->pad[player][s->pos];
}

static inline void inputs_format(char *out, size_t size, u8 pad)
{
    out[0] = 0;
    if (!pad) { snprintf(out, size, "-"); return; }
    int first = 1;
    for (int i = 0; i < 8; i++)
        if (pad & (1 << i)) {
            size_t k = strlen(out);
            snprintf(out + k, size - k, "%s%s", first ? "" : "+", input_names[i]);
            first = 0;
        }
}

/* Grabación: escribe una línea solo cuando cambian los botones de alguno de los dos jugadores */
static inline void inputs_record(FILE *f, long frame, const u8 pads[2], int last[2])
{
    if (last[0] == pads[0] && last[1] == pads[1]) return;
    last[0] = pads[0]; last[1] = pads[1];
    char a[64], b[64];
    inputs_format(a, sizeof a, pads[0]);
    inputs_format(b, sizeof b, pads[1]);
    fprintf(f, "%ld %s %s\n", frame, a, b);
    fflush(f);
}

#endif
