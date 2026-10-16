/* Preferencias que se recuerdan entre corridas (settings.ini en la carpeta de
 * datos del juego, clave=valor) */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int scale;              /* tamaño de la ventana: 1-6 */
    int sound;              /* 0/1 */
    int volume;             /* 25, 50, 75 o 100 */
    int slot;               /* ranura de estados: 1-4 */
    int aspect;             /* 0/1: pixeles de 8:7 en el Master System (los de un televisor); el Game Gear no cambia */
    int integer;            /* 0/1: escala entera */
    int model;              /* consola: -1 la que pide el cartucho, 0 Master System, 1 Game Gear (MODEL_*) */
    int hide_menu;          /* 0/1: barra de menús oculta (Video → Ocultar la barra de menús, F9) */
    int mute;               /* bits 0-3: los canales del PSG silenciados (tono 1-3 y ruido) */
    char language[8];       /* "es", "en"; vacío = el del sistema */
} Settings;

/* Los campos de settings.ini, en el orden en que se escriben (settings.c, común: tools/retrorecomp/shared) */
#define SETTINGS_FIELDS(I, B, S) \
    I(scale, 3, 1, 6) B(sound, 1) I(volume, 100, 25, 100) I(slot, 1, 1, 4) B(aspect, 1) \
    B(integer, 1) I(model, -1, -1, 1) B(hide_menu, 0) I(mute, 0, 0, 15) S(language, "", 2)
/* El volumen va de 25 en 25 (el menú Audio) */
#define SETTINGS_FIX(s) ((s)->volume = (s)->volume / 25 * 25)

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
