/* Preferencias que se recuerdan entre corridas (settings.ini en la carpeta de
 * datos del juego, clave=valor) */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int scale;              /* tamaño de la ventana: 1-6 */
    int sound;              /* 0/1 */
    int volume;             /* 25, 50, 75 o 100 */
    int slot;               /* ranura de estados: 1-4 */
    int aspect;             /* 0/1: imagen en 4:3 (los pixeles del Genesis no son cuadrados) */
    int integer;            /* 0/1: escala entera */
    int pad;                /* mando: 0 el que dice la cabecera, 3 de 3 botones, 6 de 6 botones */
    int hide_menu;          /* 0/1: barra de menús oculta (Video → Ocultar la barra de menús, F9) */
    int mute;               /* bits 0-5: canales FM 1-6 silenciados, bit 6: PSG */
    char language[8];       /* "es", "en"; vacío = el del sistema */
} Settings;

/* Los campos de settings.ini, en el orden en que se escriben (settings.c, común: tools/retrorecomp/shared) */
#define SETTINGS_FIELDS(I, B, S) \
    I(scale, 2, 1, 6) B(sound, 1) I(volume, 100, 25, 100) I(slot, 1, 1, 4) B(aspect, 1) \
    B(integer, 1) I(pad, 0, -1, 7) B(hide_menu, 0) I(mute, 0, 0, 127) S(language, "", 2)
/* El mando solo es 0, 3 o 6 (cualquier otro valor, entre -1 y 7 al leerlo, queda en 0) */
#define SETTINGS_FIX(s) ((s)->pad = (s)->pad == 3 || (s)->pad == 6 ? (s)->pad : 0)

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
