/*
 * Funciones de la ventana (SDL2) idénticas en los cuatro recompiladores que usan el struct App de cada
 * main_sdl.c, que debe tener los campos paused, fullscreen, menu_hidden, mouse_y, win, ren, bar, dlg,
 * notice, notice_until y export_dir. Se incluye después de definir App, con resize_window declarada antes, y
 * necesita player_util.h. Lo que depende de la consola (menús, estados, bucle, entradas) sigue en su
 * main_sdl.c.
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
