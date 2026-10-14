/*
 * Capa de plataforma con SDL2 para NEORecomp: ventana con barra de menús, controles (teclado y
 * mandos), sonido estéreo a 44 100 Hz, tarjeta de memoria, estados guardados y ritmo de 59,60 cuadros
 * por segundo (el del AES). La usan el intérprete (`neo`) y los juegos que genera neorecomp (con
 * -DNEO_RECOMP).
 *
 * Uso: neo [juego] [--bios archivo] [--scale N] [--fullscreen] [--no-menu] [--record archivo | --play archivo]
 *          [--trace archivo]
 *   juego    un .neo, un .zip al estilo MAME o una carpeta con los chips (si no, se pide)
 *   --bios   la BIOS del AES: neo-epo.bin, neo-po.bin, el aes.zip de MAME o la propia (neobios.bin). Si
 *            no se da, se busca (variable NEO_BIOS; la propia y luego la de SNK, junto al juego, roms/;
 *            la copia de la carpeta de datos) o se pide, y
 *            se guarda una copia en la carpeta de datos para las siguientes veces
 *   --fullscreen  arranca en pantalla completa
 *   --no-menu     sin barra de menús (ni con el ratón, Alt o F10); los atajos siguen
 *   --record graba los botones de los dos jugadores en un archivo de texto (ver inputs.h)
 *   --play   reproduce un archivo grabado (el teclado se ignora)
 *   --trace  al salir, agrega a `archivo` las instrucciones de ROM que se ejecutaron (para neorecomp)
 * Recompilado: juego [juego] [--interp] [--force] [--uncovered archivo] y las mismas opciones. El juego
 *   se busca en el argumento, en la copia que se guarda la primera vez (juego.neo en la carpeta de
 *   datos) o en la carpeta actual (un .neo o un .zip con el SHA1 correcto), y si no, se pide.
 *   --interp     solo el intérprete (para comparar)
 *   --force      acepta otro juego (corre con el intérprete)
 *   --uncovered  como --trace: lo que el intérprete tuvo que ejecutar (para neorecomp --trace)
 * Para pruebas sin ventana: --ui-shot archivo.png [--ui-frames N] [--ui-menu N] [--ui-action N].
 *
 * Jugador 1: flechas, Z X C V = A B C D, Enter = Start, Mayús = Select. Jugador 2: I J K L,
 * B N M , = A B C D, H = Start, G = Select. Mandos: el primero es el jugador 1 y el segundo el 2.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\NEORecomp\<juego>\, en Linux
 * ~/.local/share/NEORecomp/<juego>/, en macOS ~/Library/Application Support/NEORecomp/<juego>/ (el
 * intérprete usa "interprete").
 * Ahí van la copia de la BIOS (bios.bin), la tarjeta de memoria de cada juego (<juego>.tarjeta: se
 * guarda al salir y cada 5 segundos si cambió), los estados guardados (<juego>.estado1-4), las
 * preferencias (settings.ini) y lo exportado.
 *
 * La barra de menús y los diálogos vienen de Moskva 89 (ui_menu.c, ui_font.c): se dibujan dentro
 * de la ventana para verse igual en todos los sistemas. Con un menú o un diálogo abierto, el juego
 * se pausa.
 */
#include <SDL.h>

#ifdef NEO_RECOMP
#include <dirent.h>
#include "recomp.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "export.h"
#include "files.h"
#include "neo.h"
#include "i18n.h"
#include "icono.h"
#include "inputs.h"
#include "png.h"
#include "settings.h"
#include "ui_font.h"
#include "ui_menu.h"
#include "player_util.h"

#define AES_FPS (24167829.0 / (M68K_PER_FRAME * MCLK_PER_M68K))      /* 59.60 */

static void fail(const char *msg);

/* ------------------------------------------------------------------ */
/* Estado de la aplicación                                             */
/* ------------------------------------------------------------------ */

enum {
    A_NONE, A_OPEN_ROM, A_RESET, A_PAUSE, A_CARD, A_QUIT,
    A_SCALE_1, A_SCALE_6 = A_SCALE_1 + 5, A_FULLSCREEN, A_ASPECT, A_INTEGER,
    A_SCREENSHOT, A_OPEN_EXPORTS,
    A_HELP_CONTROLS, A_HELP_GAME, A_ABOUT,
    A_SOUND, A_VOLUME_25, A_VOLUME_100 = A_VOLUME_25 + 3, A_CHAN_0, A_CHAN_6 = A_CHAN_0 + 6,
    A_SAVE_STATE, A_LOAD_STATE, A_SLOT_1, A_SLOT_4 = A_SLOT_1 + 3,
    A_HIDE_MENU,            /* antes de A_LANGUAGE_0 para no mover los números que prueba ui_golden */
    A_LANGUAGE_0,
};

enum { TAG_INFO = 1 };

typedef struct {
    Neo *neo;
    Bios bios;              /* se conserva al cambiar de juego */
    int loaded;
    int interp_only;        /* recompilado: solo el intérprete (--interp, o el juego es otro) */

    char name[96];          /* nombre del juego (archivos y título) */
    char title[96];         /* nombre para mostrar */
    char rom_path[1024];

    int paused, hold_input, fullscreen, running, mouse_y, fast;
    int menu_hidden;        /* MENU_SHOWN, MENU_HIDDEN o MENU_OFF (player_common.h) */
    Settings cfg;
    char *pref;
    char settings_path[1024], card_path[1024], bios_copy[1024], export_dir[1100];

    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
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

#define PLAYER_NAME "NEORecomp"
#include "player_rom.h"
static const char *ui_shot_path;
static SDL_Surface *ui_shot_surface;   /* --ui-shot: se dibuja en una superficie, sin ventana */

/* ------------------------------------------------------------------ */
/* Nombres y carpetas                                                  */
/* ------------------------------------------------------------------ */

/* Nombre del juego (para archivos y título) a partir del de su archivo, sin la extensión */
static void set_game(App *app, const char *path, const char *title)
{
    snprintf(app->name, sizeof app->name, "%s", file_name(path));
    char *dot = strrchr(app->name, '.');
    if (dot && dot != app->name) *dot = 0;
    for (char *c = app->name; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' ||
            *c == '>' || *c == '|') *c = '_';
    snprintf(app->title, sizeof app->title, "%s", title && *title ? title : app->name);
    snprintf(app->card_path, sizeof app->card_path, "%s%s.tarjeta", app->pref, app->name);
}

/* ------------------------------------------------------------------ */
/* Juego y BIOS                                                        */
/* ------------------------------------------------------------------ */

/* Espera a que arrastren un archivo a la ventana (o a que la cierren) */
/* Pide un archivo con el diálogo nativo o arrastrándolo. 0 si el usuario cancela */
static int ask_file(const char *choose, const char *drop, int first, char *out, size_t size)
{
    if (ask_rom_path(choose, out, size)) return 1;
    if (first) inform(drop);
    return wait_for_drop(out, size);
}

/* La BIOS: --bios, la búsqueda del runtime (NEO_BIOS, junto al juego, roms/), la copia de la carpeta
 * de datos, o se pide. La que no venía de la copia se guarda ahí */
static int get_bios(App *app, const char *arg, const char *rom_path)
{
    const char *err = NULL;
    static u8 l0[L0_SIZE];
    l0_generate(l0);
    if (arg) {
        if (bios_load(&app->bios, arg, l0, &err)) fail(err);
    } else if (bios_find(&app->bios, rom_path, l0, &err) && bios_load(&app->bios, app->bios_copy, l0, &err)) {
        char path[1024];
        int first = 1;
        for (;;) {
            if (!ask_file(tr(T_BIOS_CHOOSE), tr(T_BIOS_DROP), first, path, sizeof path)) return 0;
            first = 0;
            if (!bios_load(&app->bios, path, l0, &err)) break;
            char msg[1200];
            snprintf(msg, sizeof msg, tr(T_BIOS_BAD_FMT), err);
            inform(msg);
        }
    }
    if (strcmp(app->bios.path, app->bios_copy) && !u8_write_file(app->bios_copy, app->bios.rom, BIOS_SIZE))
        fprintf(stderr, "no se pudo guardar la copia de la BIOS en %s\n", app->bios_copy);
    return 1;
}

/* Lee un juego. 1 si se puede correr; 0 y el mensaje en msg si no. Recompilado: 2 si es otro juego
 * (el cartucho queda cargado, por si se acepta con --force) */
static int read_game(const char *path, Cart *cart, char *msg, size_t size)
{
    const char *why = NULL;
    if (cart_load_path(cart, path, &why)) { snprintf(msg, size, tr(T_ROM_BAD_FMT), path, why); return 0; }
    int fatal;
    const char *u = cart_unsupported(cart, &fatal);
    if (u && fatal) { snprintf(msg, size, tr(T_ROM_BAD_FMT), path, u); cart_free(cart); return 0; }
#ifdef NEO_RECOMP
    if (strcmp(cart->sha1, recomp_rom_sha1)) {
        snprintf(msg, size, tr(T_ROM_WRONG_FMT), path, recomp_name, recomp_rom_sha1);
        return 2;
    }
#endif
    return 1;
}

/* Pide el juego hasta que den uno que se pueda correr. 0 si el usuario cancela */
static int ask_for_game(Cart *cart, char *path_out, size_t path_size)
{
    char msg[4096], drop[1024];
    snprintf(drop, sizeof drop, tr(T_ROM_DROP_FMT), "Neo Geo");
    for (int first = 1;; first = 0) {
        if (!ask_file(tr(first ? T_ROM_CHOOSE : T_ROM_CHOOSE_AGAIN), drop, first, path_out, path_size)) return 0;
        int r = read_game(path_out, cart, msg, sizeof msg);
        if (r == 1) return 1;
        if (r == 2) cart_free(cart);
        inform(msg);
    }
}

#ifdef NEO_RECOMP
/* Un juego de la carpeta actual (.neo o .zip) con el SHA1 que se recompiló */
static int find_game_here(Cart *cart, char *path_out, size_t size)
{
    DIR *d = opendir(".");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    char msg[2048];
    while (!found && (e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (SDL_strcasecmp(dot, ".neo") && SDL_strcasecmp(dot, ".zip"))) continue;
        int r = read_game(e->d_name, cart, msg, sizeof msg);
        if (r == 1) { snprintf(path_out, size, "%s", e->d_name); found = 1; }
        else if (r == 2) cart_free(cart);
    }
    closedir(d);
    return found;
}
#endif

/* ------------------------------------------------------------------ */
/* Partida                                                             */
/* ------------------------------------------------------------------ */

static void notify(App *app, const char *text);

static int save_card(App *app)
{
    if (!app->loaded || !app->neo->card.data || !app->neo->card.dirty) return 1;
    return neo_card_save(app->neo, app->card_path);
}

static void apply_card(App *app)
{
    Neo *g = app->neo;
    if (app->cfg.card && !g->card.data) neo_card_insert(g, app->card_path);
    else if (!app->cfg.card && g->card.data) {
        save_card(app);
        free(g->card.data);
        g->card.data = NULL;
    }
}

static void write_trace(App *app)
{
    if (app->trace_path && app->loaded && app->neo->trace) neo_trace_write(app->neo, app->trace_path);
}

/* Enciende la consola con el juego (el cartucho pasa a ser de la consola) */
static void power_on(App *app, const Cart *cart)
{
    if (app->loaded) { save_card(app); write_trace(app); neo_free(app->neo); }
    neo_assemble(app->neo, cart, &app->bios);
    app->loaded = 1;
    apply_card(app);
    if (app->trace_path) { neo_trace_start(app->neo); neo_trace_load(app->neo, app->trace_path); }
    app->frame = 0;
    app->last_pad[0] = app->last_pad[1] = -1;
    if (app->script.n) app->script.pos = 0;
}

/* Cambia de juego (arrastrar y soltar o Abrir juego): guarda la tarjeta y enciende el nuevo */
static int switch_game(App *app, const char *path)
{
    static Cart cart;
    char msg[4096];
    int r = read_game(path, &cart, msg, sizeof msg);
    if (r != 1) {
        if (r == 2) cart_free(&cart);
        menubar_close(&app->bar);
        dialog_open(&app->dlg, DIALOG_MESSAGE, TAG_INFO, app->title, msg);
        return 0;
    }
    save_card(app);
    snprintf(app->rom_path, sizeof app->rom_path, "%s", path);
    set_game(app, path, cart.title);
    power_on(app, &cart);
    app->win_title[0] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Imagen                                                              */
/* ------------------------------------------------------------------ */

/* Ancho en pantalla: con el aspecto corregido, la imagen ocupa un 4:3 (como en un televisor) */
static double display_w(const App *app, int gw, int gh)
{
    return app->cfg.aspect ? gh * 4.0 / 3.0 : gw;
}

static void resize_window(App *app)
{
    if (app->fullscreen) return;
    int w = (int)(display_w(app, SCREEN_W, SCREEN_H) * app->cfg.scale + 0.5);
    SDL_SetWindowMinimumSize(app->win, SCREEN_W, SCREEN_H + bar_space(app, 1));
    SDL_SetWindowSize(app->win, w, SCREEN_H * app->cfg.scale + bar_space(app, ui_scale(w)));
}

/* ------------------------------------------------------------------ */
/* Avisos, exportación                                                 */
/* ------------------------------------------------------------------ */

static void screenshot(App *app)
{
    char base[1300], path[1400], text[1500];
    if (!export_base(app, "captura", base, sizeof base)) {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), app->export_dir);
        message(app, tr(T_EXPORT_TITLE), text);
        return;
    }
    snprintf(path, sizeof path, "%s.png", base);
    if (screenshot_write(path, app->neo->fb, SCREEN_W, SCREEN_H)) {
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
    u8 *data = neo_state_save(app->neo, &size);
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
    if (neo_state_load(app->neo, data, size)) {
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

static MenuItem menu_game[16], menu_video[12], menu_audio[16], menu_export[4], menu_language[LANG_COUNT], menu_help[4];
static Menu menus[6];
static char size_labels[6][40], volume_labels[4][40], chan_labels[4][40], slot_labels[4][40];

static void build_menus(App *app)
{
    int n = 0, k = 0;
#ifndef NEO_RECOMP
    ITEM(menu_game, n, tr(T_OPEN_ROM), "Ctrl+O", A_OPEN_ROM, ITEM_ACTION);
    SEPARATOR(menu_game, n);
#endif
    ITEM(menu_game, n, tr(T_RESET), "Ctrl+R", A_RESET, ITEM_ACTION);
    ITEM(menu_game, n, tr(T_PAUSE), "P", A_PAUSE, ITEM_CHECK);
    ITEM(menu_game, n, tr(T_CARD), NULL, A_CARD, ITEM_CHECK);
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
    for (int i = 0; i < 4; i++) {
        snprintf(chan_labels[i], sizeof chan_labels[i], tr(T_CHAN_FM_FMT), i + 1);
        ITEM(menu_audio, n, chan_labels[i], NULL, A_CHAN_0 + i, ITEM_CHECK);
    }
    ITEM(menu_audio, n, tr(T_CHAN_SSG), NULL, A_CHAN_0 + 4, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_ADPCMA), NULL, A_CHAN_0 + 5, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_ADPCMB), NULL, A_CHAN_0 + 6, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_AUDIO), menu_audio, n };

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
    case A_CARD:         *checked = app->cfg.card; return 1;
    case A_FULLSCREEN:   *checked = app->fullscreen; return 1;
    case A_HIDE_MENU:    *checked = app->menu_hidden != MENU_SHOWN; return 1;
    case A_INTEGER:      *checked = app->cfg.integer; return 1;
    case A_ASPECT:       *checked = app->cfg.aspect; return 1;
    case A_SOUND:        *checked = app->cfg.sound; return 1;
    default:
        if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            *checked = app->cfg.volume == 25 * (action - A_VOLUME_25 + 1);
            return app->cfg.sound;
        }
        if (action >= A_CHAN_0 && action <= A_CHAN_6) {
            *checked = !(app->cfg.mute & (1 << (action - A_CHAN_0)));    /* marcado = suena */
            return 1;
        }
        if (action >= A_SLOT_1 && action <= A_SLOT_4) {
            *checked = app->cfg.slot == action - A_SLOT_1 + 1;
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

static void ask_open_game(App *app)
{
    char path[1024];
    if (ask_rom_path(tr(T_ROM_CHOOSE), path, sizeof path)) switch_game(app, path);
    else message(app, tr(T_OPEN_ROM), tr(T_ROM_CHOOSE));   /* sin diálogo nativo: se arrastra */
}

static void game_info(App *app)
{
    char text[2048], folder[64];
    const Cart *c = &app->neo->cart;
    size_t len = strlen(app->pref);
    /* Una ruta larga no cabe en el diálogo: se deja el final */
    if (len > 44) snprintf(folder, sizeof folder, "…%s", app->pref + len - 43);
    else snprintf(folder, sizeof folder, "%s", app->pref);
    snprintf(text, sizeof text, tr(T_GAME_TEXT_FMT), app->title, c->hdr.ngh, (int)(c->size / 1024),
             app->bios.ver ? app->bios.ver->file : "?", app->neo->card.data ? file_name(app->card_path) : "-", c->sha1, folder);
#ifdef NEO_RECOMP
    size_t tl = strlen(text);
    const Neo *g = app->neo;
    if (app->interp_only) snprintf(text + tl, sizeof text - tl, "\n\n%s", tr(T_RECOMP_OFF));
    else snprintf(text + tl, sizeof text - tl, tr(T_RECOMP_STATS_FMT), recomp_instructions,
                  g->cyc ? 100.0 * (double)g->cyc_recomp / (double)g->cyc : 0.0);
#endif
    message(app, tr(T_GAME_TITLE), text);
}

static void do_action(App *app, int action)
{
    switch (action) {
    case A_OPEN_ROM:     ask_open_game(app); break;
    case A_RESET:        save_card(app); neo_reset(app->neo); break;
    case A_PAUSE:        app->paused = !app->paused; break;
    case A_CARD:
        app->cfg.card = !app->cfg.card;
        apply_card(app);
        notify(app, tr(app->cfg.card ? T_TITLE_CARD_IN : T_TITLE_CARD_OUT));
        break;
    case A_SAVE_STATE:   save_state(app); break;
    case A_LOAD_STATE:   load_state(app); break;
    case A_QUIT:         app->running = 0; break;
    case A_FULLSCREEN:   toggle_fullscreen(app); break;
    case A_HIDE_MENU:    toggle_menu(app); if (app->menu_hidden != MENU_OFF) app->cfg.hide_menu = app->menu_hidden == MENU_HIDDEN; break;
    case A_INTEGER:      app->cfg.integer = !app->cfg.integer; break;
    case A_ASPECT:       app->cfg.aspect = !app->cfg.aspect; resize_window(app); break;
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
#ifndef NEO_RECOMP
    case SDLK_o:      if (ctrl && !replay) { do_action(app, A_OPEN_ROM); return 1; } break;
#endif
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
#ifndef NEO_RECOMP
        if (!(app->play || app->record)) switch_game(app, e->drop.file);
#endif
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

/* Mando de SDL (disposición de Xbox): A B X Y = A B C D (como el arco del mando del AES, de abajo a
 * arriba), Start = Start, Back = Select */
static u16 read_pad(SDL_GameController *pad)
{
    if (!pad) return 0;
    u16 r = 0;
    #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
    if (BTN(A)) r |= NEO_A;
    if (BTN(B)) r |= NEO_B;
    if (BTN(X)) r |= NEO_C;
    if (BTN(Y)) r |= NEO_D;
    if (BTN(BACK)) r |= NEO_SELECT;
    if (BTN(START)) r |= NEO_START;
    if (BTN(DPAD_UP)) r |= NEO_UP;
    if (BTN(DPAD_DOWN)) r |= NEO_DOWN;
    if (BTN(DPAD_LEFT)) r |= NEO_LEFT;
    if (BTN(DPAD_RIGHT)) r |= NEO_RIGHT;
    #undef BTN
    int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -16000) r |= NEO_LEFT;
    if (lx > 16000) r |= NEO_RIGHT;
    if (ly < -16000) r |= NEO_UP;
    if (ly > 16000) r |= NEO_DOWN;
    return r;
}

static void player_pads(App *app, u16 pads[2])
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u16 p1 = 0, p2 = 0;
    if (k[SDL_SCANCODE_Z]) p1 |= NEO_A;
    if (k[SDL_SCANCODE_X]) p1 |= NEO_B;
    if (k[SDL_SCANCODE_C]) p1 |= NEO_C;
    if (k[SDL_SCANCODE_V]) p1 |= NEO_D;
    if (k[SDL_SCANCODE_RETURN]) p1 |= NEO_START;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) p1 |= NEO_SELECT;
    if (k[SDL_SCANCODE_UP]) p1 |= NEO_UP;
    if (k[SDL_SCANCODE_DOWN]) p1 |= NEO_DOWN;
    if (k[SDL_SCANCODE_LEFT]) p1 |= NEO_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) p1 |= NEO_RIGHT;
    if (k[SDL_SCANCODE_B]) p2 |= NEO_A;
    if (k[SDL_SCANCODE_N]) p2 |= NEO_B;
    if (k[SDL_SCANCODE_M]) p2 |= NEO_C;
    if (k[SDL_SCANCODE_COMMA]) p2 |= NEO_D;
    if (k[SDL_SCANCODE_H]) p2 |= NEO_START;
    if (k[SDL_SCANCODE_G]) p2 |= NEO_SELECT;
    if (k[SDL_SCANCODE_I]) p2 |= NEO_UP;
    if (k[SDL_SCANCODE_K]) p2 |= NEO_DOWN;
    if (k[SDL_SCANCODE_J]) p2 |= NEO_LEFT;
    if (k[SDL_SCANCODE_L]) p2 |= NEO_RIGHT;
    pads[0] = p1 | read_pad(app->pads[0]);
    pads[1] = p2 | read_pad(app->pads[1]);
    /* No pueden ir izquierda y derecha (ni arriba y abajo) a la vez en un mando de verdad */
    for (int i = 0; i < 2; i++) {
        if ((pads[i] & (NEO_LEFT | NEO_RIGHT)) == (NEO_LEFT | NEO_RIGHT)) pads[i] &= (u16)~(NEO_LEFT | NEO_RIGHT);
        if ((pads[i] & (NEO_UP | NEO_DOWN)) == (NEO_UP | NEO_DOWN)) pads[i] &= (u16)~(NEO_UP | NEO_DOWN);
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
    app->neo->pad[0] = pads[0];
    app->neo->pad[1] = pads[1];
    app->neo->audio.mute = (u8)app->cfg.mute;
    app->frame++;
    neo_run_frame(app->neo);
    static int16_t samples[AUDIO_BINS * 2];
    int n = neo_audio_take(app->neo, samples, AUDIO_BINS);
    if (app->audio && app->cfg.sound && audible) {
        int vol = app->cfg.volume;
        if (vol != 100) for (int i = 0; i < n * 2; i++) samples[i] = (int16_t)(samples[i] * vol / 100);
        /* Se mantiene una cola corta: si el audio se adelanta (relojes distintos), se descarta un cuadro */
        Uint32 queued = SDL_GetQueuedAudioSize(app->audio) / 4;
        if (queued < AUDIO_RATE / 10) SDL_QueueAudio(app->audio, samples, (Uint32)n * 4);
    }
    /* La tarjeta de memoria se guarda cada 5 segundos si cambió */
    if (app->frame % 300 == 0 && !save_card(app)) {
        char text[1200];
        snprintf(text, sizeof text, tr(T_CARD_SAVE_FAILED_FMT), app->card_path);
        message(app, app->title, text);
    }
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

    if (!app->tex)
        app->tex = SDL_CreateTexture(app->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_UpdateTexture(app->tex, NULL, app->neo->fb, SCREEN_W * (int)sizeof(Uint32));

    /* Tamaño en pantalla: escala entera más grande que cabe (o libre), centrada bajo la barra */
    int avail_h = h - bar;
    double dw = display_w(app, SCREEN_W, SCREEN_H);
    double fit = (double)w / dw < (double)avail_h / SCREEN_H ? (double)w / dw : (double)avail_h / SCREEN_H;
    if (app->cfg.integer) fit = fit < 1 ? 1 : (double)(int)(fit + 0.01);   /* el redondeo del ancho no debe bajar una escala */
    SDL_Rect dst;
    dst.w = (int)(dw * fit + 0.5);
    dst.h = (int)(SCREEN_H * fit + 0.5);
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

/* Icono de la ventana: el mando del AES, recortado al dibujo y centrado en un cuadrado */
/* ------------------------------------------------------------------ */

static unsigned char *sdl_read_file(const char *path, size_t *size) { return u8_read_file(path, size); }

int main(int argc, char *argv[])
{
    static App app_storage;
    App *app = &app_storage;
    const char *rom_arg = NULL, *bios_arg = NULL, *record_path = NULL, *play_path = NULL;
    int scale_arg = 0, shot_menu = -1, shot_action = 0, force = 0, start_full = 0, no_menu = 0;
    long shot_frames = 300;
    char msg[4096];
    the_app = app;
    app->mouse_y = 1 << 20;     /* lejos de la barra hasta que se mueva el ratón: oculta, no aparece sola */
    app->neo = calloc(1, sizeof *app->neo);
    if (!app->neo) fail("sin memoria");
    neo_read_file = sdl_read_file;          /* rutas UTF-8 también en Windows */

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail(SDL_GetError());

    /* Carpeta de datos: tarjetas, copia de la BIOS y preferencias */
#ifdef NEO_RECOMP
    app->pref = SDL_GetPrefPath("NEORecomp", recomp_name);
#else
    app->pref = SDL_GetPrefPath("NEORecomp", "interprete");
#endif
    if (!app->pref) fail(tr(T_NO_DATA_DIR));
    snprintf(app->settings_path, sizeof app->settings_path, "%ssettings.ini", app->pref);
    snprintf(app->bios_copy, sizeof app->bios_copy, "%sbios.bin", app->pref);
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
    set_game(app, "neo", NULL);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) start_full = 1;
        else if (!strcmp(argv[i], "--no-menu")) no_menu = 1;
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_arg = argv[++i];
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
    app->menu_hidden = no_menu ? MENU_OFF : app->cfg.hide_menu ? MENU_HIDDEN : MENU_SHOWN;
    if (ui_shot_path) app->fullscreen = start_full;   /* la captura no cambia la ventana (oculta) */

    int w0 = (int)(display_w(app, SCREEN_W, SCREEN_H) * app->cfg.scale + 0.5);
    app->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                w0, SCREEN_H * app->cfg.scale,
                                SDL_WINDOW_RESIZABLE | (ui_shot_path ? SDL_WINDOW_HIDDEN : 0));
    if (!app->win) fail(SDL_GetError());
    /* SDL2 empieza con la entrada de texto activa; el juego solo lee teclas. Apagarla evita que
     * macOS conecte el método de entrada (InputMethodKit) y deje avisos en la consola */
    SDL_StopTextInput();
    set_window_icon(app);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (ui_shot_path) {
        /* Captura de la interfaz: se dibuja en una superficie del tamaño de la ventana */
        int sh = SCREEN_H * app->cfg.scale + bar_space(app, ui_scale(w0));
        ui_shot_surface = SDL_CreateRGBSurfaceWithFormat(0, w0, sh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (ui_shot_surface) app->ren = SDL_CreateSoftwareRenderer(ui_shot_surface);
    } else {
        app->ren = SDL_CreateRenderer(app->win, -1, SDL_RENDERER_ACCELERATED);
        if (!app->ren) app->ren = SDL_CreateRenderer(app->win, -1, 0);
    }
    if (!app->ren) fail(SDL_GetError());
    SDL_SetWindowMinimumSize(app->win, SCREEN_W, SCREEN_H + bar_space(app, 1));
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 0xFF);
    SDL_RenderClear(app->ren);
    SDL_RenderPresent(app->ren);

    /* El juego (el argumento; recompilado: la copia guardada o uno de la carpeta actual; si no, se pide)
     * y la BIOS */
    static Cart cart;
    char found_path[1024] = "";
    int have = 0, from_cache = 0;
    (void)from_cache;
    if (rom_arg) {
        int r = read_game(rom_arg, &cart, msg, sizeof msg);
        if (r == 0 || (r == 2 && !force)) fail(msg);
        if (r == 2) app->interp_only = 1;       /* el código recompilado es de otro juego */
        snprintf(found_path, sizeof found_path, "%s", rom_arg);
        have = 1;
    }
#ifdef NEO_RECOMP
    char cache_path[1100];
    snprintf(cache_path, sizeof cache_path, "%sjuego.neo", app->pref);
    if (!have) {
        int r = read_game(cache_path, &cart, msg, sizeof msg);
        if (r == 1) { have = from_cache = 1; snprintf(found_path, sizeof found_path, "%s", cache_path); }
        else if (r == 2) cart_free(&cart);
    }
    if (!have) have = find_game_here(&cart, found_path, sizeof found_path);
#endif
    if (!have && !ask_for_game(&cart, found_path, sizeof found_path)) return 0;
#ifdef NEO_RECOMP
    /* Copia para las siguientes veces (solo el juego correcto) */
    if (!from_cache && !app->interp_only && !cart_write_neo(&cart, cache_path))
        fprintf(stderr, "no se pudo guardar la copia del juego en %s\n", cache_path);
    neo_force_interp = app->interp_only;
#endif
    snprintf(app->rom_path, sizeof app->rom_path, "%s", found_path);
    set_game(app, found_path, cart.title);
    if (!get_bios(app, bios_arg, found_path)) return 0;
    SDL_SetWindowTitle(app->win, app->title);

    if (play_path) {
        const char *ierr = inputs_load(&app->script, play_path);
        if (ierr) fail(ierr);
        app->play = 1;
    }
    app->bar = (MenuBar){ menus, 0, menu_state, app, -1, -1, 0, 0 };
    build_menus(app);
    power_on(app, &cart);
    if (record_path && !(app->record = u8_fopen(record_path, "w"))) fail(tr(T_RECORD_FAILED));
    if (app->record) fprintf(app->record, "# NEORecomp: cuadro jugador1 jugador2\n");

    resize_window(app);
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (start_full && !ui_shot_path) toggle_fullscreen(app);

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

    const double frame_seconds = 1.0 / AES_FPS;
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

        /* Ritmo: 59,60 cuadros por segundo (sin límite en avance rápido) */
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
    if (!save_card(app)) {
        snprintf(msg, sizeof msg, tr(T_CARD_SAVE_FAILED_FMT), app->card_path);
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
    neo_free(app->neo);
    free(app->neo);
    return 0;
}
