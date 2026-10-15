/*
 * Preferencias que se recuerdan entre corridas: settings.ini en la carpeta de datos del juego, una línea
 * clave=valor por campo. Común a los recompiladores (retrorecomp/shared); cada consola pone en su
 * settings.h la estructura Settings y la lista de sus campos:
 *
 *   #define SETTINGS_FIELDS(I, B, S) \
 *       I(scale, 2, 1, 6)            entero: clave y campo, valor por omisión, mínimo y máximo \
 *       B(sound, 1)                  0/1 (cualquier número distinto de 0 es 1) \
 *       S(language, "", 2)           texto: clave y campo, valor por omisión, largo máximo
 *
 * El archivo se escribe en el orden de la lista. Si hace falta más que un rango (el mando del Genesis
 * solo vale 0, 3 o 6), settings.h define SETTINGS_FIX(s), que corre después de leer.
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

#ifndef SETTINGS_FIX
#define SETTINGS_FIX(s) ((void)(s))
#endif

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
#define I(k, def, lo, hi) s->k = (def);
#define B(k, def) s->k = (def);
#define S(k, def, len) snprintf(s->k, sizeof s->k, "%s", def);
    SETTINGS_FIELDS(I, B, S)
#undef I
#undef B
#undef S
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void settings_load(Settings *s, const char *path)
{
    settings_defaults(s);
    FILE *f = u8_fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        const char *key = line, *value = eq + 1;
        if (0) {}
#define I(k, def, lo, hi) else if (!strcmp(key, #k)) s->k = clamp(atoi(value), lo, hi);
#define B(k, def) else if (!strcmp(key, #k)) s->k = atoi(value) != 0;
#define S(k, def, len) else if (!strcmp(key, #k)) snprintf(s->k, sizeof s->k, "%.*s", len, value);
        SETTINGS_FIELDS(I, B, S)
#undef I
#undef B
#undef S
    }
    fclose(f);
    SETTINGS_FIX(s);
}

void settings_save(const Settings *s, const char *path)
{
    FILE *f = u8_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Preferencias del juego; se reescribe al salir\n");
#define I(k, def, lo, hi) fprintf(f, #k "=%d\n", s->k);
#define B(k, def) fprintf(f, #k "=%d\n", s->k);
#define S(k, def, len) fprintf(f, #k "=%s\n", s->k);
    SETTINGS_FIELDS(I, B, S)
#undef I
#undef B
#undef S
    fclose(f);
}
