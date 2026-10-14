/*
 * Capa de plataforma con SDL2 para GENRecomp: ventana con barra de menús, controles (teclado y
 * mandos), sonido estéreo a 44 100 Hz, estados guardados y ritmo de 59.92 cuadros por segundo. La
 * usan el intérprete (`gen`) y los juegos que genera genrecomp (con -DGEN_RECOMP).
 *
 * Uso: gen [rom.bin] [--scale N] [--tmss] [--record archivo | --play archivo] [--trace archivo]
 * Recompilado: juego [rom.bin] [--interp] [--force] [--uncovered archivo] y las mismas opciones. La
 *   ROM se busca en el argumento, en la copia que se guarda la primera vez, en rom.bin junto al
 *   ejecutable o en la carpeta actual, y si no, se pide; debe tener el SHA1 que se recompiló.
 *   --interp     solo el intérprete (para comparar)
 *   --force      acepta otra ROM (corre con el intérprete)
 *   --uncovered  como --trace: lo que el intérprete tuvo que ejecutar (para genrecomp --trace)
 *   --tmss    consola con TMSS (el juego debe escribir "SEGA" en $A14000 antes de tocar el VDP)
 *   --record  graba los botones de los dos jugadores en un archivo de texto (ver inputs.h)
 *   --play    reproduce un archivo grabado (el teclado se ignora)
 *   --trace   al salir, agrega a `archivo` las instrucciones de ROM que se ejecutaron (para genrecomp)
 * Para pruebas sin ventana: --ui-shot archivo.png [--ui-frames N] [--ui-menu N] [--ui-action N].
 *
 * Jugador 1: flechas, Z X C = A B C, Enter = Start, A S D = X Y Z, Mayús = Mode. Jugador 2: I J K L,
 * V B N = A B C, H = Start. Mandos: el primero es el jugador 1 y el segundo el 2.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\GENRecomp\<juego>\, en Linux
 * ~/.local/share/GENRecomp/<juego>/, en macOS ~/Library/Application Support/GENRecomp/<juego>/.
 * Ahí van la partida (<rom>.sav: SRAM o EEPROM; se guarda al salir y cada 5 segundos si cambió), los
 * estados guardados (<rom>.estado1-4), las preferencias (settings.ini) y lo exportado.
 *
 * La barra de menús y los diálogos vienen de Moskva 89 (ui_menu.c, ui_font.c): se dibujan dentro
 * de la ventana para verse igual en todos los sistemas. Con un menú o un diálogo abierto, el juego
 * se pausa.
 */
#include <SDL.h>

#ifdef GEN_RECOMP
#include <dirent.h>
#include "recomp.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "export.h"
#include "files.h"
#include "gen.h"
#include "i18n.h"
#include "icono.h"
#include "inputs.h"
#include "png.h"
#include "settings.h"
#include "sha1.h"
#include "ui_font.h"
#include "ui_menu.h"
#include "player_util.h"

#define NTSC_FPS (53693175.0 / (MCLK_PER_LINE * LINES_NTSC))      /* 59.92 */

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
    A_SOUND, A_VOLUME_25, A_VOLUME_100 = A_VOLUME_25 + 3, A_CHAN_0, A_CHAN_6 = A_CHAN_0 + 6,
    A_PAD_AUTO, A_PAD_3, A_PAD_6,
    A_SCREENSHOT, A_OPEN_EXPORTS,
    A_HELP_CONTROLS, A_HELP_GAME, A_ABOUT,
    A_LANGUAGE_0,
};

enum { TAG_INFO = 1 };

typedef struct {
    Gen *gen;
    Rom rom;                /* se conserva para reiniciar */
    int loaded;
    int tmss;
    int interp_only;        /* recompilado: solo el intérprete (--interp, o la ROM es otra) */

    char name[96];          /* nombre del juego (archivos y título) */
    char title[96];         /* nombre para mostrar */
    char rom_path[1024];

    int paused, hold_input, fullscreen, running, mouse_y, fast;
    Settings cfg;
    char *pref;
    char settings_path[1024], save_path[1024], export_dir[1100];

    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tex_w, tex_h;
    SDL_GameController *pads[2];
    SDL_AudioDeviceID audio;
    int audio_was_running;

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

/* Las funciones de la ventana iguales en los cuatro recompiladores (retrorecomp/shared) */
static void resize_window(App *app);
#include "player_common.h"

static App *the_app;

#define PLAYER_NAME "GENRecomp"
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

/* 1: ROM buena; 0: (recompilado) es otra ROM; -1: ilegible; -2: no es una ROM que sepamos correr
 * (el motivo en *why). El SHA1 es el de la ROM ya normalizada: .bin y .smd del mismo juego sirven. */
static int load_rom(const char *path, Rom *rom, const char **why)
{
    const char *unused;
    if (!why) why = &unused;
    rom->data = u8_read_file(path, &rom->size);
    if (!rom->data) return -1;
    size_t n;
    int fmt;
    u8 *img = cart_normalize(rom->data, rom->size, &n, &fmt, why);
    if (!img) return -2;
#ifdef GEN_RECOMP
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
#ifdef GEN_RECOMP
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
        int got = ask_rom_path(tr(first ? T_ROM_CHOOSE : T_ROM_CHOOSE_AGAIN), path_out, path_size);
        if (!got) {
            snprintf(msg, sizeof msg, tr(T_ROM_DROP_FMT), "Genesis");
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
    if (!app->loaded || !gen_sram_dirty(app->gen)) return 1;
    return gen_sram_save(app->gen, app->save_path);
}

static void apply_pad(App *app)
{
    Gen *g = app->gen;
    if (app->cfg.pad == 3) g->pad6 = 0;
    else if (app->cfg.pad == 6) g->pad6 = 3;
    else g->pad6 = strchr(g->cart.hdr.io, '6') ? 3 : 0;
}

/* Enciende (o reinicia) la consola con la ROM actual */
static void power_on(App *app)
{
    const char *err = NULL;
    if (app->loaded) gen_free(app->gen);
    if (gen_load(app->gen, app->rom.data, app->rom.size, &err)) fail(err);
    app->loaded = 1;
    if (app->tmss) { app->gen->tmss = 1; gen_reset(app->gen); }
    apply_pad(app);
    gen_sram_load(app->gen, app->save_path);
    if (app->trace_path) { gen_trace_start(app->gen); gen_trace_load(app->gen, app->trace_path); }
    app->frame = 0;
    app->last_pad[0] = app->last_pad[1] = -1;
    if (app->script.n) app->script.pos = 0;
}

static void write_trace(App *app)
{
    if (app->trace_path && app->gen->trace) gen_trace_write(app->gen, app->trace_path);
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
    write_trace(app);
    free(app->rom.data);
    app->rom = rom;
    snprintf(app->rom_path, sizeof app->rom_path, "%s", path);
#ifndef GEN_RECOMP
    set_game(app, path);
#endif
    power_on(app);
    app->win_title[0] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Imagen                                                              */
/* ------------------------------------------------------------------ */

/* Tamaño de la imagen del juego (cambia con el modo: 256 o 320 de ancho) */
static void game_size(const App *app, int *w, int *h)
{
    *w = app->loaded ? app->gen->vdp.width : 320;
    *h = app->loaded ? app->gen->vdp.height : 224;
}

/* Ancho en pantalla: con el aspecto corregido, la imagen ocupa un 4:3 (como en un televisor) */
static double display_w(const App *app, int gw, int gh)
{
    return app->cfg.aspect ? gh * 4.0 / 3.0 : gw;
}

static void resize_window(App *app)
{
    if (app->fullscreen) return;
    int w = (int)(display_w(app, 320, 224) * app->cfg.scale + 0.5);
    SDL_SetWindowSize(app->win, w, 224 * app->cfg.scale + menubar_height(ui_scale(w)));
}

/* Convierte la imagen a ARGB */
static void compose(const App *app, Uint32 *dst, int gw, int gh)
{
    const u16 *fb = app->gen->vdp.fb;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) dst[y * gw + x] = vdp_argb(fb[y * FB_W + x]);
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
    u8 *data = gen_state_save(app->gen, &size);
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
    if (gen_state_load(app->gen, data, size)) {
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

static MenuItem menu_game[16], menu_video[12], menu_audio[16], menu_input[4], menu_export[4], menu_language[LANG_COUNT], menu_help[4];
static Menu menus[7];
static char size_labels[6][40], volume_labels[4][40], chan_labels[6][40], slot_labels[4][40];

static void build_menus(App *app)
{
    int n = 0, k = 0;
#ifndef GEN_RECOMP
    ITEM(menu_game, n, tr(T_OPEN_ROM), "Ctrl+O", A_OPEN_ROM, ITEM_ACTION);
    SEPARATOR(menu_game, n);
#endif
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
    for (int i = 0; i < 6; i++) {
        snprintf(chan_labels[i], sizeof chan_labels[i], tr(T_CHAN_FM_FMT), i + 1);
        ITEM(menu_audio, n, chan_labels[i], NULL, A_CHAN_0 + i, ITEM_CHECK);
    }
    ITEM(menu_audio, n, tr(T_CHAN_PSG), NULL, A_CHAN_6, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_AUDIO), menu_audio, n };

    n = 0;
    ITEM(menu_input, n, tr(T_PAD_AUTO), NULL, A_PAD_AUTO, ITEM_RADIO);
    ITEM(menu_input, n, tr(T_PAD_3), NULL, A_PAD_3, ITEM_RADIO);
    ITEM(menu_input, n, tr(T_PAD_6), NULL, A_PAD_6, ITEM_RADIO);
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
    case A_INTEGER:      *checked = app->cfg.integer; return 1;
    case A_ASPECT:       *checked = app->cfg.aspect; return 1;
    case A_PAD_AUTO:     *checked = app->cfg.pad == 0; return 1;
    case A_PAD_3:        *checked = app->cfg.pad == 3; return 1;
    case A_PAD_6:        *checked = app->cfg.pad == 6; return 1;
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
        if (action >= A_CHAN_0 && action <= A_CHAN_6) {
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
    if (ask_rom_path(tr(T_ROM_CHOOSE), path, sizeof path)) switch_rom(app, path);
    else message(app, tr(T_OPEN_ROM), tr(T_ROM_CHOOSE));   /* sin diálogo nativo: se arrastra */
}

static void game_info(App *app)
{
    char text[2048], folder[64], saves[64];
    const Cart *c = &app->gen->cart;
    size_t len = strlen(app->pref);
    /* Una ruta larga no cabe en el diálogo: se deja el final */
    if (len > 44) snprintf(folder, sizeof folder, "…%s", app->pref + len - 43);
    else snprintf(folder, sizeof folder, "%s", app->pref);
    if (c->has_eeprom) snprintf(saves, sizeof saves, "EEPROM %u bytes", (unsigned)c->sram_size);
    else if (c->sram) snprintf(saves, sizeof saves, "SRAM $%06X-$%06X", c->sram_start, c->sram_end);
    else snprintf(saves, sizeof saves, "-");
    snprintf(text, sizeof text, tr(T_GAME_TEXT_FMT), c->hdr.name_int[0] ? c->hdr.name_int : app->title,
             c->hdr.serial, c->hdr.region, (int)(c->rom_size / 1024), c->mapper == MAPPER_SSF2 ? ", SSF2" : "",
             saves, c->sha1, folder);
#ifdef GEN_RECOMP
    size_t tl = strlen(text);
    const Gen *g = app->gen;
    if (app->interp_only) snprintf(text + tl, sizeof text - tl, "\n\n%s", tr(T_RECOMP_OFF));
    else snprintf(text + tl, sizeof text - tl, tr(T_RECOMP_STATS_FMT), recomp_instructions,
                  g->cyc ? 100.0 * (double)g->cyc_recomp / (double)g->cyc : 0.0);
#endif
    message(app, tr(T_GAME_TITLE), text);
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
    case A_INTEGER:      app->cfg.integer = !app->cfg.integer; break;
    case A_ASPECT:       app->cfg.aspect = !app->cfg.aspect; resize_window(app); break;
    case A_PAD_AUTO:     app->cfg.pad = 0; apply_pad(app); break;
    case A_PAD_3:        app->cfg.pad = 3; apply_pad(app); break;
    case A_PAD_6:        app->cfg.pad = 6; apply_pad(app); break;
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
        } else if (action >= A_CHAN_0 && action <= A_CHAN_6) {
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
#ifndef GEN_RECOMP
    case SDLK_o:      if (ctrl && !replay) { do_action(app, A_OPEN_ROM); return 1; } break;
#endif
    case SDLK_F5:     do_action(app, A_SAVE_STATE); return 1;
    case SDLK_F8:     if (!replay) { do_action(app, A_LOAD_STATE); return 1; } break;
    case SDLK_m:      if (ctrl) { do_action(app, A_SOUND); return 1; } break;
    case SDLK_p:      do_action(app, A_PAUSE); return 1;
    case SDLK_F11:    do_action(app, A_FULLSCREEN); return 1;
    case SDLK_RETURN: if (alt) { do_action(app, A_FULLSCREEN); return 1; } break;
    case SDLK_F1:     do_action(app, A_HELP_CONTROLS); return 1;
    case SDLK_F12:    do_action(app, A_SCREENSHOT); return 1;
    default: break;
    }
    return 0;
}

/* ¿Se ve la barra? En pantalla completa, solo al acercar el ratón arriba */
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
    if (bar_visible(app) && menubar_event(&app->bar, e, ui, &action)) {
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

/* Mando de SDL (disposición de Xbox): X A B = A B C en la fila de abajo, LB Y RB = X Y Z arriba */
static u16 read_pad(SDL_GameController *pad)
{
    if (!pad) return 0;
    u16 r = 0;
    #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
    if (BTN(X)) r |= GEN_A;
    if (BTN(A)) r |= GEN_B;
    if (BTN(B)) r |= GEN_C;
    if (BTN(LEFTSHOULDER)) r |= GEN_X;
    if (BTN(Y)) r |= GEN_Y;
    if (BTN(RIGHTSHOULDER)) r |= GEN_Z;
    if (BTN(BACK)) r |= GEN_MODE;
    if (BTN(START)) r |= GEN_START;
    if (BTN(DPAD_UP)) r |= GEN_UP;
    if (BTN(DPAD_DOWN)) r |= GEN_DOWN;
    if (BTN(DPAD_LEFT)) r |= GEN_LEFT;
    if (BTN(DPAD_RIGHT)) r |= GEN_RIGHT;
    #undef BTN
    int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -16000) r |= GEN_LEFT;
    if (lx > 16000) r |= GEN_RIGHT;
    if (ly < -16000) r |= GEN_UP;
    if (ly > 16000) r |= GEN_DOWN;
    return r;
}

static void player_pads(App *app, u16 pads[2])
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u16 p1 = 0, p2 = 0;
    if (k[SDL_SCANCODE_Z]) p1 |= GEN_A;
    if (k[SDL_SCANCODE_X]) p1 |= GEN_B;
    if (k[SDL_SCANCODE_C]) p1 |= GEN_C;
    if (k[SDL_SCANCODE_A]) p1 |= GEN_X;
    if (k[SDL_SCANCODE_S]) p1 |= GEN_Y;
    if (k[SDL_SCANCODE_D]) p1 |= GEN_Z;
    if (k[SDL_SCANCODE_RETURN]) p1 |= GEN_START;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) p1 |= GEN_MODE;
    if (k[SDL_SCANCODE_UP]) p1 |= GEN_UP;
    if (k[SDL_SCANCODE_DOWN]) p1 |= GEN_DOWN;
    if (k[SDL_SCANCODE_LEFT]) p1 |= GEN_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) p1 |= GEN_RIGHT;
    if (k[SDL_SCANCODE_V]) p2 |= GEN_A;
    if (k[SDL_SCANCODE_B]) p2 |= GEN_B;
    if (k[SDL_SCANCODE_N]) p2 |= GEN_C;
    if (k[SDL_SCANCODE_H]) p2 |= GEN_START;
    if (k[SDL_SCANCODE_I]) p2 |= GEN_UP;
    if (k[SDL_SCANCODE_K]) p2 |= GEN_DOWN;
    if (k[SDL_SCANCODE_J]) p2 |= GEN_LEFT;
    if (k[SDL_SCANCODE_L]) p2 |= GEN_RIGHT;
    pads[0] = p1 | read_pad(app->pads[0]);
    pads[1] = p2 | read_pad(app->pads[1]);
    /* No pueden ir izquierda y derecha (ni arriba y abajo) a la vez en un mando de verdad */
    for (int i = 0; i < 2; i++) {
        if ((pads[i] & (GEN_LEFT | GEN_RIGHT)) == (GEN_LEFT | GEN_RIGHT)) pads[i] &= (u16)~(GEN_LEFT | GEN_RIGHT);
        if ((pads[i] & (GEN_UP | GEN_DOWN)) == (GEN_UP | GEN_DOWN)) pads[i] &= (u16)~(GEN_UP | GEN_DOWN);
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
    u16 pads[2];
    player_pads(app, pads);
    if (app->play) {
        pads[0] = inputs_pad(&app->script, app->frame, 0);
        pads[1] = inputs_pad(&app->script, app->frame, 1);
    }
    if (app->record) inputs_record(app->record, app->frame, pads, app->last_pad);
    app->gen->pad[0] = pads[0];
    app->gen->pad[1] = pads[1];
    app->gen->audio.mute = (u8)app->cfg.mute;
    app->frame++;
    gen_run_frame(app->gen);
    /* Guardado automático cada 5 segundos si la RAM del cartucho cambió */
    if (app->frame % 300 == 0 && gen_sram_dirty(app->gen)) save_game(app);

    static int16_t samples[AUDIO_BINS * 2];
    int n = gen_audio_take(app->gen, samples, AUDIO_BINS);
    if (!app->audio || !app->cfg.sound || !audible) return;
    int vol = app->cfg.volume;
    if (vol != 100) for (int i = 0; i < n * 2; i++) samples[i] = (int16_t)(samples[i] * vol / 100);
    /* Se mantiene una cola corta: si el audio se adelanta (relojes distintos), se descarta un cuadro */
    Uint32 queued = SDL_GetQueuedAudioSize(app->audio) / 4;
    if (queued < AUDIO_RATE / 10) SDL_QueueAudio(app->audio, samples, (Uint32)n * 4);
}

static void update_title(App *app)
{
    char t[256];
    const char *detail = "";
    if (app->notice[0] && SDL_TICKS_PASSED(SDL_GetTicks(), app->notice_until)) app->notice[0] = 0;
    if (app->notice[0]) detail = app->notice;
    else if (app->gen->tmss_locked) detail = "TMSS";
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
    int bar = app->fullscreen ? 0 : menubar_height(ui);   /* en pantalla completa va encima */

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

/* Icono de la ventana: el mando de Genesis, recortado al dibujo y centrado en un cuadrado */
#ifdef GEN_RECOMP
/* Una ROM de la carpeta actual con el SHA1 esperado */
static int find_rom_here(Rom *rom, char *path_out, size_t size)
{
    DIR *d = opendir(".");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (SDL_strcasecmp(dot, ".bin") && SDL_strcasecmp(dot, ".md") && SDL_strcasecmp(dot, ".gen") &&
                     SDL_strcasecmp(dot, ".smd"))) continue;
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

int main(int argc, char *argv[])
{
    static App app_storage;
    App *app = &app_storage;
    const char *rom_arg = NULL, *record_path = NULL, *play_path = NULL;
    int scale_arg = 0, shot_menu = -1, shot_action = 0, force = 0;
    long shot_frames = 300;
    char msg[4096];
    the_app = app;
    app->gen = calloc(1, sizeof *app->gen);
    if (!app->gen) fail("sin memoria");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail(SDL_GetError());

    /* Carpeta de datos: partida y preferencias */
#ifdef GEN_RECOMP
    app->pref = SDL_GetPrefPath("GENRecomp", recomp_name);
#else
    app->pref = SDL_GetPrefPath("GENRecomp", "interprete");
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
#ifdef GEN_RECOMP
    set_game(app, recomp_name);
#else
    set_game(app, "gen");
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tmss")) app->tmss = 1;
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

    int w0 = (int)(display_w(app, 320, 224) * app->cfg.scale + 0.5);
    app->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                w0, 224 * app->cfg.scale,
                                SDL_WINDOW_RESIZABLE | (ui_shot_path ? SDL_WINDOW_HIDDEN : 0));
    if (!app->win) fail(SDL_GetError());
    /* SDL2 empieza con la entrada de texto activa; el juego solo lee teclas. Apagarla evita que
     * macOS conecte el método de entrada (InputMethodKit) y deje avisos en la consola */
    SDL_StopTextInput();
    set_window_icon(app);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (ui_shot_path) {
        /* Captura de la interfaz: se dibuja en una superficie del tamaño de la ventana */
        int sh = 224 * app->cfg.scale + menubar_height(ui_scale(w0));
        ui_shot_surface = SDL_CreateRGBSurfaceWithFormat(0, w0, sh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (ui_shot_surface) app->ren = SDL_CreateSoftwareRenderer(ui_shot_surface);
    } else {
        app->ren = SDL_CreateRenderer(app->win, -1, SDL_RENDERER_ACCELERATED);
        if (!app->ren) app->ren = SDL_CreateRenderer(app->win, -1, 0);
    }
    if (!app->ren) fail(SDL_GetError());
    SDL_SetWindowMinimumSize(app->win, 320, 224 + menubar_height(1));
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 0xFF);
    SDL_RenderClear(app->ren);
    SDL_RenderPresent(app->ren);

    /* ROM: el argumento; recompilado: la copia guardada, rom.bin junto al ejecutable o aquí, o una
     * ROM con el SHA1 correcto en esta carpeta; si no, pedirla */
    Rom *rom = &app->rom;
    char found_path[1024] = "";
    int have = 0, from_cache = 0;
    (void)from_cache;
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
#ifdef GEN_RECOMP
    char cache_path[1100];
    snprintf(cache_path, sizeof cache_path, "%srom.bin", app->pref);
    if (!have) {
        if (load_rom(cache_path, rom, NULL) == 1) { have = from_cache = 1; snprintf(found_path, sizeof found_path, "%s", cache_path); }
        else free(rom->data);
    }
    if (!have) {
        char *base = SDL_GetBasePath();
        char path[1100];
        snprintf(path, sizeof path, "%srom.bin", base ? base : "");
        SDL_free(base);
        const char *cands[2] = { path, "rom.bin" };
        for (int k = 0; k < 2 && !have; k++) {
            if (load_rom(cands[k], rom, NULL) == 1) { have = 1; snprintf(found_path, sizeof found_path, "%s", cands[k]); }
            else free(rom->data);
        }
    }
    if (!have) have = find_rom_here(rom, found_path, sizeof found_path);
#endif
    if (!have && !ask_for_rom(app, rom, found_path, sizeof found_path)) return 0;
    snprintf(app->rom_path, sizeof app->rom_path, "%s", found_path);
#ifdef GEN_RECOMP
    /* Copia para las siguientes veces (solo la ROM correcta) */
    if (!from_cache && !app->interp_only && !u8_write_file(cache_path, rom->data, rom->size))
        fprintf(stderr, "no se pudo guardar la copia de la ROM en %s\n", cache_path);
    gen_force_interp = app->interp_only;
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
    if (app->record) fprintf(app->record, "# GENRecomp: cuadro jugador1 jugador2\n");

    resize_window(app);
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_AudioSpec want, have_spec;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    if (!ui_shot_path) app->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have_spec, 0);
    if (app->audio) SDL_PauseAudioDevice(app->audio, 0);

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

    const double frame_seconds = 1.0 / NTSC_FPS;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();

    while (app->running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handle_event(app, &ev);
        if (!app->running) break;

        int running = game_running(app);
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        app->fast = running && keys[SDL_SCANCODE_TAB];
        if (running) {
            if (!app->audio_was_running && app->audio) SDL_ClearQueuedAudio(app->audio);
            if (app->fast) {
                for (int i = 0; i < 4; i++) step(app, 0);       /* avance rápido: sin sonido */
                if (app->audio) SDL_ClearQueuedAudio(app->audio);
            } else step(app, 1);
        } else if (app->audio_was_running && app->audio) SDL_ClearQueuedAudio(app->audio);
        app->audio_was_running = running;
        update_title(app);
        render(app);

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

    if (!shot_mode) settings_save(&app->cfg, app->settings_path);
    if (app->record) fclose(app->record);
    write_trace(app);
    if (!save_game(app)) {
        snprintf(msg, sizeof msg, tr(T_SAVE_FAILED_FMT), app->save_path);
        fail(msg);
    }
    if (app->audio) SDL_CloseAudioDevice(app->audio);
    for (int i = 0; i < 2; i++)
        if (app->pads[i]) SDL_GameControllerClose(app->pads[i]);
    if (app->tex) SDL_DestroyTexture(app->tex);
    SDL_DestroyRenderer(app->ren);
    SDL_DestroyWindow(app->win);
    SDL_free(app->pref);
    SDL_Quit();
    gen_free(app->gen);
    free(app->gen);
    free(rom->data);
    return 0;
}
