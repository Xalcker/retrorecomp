/*
 * Funciones de la ventana (SDL2) idénticas en NESRecomp, GENRecomp, NEORecomp y SMSRecomp, donde el nombre del
 * juego sale del archivo de la ROM (app->name). GBRecomp no las usa: su ejecutable es de un solo juego
 * y el nombre es de compilación. main_sdl.c define PLAYER_NAME ("GENRecomp"...) antes de incluirlo,
 * después de App, `static App *the_app;` y los datos del ícono (icono.h); necesita player_common.h.
 */
#ifndef PLAYER_ROM_H
#define PLAYER_ROM_H

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, the_app ? the_app->title : PLAYER_NAME, msg, NULL);
    exit(1);
}

static void inform(const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, the_app ? the_app->title : PLAYER_NAME, msg, NULL);
}

static void state_path(const App *app, int slot, char *out, size_t size)
{
    snprintf(out, size, "%s%s.estado%d", app->pref, app->name, slot);
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

#endif
