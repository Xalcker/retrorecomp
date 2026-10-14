/*
 * Funciones de la ventana (SDL2) que son idénticas en los cuatro recompiladores. Es un encabezado con
 * funciones static: cada main_sdl.c lo incluye después de definir su struct App, que debe tener los campos
 * paused, fullscreen, mouse_y, win, ren, bar, dlg, notice, notice_until y export_dir, y después de
 * resize_window, TAG_INFO y los demás nombres de ui_menu.h e i18n.h. Lo que depende de la consola
 * (menús, estados, bucle, entradas) sigue en su main_sdl.c.
 */
#ifndef PLAYER_COMMON_H
#define PLAYER_COMMON_H

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

static const char *file_name(const char *path)
{
    const char *s = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    return s;
}

static int ui_scale(int width)
{
    return width >= 640 ? 2 : 1;
}

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

static int game_running(const App *app)
{
    return !app->paused && !menubar_is_open(&app->bar) && app->dlg.kind == DIALOG_NONE;
}

static int bar_visible(const App *app)
{
    if (!app->fullscreen) return 1;
    int w, h;
    SDL_GetRendererOutputSize(app->ren, &w, &h);
    return menubar_is_open(&app->bar) || app->dlg.kind != DIALOG_NONE ||
           app->mouse_y < 2 * menubar_height(ui_scale(w));
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
