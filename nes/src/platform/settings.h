/* Preferencias que se recuerdan entre corridas (settings.ini en la carpeta de
 * datos del juego, clave=valor) */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int scale;              /* tamaño de la ventana: 1-6 */
    int sound;              /* 0/1 */
    int volume;             /* 25, 50, 75 o 100 */
    int slot;               /* ranura de estados: 1-4 */
    int palette;            /* 0 predeterminada, 1 grises, 2 saturada, 3 personalizada */
    int overscan;           /* 0/1: ocultar 8 líneas arriba y abajo */
    int aspect;             /* 0/1: corregir a 8:7 */
    int integer;            /* 0/1: escala entera */
    int zapper;             /* 0/1: Zapper en el puerto 2 */
    int fourscore;          /* 0/1: Four Score (4 jugadores) */
    int hide_menu;          /* 0/1: barra de menús oculta (Video → Ocultar la barra de menús, F9) */
    int mute;               /* bits 0-4: canales silenciados (pulso 1, pulso 2, triángulo, ruido, DMC) */
    char language[8];       /* "es", "en"; vacío = el del sistema */
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
