#include "i18n.h"

#include <string.h>

static Language current = LANG_ES;

/* [frase][idioma]: español, inglés */
static const char *const texts[T_COUNT][LANG_COUNT] = {
    [T_MENU_GAME]     = { "Juego", "Game" },
    [T_MENU_VIDEO]    = { "Video", "Video" },
    [T_MENU_AUDIO]    = { "Audio", "Audio" },
    [T_SOUND]         = { "Sonido", "Sound" },
    [T_VOLUME_FMT]    = { "Volumen %d%%", "Volume %d%%" },
    [T_CHAN_FM_FMT]   = { "FM %d", "FM %d" },
    [T_CHAN_SSG]      = { "SSG", "SSG" },
    [T_CHAN_ADPCMA]   = { "ADPCM-A", "ADPCM-A" },
    [T_CHAN_ADPCMB]   = { "ADPCM-B", "ADPCM-B" },
    [T_MENU_EXPORT]   = { "Exportar", "Export" },
    [T_MENU_LANGUAGE] = { "Idioma", "Language" },
    [T_MENU_HELP]     = { "Ayuda", "Help" },

    [T_OPEN_ROM]       = { "Abrir juego…", "Open game…" },
    [T_RESET]          = { "Reiniciar", "Reset" },
    [T_PAUSE]          = { "Pausa", "Pause" },
    [T_CARD]           = { "Tarjeta de memoria", "Memory card" },
    [T_QUIT]           = { "Salir", "Quit" },

    [T_SIZE_FMT]       = { "Tamaño %dx", "Size %dx" },
    [T_FULLSCREEN]     = { "Pantalla completa", "Fullscreen" },
    [T_ASPECT]         = { "Corregir aspecto (4:3)", "Correct aspect ratio (4:3)" },
    [T_INTEGER]        = { "Escala entera", "Integer scaling" },

    [T_SCREENSHOT]     = { "Captura de pantalla", "Screenshot" },
    [T_OPEN_EXPORTS]   = { "Abrir la carpeta", "Open the folder" },

    [T_HELP_CONTROLS]  = { "Controles…", "Controls…" },
    [T_HELP_GAME]      = { "Este juego…", "This game…" },
    [T_ABOUT]          = { "Acerca de…", "About…" },

    [T_OK]             = { "Aceptar", "OK" },
    [T_CANCEL]         = { "Cancelar", "Cancel" },

    [T_CONTROLS_TEXT] = {
        "Jugador 1:  flechas, Z X C V = A B C D,\n"
        "            Enter = Start, Mayús = Select\n"
        "Jugador 2:  I J K L, B N M , = A B C D,\n"
        "            H = Start, G = Select\n"
        "Mandos:     cruz o palanca, A B X Y = A B C D,\n"
        "            Start y Back = Select (el 1.º es el jugador 1)\n"
        "\n"
        "P pausa   Tab (mantener) avance rápido\n"
        "Ctrl+R reiniciar   Ctrl+M sonido   F11 pantalla completa\n"
        "F12 captura   Esc cierra el menú",
        "Player 1:   arrows, Z X C V = A B C D,\n"
        "            Enter = Start, Shift = Select\n"
        "Player 2:   I J K L, B N M , = A B C D,\n"
        "            H = Start, G = Select\n"
        "Gamepads:   d-pad or stick, A B X Y = A B C D,\n"
        "            Start and Back = Select (the 1st is player 1)\n"
        "\n"
        "P pause   Tab (hold) fast forward\n"
        "Ctrl+R reset   Ctrl+M sound   F11 fullscreen\n"
        "F12 screenshot   Esc closes the menu",
    },
    [T_GAME_TITLE]    = { "Este juego", "This game" },
    [T_GAME_TEXT_FMT] = {
        "%s\n\n"
        "NGH $%04X, %d KB\n"
        "BIOS: %s\n"
        "Tarjeta de memoria: %s\n"
        "SHA1 %s\n\n"
        "Datos: %s",
        "%s\n\n"
        "NGH $%04X, %d KB\n"
        "BIOS: %s\n"
        "Memory card: %s\n"
        "SHA1 %s\n\n"
        "Data: %s",
    },
    [T_ABOUT_TEXT_FMT] = {
        "%s, con NEORecomp.\n\n"
        "El ejecutable no incluye el juego ni la BIOS.\n\n"
        "Interfaz basada en Moskva 89, GBRecomp, NESRecomp y GENRecomp (MIT).\n"
        "Icono: KyleBing/retro-game-console-icons (CC BY 4.0).",
        "%s, with NEORecomp.\n\n"
        "The executable includes neither the game nor the BIOS.\n\n"
        "Interface based on Moskva 89, GBRecomp, NESRecomp and GENRecomp (MIT).\n"
        "Icon: KyleBing/retro-game-console-icons (CC BY 4.0).",
    },

    [T_EXPORT_TITLE]      = { "Exportar", "Export" },
    [T_EXPORT_FAILED_FMT] = { "No se pudo escribir\n%s", "Could not write\n%s" },

    [T_TITLE_PAUSED]    = { "Pausa", "Paused" },
    [T_TITLE_SHOT_FMT]  = { "Captura: %s", "Screenshot: %s" },
    [T_TITLE_FAST]      = { "avance rápido", "fast forward" },
    [T_TITLE_CARD_IN]   = { "tarjeta de memoria puesta", "memory card inserted" },
    [T_TITLE_CARD_OUT]  = { "tarjeta de memoria sacada", "memory card removed" },

    [T_ROM_CHOOSE]       = { "Elige tu juego", "Choose your game" },
    [T_ROM_CHOOSE_AGAIN] = { "Elige otro juego", "Choose another game" },
    [T_ROM_DROP_FMT] = {
        "Para jugar se necesita tu juego de %s (.neo, .zip de MAME o una carpeta).\n"
        "Arrástralo a la ventana (o ciérrala para salir).",
        "Your %s game is needed to play (.neo, MAME .zip or a folder).\n"
        "Drag it onto the window (or close it to quit).",
    },
    [T_ROM_UNREADABLE_FMT] = { "No se pudo leer \"%s\".", "Could not read \"%s\"." },
    [T_ROM_BAD_FMT]  = { "\"%s\": %s", "\"%s\": %s" },
    [T_BIOS_CHOOSE] = { "Elige la BIOS del AES", "Choose the AES BIOS" },
    [T_BIOS_DROP] = {
        "Se necesita la BIOS del Neo Geo AES: neo-epo.bin, neo-po.bin o el aes.zip de MAME.\n"
        "Arrástrala a la ventana (o ciérrala para salir). Se guarda una copia para las siguientes veces.",
        "The Neo Geo AES BIOS is needed: neo-epo.bin, neo-po.bin or MAME's aes.zip.\n"
        "Drag it onto the window (or close it to quit). A copy is kept for next time.",
    },
    [T_BIOS_BAD_FMT] = { "%s", "%s" },
    [T_NO_DATA_DIR]   = { "No se pudo crear la carpeta de datos del juego.",
                          "Could not create the game's data folder." },
    [T_CARD_SAVE_FAILED_FMT] = { "No se pudo guardar la tarjeta de memoria en\n%s", "Could not save the memory card to\n%s" },
};

static const char *const names[LANG_COUNT] = { "Español", "English" };
static const char *const codes[LANG_COUNT] = { "es", "en" };

void i18n_set(Language lang)
{
    if (lang < LANG_COUNT) current = lang;
}

Language i18n_get(void)
{
    return current;
}

const char *tr(TextId id)
{
    const char *s = id < T_COUNT ? texts[id][current] : NULL;
    return s ? s : "";
}

const char *i18n_name(Language lang)
{
    return lang < LANG_COUNT ? names[lang] : "";
}

const char *i18n_code(Language lang)
{
    return lang < LANG_COUNT ? codes[lang] : "";
}

Language i18n_from_code(const char *code)
{
    for (int i = 0; i < LANG_COUNT; i++)
        if (code && !strncmp(code, codes[i], 2)) return (Language)i;
    return LANG_COUNT;
}
