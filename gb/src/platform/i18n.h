/*
 * Textos de la interfaz en varios idiomas (mismo esquema que Moskva 89). Cada
 * frase tiene un identificador; tr() devuelve su versión en el idioma actual.
 * Los textos con %s o %d son formatos para snprintf.
 */
#ifndef I18N_H
#define I18N_H

typedef enum { LANG_ES, LANG_EN, LANG_COUNT } Language;

typedef enum {
    /* Menús */
    T_MENU_GAME, T_MENU_VIDEO, T_MENU_PALETTE, T_MENU_AUDIO, T_MENU_EXPORT, T_MENU_LANGUAGE, T_MENU_HELP,
    T_REC_MUSIC, T_COLLECT_TILES, T_SCREENSHOT, T_OPEN_EXPORTS,
    T_RESET, T_PAUSE, T_SAVE_STATE, T_LOAD_STATE, T_SLOT_FMT, T_QUIT,
    T_CONSOLE_DMG, T_CONSOLE_CGB, T_CONSOLE_SGB,
    T_PALETTE_DMG, T_PALETTE_POCKET, T_PALETTE_GRAY, T_PALETTE_GAME,
    T_DIR_UP, T_DIR_DOWN, T_DIR_LEFT, T_DIR_RIGHT,
    T_SGB_BORDER, T_SIZE_FMT, T_FULLSCREEN,
    T_SOUND, T_VOLUME_FMT,
    T_HELP_CONTROLS, T_HELP_GAME, T_ABOUT,

    /* Botones */
    T_OK, T_CANCEL,

    /* Diálogos */
    T_CONTROLS_TEXT, T_GAME_TITLE, T_GAME_TEXT_FMT, T_ABOUT_TEXT_FMT,
    T_STATE_TITLE, T_STATE_EMPTY_FMT, T_STATE_BAD_FMT, T_STATE_WRITE_FMT,
    T_EXPORT_TITLE, T_MUSIC_SAVED_FMT, T_TILES_SAVED_FMT, T_TILES_NONE, T_EXPORT_FAILED_FMT,
    T_CONSOLE_TITLE, T_CONSOLE_TEXT,
    T_MODEL_DMG, T_MODEL_CGB, T_MODEL_COMPAT, T_MODEL_SGB,

    /* Título de la ventana */
    T_TITLE_PAUSED, T_TITLE_SAVED_FMT, T_TITLE_LOADED_FMT, T_TITLE_RECORDING, T_TITLE_COLLECTING_FMT,
    T_TITLE_SHOT_FMT,

    /* Arranque y errores */
    T_ROM_CHOOSE, T_ROM_CHOOSE_AGAIN, T_ROM_DROP_FMT, T_ROM_WRONG_FMT, T_ROM_UNREADABLE_FMT,
    T_NO_DATA_DIR, T_SAVE_FAILED_FMT, T_BAD_MODEL, T_BAD_PALETTE, T_RECORD_FAILED,

    T_COUNT
} TextId;

void i18n_set(Language lang);
Language i18n_get(void);
const char *tr(TextId id);

/* Nombre del idioma en ese mismo idioma ("Español", "English") */
const char *i18n_name(Language lang);

/* Código para settings.ini ("es", "en") y su inverso (LANG_COUNT si no existe) */
const char *i18n_code(Language lang);
Language i18n_from_code(const char *code);

#endif
