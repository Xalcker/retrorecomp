/*
 * Capa de plataforma con SDL2 para NESRecomp: ventana con barra de menús, controles (teclado y
 * mandos), audio y ritmo de 60.0988 cuadros por segundo. La usan el intérprete (`nes`) y los
 * juegos que genera nesrecomp (con -DNES_RECOMP).
 *
 * Uso: juego [rom.nes] [--scale N] [--interp] [--force] [--forget-rom]
 *            [--record archivo | --play archivo] [--uncovered archivo]
 *   --interp      ejecuta todo con el intérprete (para comparar)
 *   --force       acepta una ROM con otro SHA1 (solo con el intérprete)
 *   --forget-rom  borra la copia guardada de la ROM y la vuelve a pedir
 *   --record      graba los botones de los dos jugadores en un archivo de texto (ver inputs.h)
 *   --play        reproduce un archivo grabado (el teclado se ignora)
 *   --uncovered   al salir, agrega a `archivo` las instrucciones de ROM que tuvo que ejecutar el
 *                 intérprete (en el intérprete `nes`: todas); sirve de --trace para nesrecomp
 *
 * Jugador 1: flechas, Z = B, X = A, Enter = Start, Mayús/Retroceso = Select. Jugador 2: WASD,
 * F = B, G = A, C = Start, V = Select. Mandos: el primero es el jugador 1 y el segundo el 2.
 *
 * Un juego recompilado pide la ROM una sola vez. Se busca, en orden: el argumento, la copia
 * guardada en la carpeta de datos del juego, "rom.nes" junto al ejecutable o en la carpeta
 * actual, y un .nes de la carpeta actual con el SHA1 correcto. Si no está, se pide con el diálogo
 * de abrir archivo (Windows) o arrastrándola a la ventana. Una ROM válida se copia a la carpeta de
 * datos para las siguientes veces.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\NESRecomp\<juego>\, en Linux
 * ~/.local/share/NESRecomp/<juego>/, en macOS ~/Library/Application Support/NESRecomp/<juego>/.
 * Ahí van la partida (<juego>.sav, que se guarda al salir y cada 5 segundos si cambió), los
 * estados guardados (<juego>.estado1-4), las preferencias (settings.ini), la paleta
 * personalizada (paleta.pal) y lo exportado.
 *
 * La barra de menús y los diálogos vienen de Moskva 89 (ui_menu.c, ui_font.c): se dibujan dentro
 * de la ventana para verse igual en todos los sistemas. Con un menú o un diálogo abierto, el
 * juego se pausa.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef NES_RECOMP
#include <dirent.h>
#include "recomp.h"
#endif

#include "export.h"
#include "files.h"
#include "i18n.h"
#include "icono.h"
#include "inputs.h"
#include "nes.h"
#include "png.h"
#include "settings.h"
#include "sha1.h"
#include "ui_font.h"
#include "ui_menu.h"

#define SCREEN_W 256
#define SCREEN_H 240
#define NTSC_FPS 60.0988

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
    A_SCALE_1, A_SCALE_6 = A_SCALE_1 + 5, A_FULLSCREEN, A_OVERSCAN, A_ASPECT, A_INTEGER,
    A_PAL_0, A_PAL_3 = A_PAL_0 + 3,
    A_ZAPPER, A_FOURSCORE,
    A_SOUND, A_VOLUME_25, A_VOLUME_100 = A_VOLUME_25 + 3, A_CHAN_0, A_CHAN_4 = A_CHAN_0 + 4,
    A_REC_AUDIO, A_SCREENSHOT, A_OPEN_EXPORTS,
    A_HELP_CONTROLS, A_HELP_GAME, A_ABOUT,
    A_LANGUAGE_0,
};

enum { TAG_INFO = 1 };

typedef struct {
    NES nes;
    Rom rom;                /* se conserva para reiniciar */
    int loaded;             /* nes tiene una ROM cargada */
    int interp_only;
    int recompiled;         /* este ejecutable lleva código recompilado de esta ROM */

    char name[96];          /* nombre del juego (archivos y título) */
    char title[96];         /* nombre para mostrar */
    char rom_path[1024];

    int paused, hold_input, fullscreen, running, mouse_y, fast;
    Settings cfg;
    char *pref;
    char settings_path[1024], save_path[1024], palette_path[1100], export_dir[1100];

    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tex_h;
    SDL_AudioDeviceID audio;
    SDL_GameController *pads[4];
    SDL_Rect game_rect;     /* dónde se dibuja el juego en la ventana (para el Zapper) */
    SDL_Cursor *cross;      /* mira del Zapper */
    int mouse_in_game;

    MenuBar bar;
    Dialog dlg;
    char win_title[256];
    char notice[128];
    Uint32 notice_until;

    u32 palette[64];
    int custom_palette;     /* hay paleta.pal válida */

    InputScript script;
    int play;
    FILE *record;
    int last_pad[2];
    long frame;
    const char *uncovered_path;
    WavRecorder wav;
    int audio_was_running;
} App;

static App *the_app;
static const char *ui_shot_path;
static SDL_Surface *ui_shot_surface;   /* --ui-shot: se dibuja en una superficie, sin ventana */

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, the_app ? the_app->title : "NESRecomp", msg, NULL);
    exit(1);
}

static void inform(const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, the_app ? the_app->title : "NESRecomp", msg, NULL);
}

static const char *mapper_name(int m)
{
    switch (m) {
    case 0: return "NROM";   case 1: return "MMC1";  case 2: return "UxROM";
    case 3: return "CNROM";  case 4: return "MMC3";  case 5: return "MMC5";  case 7: return "AxROM";
    case 9: return "MMC2";   case 10: return "MMC4"; case 13: return "CPROM"; case 18: return "Jaleco SS88006";
    case 19: return "Namco 163"; case 21: case 22: case 23: case 25: return "VRC2/VRC4"; case 24: case 26: return "VRC6"; case 85: return "VRC7"; case 69: return "FME-7"; case 75: return "VRC1";
    case 115: return "Kasheng"; case 184: return "Sunsoft 1"; case 206: return "DxROM"; case 210: return "Namco 175/340";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Nombres y carpetas                                                  */
/* ------------------------------------------------------------------ */

static const char *file_name(const char *path)
{
    const char *s = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    return s;
}

/* Nombre del juego: archivos y título */
static void set_game(App *app, const char *name)
{
    snprintf(app->name, sizeof app->name, "%s", name);
    for (char *c = app->name; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' ||
            *c == '>' || *c == '|') *c = '_';
    snprintf(app->title, sizeof app->title, "%s", app->name);
    for (char *c = app->title; *c; c++) if (*c == '_') *c = ' ';
    snprintf(app->save_path, sizeof app->save_path, "%s%s.sav", app->pref, app->name);
}

static void state_path(const App *app, int slot, char *out, size_t size)
{
    snprintf(out, size, "%s%s.estado%d", app->pref, app->name, slot);
}

/* ------------------------------------------------------------------ */
/* ROM                                                                 */
/* ------------------------------------------------------------------ */

/* 1: ROM buena; 0: otra ROM (solo con SHA1 esperado); -1: ilegible; -2: no es una ROM que sepamos
 * correr (el motivo queda en *why) */
static int load_rom(const char *path, Rom *rom, const char **why)
{
    rom->data = u8_read_file(path, &rom->size);
    if (!rom->data) return -1;
#ifdef NES_RECOMP
    char sha1[41];
    sha1_hex(rom->data, rom->size, sha1);
    if (strcmp(sha1, recomp_rom_sha1) != 0) return 0;
#endif
    static NES probe;
    const char *err = NULL;
    if (nes_load(&probe, rom->data, rom->size, &err)) { if (why) *why = err; return -2; }
    nes_free(&probe);
    return 1;
}

static void wrong_rom_message(const char *path, char *msg, size_t size)
{
#ifdef NES_RECOMP
    snprintf(msg, size, tr(T_ROM_WRONG_FMT), path, recomp_name, recomp_rom_sha1);
#else
    snprintf(msg, size, tr(T_ROM_UNREADABLE_FMT), path);
#endif
}

/* Espera a que arrastren un archivo a la ventana (o a que la cierren) */
static int wait_for_drop(char *out, size_t size)
{
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 0;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return 0;
        if (ev.type == SDL_DROPFILE) {
            snprintf(out, size, "%s", ev.drop.file);
            SDL_free(ev.drop.file);
            return 1;
        }
    }
    return 0;
}

/* Pide la ROM hasta que den una válida. 0 si el usuario cancela. */
static int ask_for_rom(App *app, Rom *rom, char *path_out, size_t path_size)
{
    char msg[4096];
    int first = 1;
    for (;;) {
        int got = ask_rom_path(tr(first ? T_ROM_CHOOSE : T_ROM_CHOOSE_AGAIN), path_out, path_size);
        if (!got) {
            /* Sin diálogo nativo (o cancelado en Windows: se ofrece arrastrar) */
            snprintf(msg, sizeof msg, tr(T_ROM_DROP_FMT), app->title[0] ? app->title : "NES");
            if (first) inform(msg);
            if (!wait_for_drop(path_out, path_size)) return 0;
        }
        first = 0;
        const char *why = NULL;
        int r = load_rom(path_out, rom, &why);
        if (r == 1) return 1;
        free(rom->data);
        if (r == 0) wrong_rom_message(path_out, msg, sizeof msg);
        else if (r == -2) snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), path_out, why);
        else snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), path_out);
        inform(msg);
    }
}

#ifdef NES_RECOMP
/* Un .nes de la carpeta actual con el SHA1 esperado */
static int find_rom_here(Rom *rom, char *path_out, size_t size)
{
    DIR *d = opendir(".");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        size_t k = strlen(e->d_name);
        if (k < 5 || strcmp(e->d_name + k - 4, ".nes")) continue;
        const char *why;
        if (load_rom(e->d_name, rom, &why) == 1) { snprintf(path_out, size, "%s", e->d_name); found = 1; }
        else free(rom->data);
    }
    closedir(d);
    return found;
}
#endif

/* ------------------------------------------------------------------ */
/* Partida                                                             */
/* ------------------------------------------------------------------ */

static int save_game(App *app)
{
    if (!app->loaded || !app->nes.cart.battery) return 1;
    return nes_sram_save(&app->nes, app->save_path);
}

/* Enciende (o reinicia) la consola con la ROM actual */
static void power_on(App *app)
{
    const char *err = NULL;
    u32 *tr_keep = app->nes.trace;
    if (app->loaded) {
        app->nes.trace = NULL;
        nes_free(&app->nes);
    }
    if (nes_load(&app->nes, app->rom.data, app->rom.size, &err)) fail(err);
    app->loaded = 1;
    if (app->uncovered_path) nes_trace_start(&app->nes);
    (void)tr_keep;
    nes_sram_load(&app->nes, app->save_path);
    app->frame = 0;
    app->last_pad[0] = app->last_pad[1] = -1;
    if (app->script.n) app->script.pos = 0;
}

static void write_uncovered(App *app)
{
    if (!app->uncovered_path || !app->nes.trace) return;
    nes_trace_load(&app->nes, app->uncovered_path);     /* se suma a lo de corridas anteriores */
    nes_trace_write(&app->nes, app->uncovered_path);
}

/* Cambia de ROM (arrastrar y soltar o Abrir ROM): guarda la partida actual y enciende la nueva */
static int switch_rom(App *app, const char *path)
{
    Rom rom;
    const char *why = NULL;
    char msg[4096];
    int r = load_rom(path, &rom, &why);
    if (r != 1) {
        if (r == 0) wrong_rom_message(path, msg, sizeof msg);
        else if (r == -2) snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), path, why);
        else snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), path);
        if (r != -1) free(rom.data);
        menubar_close(&app->bar);
        dialog_open(&app->dlg, DIALOG_MESSAGE, TAG_INFO, app->title, msg);
        return 0;
    }
    save_game(app);
    write_uncovered(app);
    free(app->rom.data);
    app->rom = rom;
    snprintf(app->rom_path, sizeof app->rom_path, "%s", path);
#ifndef NES_RECOMP
    char base[96];
    snprintf(base, sizeof base, "%s", file_name(path));
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    set_game(app, base);
#endif
    power_on(app);
    app->win_title[0] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Imagen                                                              */
/* ------------------------------------------------------------------ */

static int ui_scale(int width)
{
    return width >= 640 ? 2 : 1;
}

static void game_size(const App *app, int *w, int *h)
{
    *w = SCREEN_W;
    *h = app->cfg.overscan ? SCREEN_H - 16 : SCREEN_H;
}

/* Ancho de la imagen con el aspecto elegido (los pixeles del NES son 8:7) */
static double aspect_x(const App *app) { return app->cfg.aspect ? 8.0 / 7.0 : 1.0; }

static void resize_window(App *app)
{
    if (app->fullscreen) return;
    int gw, gh;
    game_size(app, &gw, &gh);
    int w = (int)(gw * aspect_x(app) * app->cfg.scale + 0.5);
    SDL_SetWindowSize(app->win, w, gh * app->cfg.scale + menubar_height(ui_scale(w)));
}

static void load_palette(App *app)
{
    app->custom_palette = 0;
    size_t len;
    unsigned char *d = u8_read_file(app->palette_path, &len);
    u32 custom[64];
    if (d && ppu_load_palette(d, len, custom)) {
        app->custom_palette = 1;
        if (app->cfg.palette == 3) memcpy(app->palette, custom, sizeof custom);
    }
    free(d);
}

static void apply_palette(App *app)
{
    int k = app->cfg.palette;
    if (k == 3) {
        if (!app->custom_palette) { app->cfg.palette = k = 0; }
        else { load_palette(app); return; }
    }
    ppu_make_palette(k, app->palette);
}

/* Convierte la imagen visible a ARGB */
static void compose(const App *app, Uint32 *dst, int gw, int gh)
{
    int y0 = app->cfg.overscan ? 8 : 0;
    const u16 *fb = app->nes.ppu.fb + y0 * SCREEN_W;
    for (int i = 0; i < gw * gh; i++) dst[i] = ppu_argb(app->palette, fb[i]);
}

/* ------------------------------------------------------------------ */
/* Avisos, exportación                                                 */
/* ------------------------------------------------------------------ */

static void message(App *app, const char *title, const char *text)
{
    menubar_close(&app->bar);
    dialog_open(&app->dlg, DIALOG_MESSAGE, TAG_INFO, title, text);
}

static void notify(App *app, const char *text)
{
    snprintf(app->notice, sizeof app->notice, "%.120s", text);
    app->notice_until = SDL_GetTicks() + 2500;
}

static int export_base(App *app, const char *kind, char *out, size_t size)
{
    if (!u8_mkdir(app->export_dir)) return 0;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char stamp[32] = "0";
    if (t) strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", t);
    snprintf(out, size, "%s%s_%s_%s", app->export_dir, app->name, kind, stamp);
    return 1;
}

static void screenshot(App *app)
{
    static Uint32 rgb[SCREEN_W * SCREEN_H];
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

static char wav_last[1400];

static void stop_audio_rec(App *app, int report)
{
    if (!wav_active(&app->wav)) return;
    wav_stop(&app->wav);
    if (report) {
        char text[1500];
        snprintf(text, sizeof text, tr(T_AUDIO_SAVED_FMT), wav_last);
        message(app, tr(T_EXPORT_TITLE), text);
    }
}

static void toggle_audio_rec(App *app)
{
    if (wav_active(&app->wav)) { stop_audio_rec(app, 1); return; }
    char base[1300], text[1500];
    if (!export_base(app, "audio", base, sizeof base)) {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), app->export_dir);
        message(app, tr(T_EXPORT_TITLE), text);
        return;
    }
    snprintf(wav_last, sizeof wav_last, "%s.wav", base);
    if (!wav_start(&app->wav, wav_last, APU_RATE)) {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), wav_last);
        message(app, tr(T_EXPORT_TITLE), text);
    }
}

static void open_exports(App *app)
{
    u8_mkdir(app->export_dir);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    char url[1300];
    const char *p = app->export_dir;
    /* file:///C:/... en Windows, file:///home/... en los demás */
    snprintf(url, sizeof url, "file://%s%s", p[0] == '/' ? "" : "/", p);
    for (char *c = url; *c; c++) if (*c == '\\') *c = '/';
    SDL_OpenURL(url);
#endif
}

/* ------------------------------------------------------------------ */
/* Estados guardados                                                   */
/* ------------------------------------------------------------------ */

static void save_state(App *app)
{
    char path[1100], text[1300];
    size_t size;
    u8 *data = nes_state_save(&app->nes, &size);
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
    if (nes_state_load(&app->nes, data, size)) {
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

static MenuItem menu_game[16], menu_video[16], menu_palette[8], menu_audio[16], menu_input[4];
static MenuItem menu_export[8], menu_language[LANG_COUNT], menu_help[4];
static Menu menus[8];
static char slot_labels[4][40], size_labels[6][40], volume_labels[4][40];

static void build_menus(App *app)
{
    int n = 0;
#ifndef NES_RECOMP
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
    menus[0] = (Menu){ tr(T_MENU_GAME), menu_game, n };

    int k = 1;
    n = 0;
    for (int i = 0; i < 6; i++) {
        snprintf(size_labels[i], sizeof size_labels[i], tr(T_SIZE_FMT), i + 1);
        ITEM(menu_video, n, size_labels[i], NULL, A_SCALE_1 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_video, n);
    ITEM(menu_video, n, tr(T_FULLSCREEN), "F11", A_FULLSCREEN, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_INTEGER), NULL, A_INTEGER, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_ASPECT), NULL, A_ASPECT, ITEM_CHECK);
    ITEM(menu_video, n, tr(T_OVERSCAN), NULL, A_OVERSCAN, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_VIDEO), menu_video, n };

    n = 0;
    ITEM(menu_palette, n, tr(T_PAL_DEFAULT), "F2", A_PAL_0, ITEM_RADIO);
    ITEM(menu_palette, n, tr(T_PAL_GRAY), NULL, A_PAL_0 + 1, ITEM_RADIO);
    ITEM(menu_palette, n, tr(T_PAL_VIVID), NULL, A_PAL_0 + 2, ITEM_RADIO);
    ITEM(menu_palette, n, tr(T_PAL_CUSTOM), NULL, A_PAL_0 + 3, ITEM_RADIO);
    menus[k++] = (Menu){ tr(T_MENU_PALETTE), menu_palette, n };

    n = 0;
    ITEM(menu_input, n, tr(T_ZAPPER), NULL, A_ZAPPER, ITEM_CHECK);
    ITEM(menu_input, n, tr(T_FOURSCORE), NULL, A_FOURSCORE, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_INPUT), menu_input, n };

    n = 0;
    ITEM(menu_audio, n, tr(T_SOUND), "Ctrl+M", A_SOUND, ITEM_CHECK);
    SEPARATOR(menu_audio, n);
    for (int i = 0; i < 4; i++) {
        snprintf(volume_labels[i], sizeof volume_labels[i], tr(T_VOLUME_FMT), 25 * (i + 1));
        ITEM(menu_audio, n, volume_labels[i], NULL, A_VOLUME_25 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_audio, n);
    ITEM(menu_audio, n, tr(T_CHAN_PULSE1), NULL, A_CHAN_0, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_PULSE2), NULL, A_CHAN_0 + 1, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_TRIANGLE), NULL, A_CHAN_0 + 2, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_NOISE), NULL, A_CHAN_0 + 3, ITEM_CHECK);
    ITEM(menu_audio, n, tr(T_CHAN_DMC), NULL, A_CHAN_0 + 4, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_AUDIO), menu_audio, n };

    n = 0;
    ITEM(menu_export, n, tr(T_REC_AUDIO), "Ctrl+G", A_REC_AUDIO, ITEM_CHECK);
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
    case A_PAUSE:        *checked = app->paused; return 1;
    case A_LOAD_STATE:   return !replay;
    case A_FULLSCREEN:   *checked = app->fullscreen; return 1;
    case A_INTEGER:      *checked = app->cfg.integer; return 1;
    case A_ASPECT:       *checked = app->cfg.aspect; return 1;
    case A_OVERSCAN:     *checked = app->cfg.overscan; return 1;
    case A_ZAPPER:       *checked = app->cfg.zapper; return !(app->play || app->record);
    case A_FOURSCORE:    *checked = app->cfg.fourscore; return !(app->play || app->record);
    case A_SOUND:        *checked = app->cfg.sound; return 1;
    case A_REC_AUDIO:    *checked = wav_active(&app->wav); return 1;
    default:
        if (action >= A_SLOT_1 && action <= A_SLOT_4)
            *checked = app->cfg.slot == action - A_SLOT_1 + 1;
        else if (action >= A_PAL_0 && action <= A_PAL_3) {
            *checked = app->cfg.palette == action - A_PAL_0;
            return action != A_PAL_3 || app->custom_palette;
        } else if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            *checked = app->cfg.scale == action - A_SCALE_1 + 1;
            return !app->fullscreen;
        } else if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            *checked = app->cfg.volume == 25 * (action - A_VOLUME_25 + 1);
            return app->cfg.sound;
        } else if (action >= A_CHAN_0 && action <= A_CHAN_4) {
            *checked = !(app->cfg.mute & (1 << (action - A_CHAN_0)));    /* marcado = suena */
            return 1;
        } else if (action >= A_LANGUAGE_0 && action < A_LANGUAGE_0 + LANG_COUNT)
            *checked = (int)i18n_get() == action - A_LANGUAGE_0;
        return 1;
    }
}

static void toggle_fullscreen(App *app)
{
    app->fullscreen = !app->fullscreen;
    SDL_SetWindowFullscreen(app->win, app->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!app->fullscreen) resize_window(app);
}

static void ask_open_rom(App *app)
{
    char path[1024];
    if (ask_rom_path(tr(T_ROM_CHOOSE), path, sizeof path)) switch_rom(app, path);
    else message(app, tr(T_OPEN_ROM), tr(T_ROM_CHOOSE));   /* sin diálogo nativo: se arrastra */
}

/* Con el Zapper, el ratón sobre el juego es una mira */
static void update_cursor(App *app)
{
    if (!app->cross) app->cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    if (app->cross && app->cfg.zapper && app->mouse_in_game) SDL_SetCursor(app->cross);
    else SDL_SetCursor(SDL_GetDefaultCursor());
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
    case A_OVERSCAN:     app->cfg.overscan = !app->cfg.overscan; resize_window(app); break;
    case A_ZAPPER:       app->cfg.zapper = !app->cfg.zapper; if (app->cfg.zapper) app->cfg.fourscore = 0; update_cursor(app); break;
    case A_FOURSCORE:    app->cfg.fourscore = !app->cfg.fourscore; if (app->cfg.fourscore) app->cfg.zapper = 0; update_cursor(app); break;
    case A_SOUND:        app->cfg.sound = !app->cfg.sound; break;
    case A_REC_AUDIO:    toggle_audio_rec(app); break;
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
    case A_HELP_GAME: {
        char text[2048], folder[64];
        const Cart *c = &app->nes.cart;
        size_t len = strlen(app->pref);
        /* Una ruta larga no cabe en el diálogo: se deja el final */
        if (len > 44) snprintf(folder, sizeof folder, "…%s", app->pref + len - 43);
        else snprintf(folder, sizeof folder, "%s", app->pref);
        char sha[41];
        sha1_hex(app->rom.data, app->rom.size, sha);
        double pct = app->nes.cyc ? 100.0 * app->nes.cyc_recomp / app->nes.cyc : 0.0;
        snprintf(text, sizeof text, tr(T_GAME_TEXT_FMT), app->title, c->mapper, mapper_name(c->mapper),
                 (int)(c->prg_size / 1024), c->chr_is_ram ? 0 : (int)(c->chr_size / 1024),
                 c->chr_is_ram ? " (RAM)" : "", sha, pct, folder);
        message(app, tr(T_GAME_TITLE), text);
        break;
    }
    case A_ABOUT: {
        char text[1024];
        snprintf(text, sizeof text, tr(T_ABOUT_TEXT_FMT), app->title);
        message(app, app->title, text);
        break;
    }
    default:
        if (action >= A_SLOT_1 && action <= A_SLOT_4) {
            app->cfg.slot = action - A_SLOT_1 + 1;
        } else if (action >= A_PAL_0 && action <= A_PAL_3) {
            app->cfg.palette = action - A_PAL_0;
            apply_palette(app);
        } else if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            app->cfg.scale = action - A_SCALE_1 + 1;
            resize_window(app);
        } else if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            app->cfg.volume = 25 * (action - A_VOLUME_25 + 1);
        } else if (action >= A_CHAN_0 && action <= A_CHAN_4) {
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

/* F2: la siguiente paleta disponible */
static void next_palette(App *app)
{
    int k = app->cfg.palette;
    do k = (k + 1) % 4; while (k == 3 && !app->custom_palette);
    app->cfg.palette = k;
    apply_palette(app);
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
    case SDLK_o:
#ifndef NES_RECOMP
                      if (ctrl && !replay) { do_action(app, A_OPEN_ROM); return 1; }
#endif
                      break;
    case SDLK_m:      if (ctrl) { do_action(app, A_SOUND); return 1; } break;
    case SDLK_p:      do_action(app, A_PAUSE); return 1;
    case SDLK_F5:     do_action(app, A_SAVE_STATE); return 1;
    case SDLK_F8:     if (!replay) { do_action(app, A_LOAD_STATE); return 1; } break;
    case SDLK_F11:    do_action(app, A_FULLSCREEN); return 1;
    case SDLK_RETURN: if (alt) { do_action(app, A_FULLSCREEN); return 1; } break;
    case SDLK_F1:     do_action(app, A_HELP_CONTROLS); return 1;
    case SDLK_F2:     next_palette(app); return 1;
    case SDLK_F12:    do_action(app, A_SCREENSHOT); return 1;
    case SDLK_g:      if (ctrl) { do_action(app, A_REC_AUDIO); return 1; } break;
    default: break;
    }
    return 0;
}

/* ¿Se ve la barra? En pantalla completa, solo al acercar el ratón arriba */
static int bar_visible(const App *app)
{
    if (!app->fullscreen) return 1;
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    return menubar_is_open(&app->bar) || app->dlg.kind != DIALOG_NONE ||
           app->mouse_y < 2 * menubar_height(ui_scale(w));
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
    if (e->type == SDL_MOUSEMOTION)
        app->mouse_y = e->motion.y;
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        /* Al primer lugar libre: el primero que se conecta es el jugador 1 */
        for (int i = 0; i < 4; i++)
            if (!app->pads[i]) {
                app->pads[i] = SDL_GameControllerOpen(e->cdevice.which);
                break;
            }
        return;
    }
    if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_GameController *gone = SDL_GameControllerFromInstanceID(e->cdevice.which);
        for (int i = 0; i < 4; i++)
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

static u8 read_pad(SDL_GameController *pad)
{
    if (!pad) return 0;
    u8 r = 0;
    #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
    if (BTN(B) || BTN(Y)) r |= BTN_A;                  /* la posición: A del NES a la derecha */
    if (BTN(A) || BTN(X)) r |= BTN_B;
    if (BTN(BACK)) r |= BTN_SELECT;
    if (BTN(START)) r |= BTN_START;
    if (BTN(DPAD_UP)) r |= BTN_UP;
    if (BTN(DPAD_DOWN)) r |= BTN_DOWN;
    if (BTN(DPAD_LEFT)) r |= BTN_LEFT;
    if (BTN(DPAD_RIGHT)) r |= BTN_RIGHT;
    #undef BTN
    int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -16000) r |= BTN_LEFT;
    if (lx > 16000) r |= BTN_RIGHT;
    if (ly < -16000) r |= BTN_UP;
    if (ly > 16000) r |= BTN_DOWN;
    return r;
}

static void player_pads(App *app, u8 pads[4])
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u8 p1 = 0, p2 = 0;
    if (k[SDL_SCANCODE_X]) p1 |= BTN_A;
    if (k[SDL_SCANCODE_Z]) p1 |= BTN_B;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_BACKSPACE]) p1 |= BTN_SELECT;
    if (k[SDL_SCANCODE_RETURN]) p1 |= BTN_START;
    if (k[SDL_SCANCODE_UP]) p1 |= BTN_UP;
    if (k[SDL_SCANCODE_DOWN]) p1 |= BTN_DOWN;
    if (k[SDL_SCANCODE_LEFT]) p1 |= BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) p1 |= BTN_RIGHT;
    if (k[SDL_SCANCODE_G]) p2 |= BTN_A;
    if (k[SDL_SCANCODE_F]) p2 |= BTN_B;
    if (k[SDL_SCANCODE_V]) p2 |= BTN_SELECT;
    if (k[SDL_SCANCODE_C]) p2 |= BTN_START;
    if (k[SDL_SCANCODE_W]) p2 |= BTN_UP;
    if (k[SDL_SCANCODE_S]) p2 |= BTN_DOWN;
    if (k[SDL_SCANCODE_A]) p2 |= BTN_LEFT;
    if (k[SDL_SCANCODE_D]) p2 |= BTN_RIGHT;
    pads[0] = p1 | read_pad(app->pads[0]);
    pads[1] = p2 | read_pad(app->pads[1]);
    pads[2] = read_pad(app->pads[2]);                       /* jugadores 3 y 4 (Four Score): solo con mandos */
    pads[3] = read_pad(app->pads[3]);
    /* No pueden ir izquierda y derecha (ni arriba y abajo) a la vez en un control de verdad */
    for (int i = 0; i < 4; i++) {
        if ((pads[i] & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) pads[i] &= ~(BTN_LEFT | BTN_RIGHT);
        if ((pads[i] & (BTN_UP | BTN_DOWN)) == (BTN_UP | BTN_DOWN)) pads[i] &= ~(BTN_UP | BTN_DOWN);
    }
    /* Tras cerrar un menú o diálogo, los botones se ignoran hasta que se suelten */
    if (app->hold_input) {
        if (pads[0] || pads[1] || pads[2] || pads[3]) pads[0] = pads[1] = pads[2] = pads[3] = 0;
        else app->hold_input = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Ciclo principal                                                     */
/* ------------------------------------------------------------------ */

/* ¿Avanza el juego? Con un menú o diálogo abierto, o en pausa, no */
static int game_running(const App *app)
{
    return !app->paused && !menubar_is_open(&app->bar) && app->dlg.kind == DIALOG_NONE;
}

/* Posición del ratón sobre la imagen del juego (y botón izquierdo = gatillo). Fuera del juego, la mira apunta fuera. */
static void zapper_input(App *app)
{
    int mx, my;
    Uint32 b = SDL_GetMouseState(&mx, &my);
    int w, h, gw, gh;
    SDL_GetWindowSize(app->win, &w, &h);
    SDL_GetRendererOutputSize(app->ren, &gw, &gh);
    if (w > 0) { mx = mx * gw / w; my = my * gh / h; }          /* pantallas de alta densidad */
    const SDL_Rect *r = &app->game_rect;
    int in = r->w > 0 && mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h;
    app->nes.zap_trigger = in && (b & SDL_BUTTON(SDL_BUTTON_LEFT)) && app->dlg.kind == DIALOG_NONE && !menubar_is_open(&app->bar);
    if (in) {
        int y0 = app->cfg.overscan ? 8 : 0, vis_h = app->cfg.overscan ? SCREEN_H - 16 : SCREEN_H;
        app->nes.zap_x = (mx - r->x) * SCREEN_W / r->w;
        app->nes.zap_y = y0 + (my - r->y) * vis_h / r->h;
    } else {
        app->nes.zap_x = app->nes.zap_y = -1;
    }
    if (in != app->mouse_in_game) { app->mouse_in_game = in; update_cursor(app); }
}

static void step(App *app, int audible)
{
    u8 pads[4];
    player_pads(app, pads);
    if (app->play) {
        pads[0] = inputs_pad(&app->script, app->frame, 0);
        pads[1] = inputs_pad(&app->script, app->frame, 1);
    }
    if (app->record) inputs_record(app->record, app->frame, pads, app->last_pad);
    app->nes.fourscore = (u8)app->cfg.fourscore;
    app->nes.zapper_on = (u8)app->cfg.zapper;
    if (app->cfg.zapper && !app->play) zapper_input(app);
    else { app->nes.zap_x = app->nes.zap_y = -1; app->nes.zap_trigger = 0; }
    app->nes.pad[0] = pads[0];
    app->nes.pad[1] = pads[1];
    app->nes.pad[2] = pads[2];
    app->nes.pad[3] = pads[3];
    app->nes.apu.mute = (u8)app->cfg.mute;
    app->frame++;
    nes_run_frame(&app->nes);

    /* Guardado automático cada 5 segundos si la RAM del cartucho cambió */
    if (app->frame % 300 == 0 && nes_sram_dirty(&app->nes)) save_game(app);

    static int16_t samples[APU_BUF];
    int n = apu_take(&app->nes, samples, APU_BUF);
    wav_samples(&app->wav, samples, n);          /* el WAV se graba aunque esté sin sonido */
    if (!app->audio || !app->cfg.sound || !audible) return;
    int vol = app->cfg.volume;
    if (vol != 100) for (int i = 0; i < n; i++) samples[i] = (int16_t)(samples[i] * vol / 100);
    /* La cola se mantiene cerca de 'target' ajustando el ritmo de las muestras del APU */
    const int target = 1600;
    int queued = (int)(SDL_GetQueuedAudioSize(app->audio) / 2);
    double err = (double)(target - queued) / target;
    if (err > 1) err = 1;
    if (err < -1) err = -1;
    app->nes.apu.rate_mul = 1.0 + 0.01 * err;
    if (queued < target * 4) SDL_QueueAudio(app->audio, samples, (Uint32)n * 2);
}

static void update_title(App *app)
{
    char t[256];
    const char *detail = "";
    if (app->notice[0] && SDL_TICKS_PASSED(SDL_GetTicks(), app->notice_until)) app->notice[0] = 0;
    if (app->notice[0]) detail = app->notice;
    else if (app->paused) detail = tr(T_TITLE_PAUSED);
    else if (app->fast) detail = tr(T_TITLE_FAST);
    else if (wav_active(&app->wav)) detail = tr(T_TITLE_RECORDING);
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
    if (app->tex_h != gh) {
        if (app->tex) SDL_DestroyTexture(app->tex);
        app->tex = SDL_CreateTexture(app->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, gw, gh);
        app->tex_h = gh;
    }
    static Uint32 rgb[SCREEN_W * SCREEN_H];
    compose(app, rgb, gw, gh);
    SDL_UpdateTexture(app->tex, NULL, rgb, gw * (int)sizeof(Uint32));

    /* Tamaño en pantalla: escala entera más grande que cabe (o libre), centrada bajo la barra */
    int avail_h = h - bar;
    double ax = aspect_x(app);
    double fit = (double)w / (gw * ax) < (double)avail_h / gh ? (double)w / (gw * ax) : (double)avail_h / gh;
    if (app->cfg.integer) fit = fit < 1 ? 1 : (double)(int)(fit + 0.01);   /* el redondeo del ancho no debe bajar una escala */
    SDL_Rect dst;
    dst.w = (int)(gw * ax * fit + 0.5);
    dst.h = (int)(gh * fit + 0.5);
    dst.x = (w - dst.w) / 2;
    dst.y = bar + (avail_h - dst.h) / 2;
    app->game_rect = dst;

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

/* Icono de la ventana: el control de NES, recortado al dibujo y centrado en un cuadrado */
static void set_window_icon(App *app)
{
    int w, h;
    unsigned char *px = png_decode_rgba(icono_png, sizeof icono_png, &w, &h);
    if (!px) return;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[((size_t)y * w + x) * 4 + 3] > 16) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 >= x0 && y1 >= y0) {
        int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
        int side = (cw > ch ? cw : ch) + (cw > ch ? cw : ch) / 8;     /* un margen del ~6% por lado */
        unsigned char *sq = calloc((size_t)side * side, 4);
        if (sq) {
            int ox = (side - cw) / 2, oy = (side - ch) / 2;
            for (int y = 0; y < ch; y++)
                memcpy(sq + ((size_t)(y + oy) * side + ox) * 4, px + ((size_t)(y + y0) * w + x0) * 4, (size_t)cw * 4);
            SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(sq, side, side, 32, side * 4, SDL_PIXELFORMAT_RGBA32);
            if (icon) { SDL_SetWindowIcon(app->win, icon); SDL_FreeSurface(icon); }
            free(sq);
        }
    }
    free(px);
}

static Language detect_language(void)
{
    Language found = LANG_EN;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_Locale *locales = SDL_GetPreferredLocales();
    if (locales) {
        for (SDL_Locale *l = locales; l->language; l++) {
            Language lang = i18n_from_code(l->language);
            if (lang < LANG_COUNT) {
                found = lang;
                break;
            }
        }
        SDL_free(locales);
    }
#endif
    return found;
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    static App app_storage;
    App *app = &app_storage;
    const char *rom_arg = NULL, *record_path = NULL, *play_path = NULL;
    int scale_arg = 0, force = 0, forget = 0, shot_menu = -1, shot_action = 0;
    long shot_frames = 300;
    char msg[4096];
    the_app = app;
    app->last_pad[0] = app->last_pad[1] = -1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail(SDL_GetError());

    /* Carpeta de datos: copia de la ROM, partida, estados y preferencias */
#ifdef NES_RECOMP
    const char *pref_name = recomp_name;
    app->recompiled = 1;
#else
    const char *pref_name = "interprete";
#endif
    app->pref = SDL_GetPrefPath("NESRecomp", pref_name);
    if (!app->pref) fail(tr(T_NO_DATA_DIR));
    char cache_path[1100];
    snprintf(cache_path, sizeof cache_path, "%srom.nes", app->pref);
    snprintf(app->settings_path, sizeof app->settings_path, "%ssettings.ini", app->pref);
    snprintf(app->palette_path, sizeof app->palette_path, "%spaleta.pal", app->pref);
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
#ifdef NES_RECOMP
    set_game(app, recomp_name);
#else
    set_game(app, "nes");
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interp")) app->interp_only = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--forget-rom")) forget = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_path = argv[++i];
        else if ((!strcmp(argv[i], "--uncovered") || !strcmp(argv[i], "--trace")) && i + 1 < argc)
            app->uncovered_path = argv[++i];
        else if (!strcmp(argv[i], "--ui-shot") && i + 1 < argc) ui_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--ui-frames") && i + 1 < argc) shot_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ui-menu") && i + 1 < argc) shot_menu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ui-action") && i + 1 < argc) shot_action = atoi(argv[++i]);
        else rom_arg = argv[i];
    }
    if (scale_arg > 0) app->cfg.scale = scale_arg > 6 ? 6 : scale_arg;
    if (forget) u8_remove(cache_path);
    apply_palette(app);
    load_palette(app);
    apply_palette(app);

    app->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                SCREEN_W * app->cfg.scale, SCREEN_H * app->cfg.scale,
                                SDL_WINDOW_RESIZABLE | (ui_shot_path ? SDL_WINDOW_HIDDEN : 0));
    if (!app->win) fail(SDL_GetError());
    /* SDL2 empieza con la entrada de texto activa; el juego solo lee teclas. Apagarla evita que
     * macOS conecte el método de entrada (InputMethodKit) y deje avisos en la consola */
    SDL_StopTextInput();
    set_window_icon(app);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (ui_shot_path) {
        /* Captura de la interfaz: se dibuja en una superficie del tamaño de la ventana */
        int sw = (int)(SCREEN_W * (app->cfg.aspect ? 8.0 / 7.0 : 1.0) * app->cfg.scale + 0.5);
        int sh = (app->cfg.overscan ? SCREEN_H - 16 : SCREEN_H) * app->cfg.scale + menubar_height(ui_scale(sw));
        ui_shot_surface = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (ui_shot_surface) app->ren = SDL_CreateSoftwareRenderer(ui_shot_surface);
    } else {
        app->ren = SDL_CreateRenderer(app->win, -1, SDL_RENDERER_ACCELERATED);
        if (!app->ren) app->ren = SDL_CreateRenderer(app->win, -1, 0);
    }
    if (!app->ren) fail(SDL_GetError());
    SDL_SetWindowMinimumSize(app->win, SCREEN_W, SCREEN_H - 16 + menubar_height(1));
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 0xFF);
    SDL_RenderClear(app->ren);
    SDL_RenderPresent(app->ren);

    /* ROM: argumento, copia guardada, rom.nes junto al ejecutable o aquí, un .nes con el SHA1
     * correcto en esta carpeta, o pedirla */
    Rom *rom = &app->rom;
    int have = 0, from_cache = 0;
    (void)from_cache;
    char found_path[1024] = "";
    if (rom_arg) {
        const char *why = NULL;
        int r = load_rom(rom_arg, rom, &why);
        if (r == -1) {
            snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), rom_arg);
            fail(msg);
        }
        if (r == -2) {
            snprintf(msg, sizeof msg, tr(T_ROM_BAD_FMT), rom_arg, why);
            fail(msg);
        }
        if (r == 0) {
            if (!force) {
                wrong_rom_message(rom_arg, msg, sizeof msg);
                fail(msg);
            }
            app->interp_only = 1;    /* el código recompilado es de otra ROM */
            const char *e2;
            static NES probe;
            if (nes_load(&probe, rom->data, rom->size, &e2)) fail(e2);
            nes_free(&probe);
        }
        snprintf(found_path, sizeof found_path, "%s", rom_arg);
        have = 1;
    }
#ifdef NES_RECOMP
    if (!have && load_rom(cache_path, rom, NULL) == 1) { have = from_cache = 1; snprintf(found_path, sizeof found_path, "%s", cache_path); }
    if (!have) {
        free(rom->data);
        char *base = SDL_GetBasePath();
        char path[1100];
        snprintf(path, sizeof path, "%srom.nes", base ? base : "");
        SDL_free(base);
        if (load_rom(path, rom, NULL) == 1) { have = 1; snprintf(found_path, sizeof found_path, "%s", path); }
        else {
            free(rom->data);
            if (load_rom("rom.nes", rom, NULL) == 1) { have = 1; snprintf(found_path, sizeof found_path, "rom.nes"); }
            else free(rom->data);
        }
    }
    if (!have && find_rom_here(rom, found_path, sizeof found_path)) have = 1;
#endif
    if (!have && !ask_for_rom(app, rom, found_path, sizeof found_path)) return 0;
    fprintf(stderr, "ROM: %s\n", found_path);
    snprintf(app->rom_path, sizeof app->rom_path, "%s", found_path);
#ifndef NES_RECOMP
    {   /* el intérprete nombra la partida por la ROM */
        char base[96];
        snprintf(base, sizeof base, "%s", file_name(found_path));
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        set_game(app, base);
    }
#endif
    /* Copia para las siguientes veces (solo una ROM recompilada, y solo si es la correcta) */
#ifdef NES_RECOMP
    if (!from_cache && !app->interp_only && !u8_write_file(cache_path, rom->data, rom->size))
        fprintf(stderr, "no se pudo guardar la copia de la ROM en %s\n", cache_path);
#endif
    SDL_SetWindowTitle(app->win, app->title);
    nes_force_interp = app->interp_only;

    if (play_path) {
        const char *ierr = inputs_load(&app->script, play_path);
        if (ierr) fail(ierr);
        app->play = 1;
    }

    app->bar = (MenuBar){ menus, 0, menu_state, app, -1, -1, 0, 0 };
    build_menus(app);
    power_on(app);
    if (record_path && !(app->record = u8_fopen(record_path, "w"))) fail(tr(T_RECORD_FAILED));
    if (app->record) fprintf(app->record, "# NESRecomp: cuadro jugador1 jugador2\n");

    resize_window(app);
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_AudioSpec want, have_spec;
    SDL_zero(want);
    want.freq = APU_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    app->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have_spec, 0);
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
                for (int i = 0; i < 4; i++) step(app, 0);       /* avance rápido: sin audio */
                if (app->audio) SDL_ClearQueuedAudio(app->audio);
            } else step(app, 1);
        } else if (app->audio_was_running && app->audio) SDL_ClearQueuedAudio(app->audio);
        app->audio_was_running = running;
        update_title(app);
        render(app);

        /* Ritmo: 60.0988 cuadros por segundo (sin límite en avance rápido) */
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

    stop_audio_rec(app, 0);
    if (!shot_mode) settings_save(&app->cfg, app->settings_path);
    if (app->record) fclose(app->record);
    if (!save_game(app)) {
        snprintf(msg, sizeof msg, tr(T_SAVE_FAILED_FMT), app->save_path);
        fail(msg);
    }
    write_uncovered(app);
    if (app->audio) SDL_CloseAudioDevice(app->audio);
    for (int i = 0; i < 4; i++)
        if (app->pads[i]) SDL_GameControllerClose(app->pads[i]);
    if (app->tex) SDL_DestroyTexture(app->tex);
    SDL_DestroyRenderer(app->ren);
    SDL_DestroyWindow(app->win);
    SDL_free(app->pref);
    SDL_Quit();
    nes_free(&app->nes);
    free(rom->data);
    return 0;
}
