/* Preferencias que se recuerdan entre corridas (settings.ini en la carpeta de
 * datos del juego, clave=valor) */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    char model[8];          /* "auto", "dmg", "cgb", "sgb" */
    int dmg_palette;        /* 0 verde, 1 Pocket, 2 grises */
    char compat_palette[12];    /* "auto" o una combinación del GBC ("up+a") */
    int sgb_border;         /* 0/1 */
    int scale;              /* tamaño de la ventana: 1-6 */
    int sound;              /* 0/1 */
    int volume;             /* 25, 50, 75 o 100 */
    int slot;               /* ranura de estados: 1-4 */
    int link_audio;         /* con el cable: 0 jugador 1, 1 jugador 2, 2 los dos */
    char language[8];       /* "es", "en"; vacío = el del sistema */
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
