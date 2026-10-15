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
    [T_CHAN_TONE_FMT] = { "Tono %d (SN76489)", "Tone %d (SN76489)" },
    [T_CHAN_NOISE]    = { "Ruido (SN76489)", "Noise (SN76489)" },
    [T_MENU_INPUT]    = { "Entrada", "Input" },
    [T_MENU_EXPORT]   = { "Exportar", "Export" },
    [T_MENU_LANGUAGE] = { "Idioma", "Language" },
    [T_MENU_HELP]     = { "Ayuda", "Help" },

    [T_OPEN_ROM]       = { "Abrir ROM…", "Open ROM…" },
    [T_RESET]          = { "Reiniciar", "Reset" },
    [T_PAUSE]          = { "Pausa", "Pause" },
    [T_SAVE_STATE]     = { "Guardar estado", "Save state" },
    [T_LOAD_STATE]     = { "Cargar estado", "Load state" },
    [T_SLOT_FMT]       = { "Ranura %d", "Slot %d" },
    [T_QUIT]           = { "Salir", "Quit" },

    [T_SIZE_FMT]       = { "Tamaño %dx", "Size %dx" },
    [T_FULLSCREEN]     = { "Pantalla completa", "Fullscreen" },
    [T_HIDE_MENU]      = { "Ocultar la barra de menús", "Hide the menu bar" },
    [T_ASPECT]         = { "Corregir aspecto (pixeles de 8:7)", "Correct aspect ratio (8:7 pixels)" },
    [T_INTEGER]        = { "Escala entera", "Integer scaling" },

    [T_MODEL_AUTO]     = { "Consola: la que pide el juego", "Console: the one the game asks for" },
    [T_MODEL_SMS]      = { "Master System", "Master System" },
    [T_MODEL_GG]       = { "Game Gear", "Game Gear" },

    [T_SCREENSHOT]     = { "Captura de pantalla", "Screenshot" },
    [T_OPEN_EXPORTS]   = { "Abrir la carpeta", "Open the folder" },

    [T_HELP_CONTROLS]  = { "Controles…", "Controls…" },
    [T_HELP_GAME]      = { "Este juego…", "This game…" },
    [T_ABOUT]          = { "Acerca de…", "About…" },

    [T_OK]             = { "Aceptar", "OK" },
    [T_CANCEL]         = { "Cancelar", "Cancel" },

    [T_CONTROLS_TEXT] = {
        "Jugador 1:  flechas, Z X = botones 1 y 2,\n"
        "            Enter = Start (Game Gear), Espacio = Pausa\n"
        "Jugador 2:  I J K L, V B = botones 1 y 2\n"
        "Mandos:     cruz o palanca, A (o X) = 1, B = 2,\n"
        "            Start = Start, Back = Pausa\n"
        "            (el 1.º es el jugador 1)\n"
        "\n"
        "P pausa   Tab (mantener) avance rápido\n"
        "F5 guardar estado   F8 cargar estado\n"
        "Ctrl+R reiniciar   Ctrl+M sonido   F11 pantalla completa\n"
        "F12 captura   F9 ocultar menús   Esc cierra el menú",
        "Player 1:   arrows, Z X = buttons 1 and 2,\n"
        "            Enter = Start (Game Gear), Space = Pause\n"
        "Player 2:   I J K L, V B = buttons 1 and 2\n"
        "Gamepads:   d-pad or stick, A (or X) = 1, B = 2,\n"
        "            Start = Start, Back = Pause\n"
        "            (the 1st is player 1)\n"
        "\n"
        "P pause   Tab (hold) fast forward\n"
        "F5 save state   F8 load state\n"
        "Ctrl+R reset   Ctrl+M sound   F11 fullscreen\n"
        "F12 screenshot   F9 hide menus   Esc closes the menu",
    },
    [T_GAME_TITLE]    = { "Este juego", "This game" },
    [T_GAME_TEXT_FMT] = {
        "%s\n\n"
        "%s, %d KB, mapper %s\n"
        "Código %s, región %s\n"
        "Guardado: %s\n"
        "SHA1 %s\n\n"
        "Datos: %s",
        "%s\n\n"
        "%s, %d KB, mapper %s\n"
        "Code %s, region %s\n"
        "Saves: %s\n"
        "SHA1 %s\n\n"
        "Data: %s",
    },
    [T_SAVES_RAM]  = { "RAM del cartucho (%d KB)", "cartridge RAM (%d KB)" },
    [T_SAVES_NONE] = { "no usa la RAM del cartucho (todavía)", "does not use cartridge RAM (yet)" },
    [T_ABOUT_TEXT_FMT] = {
        "%s, con SMSRecomp.\n\n"
        "El ejecutable no incluye la ROM.\n\n"
        "Interfaz basada en Moskva 89, GBRecomp, NESRecomp y GENRecomp (MIT).\n"
        "Icono: KyleBing/retro-game-console-icons (CC BY 4.0).",
        "%s, with SMSRecomp.\n\n"
        "The executable does not include the ROM.\n\n"
        "Interface based on Moskva 89, GBRecomp, NESRecomp and GENRecomp (MIT).\n"
        "Icon: KyleBing/retro-game-console-icons (CC BY 4.0).",
    },

    [T_EXPORT_TITLE]      = { "Exportar", "Export" },
    [T_EXPORT_FAILED_FMT] = { "No se pudo escribir\n%s", "Could not write\n%s" },
    [T_STATE_TITLE]     = { "Estados guardados", "Save states" },
    [T_STATE_EMPTY_FMT] = { "La ranura %d está vacía.", "Slot %d is empty." },
    [T_STATE_BAD_FMT]   = {
        "El estado de la ranura %d no sirve para esta ROM o esta versión.",
        "The state in slot %d does not match this ROM or version.",
    },
    [T_STATE_WRITE_FMT] = { "No se pudo guardar el estado en\n%s", "Could not save the state to\n%s" },

    [T_TITLE_PAUSED]    = { "Pausa", "Paused" },
    [T_TITLE_SHOT_FMT]  = { "Captura: %s", "Screenshot: %s" },
    [T_TITLE_FAST]      = { "avance rápido", "fast forward" },
    [T_TITLE_SAVED_FMT] = { "Estado %d guardado", "State %d saved" },
    [T_TITLE_LOADED_FMT] = { "Estado %d cargado", "State %d loaded" },
    [T_TITLE_PLAYING]   = { "reproduciendo entradas", "playing back inputs" },

    [T_ROM_CHOOSE]       = { "Elige tu ROM", "Choose your ROM" },
    [T_ROM_CHOOSE_AGAIN] = { "Elige otra ROM", "Choose another ROM" },
    [T_ROM_DROP_FMT] = {
        "Para jugar se necesita tu ROM de %s.\n"
        "Arrástrala a la ventana (o ciérrala para salir).",
        "Your %s ROM is needed to play.\n"
        "Drag it onto the window (or close it to quit).",
    },
    [T_ROM_WRONG_FMT] = {
        "\"%s\" no es la ROM de %s que se recompiló.\n"
        "Se necesita la que tiene SHA1 %s.",
        "\"%s\" is not the %s ROM that was recompiled.\n"
        "The one with SHA1 %s is needed.",
    },
    [T_ROM_UNREADABLE_FMT] = { "No se pudo leer \"%s\".", "Could not read \"%s\"." },
    [T_ROM_BAD_FMT]  = { "\"%s\": %s", "\"%s\": %s" },
    [T_NO_DATA_DIR]   = { "No se pudo crear la carpeta de datos del juego.",
                          "Could not create the game's data folder." },
    [T_SAVE_FAILED_FMT] = { "No se pudo guardar la partida en\n%s", "Could not save the game to\n%s" },
    [T_RECORD_FAILED] = { "No se pudo crear el archivo de grabación.", "Could not create the recording file." },
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
