/*
 * Capa de plataforma con SDL2 para SMSRecomp: ventana con barra de menús, controles (teclado y mandos),
 * sonido a 44 100 Hz (estéreo en el Game Gear), estados guardados y ritmo de 59.92 cuadros por segundo. La usan
 * el intérprete (`sms`) y los juegos que genera smsrecomp (con -DSMS_RECOMP).
 *
 * Uso: sms [rom.sms|rom.gg] [--model sms|gg] [--scale N] [--fullscreen] [--no-menu]
 *          [--record archivo | --play archivo] [--trace archivo]
 * Recompilado: juego [rom.sms] [--interp] [--force] [--uncovered archivo] y las mismas opciones. La ROM se busca en
 *   el argumento, en la copia que se guarda la primera vez, en rom.sms o rom.gg junto al ejecutable o en la carpeta
 *   actual, y si no, se pide; debe tener el SHA1 que se recompiló.
 *   --interp     solo el intérprete (para comparar)
 *   --force      acepta otra ROM (corre con el intérprete)
 *   --uncovered  como --trace: lo que el intérprete tuvo que ejecutar (para smsrecomp --trace)
 *   --model       la consola, si no la que pide el cartucho (Entrada → Consola)
 *   --fullscreen  arranca en pantalla completa
 *   --no-menu     sin barra de menús (ni con el ratón, Alt o F10); los atajos siguen
 *   --record      graba los botones de los dos jugadores en un archivo de texto (ver inputs.h)
 *   --play        reproduce un archivo grabado (el teclado se ignora)
 *   --trace       al salir, agrega a `archivo` las instrucciones de la ROM que se ejecutaron (como smstrace)
 * Para pruebas sin ventana: --ui-shot archivo.png [--ui-frames N] [--ui-menu N] [--ui-action N].
 *
 * Jugador 1: flechas, Z X = botones 1 y 2, Enter = Start (Game Gear), Espacio = Pausa (Master System; en el
 * Game Gear con un juego de SMS, Start). Jugador 2: I J K L, V B = 1 y 2. Mandos: el primero es el jugador 1 y
 * el segundo el 2. En una pantalla táctil, el mando en pantalla (player_touch.h) es el jugador 1, con Pausa o
 * Start según la consola; en Android, Atrás abre el menú.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\SMSRecomp\<juego>\, en Linux
 * ~/.local/share/SMSRecomp/<juego>/, en macOS ~/Library/Application Support/SMSRecomp/<juego>/.
 * Ahí van la partida (<rom>.sav: la RAM del cartucho; se guarda al salir y cada 5 segundos si cambió), los
 * estados guardados (<rom>.estado1-4), las preferencias (settings.ini) y lo exportado.
 *
 * La barra de menús y los diálogos vienen de Moskva 89 (ui_menu.c, ui_font.c): se dibujan dentro
 * de la ventana para verse igual en todos los sistemas. Con un menú o un diálogo abierto, el juego
 * se pausa.
 */
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef SMS_RECOMP
#include <dirent.h>
#include "recomp.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "export.h"
#include "files.h"
#include "i18n.h"
#include "icono.h"
#include "inputs.h"
#include "png.h"
#include "settings.h"
#include "sha1.h"
#include "sms.h"
#include "ui_font.h"
#include "ui_menu.h"
#include "player_util.h"
#include "player_audio.h"
#include "player_touch.h"

#define NTSC_FPS (53693175.0 / (MCLK_PER_LINE * LINES_NTSC))      /* 59.92 */
/* El filtro del diálogo para elegir la ROM en Windows (ask_rom_path, files.h) */
#define ROM_TYPES "ROM de Master System o Game Gear (*.sms, *.gg)|*.sms;*.gg"

static void fail(const char *msg);

/* ------------------------------------------------------------------ */
/* Estado de la aplicación                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;
    size_t size;
} Rom;

enum {
    A_NONE, A_OPEN_ROM, A_RESET, A_PAUSE, A_SAVE_STATE, A_LOAD_STATE, A_SLOT_1, A_SLOT_4 = A_SLOT_1 + 3, A_QUIT,
    A_SCALE_1, A_SCALE_6 = A_SCALE_1 + 5, A_FULLSCREEN, A_ASPECT, A_INTEGER,
    A_SOUND, A_VOLUME_25, A_VOLUME_100 = A_VOLUME_25 + 3, A_CHAN_0, A_CHAN_3 = A_CHAN_0 + 3,
    A_MODEL_AUTO, A_MODEL_SMS, A_MODEL_GG,
    A_SCREENSHOT, A_OPEN_EXPORTS,
    A_HELP_CONTROLS, A_HELP_GAME, A_ABOUT,
    A_HIDE_MENU,            /* antes de A_LANGUAGE_0 para no mover los números que prueba ui_golden */
    A_LANGUAGE_0,
};

enum { TAG_INFO = 1 };

typedef struct {
    Sms *sms;
    Rom rom;                /* se conserva para reiniciar */
    int loaded;
    int interp_only;        /* recompilado: solo el intérprete (--interp, o la ROM es otra) */

    char name[96];          /* nombre del juego (archivos y título) */
    char title[96];         /* nombre para mostrar */
    char rom_path[1024];

    int paused, hold_input, fullscreen, running, mouse_y, fast;
    int menu_hidden;        /* MENU_SHOWN, MENU_HIDDEN o MENU_OFF (player_common.h) */
    Settings cfg;
    char *pref;
    char settings_path[1024], save_path[1024], export_dir[1100];

    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tex_w, tex_h;
    SDL_GameController *pads[2];
    PlayerAudio audio;      /* la salida de sonido (player_audio.h) */
    TouchPad touch;         /* el mando en pantalla (player_touch.h) */

    MenuBar bar;
    Dialog dlg;
    char win_title[256];
    char notice[128];
    Uint32 notice_until;
    long frame;

    InputScript script;
    int play;
    FILE *record;
    int last_pad[2];
    const char *trace_path;
} App;

/* Las funciones de la ventana iguales en los recompiladores (retrorecomp/shared) */
static void resize_window(App *app);
#include "player_common.h"

static App *the_app;

#define PLAYER_NAME "SMSRecomp"
#include "player_rom.h"
static const char *ui_shot_path;
static SDL_Surface *ui_shot_surface;   /* --ui-shot: se dibuja en una superficie, sin ventana */

/* ------------------------------------------------------------------ */
/* Nombres y carpetas                                                  */
/* ------------------------------------------------------------------ */

/* Nombre del juego (para archivos y título) a partir del de la ROM, sin la extensión */
static void set_game(App *app, const char *path)
{
    snprintf(app->name, sizeof app->name, "%s", file_name(path));
    char *dot = strrchr(app->name, '.');
    if (dot && dot != app->name) *dot = 0;
    for (char *c = app->name; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' ||
            *c == '>' || *c == '|') *c = '_';
    snprintf(app->title, sizeof app->title, "%s", app->name);
    snprintf(app->save_path, sizeof app->save_path, "%s%s.sav", app->pref, app->name);
}

/* ------------------------------------------------------------------ */
/* ROM                                                                 */
/* ------------------------------------------------------------------ */

/* 1: ROM buena; 0: (recompilado) es otra ROM; -1: ilegible; -2: no es una ROM que sepamos correr (el motivo en
 * *why). El SHA1 es el de la ROM ya normalizada: con o sin cabecera de copiador, sirve igual */
static int load_rom(const char *path, Rom *rom, const char **why)
{
    const char *unused;
    if (!why) why = &unused;
    rom->data = u8_read_file(path, &rom->size);
    if (!rom->data) return -1;
    size_t n;
    int copier;
    u8 *img = cart_normalize(rom->data, rom->size, &n, &copier, why);
    if (!img) return -2;
#ifdef SMS_RECOMP
    char sha1[41];
    sha1_hex(img, n, sha1);
    free(img);
    return strcmp(sha1, recomp_rom_sha1) == 0;
#else
    free(img);
    return 1;
#endif
}

static void wrong_rom_message(const char *path, char *msg, size_t size)
{
#ifdef SMS_RECOMP
    snprintf(msg, size, tr(T_ROM_WRONG_FMT), path, recomp_name, recomp_rom_sha1);
#else
    snprintf(msg, size, tr(T_ROM_UNREADABLE_FMT), path);
#endif
}

/* Espera a que arrastren un archivo a la ventana (o a que la cierren) */
/* Pide la ROM hasta que den una válida. 0 si el usuario cancela. */
static int ask_for_rom(App *app, Rom *rom, char *path_out, size_t path_size)
{
    char msg[4096];
    int first = 1;
    (void)app;
    for (;;) {
        int got = ask_rom_path(tr(first ? T_ROM_CHOOSE : T_ROM_CHOOSE_AGAIN), ROM_TYPES, path_out, path_size);
        if (!got) {
#ifdef __ANDROID__
            return 0;       /* canceló el selector de archivos: no hay ventana a la cual arrastrarla */
#endif
            snprintf(msg, sizeof msg, tr(T_ROM_DROP_FMT), "Master System o Game Gear");
            if (first) inform(msg);
            if (!wait_for_drop(path_out, path_size)) return 0;
        }
        first = 0;
        const char *why = NULL;
        int r = load_rom(path_out, rom, &why);
        if (r == 1) return 1;
        free(rom->data);
        if (r == -2) snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), path_out, why);
        else if (r == 0) wrong_rom_message(path_out, msg, sizeof msg);
        else snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), path_out);
        inform(msg);
    }
}

/* ------------------------------------------------------------------ */
/* Partida                                                             */
/* ------------------------------------------------------------------ */

static int save_game(App *app)
{
    if (!app->loaded || !sms_sram_dirty(app->sms)) return 1;
    return sms_sram_save(app->sms, app->save_path);
}

/* ¿Corre en el modo del Game Gear (pantalla de 160×144, Start y estéreo)? */
static int gg_mode(const App *app)
{
    return app->loaded && app->sms->model == MODEL_GG && !app->sms->sms_mode;
}

/* El mando en pantalla (player_touch.h): los botones 1 y 2 abajo a la derecha y, arriba, la Pausa de la consola
 * (Master System) o el Start (Game Gear) */
static const TouchButton touch_keys_sms[] = {
    { "1", SMS_A, TOUCH_RIGHT, 0, -2.15f, -1.1f },
    { "2", SMS_B, TOUCH_RIGHT, 0, -0.85f, -1.4f },
    { "PAUSE", SMS_PAUSE, TOUCH_RIGHT, 1, -1.5f, -2.9f },
};
static const TouchButton touch_keys_gg[] = {
    { "1", SMS_A, TOUCH_RIGHT, 0, -2.15f, -1.1f },
    { "2", SMS_B, TOUCH_RIGHT, 0, -0.85f, -1.4f },
    { "START", SMS_START, TOUCH_RIGHT, 1, -1.5f, -2.9f },
};
static const TouchLayout touch_layout_sms = { SMS_UP, SMS_DOWN, SMS_LEFT, SMS_RIGHT, touch_keys_sms, 3 };
static const TouchLayout touch_layout_gg = { SMS_UP, SMS_DOWN, SMS_LEFT, SMS_RIGHT, touch_keys_gg, 3 };

static void write_trace(App *app)
{
    if (app->trace_path && app->loaded && app->sms->cart.trace) sms_trace_write(app->sms, app->trace_path);
}

/* Enciende (o reinicia) la consola con la ROM actual, en el modelo elegido (Entrada → Consola). La traza de
 * --trace se escribe antes de soltar la consola, y la nueva empieza con lo que ya tenía el archivo */
static void power_on(App *app)
{
    const char *err = NULL;
    if (app->loaded) { write_trace(app); sms_free(app->sms); }
    if (sms_load(app->sms, app->rom.data, app->rom.size, app->rom_path, app->cfg.model, &err)) fail(err);
    app->loaded = 1;
    app->touch.layout = gg_mode(app) ? &touch_layout_gg : &touch_layout_sms;
    sms_sram_load(app->sms, app->save_path);
    if (app->trace_path) { sms_trace_start(app->sms); sms_trace_load(app->sms, app->trace_path); }
    app->frame = 0;
    app->last_pad[0] = app->last_pad[1] = -1;
    if (app->script.n) app->script.pos = 0;
}

/* Cambia de ROM (arrastrar y soltar o Abrir ROM): guarda la partida actual y enciende la nueva */
static int switch_rom(App *app, const char *path)
{
    Rom rom;
    const char *why = NULL;
    char msg[4096];
    int r = load_rom(path, &rom, &why);
    if (r != 1) {
        if (r == -2) snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), path, why);
        else if (r == 0) wrong_rom_message(path, msg, sizeof msg);
        else snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), path);
        if (r != -1) free(rom.data);
        menubar_close(&app->bar);
        dialog_open(&app->dlg, DIALOG_MESSAGE, TAG_INFO, app->title, msg);
        return 0;
    }
    save_game(app);
    free(app->rom.data);
    app->rom = rom;
    snprintf(app->rom_path, sizeof app->rom_path, "%s", path);
#ifndef SMS_RECOMP
    set_game(app, path);
#endif
    power_on(app);
    resize_window(app);         /* puede ser de la otra consola */
    app->win_title[0] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Imagen                                                              */
/* ------------------------------------------------------------------ */

/* Lo que se ve de la imagen (vdp_view): 256×192 (o 224) en el Master System, 160×144 en el Game Gear */
static void game_view(const App *app, int *x0, int *y0, int *w, int *h)
{
    if (app->loaded) vdp_view(app->sms, x0, y0, w, h);
    else { *x0 = *y0 = 0; *w = 256; *h = 192; }
}

static void game_size(const App *app, int *w, int *h)
{
    int x0, y0;
    game_view(app, &x0, &y0, w, h);
}

/* Ancho en pantalla: con el aspecto corregido, los pixeles del Master System son de 8:7 (como en un televisor);
 * los de la pantalla del Game Gear se dejan cuadrados */
static double display_w(const App *app, int gw, int gh)
{
    (void)gh;
    return app->cfg.aspect && !gg_mode(app) ? gw * 8.0 / 7.0 : gw;
}

/* El tamaño base de la ventana: la imagen de 192 líneas del Master System o la pantalla del Game Gear */
static void base_size(const App *app, int *w, int *h)
{
    *h = gg_mode(app) ? 144 : 192;
    *w = (int)(display_w(app, gg_mode(app) ? 160 : 256, *h) + 0.5);
}

static void resize_window(App *app)
{
    if (app->fullscreen) return;
    int bw, bh;
    base_size(app, &bw, &bh);
    SDL_SetWindowMinimumSize(app->win, 320, 240 + bar_space(app, 1));
    SDL_SetWindowSize(app->win, bw * app->cfg.scale, bh * app->cfg.scale + bar_space(app, ui_scale(bw * app->cfg.scale)));
}

/* Convierte lo que se ve de la imagen a ARGB */
static void compose(const App *app, Uint32 *dst, int gw, int gh)
{
    int x0, y0, w, h;
    game_view(app, &x0, &y0, &w, &h);
    const u16 *fb = app->sms->vdp.fb;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) dst[y * gw + x] = vdp_argb(fb[(y + y0) * FB_W + x + x0]);
}

/* ------------------------------------------------------------------ */
/* Avisos, exportación                                                 */
/* ------------------------------------------------------------------ */

static void screenshot(App *app)
{
    static Uint32 rgb[FB_W * FB_H];
    char base[1300], path[1400], text[1500];
    int gw, gh;
    game_size(app, &gw, &gh);
    compose(app, rgb, gw, gh);
    if (!export_base(app, "captura", base, sizeof base)) {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), app->export_dir);
        message(app, tr(T_EXPORT_TITLE), text);
        return;
    }
    snprintf(path, sizeof path, "%s.png", base);
    if (screenshot_write(path, rgb, gw, gh)) {
        snprintf(text, sizeof text, tr(T_TITLE_SHOT_FMT), file_name(path));
        notify(app, text);
    } else {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), path);
        message(app, tr(T_EXPORT_TITLE), text);
    }
}

/* ------------------------------------------------------------------ */
/* Estados guardados                                                   */
/* ------------------------------------------------------------------ */

static void save_state(App *app)
{
    char path[1100], text[1300];
    size_t size;
    u8 *data = sms_state_save(app->sms, &size);
    state_path(app, app->cfg.slot, path, sizeof path);
    if (data && u8_write_file(path, data, size)) {
        snprintf(text, sizeof text, tr(T_TITLE_SAVED_FMT), app->cfg.slot);
        notify(app, text);
    } else {
        snprintf(text, sizeof text, tr(T_STATE_WRITE_FMT), path);
        message(app, tr(T_STATE_TITLE), text);
    }
    free(data);
}

static void load_state(App *app)
{
    char path[1100], text[1300];
    size_t size;
    state_path(app, app->cfg.slot, path, sizeof path);
    u8 *data = u8_read_file(path, &size);
    if (!data) {
        snprintf(text, sizeof text, tr(T_STATE_EMPTY_FMT), app->cfg.slot);
        message(app, tr(T_STATE_TITLE), text);
        return;
    }
    if (sms_state_load(app->sms, data, size)) {
        snprintf(text, sizeof text, tr(T_TITLE_LOADED_FMT), app->cfg.slot);
        notify(app, text);
    } else {
        snprintf(text, sizeof text, tr(T_STATE_BAD_FMT), app->cfg.slot);
        message(app, tr(T_STATE_TITLE), text);
    }
    free(data);
}

/* ------------------------------------------------------------------ */
/* Menús                                                               */
/* ------------------------------------------------------------------ */

#define SEPARATOR(m, n) m[n++] = (MenuItem){ "", NULL, A_NONE, ITEM_SEPARATOR }
#define ITEM(m, n, text, key, action, kind) m[n++] = (MenuItem){ text, key, action, kind }

static MenuItem menu_game[16], menu_video[12], menu_audio[12], menu_input[4], menu_export[4], menu_language[LANG_COUNT], menu_help[4];
static Menu menus[7];
static char size_labels[6][40], volume_labels[4][40], chan_labels[3][40], slot_labels[4][40];

static void build_menus(App *app)
{
    int n = 0, k = 0;
    ITEM(menu_game, n, tr(T_OPEN_ROM), "Ctrl+O", A_OPEN_ROM, ITEM_ACTION);
    SEPARATOR(menu_game, n);
    ITEM(menu_game, n, tr(T_RESET), "Ctrl+R", A_RESET, ITEM_ACTION);
    ITEM(menu_game, n, tr(T_PAUSE), "P", A_PAUSE, ITEM_CHECK);
    SEPARATOR(menu_game, n);
    ITEM(menu_game, n, tr(T_SAVE_STATE), "F5", A_SAVE_STATE, ITEM_ACTION);
    ITEM(menu_game, n, tr(T_LOAD_STATE), "F8", A_LOAD_STATE, ITEM_ACTION);
    for (int i = 0; i < 4; i++) {
        snprintf(slot_labels[i], sizeof slot_labels[i], tr(T_SLOT_FMT), i + 1);
        ITEM(menu_game, n, slot_labels[i], NULL, A_SLOT_1 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_game, n);
    ITEM(menu_game, n, tr(T_QUIT), "Alt+F4", A_QUIT, ITEM_ACTION);
    menus[k++] = (Menu){ tr(T_MENU_GAME), menu_game, n };

    n = 0;
    for (int i = 0; i < 6; i++) {
        snprintf(size_labels[i], sizeof size_labels[i], tr(T_SIZE_FMT), i + 1);
        ITEM(menu_video, n, size_labels[i], NULL, A_SCALE_1 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_video, n);
    ITEM(menu_video, n, tr(T_FULLSCREEN), "F11", A_FULLSCREEN, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_HIDE_MENU), "F9", A_HIDE_MENU, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_INTEGER), NULL, A_INTEGER, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_ASPECT), NULL, A_ASPECT, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_VIDEO), menu_video, n };

    n = 0;
    ITEM(menu_audio, n, tr(T_SOUND), "Ctrl+M", A_SOUND, ITEM_CHECK);
    SEPARATOR(menu_audio, n);
    for (int i = 0; i < 4; i++) {
        snprintf(volume_labels[i], sizeof volume_labels[i], tr(T_VOLUME_FMT), 25 * (i + 1));
        ITEM(menu_audio, n, volume_labels[i], NULL, A_VOLUME_25 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_audio, n);
    for (int i = 0; i < 3; i++) {
        snprintf(chan_labels[i], sizeof chan_labels[i], tr(T_CHAN_TONE_FMT), i + 1);
        ITEM(menu_audio, n, chan_labels[i], NULL, A_CHAN_0 + i, ITEM_CHECK);
    }
    ITEM(menu_audio, n, tr(T_CHAN_NOISE), NULL, A_CHAN_3, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_AUDIO), menu_audio, n };

    n = 0;
    ITEM(menu_input, n, tr(T_MODEL_AUTO), NULL, A_MODEL_AUTO, ITEM_RADIO);
    ITEM(menu_input, n, tr(T_MODEL_SMS), NULL, A_MODEL_SMS, ITEM_RADIO);
    ITEM(menu_input, n, tr(T_MODEL_GG), NULL, A_MODEL_GG, ITEM_RADIO);
    menus[k++] = (Menu){ tr(T_MENU_INPUT), menu_input, n };

    n = 0;
    ITEM(menu_export, n, tr(T_SCREENSHOT), "F12", A_SCREENSHOT, ITEM_ACTION);
    SEPARATOR(menu_export, n);
    ITEM(menu_export, n, tr(T_OPEN_EXPORTS), NULL, A_OPEN_EXPORTS, ITEM_ACTION);
    menus[k++] = (Menu){ tr(T_MENU_EXPORT), menu_export, n };

    for (int i = 0; i < LANG_COUNT; i++)
        menu_language[i] = (MenuItem){ i18n_name((Language)i), NULL, A_LANGUAGE_0 + i, ITEM_RADIO };
    menus[k++] = (Menu){ tr(T_MENU_LANGUAGE), menu_language, LANG_COUNT };

    n = 0;
    ITEM(menu_help, n, tr(T_HELP_CONTROLS), "F1", A_HELP_CONTROLS, ITEM_ACTION);
    ITEM(menu_help, n, tr(T_HELP_GAME), NULL, A_HELP_GAME, ITEM_ACTION);
    ITEM(menu_help, n, tr(T_ABOUT), NULL, A_ABOUT, ITEM_ACTION);
    menus[k++] = (Menu){ tr(T_MENU_HELP), menu_help, n };

    menubar_close(&app->bar);
    app->bar.count = k;
}

static int menu_state(void *ctx, int action, int *checked)
{
    App *app = ctx;
    /* Con una grabación, lo que cambia la partida desfasaría los botones */
    int replay = app->play || app->record;
    switch (action) {
    case A_OPEN_ROM:     return !replay;
    case A_RESET:        return !replay;
    case A_LOAD_STATE:   return !replay;
    case A_PAUSE:        *checked = app->paused; return 1;
    case A_FULLSCREEN:   *checked = app->fullscreen; return 1;
    case A_HIDE_MENU:    *checked = app->menu_hidden != MENU_SHOWN; return 1;
    case A_INTEGER:      *checked = app->cfg.integer; return 1;
    case A_ASPECT:       *checked = app->cfg.aspect; return 1;
    case A_MODEL_AUTO:   *checked = app->cfg.model == MODEL_AUTO; return !replay;
    case A_MODEL_SMS:    *checked = app->cfg.model == MODEL_SMS; return !replay;
    case A_MODEL_GG:     *checked = app->cfg.model == MODEL_GG; return !replay;
    case A_SOUND:        *checked = app->cfg.sound; return 1;
    default:
        if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            *checked = app->cfg.volume == 25 * (action - A_VOLUME_25 + 1);
            return app->cfg.sound;
        }
        if (action >= A_SLOT_1 && action <= A_SLOT_4) {
            *checked = app->cfg.slot == action - A_SLOT_1 + 1;
            return 1;
        }
        if (action >= A_CHAN_0 && action <= A_CHAN_3) {
            *checked = !(app->cfg.mute & (1 << (action - A_CHAN_0)));    /* marcado = suena */
            return 1;
        }
        if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            *checked = app->cfg.scale == action - A_SCALE_1 + 1;
            return !app->fullscreen;
        } else if (action >= A_LANGUAGE_0 && action < A_LANGUAGE_0 + LANG_COUNT)
            *checked = (int)i18n_get() == action - A_LANGUAGE_0;
        return 1;
    }
}

static void ask_open_rom(App *app)
{
    char path[1024];
    if (ask_rom_path(tr(T_ROM_CHOOSE), ROM_TYPES, path, sizeof path)) switch_rom(app, path);
    else message(app, tr(T_OPEN_ROM), tr(T_ROM_CHOOSE));   /* sin diálogo nativo: se arrastra */
}

static void game_info(App *app)
{
    char text[2048], folder[64], saves[64], code[16];
    const Sms *g = app->sms;
    const Cart *c = &g->cart;
    size_t len = strlen(app->pref);
    /* Una ruta larga no cabe en el diálogo: se deja el final */
    if (len > 44) snprintf(folder, sizeof folder, "…%s", app->pref + len - 43);
    else snprintf(folder, sizeof folder, "%s", app->pref);
    if (c->ram_used) snprintf(saves, sizeof saves, tr(T_SAVES_RAM), c->mapper == MAPPER_CODEMASTERS ? 8 : CART_RAM_SIZE / 1024);
    else snprintf(saves, sizeof saves, "%s", tr(T_SAVES_NONE));
    snprintf(code, sizeof code, "%s", c->hdr.found ? c->hdr.product : "-");
    const char *name = c->db ? c->db->name : c->sdsc.found && c->sdsc.name[0] ? c->sdsc.name : app->title;
    const char *console = g->model == MODEL_GG ? (g->sms_mode ? "Game Gear (modo SMS)" : "Game Gear") : "Master System";
    char mapper[48];
    if (c->mapper == MAPPER_NONE) snprintf(mapper, sizeof mapper, "%s", mapper_name(c->mapper));
    else snprintf(mapper, sizeof mapper, "mapper %s", mapper_name(c->mapper));
    snprintf(text, sizeof text, tr(T_GAME_TEXT_FMT), name, console, (int)(c->rom_size / 1024), mapper,
             code, c->hdr.found ? region_name(c->hdr.region) : "-", saves, c->sha1, folder);
#ifdef SMS_RECOMP
    size_t tl = strlen(text);
    u64 run = z80_stat.native + z80_stat.interp;
    if (app->interp_only) snprintf(text + tl, sizeof text - tl, "\n\n%s", tr(T_RECOMP_OFF));
    else snprintf(text + tl, sizeof text - tl, tr(T_RECOMP_STATS_FMT), recomp_instructions,
                  run ? 100.0 * (double)z80_stat.native / (double)run : 0.0);
#endif
    message(app, tr(T_GAME_TITLE), text);
}

/* Entrada → Consola: la consola cambia al encender, así que reinicia (guardando la partida) */
static void set_model(App *app, int model)
{
    if (app->cfg.model == model) return;
    app->cfg.model = model;
    save_game(app);
    power_on(app);
    resize_window(app);
}

static void do_action(App *app, int action)
{
    switch (action) {
    case A_OPEN_ROM:     ask_open_rom(app); break;
    case A_RESET:        save_game(app); power_on(app); break;
    case A_PAUSE:        app->paused = !app->paused; break;
    case A_SAVE_STATE:   save_state(app); break;
    case A_LOAD_STATE:   load_state(app); break;
    case A_QUIT:         app->running = 0; break;
    case A_FULLSCREEN:   toggle_fullscreen(app); break;
    case A_HIDE_MENU:    toggle_menu(app); if (app->menu_hidden != MENU_OFF) app->cfg.hide_menu = app->menu_hidden == MENU_HIDDEN; break;
    case A_INTEGER:      app->cfg.integer = !app->cfg.integer; break;
    case A_ASPECT:       app->cfg.aspect = !app->cfg.aspect; resize_window(app); break;
    case A_MODEL_AUTO:   set_model(app, MODEL_AUTO); break;
    case A_MODEL_SMS:    set_model(app, MODEL_SMS); break;
    case A_MODEL_GG:     set_model(app, MODEL_GG); break;
    case A_SOUND:        app->cfg.sound = !app->cfg.sound; break;
    case A_SCREENSHOT:   screenshot(app); break;
    case A_OPEN_EXPORTS: open_exports(app); break;
    case A_HELP_CONTROLS: {
        /* El título es la opción del menú, sin los puntos suspensivos */
        char title[64];
        snprintf(title, sizeof title, "%s", tr(T_HELP_CONTROLS));
        char *dots = strstr(title, "…");
        if (dots) *dots = 0;
        message(app, title, tr(T_CONTROLS_TEXT));
        break;
    }
    case A_HELP_GAME:    game_info(app); break;
    case A_ABOUT: {
        char text[1024];
        snprintf(text, sizeof text, tr(T_ABOUT_TEXT_FMT), app->title);
        message(app, app->title, text);
        break;
    }
    default:
        if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            app->cfg.scale = action - A_SCALE_1 + 1;
            resize_window(app);
        } else if (action >= A_SLOT_1 && action <= A_SLOT_4) {
            app->cfg.slot = action - A_SLOT_1 + 1;
        } else if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            app->cfg.volume = 25 * (action - A_VOLUME_25 + 1);
        } else if (action >= A_CHAN_0 && action <= A_CHAN_3) {
            app->cfg.mute ^= 1 << (action - A_CHAN_0);
        } else if (action >= A_LANGUAGE_0 && action < A_LANGUAGE_0 + LANG_COUNT) {
            i18n_set((Language)(action - A_LANGUAGE_0));
            snprintf(app->cfg.language, sizeof app->cfg.language, "%s", i18n_code(i18n_get()));
            build_menus(app);
            app->win_title[0] = 0;      /* que se vuelva a poner el título */
        }
        break;
    }
}

/* Atajos de teclado que no pasan por el menú */
static int shortcut(App *app, const SDL_KeyboardEvent *k)
{
    if (k->repeat) return 0;
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    int alt = (k->keysym.mod & KMOD_ALT) != 0;
    int replay = app->play || app->record;
    switch (k->keysym.sym) {
    case SDLK_r:      if (ctrl && !replay) { do_action(app, A_RESET); return 1; } break;
    case SDLK_o:      if (ctrl && !replay) { do_action(app, A_OPEN_ROM); return 1; } break;
    case SDLK_F5:     do_action(app, A_SAVE_STATE); return 1;
    case SDLK_F8:     if (!replay) { do_action(app, A_LOAD_STATE); return 1; } break;
    case SDLK_m:      if (ctrl) { do_action(app, A_SOUND); return 1; } break;
    case SDLK_p:      do_action(app, A_PAUSE); return 1;
    case SDLK_F11:    do_action(app, A_FULLSCREEN); return 1;
    case SDLK_F9:     do_action(app, A_HIDE_MENU); return 1;
    case SDLK_RETURN: if (alt) { do_action(app, A_FULLSCREEN); return 1; } break;
    case SDLK_F1:     do_action(app, A_HELP_CONTROLS); return 1;
    case SDLK_F12:    do_action(app, A_SCREENSHOT); return 1;
    default: break;
    }
    return 0;
}

/* Reparte un evento entre el diálogo, la barra de menús y los atajos */
static void handle_event(App *app, SDL_Event *e)
{
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    int ui = ui_scale(w);

    if (e->type == SDL_QUIT) {
        app->running = 0;
        return;
    }
    if (e->type == SDL_DROPFILE) {
        if (!(app->play || app->record)) switch_rom(app, e->drop.file);
        SDL_free(e->drop.file);
        return;
    }
    if (touch_and_back(app, e)) return;
    if (e->type == SDL_MOUSEMOTION)
        app->mouse_y = e->motion.y;
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        /* Al primer lugar libre: el primero que se conecta es el jugador 1 */
        for (int i = 0; i < 2; i++)
            if (!app->pads[i]) {
                app->pads[i] = SDL_GameControllerOpen(e->cdevice.which);
                break;
            }
        return;
    }
    if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_GameController *gone = SDL_GameControllerFromInstanceID(e->cdevice.which);
        for (int i = 0; i < 2; i++)
            if (gone && app->pads[i] == gone) {
                SDL_GameControllerClose(app->pads[i]);
                app->pads[i] = NULL;
            }
        return;
    }

    if (app->dlg.kind != DIALOG_NONE) {
        dialog_event(&app->dlg, e, w, h, ui);
        app->hold_input = 1;
        return;
    }

    int action = 0;
    if (bar_enabled(app) && menubar_event(&app->bar, e, ui, &action)) {
        if (action) do_action(app, action);
        app->hold_input = 1;
        return;
    }
    if (e->type == SDL_KEYDOWN)
        shortcut(app, &e->key);
}

/* ------------------------------------------------------------------ */
/* Controles                                                           */
/* ------------------------------------------------------------------ */

/* Mando de SDL (disposición de Xbox): A (o X) = botón 1, B (o Y) = botón 2, Back = Pausa */
static u8 read_pad(SDL_GameController *pad)
{
    if (!pad) return 0;
    u8 r = 0;
    #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
    if (BTN(A) || BTN(X)) r |= SMS_A;
    if (BTN(B) || BTN(Y)) r |= SMS_B;
    if (BTN(START)) r |= SMS_START;
    if (BTN(BACK)) r |= SMS_PAUSE;
    if (BTN(DPAD_UP)) r |= SMS_UP;
    if (BTN(DPAD_DOWN)) r |= SMS_DOWN;
    if (BTN(DPAD_LEFT)) r |= SMS_LEFT;
    if (BTN(DPAD_RIGHT)) r |= SMS_RIGHT;
    #undef BTN
    int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -16000) r |= SMS_LEFT;
    if (lx > 16000) r |= SMS_RIGHT;
    if (ly < -16000) r |= SMS_UP;
    if (ly > 16000) r |= SMS_DOWN;
    return r;
}

static void player_pads(App *app, u8 pads[2])
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u8 p1 = 0, p2 = 0;
    if (k[SDL_SCANCODE_Z]) p1 |= SMS_A;
    if (k[SDL_SCANCODE_X]) p1 |= SMS_B;
    if (k[SDL_SCANCODE_RETURN]) p1 |= SMS_START;
    if (k[SDL_SCANCODE_SPACE]) p1 |= SMS_PAUSE;
    if (k[SDL_SCANCODE_UP]) p1 |= SMS_UP;
    if (k[SDL_SCANCODE_DOWN]) p1 |= SMS_DOWN;
    if (k[SDL_SCANCODE_LEFT]) p1 |= SMS_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) p1 |= SMS_RIGHT;
    if (k[SDL_SCANCODE_V]) p2 |= SMS_A;
    if (k[SDL_SCANCODE_B]) p2 |= SMS_B;
    if (k[SDL_SCANCODE_I]) p2 |= SMS_UP;
    if (k[SDL_SCANCODE_K]) p2 |= SMS_DOWN;
    if (k[SDL_SCANCODE_J]) p2 |= SMS_LEFT;
    if (k[SDL_SCANCODE_L]) p2 |= SMS_RIGHT;
    pads[0] = (u8)(p1 | read_pad(app->pads[0]) | touch_buttons(&app->touch));
    pads[1] = (u8)(p2 | read_pad(app->pads[1]));
    /* No pueden ir izquierda y derecha (ni arriba y abajo) a la vez en un mando de verdad */
    for (int i = 0; i < 2; i++) {
        if ((pads[i] & (SMS_LEFT | SMS_RIGHT)) == (SMS_LEFT | SMS_RIGHT)) pads[i] &= (u8)~(SMS_LEFT | SMS_RIGHT);
        if ((pads[i] & (SMS_UP | SMS_DOWN)) == (SMS_UP | SMS_DOWN)) pads[i] &= (u8)~(SMS_UP | SMS_DOWN);
    }
    /* Tras cerrar un menú o diálogo, los botones se ignoran hasta que se suelten */
    if (app->hold_input) {
        if (pads[0] || pads[1]) pads[0] = pads[1] = 0;
        else app->hold_input = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Ciclo principal                                                     */
/* ------------------------------------------------------------------ */

/* ¿Avanza el juego? Con un menú o diálogo abierto, o en pausa, no */
static void step(App *app, int audible)
{
    u8 pads[2];
    player_pads(app, pads);
    if (app->play) {
        pads[0] = inputs_pad(&app->script, app->frame, 0);
        pads[1] = inputs_pad(&app->script, app->frame, 1);
    }
    if (app->record) inputs_record(app->record, app->frame, pads, app->last_pad);
    app->sms->pad[0] = pads[0];
    app->sms->pad[1] = pads[1];
    app->sms->audio.mute = (u8)app->cfg.mute;
    app->frame++;
    sms_run_frame(app->sms);
    /* Guardado automático cada 5 segundos si la RAM del cartucho cambió */
    if (app->frame % 300 == 0 && sms_sram_dirty(app->sms)) save_game(app);

    static int16_t samples[AUDIO_BINS * 2];
    int n = sms_audio_take(app->sms, samples, AUDIO_BINS);
    if (!app->cfg.sound || !audible) {
        player_audio_stop(&app->audio);
        return;
    }
    int vol = app->cfg.volume;
    if (vol != 100) for (int i = 0; i < n * 2; i++) samples[i] = (int16_t)(samples[i] * vol / 100);
    player_audio_push_s16(&app->audio, samples, n, 2);
}

static void update_title(App *app)
{
    char t[256];
    const char *detail = "";
    if (app->notice[0] && SDL_TICKS_PASSED(SDL_GetTicks(), app->notice_until)) app->notice[0] = 0;
    if (app->notice[0]) detail = app->notice;
    else if (app->paused) detail = tr(T_TITLE_PAUSED);
    else if (app->fast) detail = tr(T_TITLE_FAST);
    else if (app->play) detail = tr(T_TITLE_PLAYING);
    if (detail[0]) snprintf(t, sizeof t, "%s - %s", app->title, detail);
    else snprintf(t, sizeof t, "%s", app->title);
    if (strcmp(t, app->win_title) != 0) {
        snprintf(app->win_title, sizeof app->win_title, "%s", t);
        SDL_SetWindowTitle(app->win, app->win_title);
    }
}

static void render(App *app)
{
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    int ui = ui_scale(w);
    int bar = bar_space(app, ui);   /* en pantalla completa o con la barra oculta va encima */

    int gw, gh;
    game_size(app, &gw, &gh);
    if (app->tex_w != gw || app->tex_h != gh) {
        if (app->tex) SDL_DestroyTexture(app->tex);
        app->tex = SDL_CreateTexture(app->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, gw, gh);
        app->tex_w = gw;
        app->tex_h = gh;
    }
    static Uint32 rgb[FB_W * FB_H];
    compose(app, rgb, gw, gh);
    SDL_UpdateTexture(app->tex, NULL, rgb, gw * (int)sizeof(Uint32));

    /* Tamaño en pantalla: escala entera más grande que cabe (o libre), centrada bajo la barra */
    int avail_h = h - bar;
    double dw = display_w(app, gw, gh);
    double fit = (double)w / dw < (double)avail_h / gh ? (double)w / dw : (double)avail_h / gh;
    if (app->cfg.integer) fit = fit < 1 ? 1 : (double)(int)(fit + 0.01);   /* el redondeo del ancho no debe bajar una escala */
    SDL_Rect dst;
    dst.w = (int)(dw * fit + 0.5);
    dst.h = (int)(gh * fit + 0.5);
    dst.x = (w - dst.w) / 2;
    dst.y = bar + (avail_h - dst.h) / 2;

    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 255);
    SDL_RenderClear(app->ren);
    SDL_RenderCopy(app->ren, app->tex, NULL, &dst);
    touch_draw(&app->touch, app->ren, w, h);

    if (bar_visible(app))
        menubar_draw(&app->bar, app->ren, w, ui);
    dialog_draw(&app->dlg, app->ren, w, h, ui);

    /* Para pruebas: captura de la ventana completa (--ui-shot) */
    if (ui_shot_path) {
        if (ui_shot_surface) screenshot_write(ui_shot_path, (const Uint32 *)ui_shot_surface->pixels, w, h);
        ui_shot_path = NULL;
    }
    SDL_RenderPresent(app->ren);
}

/* ------------------------------------------------------------------ */

#ifdef SMS_RECOMP
/* Una ROM de la carpeta actual con el SHA1 esperado */
static int find_rom_here(Rom *rom, char *path_out, size_t size)
{
    DIR *d = opendir(".");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (SDL_strcasecmp(dot, ".sms") && SDL_strcasecmp(dot, ".gg"))) continue;
        const char *why;
        int r = load_rom(e->d_name, rom, &why);
        if (r == 1) { snprintf(path_out, size, "%s", e->d_name); found = 1; }
        else if (r != -1) free(rom->data);
    }
    closedir(d);
    return found;
}
#endif

/* ------------------------------------------------------------------ */
/* Bucle                                                               */
/* ------------------------------------------------------------------ */

/* Un cuadro de la ventana en tres partes: los eventos, la emulación (un cuadro, o cuatro sin sonido en avance
 * rápido) y el dibujo. El bucle de escritorio las llama con su ritmo; en el navegador, web_frame */
static void frame_input(App *app)
{
    SDL_Event ev;
    background_poll(app);
    while (SDL_PollEvent(&ev)) handle_event(app, &ev);
}

static void frame_run(App *app)
{
    int running = game_running(app);
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    app->fast = running && keys[SDL_SCANCODE_TAB];
    if (running) {
        if (app->fast) for (int i = 0; i < 4; i++) step(app, 0);       /* avance rápido: sin sonido */
        else step(app, 1);
    } else {
        player_audio_stop(&app->audio);
    }
}

static void frame_draw(App *app)
{
    update_title(app);
    render(app);
}

/* Al salir: guarda las preferencias (salvo en las capturas de prueba), la traza y la partida, y libera todo */
static void finish(App *app, int save_settings)
{
    char msg[1200];
    if (save_settings) settings_save(&app->cfg, app->settings_path);
    if (app->record) fclose(app->record);
    write_trace(app);
    if (!save_game(app)) {
        snprintf(msg, sizeof msg, tr(T_SAVE_FAILED_FMT), app->save_path);
        fail(msg);
    }
    player_audio_close(&app->audio);
    for (int i = 0; i < 2; i++)
        if (app->pads[i]) SDL_GameControllerClose(app->pads[i]);
    if (app->tex) SDL_DestroyTexture(app->tex);
    SDL_DestroyRenderer(app->ren);
    SDL_DestroyWindow(app->win);
    SDL_free(app->pref);
    SDL_Quit();
    sms_free(app->sms);
    free(app->sms);
    free(app->rom.data);
}

#ifdef __ANDROID__
/* Al irse a segundo plano (player_common.h): la partida y las preferencias, como al salir */
static void app_save(App *app)
{
    settings_save(&app->cfg, app->settings_path);
    save_game(app);
}
#endif

#ifdef __EMSCRIPTEN__
/* En el navegador (make web): el navegador llama a esto en cada refresco de la pantalla, que puede ir a 60,
 * 120 o 144 Hz; se emulan los cuadros que tocan por el tiempo pasado (a lo más cuatro, para no ponerse al
 * día de golpe al volver a una pestaña). Cada dos segundos guarda las preferencias si cambiaron y copia la
 * carpeta de datos (IDBFS) a IndexedDB, porque una pestaña cerrada no avisa */
static void web_frame(void *arg)
{
    App *app = arg;
    static double last, pending;
    static Settings saved;
    static unsigned ticks;
    const double frame_ms = 1000.0 / NTSC_FPS;
    double now = emscripten_get_now();
    pending += last ? now - last : frame_ms;
    last = now;
    if (pending > 4 * frame_ms) pending = frame_ms;
    frame_input(app);
    if (!app->running) {
        finish(app, 1);
        emscripten_run_script("FS.syncfs(false, function () {})");
        emscripten_cancel_main_loop();
        return;
    }
    for (int n = 0; pending >= frame_ms && n < 4; n++, pending -= frame_ms) frame_run(app);
    frame_draw(app);
    if (++ticks % 120 == 0) {
        if (memcmp(&saved, &app->cfg, sizeof saved)) {
            settings_save(&app->cfg, app->settings_path);
            saved = app->cfg;
        }
        emscripten_run_script("if (!Module.syncing) { Module.syncing = 1; FS.syncfs(false, function () { Module.syncing = 0; }); }");
    }
}
#endif

int main(int argc, char *argv[])
{
    static App app_storage;
    App *app = &app_storage;
    const char *rom_arg = NULL, *record_path = NULL, *play_path = NULL, *model_arg = NULL;
    int scale_arg = 0, shot_menu = -1, shot_action = 0, start_full = 0, no_menu = 0, force = 0;
    long shot_frames = 300;
    char msg[4096];
    the_app = app;
    app->mouse_y = 1 << 20;     /* lejos de la barra hasta que se mueva el ratón: oculta, no aparece sola */
    touch_init(&app->touch, &touch_layout_sms);
    app->sms = calloc(1, sizeof *app->sms);
    if (!app->sms) fail("sin memoria");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail(SDL_GetError());

    /* Carpeta de datos: partida y preferencias */
#ifdef SMS_RECOMP
    app->pref = SDL_GetPrefPath("SMSRecomp", recomp_name);
#else
    app->pref = SDL_GetPrefPath("SMSRecomp", "interprete");
#endif
    if (!app->pref) fail(tr(T_NO_DATA_DIR));
    snprintf(app->settings_path, sizeof app->settings_path, "%ssettings.ini", app->pref);
    snprintf(app->export_dir, sizeof app->export_dir, "%sexportado%c", app->pref, app->pref[strlen(app->pref) - 1]);
    /* --ui-shot (pruebas de interfaz): ajustes de fábrica, en español y sin tocar settings.ini, para que la
     * misma captura salga igual en cualquier máquina y usuario */
    int shot_mode = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--ui-shot")) shot_mode = 1;
    if (shot_mode) {
        settings_defaults(&app->cfg);
        i18n_set(LANG_ES);
    } else {
        settings_load(&app->cfg, app->settings_path);
        Language lang = i18n_from_code(app->cfg.language);
        i18n_set(lang < LANG_COUNT ? lang : detect_language());
    }
#ifdef SMS_RECOMP
    set_game(app, recomp_name);
#else
    set_game(app, "sms");
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) model_arg = argv[++i];
        else if (!strcmp(argv[i], "--fullscreen")) start_full = 1;
        else if (!strcmp(argv[i], "--no-menu")) no_menu = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_path = argv[++i];
        else if ((!strcmp(argv[i], "--trace") || !strcmp(argv[i], "--uncovered")) && i + 1 < argc) app->trace_path = argv[++i];
        else if (!strcmp(argv[i], "--interp")) app->interp_only = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--ui-shot") && i + 1 < argc) ui_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--ui-frames") && i + 1 < argc) shot_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ui-menu") && i + 1 < argc) shot_menu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ui-action") && i + 1 < argc) shot_action = atoi(argv[++i]);
        else rom_arg = argv[i];
    }
    if (scale_arg > 0) app->cfg.scale = scale_arg > 6 ? 6 : scale_arg;
    if (model_arg) {
        int m = model_from_name(model_arg);
        if (m < MODEL_AUTO) { snprintf(msg, sizeof msg, "--model: \"%s\" no es sms ni gg", model_arg); fail(msg); }
        app->cfg.model = m;
    }
    app->menu_hidden = no_menu ? MENU_OFF : app->cfg.hide_menu ? MENU_HIDDEN : MENU_SHOWN;
    if (ui_shot_path) {
        app->fullscreen = start_full;   /* la captura no cambia la ventana (oculta) */
        if (!rom_arg) fail("--ui-shot necesita la ROM");
    }

    int bw, bh;
    base_size(app, &bw, &bh);
    app->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                bw * app->cfg.scale, bh * app->cfg.scale,
                                SDL_WINDOW_RESIZABLE | (ui_shot_path ? SDL_WINDOW_HIDDEN : 0));
    if (!app->win) fail(SDL_GetError());
    /* SDL2 empieza con la entrada de texto activa; el juego solo lee teclas. Apagarla evita que
     * macOS conecte el método de entrada (InputMethodKit) y deje avisos en la consola */
    SDL_StopTextInput();
    set_window_icon(app);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    /* La captura de la interfaz se dibuja en una superficie del tamaño de la ventana, que depende de la consola:
     * se crea después de cargar la ROM */
    if (!ui_shot_path) {
        app->ren = SDL_CreateRenderer(app->win, -1, SDL_RENDERER_ACCELERATED);
        if (!app->ren) app->ren = SDL_CreateRenderer(app->win, -1, 0);
        if (!app->ren) fail(SDL_GetError());
        SDL_SetWindowMinimumSize(app->win, 320, 240 + bar_space(app, 1));
        SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 0xFF);
        SDL_RenderClear(app->ren);
        SDL_RenderPresent(app->ren);
    }

    /* ROM: el argumento; recompilado: la copia guardada, rom.sms o rom.gg junto al ejecutable o aquí, o una ROM con el
     * SHA1 correcto en esta carpeta; si no, pedirla */
    Rom *rom = &app->rom;
    char found_path[1024] = "";
    int have = 0, from_cache = 0;
    (void)from_cache;
    (void)force;
    if (rom_arg) {
        const char *why = NULL;
        int r = load_rom(rom_arg, rom, &why);
        if (r == -1) { snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), rom_arg); fail(msg); }
        if (r == -2) { snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), rom_arg, why); fail(msg); }
        if (r == 0) {
            if (!force) { wrong_rom_message(rom_arg, msg, sizeof msg); fail(msg); }
            app->interp_only = 1;       /* el código recompilado es de otra ROM */
        }
        snprintf(found_path, sizeof found_path, "%s", rom_arg);
        have = 1;
    }
#ifdef SMS_RECOMP
    /* La copia lleva la extensión de la ROM: con ella sms_load elige el modelo (.gg) */
    char cache_path[2][1100];
    snprintf(cache_path[0], sizeof cache_path[0], "%srom.sms", app->pref);
    snprintf(cache_path[1], sizeof cache_path[1], "%srom.gg", app->pref);
    for (int k = 0; k < 2 && !have; k++) {
        if (load_rom(cache_path[k], rom, NULL) == 1) { have = from_cache = 1; snprintf(found_path, sizeof found_path, "%s", cache_path[k]); }
        else free(rom->data);
    }
    if (!have) {
        char *base = SDL_GetBasePath();
        char p[4][1100];
        snprintf(p[0], sizeof p[0], "%srom.sms", base ? base : "");
        snprintf(p[1], sizeof p[1], "%srom.gg", base ? base : "");
        snprintf(p[2], sizeof p[2], "rom.sms");
        snprintf(p[3], sizeof p[3], "rom.gg");
        SDL_free(base);
        for (int k = 0; k < 4 && !have; k++) {
            if (load_rom(p[k], rom, NULL) == 1) { have = 1; snprintf(found_path, sizeof found_path, "%s", p[k]); }
            else free(rom->data);
        }
    }
    if (!have) have = find_rom_here(rom, found_path, sizeof found_path);
#endif
    if (!have && !ask_for_rom(app, rom, found_path, sizeof found_path)) return 0;
    snprintf(app->rom_path, sizeof app->rom_path, "%s", found_path);
#ifdef SMS_RECOMP
    /* Copia para las siguientes veces (solo la ROM correcta), con su extensión */
    const char *dot = strrchr(found_path, '.');
    int gg = dot && (!strcmp(dot, ".gg") || !strcmp(dot, ".GG"));
    if (!from_cache && !app->interp_only && !u8_write_file(cache_path[gg], rom->data, rom->size))
        fprintf(stderr, "no se pudo guardar la copia de la ROM en %s\n", cache_path[gg]);
    sms_force_interp = app->interp_only;
    z80_stat_of = app->sms;             /* para «Este juego»: cuánto del Z80 corre nativo */
#else
    set_game(app, found_path);
#endif
    SDL_SetWindowTitle(app->win, app->title);

    if (play_path) {
        const char *ierr = inputs_load(&app->script, play_path);
        if (ierr) fail(ierr);
        app->play = 1;
    }
    app->bar = (MenuBar){ menus, 0, menu_state, app, -1, -1, 0, 0 };
    build_menus(app);
    power_on(app);
    if (record_path && !(app->record = u8_fopen(record_path, "w"))) fail(tr(T_RECORD_FAILED));
    if (app->record) fprintf(app->record, "# SMSRecomp: cuadro jugador1 jugador2\n");

    if (ui_shot_path) {
        base_size(app, &bw, &bh);
        int sw = bw * app->cfg.scale, sh = bh * app->cfg.scale + bar_space(app, ui_scale(sw));
        ui_shot_surface = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (ui_shot_surface) app->ren = SDL_CreateSoftwareRenderer(ui_shot_surface);
        if (!app->ren) fail(SDL_GetError());
    }
    resize_window(app);
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (start_full && !ui_shot_path) toggle_fullscreen(app);

    if (!ui_shot_path) player_audio_open(&app->audio, AUDIO_RATE);

    if (ui_shot_path) {
        for (long i = 0; i < shot_frames; i++) step(app, 0);
        if (shot_action) do_action(app, shot_action);
        if (shot_menu >= 0 && shot_menu < app->bar.count) {
            app->bar.open = shot_menu;
            app->bar.hover = -1;
        }
        render(app);
        app->running = 0;
    } else {
        app->running = 1;
    }

#ifdef __EMSCRIPTEN__
    if (app->running) {
        emscripten_set_main_loop_arg(web_frame, app, 0, 1);    /* no vuelve */
        return 0;
    }
#endif
    const double frame_seconds = 1.0 / NTSC_FPS;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();

    while (app->running) {
        frame_input(app);
        if (!app->running) break;
        frame_run(app);
        frame_draw(app);

        /* Ritmo: 59.92 cuadros por segundo (sin límite en avance rápido) */
        if (app->fast) { next = SDL_GetPerformanceCounter(); continue; }
        next += (Uint64)(frame_seconds * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) {
            Uint32 ms = (Uint32)((next - now) * 1000 / freq);
            if (ms > 1) SDL_Delay(ms - 1);
            while (SDL_GetPerformanceCounter() < next) {}
        } else if (now - next > freq / 4) {
            next = now;     /* muy atrasados (ventana movida, etc.): reiniciar */
        }
    }

    finish(app, !shot_mode);
    return 0;
}
