/*
 * Entradas grabadas: archivo de texto con una línea por cambio de botones.
 *
 *   # comentario
 *   0     -              (ningún botón desde el cuadro 0)
 *   300   START
 *   310   -
 *   400   RIGHT+A
 *
 * Los botones son A, B, SELECT, START, RIGHT, LEFT, UP, DOWN unidos con '+',
 * o '-' para ninguno. También se acepta un número hexadecimal con $ o 0x
 * (formato PAD_*).
 * El estado se mantiene hasta la siguiente línea.
 *
 * Líneas opcionales al principio: "modelo dmg|cgb|sgb" (con qué modelo se
 * grabó; un mismo juego tarda distinto en cada uno) y "paleta up+a" (la
 * combinación del modo de compatibilidad del GBC).
 *
 * Lo usan la plataforma SDL (--record / --play) y difftest (--input).
 */
#ifndef INPUTS_H
#define INPUTS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"

typedef struct {
    long *frame;
    u8 *pad;
    size_t n, cap, pos;
    int model;          /* GB_MODEL_AUTO si el archivo no lo dice */
    char palette[16];   /* "" si no lo dice */
} InputScript;

/* "dmg", "cgb", "sgb" → GB_MODEL_*; cualquier otra cosa → -2 */
static inline int model_from_name(const char *s)
{
    if (!strcmp(s, "dmg")) return GB_MODEL_DMG;
    if (!strcmp(s, "cgb")) return GB_MODEL_CGB;
    if (!strcmp(s, "sgb")) return GB_MODEL_SGB;
    if (!strcmp(s, "auto")) return GB_MODEL_AUTO;
    return -2;
}

static inline const char *model_name(const GB *g)
{
    return g->cgb_hw ? "cgb" : g->sgb ? "sgb" : "dmg";
}

static const char *const input_names[8] = {
    "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN",
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
    s->model = GB_MODEL_AUTO;
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
        char pads[128], word[16];
        if (sscanf(line, " modelo %15s", word) == 1) {
            s->model = model_from_name(word);
            if (s->model < GB_MODEL_AUTO) {
                snprintf(err, sizeof err, "%s:%d: modelo desconocido", path, lineno);
                fclose(f);
                return err;
            }
            continue;
        }
        if (sscanf(line, " paleta %15s", word) == 1) {
            snprintf(s->palette, sizeof s->palette, "%s", word);
            continue;
        }
        int got = sscanf(line, "%ld %127s", &frame, pads);
        if (got <= 0) continue;
        u8 pad;
        if (got != 2 || !inputs_parse_pad(pads, &pad) || (s->n && frame < s->frame[s->n - 1])) {
            snprintf(err, sizeof err, "%s:%d: línea inválida", path, lineno);
            fclose(f);
            return err;
        }
        if (s->n == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 256;
            s->frame = realloc(s->frame, s->cap * sizeof *s->frame);
            s->pad = realloc(s->pad, s->cap);
        }
        s->frame[s->n] = frame;
        s->pad[s->n] = pad;
        s->n++;
    }
    fclose(f);
    return NULL;
}

/* Botones en el cuadro `frame` (los cuadros deben pedirse en orden) */
static inline u8 inputs_pad(InputScript *s, long frame)
{
    while (s->pos + 1 < s->n && s->frame[s->pos + 1] <= frame) s->pos++;
    if (!s->n || s->frame[s->pos] > frame) return 0;
    return s->pad[s->pos];
}

/* Último cuadro con un cambio de botones */
static inline long inputs_last_frame(const InputScript *s)
{
    return s->n ? s->frame[s->n - 1] : 0;
}

/* Grabación: encabezado con el modelo (y la paleta, si se eligió) */
static inline void inputs_record_header(FILE *f, const GB *g, const char *palette)
{
    fprintf(f, "modelo %s\n", model_name(g));
    if (palette && *palette) fprintf(f, "paleta %s\n", palette);
}

/* Grabación: escribe una línea solo cuando cambian los botones */
static inline void inputs_record(FILE *f, long frame, u8 pad, int *last)
{
    if (*last == pad) return;
    *last = pad;
    fprintf(f, "%ld ", frame);
    if (!pad) {
        fputs("-", f);
    } else {
        int first = 1;
        for (int i = 0; i < 8; i++)
            if (pad & (1 << i)) {
                fprintf(f, "%s%s", first ? "" : "+", input_names[i]);
                first = 0;
            }
    }
    fputc('\n', f);
    fflush(f);
}

#endif
