/* Un settings.h de mentira para tests/comun_test.c: un campo de cada clase y un SETTINGS_FIX */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int scale, sound, pad;
    char language[8], model[8];
} Settings;

#define SETTINGS_FIELDS(I, B, S) \
    S(model, "auto", 7) I(scale, 2, 1, 6) B(sound, 1) I(pad, 0, -1, 7) S(language, "", 2)
#define SETTINGS_FIX(s) ((s)->pad = (s)->pad == 3 || (s)->pad == 6 ? (s)->pad : 0)

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
