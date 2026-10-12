/* Preferencias que se recuerdan entre corridas (settings.ini en la carpeta de
 * datos del juego, clave=valor) */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int scale;              /* tamaño de la ventana: 1-6 */
    int sound;              /* 0/1 */
    int volume;             /* 25, 50, 75 o 100 */
    int slot;               /* ranura de estados: 1-4 */
    int aspect;             /* 0/1: imagen en 4:3 (los pixeles del Neo Geo no son cuadrados) */
    int integer;            /* 0/1: escala entera */
    int card;               /* 0/1: tarjeta de memoria puesta */
    int mute;               /* bits 0-3: FM 1-4, bit 4: SSG, bit 5: ADPCM-A, bit 6: ADPCM-B silenciados */
    char language[8];       /* "es", "en"; vacío = el del sistema */
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);

#endif
