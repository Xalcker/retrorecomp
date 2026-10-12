/*
 * Capa de plataforma con SDL2 para un juego recompilado: ventana con barra de
 * menús, controles (teclado y control), audio y ritmo de 59.7275 cuadros por
 * segundo.
 *
 * Uso: juego [rom.gb] [--scale N] [--interp] [--force] [--forget-rom]
 *            [--record archivo | --play archivo] [--uncovered archivo]
 *            [--model dmg|cgb|sgb] [--palette up+a] [--link]
 *   --interp      ejecuta todo con el intérprete (para comparar)
 *   --force       acepta una ROM con otro SHA1 (solo con el intérprete)
 *   --forget-rom  borra la copia guardada de la ROM y la vuelve a pedir
 *   --model       fuerza el modelo; por omisión, color si el juego lo admite,
 *                 si no Super Game Boy (con borde) si lo admite, si no un Game Boy
 *                 Color en modo de compatibilidad (el juego de DMG coloreado).
 *                 También se elige en el menú Juego.
 *   --palette     paleta del modo de compatibilidad, como la combinación de
 *                 botones al encender un GBC: up, down, left, right, up+a,
 *                 down+a, left+a, right+a, up+b, down+b, left+b o right+b
 *   --record      graba los botones en un archivo de texto (ver inputs.h), para
 *                 reproducirlos con --play o con difftest --input
 *   --play        reproduce un archivo grabado (el teclado se ignora)
 *   --uncovered archivo  al salir, agrega a `archivo` las direcciones de ROM
 *                 que tuvo que ejecutar el intérprete (para gbrecomp --trace)
 *   --link        empieza con el cable link (también en el menú Juego)
 *
 * Multijugador: el jugador 1 usa las flechas (y el primer control), el 2 WASD
 * (y el segundo control), el 3 y el 4 el tercer y cuarto control. Con el cable
 * link hay un segundo Game Boy a la derecha, que maneja el jugador 2, con su
 * propia partida (<juego>_jugador2.sav). En el Super Game Boy, los juegos de
 * varios jugadores leen los controles 2-4.
 *
 * La ROM se pide una sola vez. Se busca, en orden: el argumento, la copia
 * guardada en la carpeta de datos del juego, y "rom.gb" junto al ejecutable o
 * en la carpeta actual. Si no está, se pide con el diálogo de abrir archivo
 * (Windows) o arrastrándola a la ventana. Una ROM válida (SHA1 correcto) se
 * copia a la carpeta de datos para las siguientes veces.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\GBRecomp\<juego>\, en
 * Linux ~/.local/share/GBRecomp/<juego>/, en macOS ~/Library/Application
 * Support/GBRecomp/<juego>/. Ahí van también la partida (<juego>.sav,
 * compatible con BGB y VBA-M, que se guarda al salir y cada 5 segundos si
 * cambió), los estados guardados (<juego>.estado1-4) y las preferencias
 * (settings.ini).
 *
 * La barra de menús y los diálogos vienen de Moskva 89 (ui_menu.c,
 * ui_font.c): se dibujan dentro de la ventana para verse igual en todos los
 * sistemas. Mientras un menú o un diálogo está abierto, el juego se pausa.
 *
 * Con -DGB_INTERP (el objetivo `gb` del Makefile: `gb juego.gb`, como `nes
 * juego.nes` en NESRecomp) es el intérprete con ventana: acepta cualquier ROM,
 * sin código recompilado, y el título, el nombre y el SHA1 salen de la ROM
 * abierta en vez de gen/info.c. No guarda copia de la ROM; la partida, los
 * estados y lo exportado van a la carpeta de datos "interprete", con el nombre
 * del archivo de la ROM.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "export.h"
#include "files.h"
#include "i18n.h"
#include "iconos.h"
#include "inputs.h"
#include "png.h"
#ifdef GB_INTERP
#include "gb.h"
/* Lo que en un juego recompilado viene de gen/info.c; aquí se llena al abrir la ROM */
static char interp_title[17] = "Game Boy", interp_name[96] = "gb", interp_sha1[41];
#define recomp_rom_title interp_title
#define recomp_name interp_name
#define recomp_rom_sha1 interp_sha1
#else
#include "recomp.h"
#endif
#include "settings.h"
#include "sha1.h"
#include "ui_font.h"
#include "ui_menu.h"

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, recomp_rom_title, msg, NULL);
    exit(1);
}

static void inform(const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, recomp_rom_title, msg, NULL);
}

/* ------------------------------------------------------------------ */
/* ROM                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;
    size_t size;
} Rom;

/* 1: es la ROM esperada; 0: otra ROM; -1: no se pudo leer. El intérprete acepta cualquiera. */
static int load_rom(const char *path, Rom *rom)
{
    rom->data = u8_read_file(path, &rom->size);
    if (!rom->data) return -1;
#ifdef GB_INTERP
    return 1;
#else
    char sha1[41];
    sha1_hex(rom->data, rom->size, sha1);
    return strcmp(sha1, recomp_rom_sha1) == 0;
#endif
}

#ifdef GB_INTERP
/* Título (cabecera), nombre (el del archivo, para la partida y los estados) y SHA1 de la ROM abierta */
static void interp_identity(const char *path, const Rom *rom)
{
    int k = 0;
    for (int i = 0x134; i < 0x144 && rom->data[i]; i++) {
        unsigned char ch = rom->data[i];
        interp_title[k++] = ch >= 0x20 && ch < 0x7F ? (char)ch : '?';
    }
    while (k > 0 && interp_title[k - 1] == ' ') k--;
    interp_title[k] = 0;
    if (!k) snprintf(interp_title, sizeof interp_title, "Game Boy");
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t n = strlen(base);
    if (n >= sizeof interp_name) n = sizeof interp_name - 1;
    memcpy(interp_name, base, n);
    interp_name[n] = 0;
    char *dot = strrchr(interp_name, '.');
    if (dot && dot != interp_name) *dot = 0;
    sha1_hex(rom->data, rom->size, interp_sha1);
}
#endif

static void wrong_rom_message(const char *path, char *msg, size_t size)
{
    snprintf(msg, size, tr(T_ROM_WRONG_FMT), path, recomp_rom_title, recomp_rom_sha1);
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

/* Ruta de la ROM que eligió el usuario (el intérprete nombra la partida por ella) */
static char chosen_rom[1024] = "rom.gb";

/* Pide la ROM hasta que den una válida. 0 si el usuario cancela. */
static int ask_for_rom(Rom *rom)
{
    char msg[4096], path[1024];
    int first = 1;
    for (;;) {
        int got = ask_rom_path(tr(first ? T_ROM_CHOOSE : T_ROM_CHOOSE_AGAIN), path, sizeof path);
        if (!got) {
            /* Sin diálogo nativo (o cancelado en Windows: se ofrece arrastrar) */
            snprintf(msg, sizeof msg, tr(T_ROM_DROP_FMT), recomp_rom_title);
            if (first) inform(msg);
            if (!wait_for_drop(path, sizeof path)) return 0;
        }
        first = 0;
        int r = load_rom(path, rom);
        if (r == 1) { snprintf(chosen_rom, sizeof chosen_rom, "%s", path); return 1; }
        if (r == 0) {
            free(rom->data);
            wrong_rom_message(path, msg, sizeof msg);
        } else {
            snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), path);
        }
        inform(msg);
    }
}

/* ------------------------------------------------------------------ */
/* Cobertura (--uncovered)                                             */
/* ------------------------------------------------------------------ */

#define MAX_BANKS 512
static u8 *uncovered[MAX_BANKS];

static void mark_uncovered(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 bank = pc < 0x4000 ? g->mbc.bank0 : g->mbc.bankx;
    if (pc >= 0x8000 || bank >= MAX_BANKS) return;
    if (!uncovered[bank]) uncovered[bank] = calloc(0x4000, 1);
    if (uncovered[bank]) uncovered[bank][pc & 0x3FFF] = 1;
}

static void write_uncovered(const char *path)
{
    FILE *f = u8_fopen(path, "a");
    if (!f) return;
    for (u32 b = 0; b < MAX_BANKS; b++)
        for (u32 off = 0; uncovered[b] && off < 0x4000; off++)
            if (uncovered[b][off])
                fprintf(f, "%02X:%04X\n", b, (unsigned)(b == 0 ? off : 0x4000 + off));
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Partidas                                                            */
/* ------------------------------------------------------------------ */

static int save_game(GB *g, const char *path)
{
    size_t size;
    u8 *data = gb_save_serialize(g, (long long)time(NULL), &size);
    int ok = data && u8_write_file(path, data, size);
    free(data);
    if (ok) g->sram_dirty = 0;
    return ok;
}

static void load_game(GB *g, const char *path)
{
    size_t size;
    u8 *data = u8_read_file(path, &size);
    if (data) gb_save_deserialize(g, data, size, (long long)time(NULL));
    free(data);
}

/* ------------------------------------------------------------------ */
/* Controles y colores                                                 */
/* ------------------------------------------------------------------ */

/*
 * Botones del jugador `player` (0-3): el teclado (el jugador 1 con las
 * flechas, el 2 con WASD) y el control número `player` que se haya conectado.
 */
static u8 read_pad(SDL_GameController *pad, int player)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u8 p = 0;
    if (player == 0) {
        if (k[SDL_SCANCODE_RIGHT]) p |= PAD_RIGHT;
        if (k[SDL_SCANCODE_LEFT]) p |= PAD_LEFT;
        if (k[SDL_SCANCODE_UP]) p |= PAD_UP;
        if (k[SDL_SCANCODE_DOWN]) p |= PAD_DOWN;
        if (k[SDL_SCANCODE_X]) p |= PAD_A;
        if (k[SDL_SCANCODE_Z]) p |= PAD_B;
        if (k[SDL_SCANCODE_RETURN]) p |= PAD_START;
        if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) p |= PAD_SELECT;
    } else if (player == 1) {
        if (k[SDL_SCANCODE_D]) p |= PAD_RIGHT;
        if (k[SDL_SCANCODE_A]) p |= PAD_LEFT;
        if (k[SDL_SCANCODE_W]) p |= PAD_UP;
        if (k[SDL_SCANCODE_S]) p |= PAD_DOWN;
        if (k[SDL_SCANCODE_G]) p |= PAD_A;
        if (k[SDL_SCANCODE_F]) p |= PAD_B;
        if (k[SDL_SCANCODE_E]) p |= PAD_START;
        if (k[SDL_SCANCODE_Q]) p |= PAD_SELECT;
    }

    if (pad) {
        /* A y B en la posición de Nintendo: el A del Game Boy es el botón de
         * la derecha (B en un control de Xbox) */
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p |= PAD_RIGHT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  p |= PAD_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    p |= PAD_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  p |= PAD_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))     p |= PAD_A;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A))     p |= PAD_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) p |= PAD_START;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK))  p |= PAD_SELECT;
    }

    /* La cruceta física no permite opuestos a la vez */
    if ((p & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) p &= (u8)~(PAD_LEFT | PAD_RIGHT);
    if ((p & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) p &= (u8)~(PAD_UP | PAD_DOWN);
    return p;
}

/* Tonos del LCD del DMG, del más claro al más oscuro (como en Moskva 89) */
static const Uint32 dmg_palettes[3][4] = {
    { 0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F },     /* verde */
    { 0xFFC4CFA1, 0xFF8B956D, 0xFF4D533C, 0xFF1F1F1F },     /* Pocket */
    { 0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000 },     /* grises */
};

/* Color del Game Boy Color (RGB555, rojo en los bits bajos) a ARGB8888 */
static Uint32 rgb555_to_argb(u16 v)
{
    Uint32 r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

/* Combinaciones de botones del modo de compatibilidad, en el orden del menú */
static const struct { const char *code; TextId dir; int button; } combos[12] = {
    { "up", T_DIR_UP, 0 },      { "down", T_DIR_DOWN, 0 },
    { "left", T_DIR_LEFT, 0 },  { "right", T_DIR_RIGHT, 0 },
    { "up+a", T_DIR_UP, 'A' },  { "down+a", T_DIR_DOWN, 'A' },
    { "left+a", T_DIR_LEFT, 'A' }, { "right+a", T_DIR_RIGHT, 'A' },
    { "up+b", T_DIR_UP, 'B' },  { "down+b", T_DIR_DOWN, 'B' },
    { "left+b", T_DIR_LEFT, 'B' }, { "right+b", T_DIR_RIGHT, 'B' },
};

/* ------------------------------------------------------------------ */
/* Aplicación                                                          */
/* ------------------------------------------------------------------ */

enum {
    A_NONE, A_RESET, A_PAUSE, A_SAVE_STATE, A_LOAD_STATE, A_SLOT_1, A_SLOT_4 = A_SLOT_1 + 3,
    A_CONSOLE_DMG, A_CONSOLE_CGB, A_CONSOLE_SGB, A_LINK, A_QUIT,
    A_DMG_PALETTE_0, A_DMG_PALETTE_2 = A_DMG_PALETTE_0 + 2,
    A_COMPAT_AUTO, A_COMPAT_0, A_COMPAT_11 = A_COMPAT_0 + 11,
    A_SGB_BORDER, A_SCALE_1, A_SCALE_6 = A_SCALE_1 + 5, A_FULLSCREEN,
    A_SOUND, A_VOLUME_25, A_VOLUME_100 = A_VOLUME_25 + 3,
    A_REC_MUSIC, A_COLLECT_TILES, A_SCREENSHOT, A_OPEN_EXPORTS,
    A_HELP_CONTROLS, A_HELP_GAME, A_ABOUT,
    A_LINK_AUDIO_0, A_LINK_AUDIO_3 = A_LINK_AUDIO_0 + 3,
    A_LANGUAGE_0,
};

enum { TAG_INFO = 1, TAG_CONSOLE };

typedef struct {
    GB gb;
    Rom rom;                /* se conserva para reiniciar o cambiar de consola */
    int model;              /* GB_MODEL_* con que se encendió */
    int forced_model;       /* --model o la grabación (GB_MODEL_AUTO si no) */
    const char *forced_palette;
    int interp_only;
    int pending_model;      /* consola elegida en el menú, a confirmar */

    int paused;
    int hold_input;         /* ignorar controles hasta que se suelten */
    int fullscreen;
    int running;
    int mouse_y;
    Settings cfg;
    char *pref;
    char settings_path[1024], save_path[1024];

    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tex_w, tex_h;
    SDL_AudioDeviceID audio;
    SDL_GameController *pads[4];    /* control de cada jugador, en orden de conexión */

    /* Cable link local: un segundo Game Boy con la misma ROM, a la derecha */
    int linked;
    GB gb2;
    GbLink link;
    char save_path2[1024];
    Uint32 labels_until;    /* "J1"/"J2" sobre las pantallas hasta este instante */

    MenuBar bar;
    Dialog dlg;
    char title[256];
    char notice[128];       /* aviso temporal en el título de la ventana */
    Uint32 notice_until;

    /* Grabaciones y cobertura */
    InputScript script;
    int play;
    FILE *record;
    int last_pad;
    long frame;
    const char *uncovered_path;

    /* Exportar música y gráficos */
    MusicRecorder music;
    TileCollector tiles;
    char export_dir[1100];
} App;

/* Para el gancho de sonido del runtime, que no lleva contexto */
static App *the_app;

static void on_apu_write(GB *g, u16 addr, u8 value)
{
    music_register(&the_app->music, g, addr, value);
}

/* Ruta sin extensión para un archivo exportado: carpeta, juego, tipo y hora */
static int export_base(App *app, const char *kind, char *out, size_t size)
{
    if (!u8_mkdir(app->export_dir)) return 0;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char stamp[32] = "0";
    if (t) strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", t);
    snprintf(out, size, "%s%s_%s_%s", app->export_dir, recomp_name, kind, stamp);
    return 1;
}

static const char *file_name(const char *path)
{
    const char *s = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    return s;
}

/* Pruebas de la interfaz: --ui-shot archivo.png [--ui-frames N] [--ui-menu N]
 * [--ui-action N] [--ui-export] corre N cuadros sin ventana visible (con
 * --ui-export, grabando música y gráficos), abre un menú o hace
 * una acción, guarda la imagen y sale */
static const char *ui_shot_path;

static int ui_scale(int width)
{
    return width >= 640 ? 2 : 1;
}

/* ¿Puede correr el juego en esta consola? Depende de la cabecera */
static int model_allowed(const Rom *rom, int model)
{
    int cgb_flag = rom->data[0x143];
    int sgb_flag = rom->data[0x146] == 0x03 && rom->data[0x14B] == 0x33;
    switch (model) {
    case GB_MODEL_DMG: return (cgb_flag & 0xC0) != 0xC0;   /* no si es solo para color */
    case GB_MODEL_CGB: return 1;
    case GB_MODEL_SGB: return sgb_flag && (cgb_flag & 0xC0) != 0xC0;
    default:           return 0;
    }
}

/* Consola con que se enciende: --model o la grabación, si no la preferencia,
 * si no la que mejor le queda (color, SGB, o un GBC que colorea el juego) */
static int choose_model(const App *app)
{
    int m = app->forced_model;
    if (m == GB_MODEL_AUTO) m = model_from_name(app->cfg.model);
    if (m >= GB_MODEL_DMG && model_allowed(&app->rom, m)) return m;
    if (app->rom.data[0x143] & 0x80) return GB_MODEL_CGB;
    if (model_allowed(&app->rom, GB_MODEL_SGB)) return GB_MODEL_SGB;
    return GB_MODEL_CGB;
}

static int is_compat(const GB *g)
{
    return g->cgb_hw && !g->cgb;
}

/* Separación entre las dos pantallas con el cable link */
#define LINK_GAP 8

/* Tamaño de la imagen: 160×144, o 256×224 con el borde del SGB; con el cable
 * link, las dos pantallas lado a lado (sin borde) */
static void game_size(const App *app, int *w, int *h)
{
    if (app->linked) {
        *w = 2 * GB_SCREEN_W + LINK_GAP;
        *h = GB_SCREEN_H;
        return;
    }
    int border = app->gb.sgb && app->cfg.sgb_border;
    *w = border ? SGB_SCREEN_W : GB_SCREEN_W;
    *h = border ? SGB_SCREEN_H : GB_SCREEN_H;
}

static void resize_window(App *app)
{
    if (app->fullscreen) return;
    int gw, gh;
    game_size(app, &gw, &gh);
    int w = gw * app->cfg.scale;
    SDL_SetWindowSize(app->win, w, gh * app->cfg.scale + menubar_height(ui_scale(w)));
}

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

/* Paleta del modo de compatibilidad del GBC: la misma para los dos Game Boy
 * del cable link (las del DMG se aplican al dibujar, a los dos) */
static void apply_palette(App *app)
{
    if (!is_compat(&app->gb)) return;
    const char *p = app->forced_palette ? app->forced_palette : app->cfg.compat_palette;
    GB *gbs[2] = { &app->gb, app->linked ? &app->gb2 : NULL };
    for (int i = 0; i < 2; i++)
        if (gbs[i] && !gb_compat_palette(gbs[i], p)) gb_compat_palette(gbs[i], "auto");
}

/* --- Exportar --- */

static void stop_music(App *app, int report)
{
    if (!music_active(&app->music)) return;
    int seconds = (int)((app->gb.rt - app->music.start_rt) / GB_CPU_HZ);
    music_stop(&app->music, &app->gb, recomp_rom_title);
    app->gb.apu_log = NULL;
    if (report) {
        char text[2048];
        const char *name = file_name(app->music.base);
        snprintf(text, sizeof text, tr(T_MUSIC_SAVED_FMT), seconds, name, name);
        message(app, tr(T_EXPORT_TITLE), text);
    }
}

static void toggle_music(App *app)
{
    if (music_active(&app->music)) {
        stop_music(app, 1);
        return;
    }
    char base[1300], text[1400];
    if (!export_base(app, "musica", base, sizeof base) || !music_start(&app->music, &app->gb, base)) {
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), app->export_dir);
        message(app, tr(T_EXPORT_TITLE), text);
        return;
    }
    app->gb.apu_log = on_apu_write;
}

static void stop_tiles(App *app, int report)
{
    if (!app->tiles.active) return;
    char base[1300], text[2048];
    int count = app->tiles.count;
    if (!count) {
        tiles_stop(&app->tiles, "");
        if (report) message(app, tr(T_EXPORT_TITLE), tr(T_TILES_NONE));
        return;
    }
    if (!export_base(app, "mosaicos", base, sizeof base)) {
        tiles_stop(&app->tiles, "");
        snprintf(text, sizeof text, tr(T_EXPORT_FAILED_FMT), app->export_dir);
        if (report) message(app, tr(T_EXPORT_TITLE), text);
        return;
    }
    int sheets = tiles_stop(&app->tiles, base);
    snprintf(text, sizeof text, tr(T_TILES_SAVED_FMT), count, sheets, file_name(base));
    if (report) message(app, tr(T_EXPORT_TITLE), text);
}

static void toggle_tiles(App *app)
{
    if (app->tiles.active) stop_tiles(app, 1);
    else {
        tiles_start(&app->tiles);
        tiles_scan(&app->tiles, &app->gb);
    }
}

static void game_size(const App *app, int *w, int *h);
static void compose(const App *app, Uint32 *dst, int gw, int gh);

static void screenshot(App *app)
{
    static Uint32 rgb[SGB_SCREEN_W * SGB_SCREEN_H];
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

/* Enciende (o reinicia) la consola con el modelo elegido */
static void build_menus(App *app);

/* Ícono de la ventana según la consola encendida: Game Boy, Game Boy Color
 * (también en el modo de compatibilidad) o Super Game Boy */
static void set_window_icon(App *app)
{
    if (!app->win) return;
    const unsigned char *rgba = app->gb.sgb ? icono_sgb : app->gb.cgb_hw ? icono_gbc : icono_gb;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)rgba, ICONO_LADO, ICONO_LADO, 32,
                                                        ICONO_LADO * 4, SDL_PIXELFORMAT_RGBA32);
    if (!s) return;
    SDL_SetWindowIcon(app->win, s);
    SDL_FreeSurface(s);
}

/* --- Cable link local --- */

/* Enciende el Game Boy de la derecha (mismo modelo, su propia partida) y lo
 * conecta por cable */
static void link_start(App *app)
{
    const char *err = gb_init_model(&app->gb2, app->rom.data, (u32)app->rom.size, app->model);
    if (err) fail(err);
#ifndef GB_INTERP
    if (!app->interp_only) app->gb2.recomp_lookup = recomp_lookup;
#endif
    if (app->uncovered_path) app->gb2.trace = mark_uncovered;
    if (gb_has_battery(&app->gb2)) load_game(&app->gb2, app->save_path2);
    app->gb2.apu.enabled = 1;
    gb_link_connect(&app->link, &app->gb, &app->gb2);
    app->linked = 1;
    app->labels_until = SDL_GetTicks() + 4000;
    apply_palette(app);
}

static void link_stop(App *app)
{
    if (!app->linked) return;
    gb_link_disconnect(&app->link);
    if (gb_has_battery(&app->gb2)) save_game(&app->gb2, app->save_path2);
    gb_free(&app->gb2);
    memset(&app->gb2, 0, sizeof app->gb2);
    app->linked = 0;
}

static void power_on(App *app)
{
    int was_linked = app->linked;
    link_stop(app);         /* se vuelve a conectar con la consola nueva */
    if (app->gb.rom) {
        stop_music(app, 1);     /* la grabación sería de dos corridas */
        if (gb_has_battery(&app->gb)) save_game(&app->gb, app->save_path);
        gb_free(&app->gb);
    }
    app->model = choose_model(app);
    const char *err = gb_init_model(&app->gb, app->rom.data, (u32)app->rom.size, app->model);
    if (err) fail(err);
    apply_palette(app);
#ifndef GB_INTERP
    if (!app->interp_only) app->gb.recomp_lookup = recomp_lookup;
#endif
    app->gb.apu.enabled = 1;
    if (app->uncovered_path) app->gb.trace = mark_uncovered;
    if (gb_has_battery(&app->gb)) load_game(&app->gb, app->save_path);
    if (was_linked) link_start(app);
    build_menus(app);
    set_window_icon(app);
    if (app->win) resize_window(app);
}

static void toggle_link(App *app)
{
    if (app->linked) link_stop(app);
    else link_start(app);
    build_menus(app);       /* el menú Audio cambia con el cable */
    resize_window(app);
    notify(app, tr(app->linked ? T_TITLE_LINK_ON : T_TITLE_LINK_OFF));
}

/* --- Estados guardados --- */

static void state_path(const App *app, int slot, char *out, size_t size)
{
    snprintf(out, size, "%s%s.estado%d", app->pref, recomp_name, slot);
}

/*
 * Con el cable, un estado guarda los dos Game Boy: "GBRL", versión, el
 * tiempo real de cada uno al conectar el cable (GbLink.base) y los dos
 * estados del runtime (largo de 4 bytes y datos). Sin cable es el estado del
 * runtime tal cual ("GBRS").
 */
#define LINK_STATE_HEADER (4 + 4 + 8 + 8)

static void put32(u8 *p, u64 v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i));
}

static u64 get32(const u8 *p, int n)
{
    u64 v = 0;
    for (int i = 0; i < n; i++) v |= (u64)p[i] << (8 * i);
    return v;
}

static u8 *link_state_save(App *app, size_t *size)
{
    size_t n1, n2;
    u8 *a = gb_state_save(&app->gb, &n1), *b = gb_state_save(&app->gb2, &n2);
    u8 *data = a && b ? malloc(LINK_STATE_HEADER + 8 + n1 + n2) : NULL;
    if (data) {
        memcpy(data, "GBRL", 4);
        put32(data + 4, 1, 4);
        put32(data + 8, app->link.base[0], 8);
        put32(data + 16, app->link.base[1], 8);
        put32(data + LINK_STATE_HEADER, n1, 4);
        memcpy(data + LINK_STATE_HEADER + 4, a, n1);
        put32(data + LINK_STATE_HEADER + 4 + n1, n2, 4);
        memcpy(data + LINK_STATE_HEADER + 8 + n1, b, n2);
        *size = LINK_STATE_HEADER + 8 + n1 + n2;
    }
    free(a);
    free(b);
    return data;
}

/* 1 si se cargó; 0 si el archivo no es un par de esta ROM */
static int link_state_load(App *app, const u8 *data, size_t size)
{
    if (size < LINK_STATE_HEADER + 8 || memcmp(data, "GBRL", 4) != 0 || get32(data + 4, 4) != 1) return 0;
    size_t n1 = (size_t)get32(data + LINK_STATE_HEADER, 4);
    if (LINK_STATE_HEADER + 8 + n1 > size) return 0;
    size_t n2 = (size_t)get32(data + LINK_STATE_HEADER + 4 + n1, 4);
    if (LINK_STATE_HEADER + 8 + n1 + n2 != size) return 0;
    const u8 *a = data + LINK_STATE_HEADER + 4, *b = data + LINK_STATE_HEADER + 8 + n1;
    /* gb_state_load revisa el encabezado (ROM, versión, tamaños) antes de
     * tocar nada; los dos se guardaron juntos, así que fallan o pasan juntos */
    if (!gb_state_load(&app->gb, a, n1) || !gb_state_load(&app->gb2, b, n2)) return 0;
    app->link.base[0] = get32(data + 8, 8);
    app->link.base[1] = get32(data + 16, 8);
    return 1;
}

static void save_state(App *app)
{
    char path[1100], text[1300];
    state_path(app, app->cfg.slot, path, sizeof path);
    size_t size;
    u8 *data = app->linked ? link_state_save(app, &size) : gb_state_save(&app->gb, &size);
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
    char path[1100], text[256];
    state_path(app, app->cfg.slot, path, sizeof path);
    size_t size;
    u8 *data = u8_read_file(path, &size);
    if (!data) {
        snprintf(text, sizeof text, tr(T_STATE_EMPTY_FMT), app->cfg.slot);
        message(app, tr(T_STATE_TITLE), text);
        return;
    }
    int pair = size >= 4 && memcmp(data, "GBRL", 4) == 0;
    if (!pair && app->linked) {
        snprintf(text, sizeof text, tr(T_STATE_SINGLE), app->cfg.slot);
        message(app, tr(T_STATE_TITLE), text);
        free(data);
        return;
    }
    /* Un estado de otra consola cambia el tamaño de la imagen y los menús */
    stop_music(app, 0);     /* el tiempo de la grabación saltaría */
    int was_sgb = app->gb.sgb, was_compat = is_compat(&app->gb), was_cgb = app->gb.cgb_hw;
    int connected = 0;
    if (pair && !app->linked) {
        link_start(app);    /* un estado con cable: se conecta el cable */
        connected = 1;
    }
    int ok = pair ? link_state_load(app, data, size) : gb_state_load(&app->gb, data, size);
    if (ok) {
        snprintf(text, sizeof text, tr(connected ? T_STATE_NEEDS_LINK : T_TITLE_LOADED_FMT), app->cfg.slot);
        notify(app, text);
        if (connected || was_sgb != app->gb.sgb || was_compat != is_compat(&app->gb) || was_cgb != app->gb.cgb_hw) {
            app->model = app->gb.sgb ? GB_MODEL_SGB : app->gb.cgb_hw ? GB_MODEL_CGB : GB_MODEL_DMG;
            build_menus(app);
            set_window_icon(app);
            resize_window(app);
        }
    } else {
        if (connected) {
            link_stop(app);
            build_menus(app);
            resize_window(app);
        }
        snprintf(text, sizeof text, tr(T_STATE_BAD_FMT), app->cfg.slot);
        message(app, tr(T_STATE_TITLE), text);
    }
    free(data);
}

/* --- Menús --- */

#define SEPARATOR(m, n) m[n++] = (MenuItem){ "", NULL, A_NONE, ITEM_SEPARATOR }
#define ITEM(m, n, text, key, action, kind) m[n++] = (MenuItem){ text, key, action, kind }
#define COUNT(a) (int)(sizeof a / sizeof a[0])

/* Se arman con los textos del idioma actual y según la consola encendida */
static MenuItem menu_game[20], menu_video[12], menu_palette[16], menu_audio[12];
static MenuItem menu_export[5], menu_language[LANG_COUNT], menu_help[3];
static Menu menus[7];
static char slot_labels[4][40], compat_labels[12][48], size_labels[6][40], volume_labels[4][40];

static void build_menus(App *app)
{
    int n = 0;
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
    ITEM(menu_game, n, tr(T_CONSOLE_DMG), NULL, A_CONSOLE_DMG, ITEM_RADIO);
    ITEM(menu_game, n, tr(T_CONSOLE_CGB), NULL, A_CONSOLE_CGB, ITEM_RADIO);
    ITEM(menu_game, n, tr(T_CONSOLE_SGB), NULL, A_CONSOLE_SGB, ITEM_RADIO);
    SEPARATOR(menu_game, n);
    ITEM(menu_game, n, tr(T_LINK), NULL, A_LINK, ITEM_CHECK);
    SEPARATOR(menu_game, n);
    ITEM(menu_game, n, tr(T_QUIT), "Alt+F4", A_QUIT, ITEM_ACTION);
    menus[0] = (Menu){ tr(T_MENU_GAME), menu_game, n };

    int k = 1;

    /* Video: tamaño, pantalla completa y, en SGB, el borde */
    n = 0;
    const GB *g = &app->gb;
    if (g->sgb) {
        ITEM(menu_video, n, tr(T_SGB_BORDER), NULL, A_SGB_BORDER, ITEM_CHECK);
        SEPARATOR(menu_video, n);
    }
    for (int i = 0; i < 6; i++) {
        snprintf(size_labels[i], sizeof size_labels[i], tr(T_SIZE_FMT), i + 1);
        ITEM(menu_video, n, size_labels[i], NULL, A_SCALE_1 + i, ITEM_RADIO);
    }
    SEPARATOR(menu_video, n);
    ITEM(menu_video, n, tr(T_FULLSCREEN), "F11", A_FULLSCREEN, ITEM_CHECK);
    menus[k++] = (Menu){ tr(T_MENU_VIDEO), menu_video, n };

    /* Paleta: solo en DMG (tonos del LCD) y en compatibilidad GBC (las de la
     * boot ROM); en modo color y en SGB los colores los pone el juego */
    n = 0;
    if (!g->cgb_hw && !g->sgb) {
        ITEM(menu_palette, n, tr(T_PALETTE_DMG), "F2", A_DMG_PALETTE_0, ITEM_RADIO);
        ITEM(menu_palette, n, tr(T_PALETTE_POCKET), NULL, A_DMG_PALETTE_0 + 1, ITEM_RADIO);
        ITEM(menu_palette, n, tr(T_PALETTE_GRAY), NULL, A_DMG_PALETTE_0 + 2, ITEM_RADIO);
    } else if (is_compat(g)) {
        ITEM(menu_palette, n, tr(T_PALETTE_GAME), "F2", A_COMPAT_AUTO, ITEM_RADIO);
        SEPARATOR(menu_palette, n);
        for (int i = 0; i < 12; i++) {
            if (combos[i].button)
                snprintf(compat_labels[i], sizeof compat_labels[i], "%s + %c", tr(combos[i].dir), combos[i].button);
            else
                snprintf(compat_labels[i], sizeof compat_labels[i], "%s", tr(combos[i].dir));
            ITEM(menu_palette, n, compat_labels[i], NULL, A_COMPAT_0 + i, ITEM_RADIO);
        }
    }
    if (n) menus[k++] = (Menu){ tr(T_MENU_PALETTE), menu_palette, n };

    n = 0;
    ITEM(menu_audio, n, tr(T_SOUND), "Ctrl+M", A_SOUND, ITEM_CHECK);
    SEPARATOR(menu_audio, n);
    for (int i = 0; i < 4; i++) {
        snprintf(volume_labels[i], sizeof volume_labels[i], tr(T_VOLUME_FMT), 25 * (i + 1));
        ITEM(menu_audio, n, volume_labels[i], NULL, A_VOLUME_25 + i, ITEM_RADIO);
    }
    if (app->linked) {
        /* Con el cable: de qué Game Boy viene el sonido */
        SEPARATOR(menu_audio, n);
        ITEM(menu_audio, n, tr(T_LINK_AUDIO_P1), NULL, A_LINK_AUDIO_0, ITEM_RADIO);
        ITEM(menu_audio, n, tr(T_LINK_AUDIO_P2), NULL, A_LINK_AUDIO_0 + 1, ITEM_RADIO);
        ITEM(menu_audio, n, tr(T_LINK_AUDIO_BOTH), NULL, A_LINK_AUDIO_0 + 2, ITEM_RADIO);
        ITEM(menu_audio, n, tr(T_LINK_AUDIO_SPLIT), NULL, A_LINK_AUDIO_0 + 3, ITEM_RADIO);
    }
    menus[k++] = (Menu){ tr(T_MENU_AUDIO), menu_audio, n };

    n = 0;
    ITEM(menu_export, n, tr(T_REC_MUSIC), "Ctrl+G", A_REC_MUSIC, ITEM_CHECK);
    ITEM(menu_export, n, tr(T_COLLECT_TILES), "Ctrl+T", A_COLLECT_TILES, ITEM_CHECK);
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

static int compat_index(const char *code)
{
    for (int i = 0; i < 12; i++)
        if (!strcmp(combos[i].code, code)) return i;
    return -1;
}

static int menu_state(void *ctx, int action, int *checked)
{
    App *app = ctx;
    /* Con una grabación, lo que cambia la partida desfasaría los botones */
    int replay = app->play || app->record;
    switch (action) {
    case A_RESET:        return !replay;
    case A_PAUSE:        *checked = app->paused; return 1;
    case A_LOAD_STATE:   return !replay;
    case A_LINK:         *checked = app->linked; return !replay;
    case A_CONSOLE_DMG: case A_CONSOLE_CGB: case A_CONSOLE_SGB: {
        static const int models[3] = { GB_MODEL_DMG, GB_MODEL_CGB, GB_MODEL_SGB };
        int m = models[action - A_CONSOLE_DMG];
        *checked = app->model == m;
        return !replay && model_allowed(&app->rom, m);
    }
    case A_COMPAT_AUTO:
        *checked = compat_index(app->forced_palette ? app->forced_palette : app->cfg.compat_palette) < 0;
        return 1;
    case A_SGB_BORDER:   *checked = app->cfg.sgb_border; return 1;
    case A_FULLSCREEN:   *checked = app->fullscreen; return 1;
    case A_SOUND:        *checked = app->cfg.sound; return 1;
    case A_REC_MUSIC:    *checked = music_active(&app->music); return 1;
    case A_COLLECT_TILES: *checked = app->tiles.active; return 1;
    default:
        if (action >= A_SLOT_1 && action <= A_SLOT_4)
            *checked = app->cfg.slot == action - A_SLOT_1 + 1;
        else if (action >= A_DMG_PALETTE_0 && action <= A_DMG_PALETTE_2)
            *checked = app->cfg.dmg_palette == action - A_DMG_PALETTE_0;
        else if (action >= A_COMPAT_0 && action <= A_COMPAT_11)
            *checked = compat_index(app->forced_palette ? app->forced_palette : app->cfg.compat_palette)
                       == action - A_COMPAT_0;
        else if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            *checked = app->cfg.scale == action - A_SCALE_1 + 1;
            return !app->fullscreen;
        } else if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            *checked = app->cfg.volume == 25 * (action - A_VOLUME_25 + 1);
            return app->cfg.sound;
        } else if (action >= A_LINK_AUDIO_0 && action <= A_LINK_AUDIO_3) {
            *checked = app->cfg.link_audio == action - A_LINK_AUDIO_0;
            return app->cfg.sound;
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

static void set_compat_palette(App *app, const char *code)
{
    app->forced_palette = NULL;
    snprintf(app->cfg.compat_palette, sizeof app->cfg.compat_palette, "%s", code);
    apply_palette(app);
}

/* F2: la siguiente paleta de la consola encendida */
static void next_palette(App *app)
{
    if (is_compat(&app->gb)) {
        int i = compat_index(app->forced_palette ? app->forced_palette : app->cfg.compat_palette);
        set_compat_palette(app, i == 11 ? "auto" : combos[i + 1].code);
    } else if (!app->gb.cgb_hw && !app->gb.sgb) {
        app->cfg.dmg_palette = (app->cfg.dmg_palette + 1) % 3;
    }
}

static const char *model_label(const GB *g)
{
    if (g->sgb) return tr(T_MODEL_SGB);
    if (g->cgb) return tr(T_MODEL_CGB);
    if (g->cgb_hw) return tr(T_MODEL_COMPAT);
    return tr(T_MODEL_DMG);
}

static void do_action(App *app, int action)
{
    switch (action) {
    case A_RESET:        power_on(app); break;
    case A_PAUSE:        app->paused = !app->paused; break;
    case A_SAVE_STATE:   save_state(app); break;
    case A_LOAD_STATE:   load_state(app); break;
    case A_LINK:         toggle_link(app); break;
    case A_CONSOLE_DMG: case A_CONSOLE_CGB: case A_CONSOLE_SGB: {
        static const int models[3] = { GB_MODEL_DMG, GB_MODEL_CGB, GB_MODEL_SGB };
        app->pending_model = models[action - A_CONSOLE_DMG];
        if (app->pending_model == app->model) break;
        menubar_close(&app->bar);
        dialog_open(&app->dlg, DIALOG_CONFIRM, TAG_CONSOLE, tr(T_CONSOLE_TITLE), tr(T_CONSOLE_TEXT));
        break;
    }
    case A_QUIT:         app->running = 0; break;
    case A_COMPAT_AUTO:  set_compat_palette(app, "auto"); break;
    case A_SGB_BORDER:
        app->cfg.sgb_border = !app->cfg.sgb_border;
        resize_window(app);
        break;
    case A_FULLSCREEN:   toggle_fullscreen(app); break;
    case A_SOUND:        app->cfg.sound = !app->cfg.sound; break;
    case A_REC_MUSIC:    toggle_music(app); break;
    case A_COLLECT_TILES: toggle_tiles(app); break;
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
        u64 total = app->gb.instr_recomp + app->gb.instr_interp;
        /* Una ruta larga no cabe en el diálogo: se deja el final */
        size_t len = strlen(app->pref);
        if (len > 44) snprintf(folder, sizeof folder, "…%s", app->pref + len - 43);
        else snprintf(folder, sizeof folder, "%s", app->pref);
        snprintf(text, sizeof text, tr(T_GAME_TEXT_FMT), recomp_rom_title, recomp_rom_sha1,
                 model_label(&app->gb), total ? 100.0 * app->gb.instr_recomp / total : 0.0, folder);
        message(app, tr(T_GAME_TITLE), text);
        break;
    }
    case A_ABOUT: {
        char text[1024];
        snprintf(text, sizeof text, tr(T_ABOUT_TEXT_FMT), recomp_rom_title);
        message(app, recomp_rom_title, text);
        break;
    }
    default:
        if (action >= A_SLOT_1 && action <= A_SLOT_4) {
            app->cfg.slot = action - A_SLOT_1 + 1;
        } else if (action >= A_DMG_PALETTE_0 && action <= A_DMG_PALETTE_2) {
            app->cfg.dmg_palette = action - A_DMG_PALETTE_0;
        } else if (action >= A_COMPAT_0 && action <= A_COMPAT_11) {
            set_compat_palette(app, combos[action - A_COMPAT_0].code);
        } else if (action >= A_SCALE_1 && action <= A_SCALE_6) {
            app->cfg.scale = action - A_SCALE_1 + 1;
            resize_window(app);
        } else if (action >= A_VOLUME_25 && action <= A_VOLUME_100) {
            app->cfg.volume = 25 * (action - A_VOLUME_25 + 1);
        } else if (action >= A_LINK_AUDIO_0 && action <= A_LINK_AUDIO_3) {
            app->cfg.link_audio = action - A_LINK_AUDIO_0;
        } else if (action >= A_LANGUAGE_0 && action < A_LANGUAGE_0 + LANG_COUNT) {
            i18n_set((Language)(action - A_LANGUAGE_0));
            snprintf(app->cfg.language, sizeof app->cfg.language, "%s", i18n_code(i18n_get()));
            build_menus(app);
            app->title[0] = 0;      /* que se vuelva a poner el título */
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
    case SDLK_m:      if (ctrl) { do_action(app, A_SOUND); return 1; } break;
    case SDLK_p:      do_action(app, A_PAUSE); return 1;
    case SDLK_F5:     do_action(app, A_SAVE_STATE); return 1;
    case SDLK_F8:     if (!replay) { do_action(app, A_LOAD_STATE); return 1; } break;
    case SDLK_F11:    do_action(app, A_FULLSCREEN); return 1;
    case SDLK_RETURN: if (alt) { do_action(app, A_FULLSCREEN); return 1; } break;
    case SDLK_F1:     do_action(app, A_HELP_CONTROLS); return 1;
    case SDLK_F2:     next_palette(app); return 1;
    case SDLK_F12:    do_action(app, A_SCREENSHOT); return 1;
    case SDLK_g:      if (ctrl) { do_action(app, A_REC_MUSIC); return 1; } break;
    case SDLK_t:      if (ctrl) { do_action(app, A_COLLECT_TILES); return 1; } break;
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
        int tag = app->dlg.tag;
        DialogResult r = dialog_event(&app->dlg, e, w, h, ui);
        if (r == DIALOG_OK && tag == TAG_CONSOLE) {
            snprintf(app->cfg.model, sizeof app->cfg.model, "%s",
                     app->pending_model == GB_MODEL_DMG ? "dmg" :
                     app->pending_model == GB_MODEL_SGB ? "sgb" : "cgb");
            app->forced_model = GB_MODEL_AUTO;
            power_on(app);
        }
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

/* ¿Avanza el juego? Con un menú o diálogo abierto, o en pausa, no */
static int game_running(const App *app)
{
    return !app->paused && !menubar_is_open(&app->bar) && app->dlg.kind == DIALOG_NONE;
}

/* Botones de cada jugador. Tras usar un menú o un diálogo se ignoran hasta
 * que se suelten todos: si no, el Enter que cerró un diálogo llegaría como
 * Start. */
static void player_pads(App *app, u8 pads[4])
{
    u8 any = 0;
    for (int i = 0; i < 4; i++) any |= pads[i] = read_pad(app->pads[i], i);
    if (app->hold_input) {
        if (any) {
            memset(pads, 0, 4);
            return;
        }
        app->hold_input = 0;
    }
}

/*
 * Salida de audio (como en gb-recompiled): SDL pide muestras con un callback
 * y las toma de un búfer circular. Al empezar, y tras quedarse sin muestras,
 * espera a juntar ~50 ms antes de sonar, para que un cuadro que tarde un poco
 * más no se oiga como un chasquido. Si se juntan más de ~200 ms (el video va
 * más rápido que la tarjeta de sonido), lo nuevo se descarta.
 */
#define AUDIO_RING   (GB_SAMPLE_RATE / 2)           /* pares estéreo: medio segundo */
#define AUDIO_START  (GB_SAMPLE_RATE / 20)          /* 50 ms */
#define AUDIO_MAX    (GB_SAMPLE_RATE / 5)           /* 200 ms */
static float audio_ring[AUDIO_RING * 2];
static int audio_head, audio_tail, audio_playing;   /* protegidos por el candado de SDL */

static int audio_fill(void)
{
    return (audio_head - audio_tail + AUDIO_RING) % AUDIO_RING;
}

static void SDLCALL audio_callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    float *out = (float *)stream;
    int want = len / (int)(2 * sizeof(float)), i = 0;
    if (!audio_playing && audio_fill() >= AUDIO_START) audio_playing = 1;
    if (audio_playing) {
        for (; i < want && audio_tail != audio_head; i++) {
            out[i * 2] = audio_ring[audio_tail * 2];
            out[i * 2 + 1] = audio_ring[audio_tail * 2 + 1];
            audio_tail = (audio_tail + 1) % AUDIO_RING;
        }
        if (i < want) audio_playing = 0;     /* se acabó: volver a juntar */
    }
    for (; i < want; i++) out[i * 2] = out[i * 2 + 1] = 0.0f;
}

static void audio_push(SDL_AudioDeviceID dev, const float *samples, int n)
{
    SDL_LockAudioDevice(dev);
    for (int i = 0; i < n && audio_fill() < AUDIO_MAX; i++) {
        audio_ring[audio_head * 2] = samples[i * 2];
        audio_ring[audio_head * 2 + 1] = samples[i * 2 + 1];
        audio_head = (audio_head + 1) % AUDIO_RING;
    }
    SDL_UnlockAudioDevice(dev);
}

static void step(App *app)
{
    u8 pads[4];
    player_pads(app, pads);
    if (app->play) pads[0] = inputs_pad(&app->script, app->frame);  /* solo el jugador 1 */
    if (app->record) inputs_record(app->record, app->frame, pads[0], &app->last_pad);
    gb_set_pad(&app->gb, pads[0]);
    app->frame++;
    if (app->linked) {
        /* Cable: el jugador 2 maneja el Game Boy de la derecha */
        gb_set_pad(&app->gb2, pads[1]);
        gb_link_run_frame(&app->link);
    } else {
        /* Super Game Boy multijugador: los controles 2-4 */
        if (app->gb.sgb)
            for (int i = 1; i < 4; i++) gb_set_pad_player(&app->gb, i, pads[i]);
        gb_run_frame(&app->gb);
    }
    tiles_scan(&app->tiles, &app->gb);

    /* Guardado automático cada 5 segundos si la RAM del cartucho cambió */
    if (app->gb.sram_dirty && app->frame % 300 == 0 && gb_has_battery(&app->gb))
        save_game(&app->gb, app->save_path);
    if (app->linked && app->gb2.sram_dirty && app->frame % 300 == 0 && gb_has_battery(&app->gb2))
        save_game(&app->gb2, app->save_path2);

    static float samples[4096 * 2], samples2[4096 * 2];
    int n = gb_audio_pull(&app->gb, samples, 4096);
    music_samples(&app->music, samples, n);    /* el WAV se graba aunque esté sin sonido */
    if (app->linked) {
        /* Cable: el jugador 1, el 2, la mezcla de los dos (a la mitad, para
         * no saturar) o audífonos: cada uno en mono, el 1 a la izquierda y el
         * 2 a la derecha. Los dos avanzan lo mismo: sobra a lo más una muestra */
        int n2 = gb_audio_pull(&app->gb2, samples2, 4096);
        if (app->cfg.link_audio == 1) {
            memcpy(samples, samples2, (size_t)n2 * 2 * sizeof samples[0]);
            n = n2;
        } else if (app->cfg.link_audio == 2) {
            if (n2 < n) n = n2;
            for (int i = 0; i < n * 2; i++) samples[i] = 0.5f * (samples[i] + samples2[i]);
        } else if (app->cfg.link_audio == 3) {
            if (n2 < n) n = n2;
            for (int i = 0; i < n; i++) {
                float left = 0.5f * (samples[i * 2] + samples[i * 2 + 1]);
                float right = 0.5f * (samples2[i * 2] + samples2[i * 2 + 1]);
                samples[i * 2] = left;
                samples[i * 2 + 1] = right;
            }
        }
    }
    if (!app->audio || !app->cfg.sound) return;
    float vol = app->cfg.volume / 100.0f;
    for (int i = 0; i < n * 2; i++) samples[i] *= vol;
    audio_push(app->audio, samples, n);
}

static void update_title(App *app)
{
    char t[256], collecting[96];
    const char *detail = "";
    if (app->notice[0] && SDL_TICKS_PASSED(SDL_GetTicks(), app->notice_until)) app->notice[0] = 0;
    if (app->notice[0]) detail = app->notice;
    else if (app->paused) detail = tr(T_TITLE_PAUSED);
    else if (app->linked) detail = tr(T_TITLE_LINKED);
    else if (music_active(&app->music)) detail = tr(T_TITLE_RECORDING);
    else if (app->tiles.active) {
        snprintf(collecting, sizeof collecting, tr(T_TITLE_COLLECTING_FMT), app->tiles.count);
        detail = collecting;
    }
    if (detail[0]) snprintf(t, sizeof t, "%s - %s", recomp_rom_title, detail);
    else snprintf(t, sizeof t, "%s", recomp_rom_title);
    if (strcmp(t, app->title) != 0) {
        snprintf(app->title, sizeof app->title, "%s", t);
        SDL_SetWindowTitle(app->win, app->title);
    }
}

/* Convierte la imagen de una consola a ARGB, de gw×gh, en dst con `stride`
 * píxeles por renglón */
static void compose_gb(const App *app, const GB *g, Uint32 *dst, int stride, int gw, int gh)
{
    if (g->sgb) {
        const u16 *fb = gb_sgb_screen(g);
        /* Sin borde: solo el juego, que está en (48,40) */
        int x0 = gw == SGB_SCREEN_W ? 0 : 48, y0 = gh == SGB_SCREEN_H ? 0 : 40;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                dst[y * stride + x] = rgb555_to_argb(fb[(y0 + y) * SGB_SCREEN_W + x0 + x]);
        return;
    }
    const u16 *fb = gb_framebuffer(g);
    const Uint32 *shades = dmg_palettes[app->cfg.dmg_palette];
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            u16 v = fb[y * GB_SCREEN_W + x];
            dst[y * stride + x] = g->cgb_hw ? rgb555_to_argb(v) : shades[v & 3];
        }
}

static void compose(const App *app, Uint32 *dst, int gw, int gh)
{
    if (!app->linked) {
        compose_gb(app, &app->gb, dst, gw, gw, gh);
        return;
    }
    /* Jugador 1 a la izquierda, jugador 2 a la derecha, con una franja gris */
    for (int i = 0; i < gw * gh; i++) dst[i] = 0xFF404040;
    compose_gb(app, &app->gb, dst, gw, GB_SCREEN_W, GB_SCREEN_H);
    compose_gb(app, &app->gb2, dst + GB_SCREEN_W + LINK_GAP, gw, GB_SCREEN_W, GB_SCREEN_H);
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
    static Uint32 rgb[SGB_SCREEN_W * SGB_SCREEN_H];
    compose(app, rgb, gw, gh);
    SDL_UpdateTexture(app->tex, NULL, rgb, gw * (int)sizeof(Uint32));

    /* Escala entera más grande que cabe, centrada bajo la barra */
    int avail_h = h - bar;
    int s = w / gw < avail_h / gh ? w / gw : avail_h / gh;
    if (s < 1) s = 1;
    SDL_Rect dst = { (w - gw * s) / 2, bar + (avail_h - gh * s) / 2, gw * s, gh * s };

    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 255);
    SDL_RenderClear(app->ren);
    SDL_RenderCopy(app->ren, app->tex, NULL, &dst);

    /* Cable: "J1"/"J2" sobre cada pantalla al conectar, en pausa o con un
     * menú abierto (jugando no tapan nada) */
    if (app->linked && (!SDL_TICKS_PASSED(SDL_GetTicks(), app->labels_until) || !game_running(app))) {
        for (int k = 0; k < 2; k++) {
            char label[16];
            snprintf(label, sizeof label, tr(T_PLAYER_FMT), k + 1);
            int tw = ui_text_width(label, ui), pad = 3 * ui;
            int x = dst.x + k * (GB_SCREEN_W + LINK_GAP) * s + 4 * ui, y = dst.y + 4 * ui;
            SDL_Rect box = { x, y, tw + 2 * pad, UI_GLYPH_H * ui + 2 * pad };
            SDL_SetRenderDrawBlendMode(app->ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 170);
            SDL_RenderFillRect(app->ren, &box);
            ui_draw_text(app->ren, x + pad, y + pad, ui, label, (SDL_Color){ 255, 255, 255, 255 });
        }
    }
    if (bar_visible(app))
        menubar_draw(&app->bar, app->ren, w, ui);
    dialog_draw(&app->dlg, app->ren, w, h, ui);

    /* Para pruebas: captura de la ventana completa (--ui-shot) */
    if (ui_shot_path) {
        /* Del tamaño de lo que dibuja el renderer, que puede ser mayor que la ventana (SDL3
         * en macOS): con el de la ventana, SDL_RenderReadPixels escribía más allá de la superficie */
        int ow = w, oh = h;
        if (SDL_GetRendererOutputSize(app->ren, &ow, &oh) != 0) { ow = w; oh = h; }
        SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (shot && SDL_RenderReadPixels(app->ren, NULL, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch) == 0) {
            /* PNG, como --ui-shot de NESRecomp y GENRecomp */
            unsigned char *rgb = malloc((size_t)ow * oh * 3);
            if (rgb) {
                for (int y = 0; y < oh; y++) {
                    const Uint32 *row = (const Uint32 *)((const Uint8 *)shot->pixels + (size_t)y * shot->pitch);
                    for (int x = 0; x < ow; x++) {
                        unsigned char *px = rgb + ((size_t)y * ow + x) * 3;
                        px[0] = (unsigned char)(row[x] >> 16); px[1] = (unsigned char)(row[x] >> 8); px[2] = (unsigned char)row[x];
                    }
                }
                if (!png_write_rgb(ui_shot_path, rgb, ow, oh)) fprintf(stderr, "no se pudo escribir %s\n", ui_shot_path);
                free(rgb);
            }
        }
        SDL_FreeSurface(shot);
        ui_shot_path = NULL;
    }
    SDL_RenderPresent(app->ren);
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
    int scale_arg = 0, force = 0, forget = 0, shot_menu = -1, shot_action = 0, shot_export = 0;
    int start_linked = 0;
    long shot_frames = 300;
    char msg[4096];
    app->forced_model = GB_MODEL_AUTO;
    the_app = app;
    app->last_pad = -1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail(SDL_GetError());

    /* Carpeta de datos del juego: copia de la ROM, partida, estados y preferencias */
#ifdef GB_INTERP
    app->pref = SDL_GetPrefPath("GBRecomp", "interprete");
    app->interp_only = 1;
#else
    app->pref = SDL_GetPrefPath("GBRecomp", recomp_name);
#endif
    if (!app->pref) fail(tr(T_NO_DATA_DIR));
    char cache_path[1024];
    snprintf(cache_path, sizeof cache_path, "%srom.gb", app->pref);
    snprintf(app->save_path, sizeof app->save_path, "%s%s.sav", app->pref, recomp_name);
    snprintf(app->save_path2, sizeof app->save_path2, "%s%s_jugador2.sav", app->pref, recomp_name);
    snprintf(app->settings_path, sizeof app->settings_path, "%ssettings.ini", app->pref);
    snprintf(app->export_dir, sizeof app->export_dir, "%sexportado%c", app->pref, app->pref[strlen(app->pref) - 1]);
    settings_load(&app->cfg, app->settings_path);
    Language lang = i18n_from_code(app->cfg.language);
    i18n_set(lang < LANG_COUNT ? lang : detect_language());

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interp")) app->interp_only = 1;
        else if (!strcmp(argv[i], "--link")) start_linked = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--forget-rom")) forget = 1;
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            app->forced_model = model_from_name(argv[++i]);
            if (app->forced_model < GB_MODEL_AUTO) fail(tr(T_BAD_MODEL));
        }
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc) app->forced_palette = argv[++i];
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_path = argv[++i];
        else if (!strcmp(argv[i], "--uncovered") && i + 1 < argc) app->uncovered_path = argv[++i];
        else if (!strcmp(argv[i], "--ui-shot") && i + 1 < argc) ui_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--ui-frames") && i + 1 < argc) shot_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ui-menu") && i + 1 < argc) shot_menu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ui-action") && i + 1 < argc) shot_action = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ui-export")) shot_export = 1;
        else rom_arg = argv[i];
    }
    if (scale_arg > 0) app->cfg.scale = scale_arg > 6 ? 6 : scale_arg;
    if (forget) u8_remove(cache_path);

    app->win = SDL_CreateWindow(recomp_rom_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                GB_SCREEN_W * app->cfg.scale, GB_SCREEN_H * app->cfg.scale,
                                SDL_WINDOW_RESIZABLE | (ui_shot_path ? SDL_WINDOW_HIDDEN : 0));
    if (!app->win) fail(SDL_GetError());
    /* SDL2 empieza con la entrada de texto activa; el juego solo lee teclas.
     * Apagarla evita que macOS conecte el método de entrada (InputMethodKit),
     * que a veces deja "error messaging the mach port for
     * IMKCFRunLoopWakeUpReliable" en la consola */
    SDL_StopTextInput();
    app->ren = SDL_CreateRenderer(app->win, -1, ui_shot_path ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED);
    if (!app->ren) app->ren = SDL_CreateRenderer(app->win, -1, 0);
    if (!app->ren) fail(SDL_GetError());
    SDL_SetWindowMinimumSize(app->win, GB_SCREEN_W, GB_SCREEN_H + menubar_height(1));
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 0xFF);
    SDL_RenderClear(app->ren);
    SDL_RenderPresent(app->ren);

    /* ROM: argumento, copia guardada, rom.gb junto al ejecutable o aquí, o pedirla */
    Rom *rom = &app->rom;
    int have = 0, from_cache = 0;
    if (rom_arg) {
        int r = load_rom(rom_arg, rom);
        if (r < 0) {
            snprintf(msg, sizeof msg, tr(T_ROM_UNREADABLE_FMT), rom_arg);
            fail(msg);
        }
        if (r == 0) {
            if (!force) {
                wrong_rom_message(rom_arg, msg, sizeof msg);
                fail(msg);
            }
            app->interp_only = 1;    /* el código recompilado es de otra ROM */
        }
        have = 1;
    }
#ifndef GB_INTERP
    if (!have && load_rom(cache_path, rom) == 1) have = from_cache = 1;
    if (!have) {
        free(rom->data);
        char *base = SDL_GetBasePath();
        char path[1024];
        snprintf(path, sizeof path, "%srom.gb", base ? base : "");
        SDL_free(base);
        if (load_rom(path, rom) == 1) have = 1;
        else {
            free(rom->data);
            if (load_rom("rom.gb", rom) == 1) have = 1;
            else free(rom->data);
        }
    }
#endif
    if (!have && !ask_for_rom(rom)) return 0;
    if (rom->size < 0x150) fail("ROM < 0x150");
    fprintf(stderr, "ROM: %s\n", from_cache ? cache_path : rom_arg ? rom_arg : "elegida por el usuario o rom.gb");
#ifdef GB_INTERP
    /* El intérprete nombra la partida por la ROM */
    interp_identity(rom_arg ? rom_arg : chosen_rom, rom);
    snprintf(app->save_path, sizeof app->save_path, "%s%s.sav", app->pref, recomp_name);
    snprintf(app->save_path2, sizeof app->save_path2, "%s%s_jugador2.sav", app->pref, recomp_name);
    SDL_SetWindowTitle(app->win, recomp_rom_title);
#else
    /* Copia para las siguientes veces (solo si es la ROM correcta) */
    if (!from_cache && !app->interp_only && !u8_write_file(cache_path, rom->data, rom->size))
        fprintf(stderr, "no se pudo guardar la copia de la ROM en %s\n", cache_path);
#endif

    if (play_path) {
        /* La grabación dice con qué modelo y paleta se hizo */
        const char *ierr = inputs_load(&app->script, play_path);
        if (ierr) fail(ierr);
        app->play = 1;
        if (app->forced_model == GB_MODEL_AUTO) app->forced_model = app->script.model;
        if (!app->forced_palette && app->script.palette[0]) app->forced_palette = app->script.palette;
    }

    app->bar = (MenuBar){ menus, 0, menu_state, app, -1, -1, 0, 0 };
    power_on(app);
    /* Con --play, la grabación maneja al jugador 1 (para pruebas); grabar
     * solo guarda al jugador 1, así que no se combina con el cable */
    if (start_linked && !record_path) {
        link_start(app);
        build_menus(app);   /* el menú Audio cambia con el cable */
    }
    if (app->forced_palette && is_compat(&app->gb) && !gb_compat_palette(&app->gb, app->forced_palette))
        fail(tr(T_BAD_PALETTE));

    if (record_path && !(app->record = u8_fopen(record_path, "w"))) fail(tr(T_RECORD_FAILED));
    if (app->record) inputs_record_header(app->record, &app->gb, app->forced_palette);

    resize_window(app);
    SDL_SetWindowPosition(app->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_AudioSpec want, have_spec;
    SDL_zero(want);
    want.freq = GB_SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    app->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have_spec, 0);
    if (app->audio) SDL_PauseAudioDevice(app->audio, 0);

    if (ui_shot_path) {
        if (shot_export) {
            /* Prueba de exportación: graba música y gráficos durante los
             * cuadros, toma una captura y muestra el aviso de la música */
            toggle_music(app);
            toggle_tiles(app);
        }
        for (long i = 0; i < shot_frames; i++) step(app);
        if (shot_export) {
            screenshot(app);
            stop_tiles(app, 0);
            stop_music(app, 1);
        }
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

    const double frame_seconds = (double)GB_CYCLES_FRAME / GB_CPU_HZ;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();

    while (app->running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handle_event(app, &ev);
        if (!app->running) break;

        if (game_running(app)) step(app);
        update_title(app);
        render(app);

        /* Ritmo: 59.7275 cuadros por segundo */
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

    stop_music(app, 0);
    stop_tiles(app, 0);
    settings_save(&app->cfg, app->settings_path);
    if (app->record) fclose(app->record);
    link_stop(app);         /* guarda la partida del jugador 2 */
    if (gb_has_battery(&app->gb) && !save_game(&app->gb, app->save_path)) {
        snprintf(msg, sizeof msg, tr(T_SAVE_FAILED_FMT), app->save_path);
        fail(msg);
    }
    if (app->uncovered_path) write_uncovered(app->uncovered_path);
    if (app->audio) SDL_CloseAudioDevice(app->audio);
    for (int i = 0; i < 4; i++)
        if (app->pads[i]) SDL_GameControllerClose(app->pads[i]);
    if (app->tex) SDL_DestroyTexture(app->tex);
    SDL_DestroyRenderer(app->ren);
    SDL_DestroyWindow(app->win);
    SDL_free(app->pref);
    SDL_Quit();
    gb_free(&app->gb);
    free(rom->data);
    return 0;
}
