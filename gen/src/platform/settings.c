#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->scale = 2;
    s->sound = 1;
    s->volume = 100;
    s->slot = 1;
    s->aspect = 1;
    s->integer = 1;
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
        if (!strcmp(key, "scale"))         s->scale = clamp(atoi(value), 1, 6);
        else if (!strcmp(key, "sound"))    s->sound = atoi(value) != 0;
        else if (!strcmp(key, "volume"))   s->volume = clamp(atoi(value), 25, 100);
        else if (!strcmp(key, "slot"))     s->slot = clamp(atoi(value), 1, 4);
        else if (!strcmp(key, "aspect"))   s->aspect = atoi(value) != 0;
        else if (!strcmp(key, "integer"))  s->integer = atoi(value) != 0;
        else if (!strcmp(key, "pad"))      { int p = atoi(value); s->pad = p == 3 || p == 6 ? p : 0; }
        else if (!strcmp(key, "language")) snprintf(s->language, sizeof s->language, "%.2s", value);
    }
    fclose(f);
}

void settings_save(const Settings *s, const char *path)
{
    FILE *f = u8_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Preferencias del juego; se reescribe al salir\n");
    fprintf(f, "scale=%d\nsound=%d\nvolume=%d\nslot=%d\naspect=%d\ninteger=%d\npad=%d\nlanguage=%s\n",
            s->scale, s->sound, s->volume, s->slot, s->aspect, s->integer, s->pad, s->language);
    fclose(f);
}
