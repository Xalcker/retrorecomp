/*
 * Utilidades de la ventana (SDL2) que no dependen de la consola y son idénticas en los cuatro
 * recompiladores. Va entre los #include de main_sdl.c (necesita SDL.h, <stdio.h> e i18n.h).
 */
#ifndef PLAYER_UTIL_H
#define PLAYER_UTIL_H

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

/* La escala de la barra de menús y los diálogos. En Android la pantalla tiene muchos más pixeles por centímetro y se
 * toca con el dedo: crece con el ancho */
static int ui_scale(int width)
{
#ifdef __ANDROID__
    return width >= 960 ? width / 480 : 2;
#else
    return width >= 640 ? 2 : 1;
#endif
}

/* Espera a que se arrastre un archivo a la ventana (0 si se cierra o con Esc). En el navegador no se puede
 * esperar sin colgar la página: devuelve 0 enseguida (la página da la ROM antes de arrancar). En Android no se
 * arrastran archivos (se eligen con el selector del sistema, ask_rom_path): también 0 */
static int wait_for_drop(char *out, size_t size)
{
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)out; (void)size;
    return 0;
#endif
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

#endif
