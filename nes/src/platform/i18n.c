#include "i18n.h"

#include <string.h>

static Language current = LANG_ES;

/* [frase][idioma]: español, inglés */
static const char *const texts[T_COUNT][LANG_COUNT] = {
    [T_MENU_GAME]     = { "Juego", "Game" },
    [T_MENU_VIDEO]    = { "Video", "Video" },
    [T_MENU_PALETTE]  = { "Paleta", "Palette" },
    [T_MENU_AUDIO]    = { "Audio", "Audio" },
    [T_MENU_EXPORT]   = { "Exportar", "Export" },
    [T_MENU_LANGUAGE] = { "Idioma", "Language" },
    [T_MENU_HELP]     = { "Ayuda", "Help" },
    [T_MENU_INPUT]    = { "Entrada", "Input" },
    [T_ZAPPER]        = { "Zapper en el puerto 2 (ratón)", "Zapper in port 2 (mouse)" },
    [T_FOURSCORE]     = { "Four Score: 4 jugadores", "Four Score: 4 players" },

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
    [T_OVERSCAN]       = { "Ocultar bordes (overscan)", "Hide borders (overscan)" },
    [T_ASPECT]         = { "Corregir aspecto (8:7)", "Correct aspect ratio (8:7)" },
    [T_INTEGER]        = { "Escala entera", "Integer scaling" },

    [T_PAL_DEFAULT]    = { "Predeterminada", "Default" },
    [T_PAL_GRAY]       = { "Grises", "Grayscale" },
    [T_PAL_VIVID]      = { "Saturada", "Vivid" },
    [T_PAL_CUSTOM]     = { "Personalizada (paleta.pal)", "Custom (paleta.pal)" },

    [T_SOUND]          = { "Sonido", "Sound" },
    [T_VOLUME_FMT]     = { "Volumen %d%%", "Volume %d%%" },
    [T_CHAN_PULSE1]    = { "Pulso 1", "Pulse 1" },
    [T_CHAN_PULSE2]    = { "Pulso 2", "Pulse 2" },
    [T_CHAN_TRIANGLE]  = { "Triángulo", "Triangle" },
    [T_CHAN_NOISE]     = { "Ruido", "Noise" },
    [T_CHAN_DMC]       = { "DMC (muestras)", "DMC (samples)" },

    [T_REC_AUDIO]      = { "Grabar audio (WAV)", "Record audio (WAV)" },
    [T_SCREENSHOT]     = { "Captura de pantalla", "Screenshot" },
    [T_OPEN_EXPORTS]   = { "Abrir la carpeta", "Open the folder" },

    [T_HELP_CONTROLS]  = { "Controles…", "Controls…" },
    [T_HELP_GAME]      = { "Este juego…", "This game…" },
    [T_ABOUT]          = { "Acerca de…", "About…" },

    [T_OK]             = { "Aceptar", "OK" },
    [T_CANCEL]         = { "Cancelar", "Cancel" },

    [T_CONTROLS_TEXT] = {
        "Jugador 1:  flechas, Z = B, X = A,\n"
        "            Enter = Start, Mayús o Retroceso = Select\n"
        "Jugador 2:  WASD, F = B, G = A, C = Start, V = Select\n"
        "Controles:  cruz o palanca, A/X = B, B/Y = A,\n"
        "            Start y Back (el 1.º es el jugador 1; con el\n"
        "            Four Score, el 3.º y el 4.º son los jugadores 3 y 4)\n"
        "Zapper:     ratón (apuntar) y clic izquierdo (gatillo)\n"
        "\n"
        "P pausa   Tab (mantener) avance rápido\n"
        "F5 guardar estado   F8 cargar estado\n"
        "Ctrl+R reiniciar   Ctrl+M sonido\n"
        "F11 pantalla completa   F12 captura   Ctrl+G grabar audio\n"
        "F2 siguiente paleta   F9 ocultar menús   Esc cierra el menú",
        "Player 1:   arrows, Z = B, X = A,\n"
        "            Enter = Start, Shift or Backspace = Select\n"
        "Player 2:   WASD, F = B, G = A, C = Start, V = Select\n"
        "Gamepads:   d-pad or stick, A/X = B, B/Y = A,\n"
        "            Start and Back (the 1st is player 1; with the\n"
        "            Four Score, the 3rd and 4th are players 3 and 4)\n"
        "Zapper:     mouse (aim) and left click (trigger)\n"
        "\n"
        "P pause   Tab (hold) fast forward\n"
        "F5 save state   F8 load state\n"
        "Ctrl+R reset   Ctrl+M sound\n"
        "F11 fullscreen   F12 screenshot   Ctrl+G record audio\n"
        "F2 next palette   F9 hide menus   Esc closes the menu",
    },
    [T_GAME_TITLE]    = { "Este juego", "This game" },
    [T_GAME_TEXT_FMT] = {
        "%s\n\n"
        "Mapper %d (%s)\n"
        "PRG %d KB, CHR %d KB%s\n"
        "SHA1 %s\n"
        "Código recompilado: %.1f%% de los ciclos\n\n"
        "Datos: %s",
        "%s\n\n"
        "Mapper %d (%s)\n"
        "PRG %d KB, CHR %d KB%s\n"
        "SHA1 %s\n"
        "Recompiled code: %.1f%% of cycles\n\n"
        "Data: %s",
    },
    [T_ABOUT_TEXT_FMT] = {
        "%s, recompilado con NESRecomp.\n\n"
        "El ejecutable no incluye la ROM: se pide una sola vez\n"
        "y se comprueba su SHA1.\n\n"
        "Interfaz basada en Moskva 89 y GBRecomp (MIT).",
        "%s, recompiled with NESRecomp.\n\n"
        "The executable does not include the ROM: it is asked\n"
        "only once and its SHA1 is checked.\n\n"
        "Interface based on Moskva 89 and GBRecomp (MIT).",
    },
    [T_STATE_TITLE]     = { "Estados guardados", "Save states" },
    [T_STATE_EMPTY_FMT] = { "La ranura %d está vacía.", "Slot %d is empty." },
    [T_STATE_BAD_FMT]   = {
        "El estado de la ranura %d no sirve para esta ROM o esta versión.",
        "The state in slot %d does not match this ROM or version.",
    },
    [T_STATE_WRITE_FMT] = { "No se pudo guardar el estado en\n%s", "Could not save the state to\n%s" },

    [T_EXPORT_TITLE]      = { "Exportar", "Export" },
    [T_AUDIO_SAVED_FMT]   = { "Audio guardado:\n%s", "Audio saved:\n%s" },
    [T_EXPORT_FAILED_FMT] = { "No se pudo escribir\n%s", "Could not write\n%s" },

    [T_TITLE_PAUSED]    = { "Pausa", "Paused" },
    [T_TITLE_SAVED_FMT] = { "Estado %d guardado", "State %d saved" },
    [T_TITLE_LOADED_FMT] = { "Estado %d cargado", "State %d loaded" },
    [T_TITLE_RECORDING] = { "grabando audio", "recording audio" },
    [T_TITLE_SHOT_FMT]  = { "Captura: %s", "Screenshot: %s" },
    [T_TITLE_FAST]      = { "avance rápido", "fast forward" },

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
    [T_ROM_BAD_FMT]  = { "\"%s\": %s", "\"%s\": %s" },
    [T_NO_DATA_DIR]   = { "No se pudo crear la carpeta de datos del juego.",
                          "Could not create the game's data folder." },
    [T_SAVE_FAILED_FMT] = { "No se pudo guardar la partida en\n%s", "Could not save the game to\n%s" },
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
