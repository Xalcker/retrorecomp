#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->model, sizeof s->model, "auto");
    s->dmg_palette = 0;
    snprintf(s->compat_palette, sizeof s->compat_palette, "auto");
    s->sgb_border = 1;
    s->scale = 4;
    s->sound = 1;
    s->volume = 100;
    s->slot = 1;
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
        if (!strcmp(key, "model"))               snprintf(s->model, sizeof s->model, "%.7s", value);
        else if (!strcmp(key, "dmg_palette"))    s->dmg_palette = clamp(atoi(value), 0, 2);
        else if (!strcmp(key, "compat_palette")) snprintf(s->compat_palette, sizeof s->compat_palette, "%.11s", value);
        else if (!strcmp(key, "sgb_border"))     s->sgb_border = atoi(value) != 0;
        else if (!strcmp(key, "scale"))          s->scale = clamp(atoi(value), 1, 6);
        else if (!strcmp(key, "sound"))          s->sound = atoi(value) != 0;
        else if (!strcmp(key, "volume"))         s->volume = clamp(atoi(value), 25, 100);
        else if (!strcmp(key, "slot"))           s->slot = clamp(atoi(value), 1, 4);
        else if (!strcmp(key, "link_audio"))     s->link_audio = clamp(atoi(value), 0, 3);
        else if (!strcmp(key, "language"))       snprintf(s->language, sizeof s->language, "%.2s", value);
    }
    fclose(f);
}

void settings_save(const Settings *s, const char *path)
{
    FILE *f = u8_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Preferencias del juego recompilado; se reescribe al salir\n");
    fprintf(f, "model=%s\ndmg_palette=%d\ncompat_palette=%s\nsgb_border=%d\nscale=%d\n"
               "sound=%d\nvolume=%d\nslot=%d\nlink_audio=%d\nlanguage=%s\n",
            s->model, s->dmg_palette, s->compat_palette, s->sgb_border, s->scale,
            s->sound, s->volume, s->slot, s->link_audio, s->language);
    fclose(f);
}
