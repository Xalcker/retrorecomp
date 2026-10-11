/*
 * Entradas grabadas: archivo de texto con una línea por cambio de botones (el mismo formato que en
 * NESRecomp y GBRecomp; aquí con los botones del Genesis).
 *
 *   # comentario
 *   0     -              (ningún botón desde el cuadro 0)
 *   300   START
 *   310   -
 *   400   RIGHT+A  B     (jugador 1, y opcionalmente el jugador 2)
 *
 * Los botones son UP, DOWN, LEFT, RIGHT, A, B, C, X, Y, Z, START y MODE unidos con '+', o '-' para
 * ninguno. También se acepta un número hexadecimal con $ o 0x (los bits GEN_* de gen.h). El estado se
 * mantiene hasta la siguiente línea; si falta la segunda columna, el jugador 2 no pulsa nada.
 *
 * Lo usan la ventana (--record / --play) y las herramientas sin ventana (--input).
 */
#ifndef INPUTS_H
#define INPUTS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"

typedef struct {
    long *frame;
    u16 *pad[2];
    size_t n, cap, pos;
} InputScript;

/* En el orden de los bits GEN_* */
static const char *const input_names[12] = {
    "UP", "DOWN", "LEFT", "RIGHT", "B", "C", "A", "START", "Z", "Y", "X", "MODE",
};

static inline int inputs_parse_pad(const char *s, u16 *pad)
{
    *pad = 0;
    if (!strcmp(s, "-")) return 1;
    if (s[0] == '$' || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
        *pad = (u16)(strtoul(s + (s[0] == '$' ? 1 : 2), NULL, 16) & 0xFFF);
        return 1;
    }
    char buf[128];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        int found = 0;
        for (int i = 0; i < 12; i++) {
            const char *a = tok, *b = input_names[i];
            while (*a && *b && toupper((unsigned char)*a) == *b) { a++; b++; }
            if (!*a && !*b) { *pad |= (u16)(1 << i); found = 1; }
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
        u16 a, b = 0;
        if (got < 2 || !inputs_parse_pad(p1, &a) || (got == 3 && !inputs_parse_pad(p2, &b)) ||
            (s->n && frame < s->frame[s->n - 1])) {
            snprintf(err, sizeof err, "%s:%d: línea inválida", path, lineno);
            fclose(f);
            return err;
        }
        if (s->n == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 256;
            s->frame = realloc(s->frame, s->cap * sizeof *s->frame);
            s->pad[0] = realloc(s->pad[0], s->cap * sizeof *s->pad[0]);
            s->pad[1] = realloc(s->pad[1], s->cap * sizeof *s->pad[1]);
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
static inline u16 inputs_pad(InputScript *s, long frame, int player)
{
    while (s->pos + 1 < s->n && s->frame[s->pos + 1] <= frame) s->pos++;
    if (!s->n || s->frame[s->pos] > frame) return 0;
    return s->pad[player][s->pos];
}

static inline void inputs_format(char *out, size_t size, u16 pad)
{
    out[0] = 0;
    if (!pad) { snprintf(out, size, "-"); return; }
    int first = 1;
    for (int i = 0; i < 12; i++)
        if (pad & (1 << i)) {
            size_t k = strlen(out);
            snprintf(out + k, size - k, "%s%s", first ? "" : "+", input_names[i]);
            first = 0;
        }
}

/* Grabación: escribe una línea solo cuando cambian los botones de alguno de los dos jugadores */
static inline void inputs_record(FILE *f, long frame, const u16 pads[2], int last[2])
{
    if (last[0] == pads[0] && last[1] == pads[1]) return;
    last[0] = pads[0]; last[1] = pads[1];
    char a[96], b[96];
    inputs_format(a, sizeof a, pads[0]);
    inputs_format(b, sizeof b, pads[1]);
    fprintf(f, "%ld %s %s\n", frame, a, b);
    fflush(f);
}

#endif
