#include "i18n.h"

#include <string.h>

static Language current = LANG_ES;

/* [frase][idioma]: español, inglés */
static const char *const texts[T_COUNT][LANG_COUNT] = {
    [T_MENU_GAME]     = { "Juego", "Game" },
    [T_MENU_VIDEO]    = { "Video", "Video" },
    [T_MENU_PALETTE]  = { "Paleta", "Palette" },
    [T_MENU_AUDIO]    = { "Audio", "Audio" },
    [T_MENU_LANGUAGE] = { "Idioma", "Language" },
    [T_MENU_HELP]     = { "Ayuda", "Help" },

    [T_RESET]          = { "Reiniciar", "Reset" },
    [T_PAUSE]          = { "Pausa", "Pause" },
    [T_SAVE_STATE]     = { "Guardar estado", "Save state" },
    [T_LOAD_STATE]     = { "Cargar estado", "Load state" },
    [T_SLOT_FMT]       = { "Ranura %d", "Slot %d" },
    [T_QUIT]           = { "Salir", "Quit" },
    [T_CONSOLE_DMG]    = { "Consola: Game Boy", "Console: Game Boy" },
    [T_CONSOLE_CGB]    = { "Consola: Game Boy Color", "Console: Game Boy Color" },
    [T_CONSOLE_SGB]    = { "Consola: Super Game Boy", "Console: Super Game Boy" },
    [T_PALETTE_DMG]    = { "Verde (DMG)", "Green (DMG)" },
    [T_PALETTE_POCKET] = { "Game Boy Pocket", "Game Boy Pocket" },
    [T_PALETTE_GRAY]   = { "Grises", "Grayscale" },
    [T_PALETTE_GAME]   = { "Colores del juego", "Game colors" },
    [T_DIR_UP]         = { "Arriba", "Up" },
    [T_DIR_DOWN]       = { "Abajo", "Down" },
    [T_DIR_LEFT]       = { "Izquierda", "Left" },
    [T_DIR_RIGHT]      = { "Derecha", "Right" },
    [T_SGB_BORDER]     = { "Borde del Super Game Boy", "Super Game Boy border" },
    [T_SIZE_FMT]       = { "Tamaño ×%d", "Size ×%d" },
    [T_FULLSCREEN]     = { "Pantalla completa", "Full screen" },
    [T_SOUND]          = { "Sonido", "Sound" },
    [T_VOLUME_FMT]     = { "Volumen %d%%", "Volume %d%%" },
    [T_HELP_CONTROLS]  = { "Controles…", "Controls…" },
    [T_HELP_GAME]      = { "Datos del juego…", "Game info…" },
    [T_ABOUT]          = { "Acerca de…", "About…" },

    [T_OK]     = { "Aceptar", "OK" },
    [T_CANCEL] = { "Cancelar", "Cancel" },

    [T_CONTROLS_TEXT] = {
        "Teclado:\n"
        "  Cruceta: flechas     A: X     B: Z\n"
        "  Start: Enter         Select: Shift derecho\n"
        "Control: cruceta, A/B en la posición de\n"
        "  Nintendo, Start y Back/Select.\n"
        "\n"
        "Alt o F10: menús       P: pausa\n"
        "Ctrl+R: reiniciar      F11: pantalla completa\n"
        "F5: guardar estado     F8: cargar estado\n"
        "Ctrl+M: sonido         F2: cambiar paleta",

        "Keyboard:\n"
        "  D-pad: arrows        A: X     B: Z\n"
        "  Start: Enter         Select: right Shift\n"
        "Controller: D-pad, A/B in the Nintendo\n"
        "  layout, Start and Back/Select.\n"
        "\n"
        "Alt or F10: menus      P: pause\n"
        "Ctrl+R: reset          F11: full screen\n"
        "F5: save state         F8: load state\n"
        "Ctrl+M: sound          F2: change palette",
    },
    [T_GAME_TITLE] = { "Datos del juego", "Game info" },
    [T_GAME_TEXT_FMT] = {
        "Título: %s\n"
        "SHA1 de la ROM:\n  %s\n"
        "Consola: %s\n"
        "Recompilado: %.1f%% de lo ejecutado\n"
        "\n"
        "Carpeta de datos (copia de la ROM, partida\n"
        "y estados guardados):\n"
        "  %s",

        "Title: %s\n"
        "ROM SHA1:\n  %s\n"
        "Console: %s\n"
        "Recompiled: %.1f%% of what ran\n"
        "\n"
        "Data folder (ROM copy, save file and\n"
        "save states):\n"
        "  %s",
    },
    [T_ABOUT_TEXT_FMT] = {
        "%s, recompilado con GBRecomp\n"
        "\n"
        "El código del juego se tradujo a C y corre\n"
        "nativo; lo que no se pudo traducir lo ejecuta\n"
        "un intérprete exacto al ciclo.\n"
        "\n"
        "Este programa no incluye datos del juego: usa\n"
        "tu propia ROM, validada por su SHA1.",

        "%s, recompiled with GBRecomp\n"
        "\n"
        "The game code was translated to C and runs\n"
        "natively; whatever could not be translated runs\n"
        "on a cycle-accurate interpreter.\n"
        "\n"
        "This program contains no game data: it uses\n"
        "your own ROM, validated by its SHA1.",
    },
    [T_STATE_TITLE]     = { "Estados guardados", "Save states" },
    [T_STATE_EMPTY_FMT] = { "La ranura %d está vacía.", "Slot %d is empty." },
    [T_STATE_BAD_FMT]   = {
        "El estado de la ranura %d no es de este\n"
        "juego o de esta versión del ejecutable.",
        "The state in slot %d is not from this game\n"
        "or from this version of the executable.",
    },
    [T_STATE_WRITE_FMT] = { "No se pudo escribir\n%s", "Could not write\n%s" },
    [T_CONSOLE_TITLE] = { "Cambiar de consola", "Change console" },
    [T_CONSOLE_TEXT] = {
        "Cambiar de consola reinicia el juego.\n"
        "La partida guardada (.sav) se conserva.",
        "Changing the console resets the game.\n"
        "The save file (.sav) is kept.",
    },
    [T_MODEL_DMG]    = { "Game Boy", "Game Boy" },
    [T_MODEL_CGB]    = { "Game Boy Color", "Game Boy Color" },
    [T_MODEL_COMPAT] = { "Game Boy Color (juego de Game Boy)", "Game Boy Color (Game Boy game)" },
    [T_MODEL_SGB]    = { "Super Game Boy", "Super Game Boy" },

    [T_TITLE_PAUSED]     = { "Pausa", "Paused" },
    [T_TITLE_SAVED_FMT]  = { "Estado guardado en la ranura %d", "State saved to slot %d" },
    [T_TITLE_LOADED_FMT] = { "Estado de la ranura %d cargado", "State loaded from slot %d" },

    [T_ROM_CHOOSE]       = { "Elige tu ROM", "Choose your ROM" },
    [T_ROM_CHOOSE_AGAIN] = { "Elige la ROM correcta", "Choose the right ROM" },
    [T_ROM_DROP_FMT] = {
        "Para jugar se necesita tu ROM de %s.\n"
        "Arrástrala a la ventana del juego (o ciérrala para salir).\n"
        "Solo se pide una vez: se guarda una copia en la carpeta de datos del juego.",
        "Your %s ROM is needed to play.\n"
        "Drag it onto the game window (or close it to quit).\n"
        "It is asked only once: a copy is kept in the game's data folder.",
    },
    [T_ROM_WRONG_FMT] = {
        "\"%s\" no es la ROM de %s que se recompiló.\n"
        "Se necesita la que tiene SHA1 %s.",
        "\"%s\" is not the %s ROM that was recompiled.\n"
        "The one with SHA1 %s is needed.",
    },
    [T_ROM_UNREADABLE_FMT] = { "No se pudo leer \"%s\".", "Could not read \"%s\"." },
    [T_NO_DATA_DIR]   = { "No se pudo crear la carpeta de datos del juego.",
                          "Could not create the game's data folder." },
    [T_SAVE_FAILED_FMT] = { "No se pudo guardar la partida en\n%s", "Could not save the game to\n%s" },
    [T_BAD_MODEL]     = { "Modelo desconocido: usa dmg, cgb o sgb.",
                          "Unknown model: use dmg, cgb or sgb." },
    [T_BAD_PALETTE]   = {
        "La paleta solo se puede elegir para juegos de Game Boy en un Game Boy Color:\n"
        "up, down, left, right, up+a, …, right+b.",
        "The palette can only be chosen for Game Boy games on a Game Boy Color:\n"
        "up, down, left, right, up+a, …, right+b.",
    },
    [T_RECORD_FAILED] = { "No se pudo crear el archivo de grabación.",
                          "Could not create the recording file." },
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
