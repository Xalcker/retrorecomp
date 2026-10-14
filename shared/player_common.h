/*
 * Funciones de la ventana (SDL2) idénticas en los cuatro recompiladores que usan el struct App de cada
 * main_sdl.c, que debe tener los campos paused, fullscreen, menu_hidden, mouse_y, win, ren, bar, dlg,
 * notice, notice_until, export_dir y touch. Se incluye después de definir App, con resize_window declarada antes
 * (y en Android, app_save), y necesita player_util.h y player_touch.h. Lo que depende de la consola (menús,
 * estados, bucle, entradas) sigue en su main_sdl.c.
 */
#ifndef PLAYER_COMMON_H
#define PLAYER_COMMON_H

static int game_running(const App *app)
{
    return !app->paused && !menubar_is_open(&app->bar) && app->dlg.kind == DIALOG_NONE;
}

/* app->menu_hidden: la barra de menús visible, oculta (Video → Ocultar la barra de menús, F9: aparece como
 * en pantalla completa, con el ratón arriba o con Alt/F10) o deshabilitada (--no-menu: nunca aparece) */
enum { MENU_SHOWN, MENU_HIDDEN, MENU_OFF };

/* ¿La barra recibe el ratón y el teclado (Alt, F10)? Siempre, salvo con --no-menu */
static int bar_enabled(const App *app)
{
    return app->menu_hidden != MENU_OFF;
}

static int bar_visible(const App *app)
{
    if (!bar_enabled(app)) return 0;
    if (!app->fullscreen && app->menu_hidden == MENU_SHOWN) return 1;
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    return menubar_is_open(&app->bar) || app->dlg.kind != DIALOG_NONE ||
           app->mouse_y < 2 * menubar_height(ui_scale(w));
}

/* Lo que ocupa la barra arriba de la imagen: nada en pantalla completa ni con la barra oculta (se dibuja
 * encima cuando aparece) */
static int bar_space(const App *app, int ui)
{
    return app->fullscreen || app->menu_hidden != MENU_SHOWN ? 0 : menubar_height(ui);
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

static void toggle_fullscreen(App *app)
{
    app->fullscreen = !app->fullscreen;
    SDL_SetWindowFullscreen(app->win, app->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!app->fullscreen) resize_window(app);
}

/* Video → Ocultar la barra de menús (F9). Con --no-menu no hace nada */
static void toggle_menu(App *app)
{
    if (app->menu_hidden == MENU_OFF) return;
    app->menu_hidden = app->menu_hidden == MENU_SHOWN ? MENU_HIDDEN : MENU_SHOWN;
    resize_window(app);
}

/* Antes que la barra de menús: el botón Atrás de Android (SDLK_AC_BACK) se vuelve Esc con un diálogo abierto y, si
 * no, F10 (abre y cierra la barra de menús; el juego se pausa con un menú abierto) o, con --no-menu, P (pausa). El
 * botón del menú del mando en pantalla hace lo mismo que Atrás. Luego, los toques del mando (touch_event).
 * Devuelve 1 si el evento ya se atendió */
static int touch_and_back(App *app, SDL_Event *e)
{
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && e->key.keysym.sym == SDLK_AC_BACK) {
        SDL_Keycode k = app->dlg.kind != DIALOG_NONE ? SDLK_ESCAPE : bar_enabled(app) ? SDLK_F10 : SDLK_p;
        e->key.keysym.sym = k;
        e->key.keysym.scancode = SDL_GetScancodeFromKey(k);
        return 0;       /* sigue como esa tecla (y no esconde el mando en pantalla) */
    }
    int ui_open = app->dlg.kind != DIALOG_NONE || menubar_is_open(&app->bar);
    int top = bar_enabled(app) ? 2 * menubar_height(ui_scale(w)) : 0;
    /* Al soltar el dedo, la barra oculta deja de mostrarse (los toques del mando no mueven el ratón) */
    if (e->type == SDL_FINGERUP && !ui_open) app->mouse_y = 1 << 20;
    if (!touch_event(&app->touch, e, w, h, ui_open, top)) return 0;
    if (app->touch.menu) {
        SDL_Event back;
        memset(&back, 0, sizeof back);
        back.type = SDL_KEYDOWN;
        back.key.state = SDL_PRESSED;
        back.key.keysym.sym = SDLK_AC_BACK;
        back.key.keysym.scancode = SDL_SCANCODE_AC_BACK;
        app->touch.menu = 0;
        SDL_PushEvent(&back);
    }
    return 1;
}

#ifdef __ANDROID__
/* Android puede cerrar el juego en segundo plano sin avisar: al irse se guarda lo mismo que al salir (app_save, de
 * cada main_sdl.c). El aviso (SDL_APP_WILLENTERBACKGROUND) llega en el hilo de Android, y el bucle del juego se
 * detiene antes de verlo en la cola de eventos: background_watch le pide el guardado al bucle (background_poll, al
 * empezar cada cuadro) y lo espera un momento. No lo espera si el hilo del juego está esperando a Java (el selector
 * de archivos: android_jni.c) */
static void app_save(App *app);
extern SDL_atomic_t android_in_java;
static SDL_atomic_t background_request;
static SDL_sem *background_done;

static int SDLCALL background_watch(void *ctx, SDL_Event *e)
{
    (void)ctx;
    if (e->type == SDL_APP_WILLENTERBACKGROUND && !SDL_AtomicGet(&android_in_java)) {
        while (SDL_SemTryWait(background_done) == 0) {}
        SDL_AtomicSet(&background_request, 1);
        SDL_SemWaitTimeout(background_done, 500);
    }
    return 1;
}

static void background_poll(App *app)
{
    if (!background_done) {
        background_done = SDL_CreateSemaphore(0);
        if (background_done) SDL_AddEventWatch(background_watch, NULL);
    }
    if (SDL_AtomicCAS(&background_request, 1, 0)) {
        app_save(app);
        SDL_SemPost(background_done);
    }
}
#else
static void background_poll(App *app)
{
    (void)app;
}
#endif

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

#endif
