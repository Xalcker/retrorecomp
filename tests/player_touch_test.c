/*
 * Prueba de shared/player_touch.h (el mando en pantalla) con toques simulados en una pantalla de 2400x1080, como
 * la de un teléfono en horizontal: la cruz en ocho direcciones, varios dedos a la vez, deslizar de un botón a otro,
 * los dos botones entre ellos, el botón del menú, qué toques del ratón de SDL pasan a la barra de menús y cuándo
 * se esconde. También lo dibuja en un renderer de software. Usa SDL2 sin ventana.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -Ishared $(pkg-config --cflags sdl2) -o player_touch_test \
 *       tests/player_touch_test.c shared/ui_font.c $(pkg-config --libs sdl2) -lm && ./player_touch_test
 */
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "ui_font.h"
#include "player_touch.h"

enum { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, KEY_A = 16, KEY_B = 32, SELECT = 64, START = 128 };

static const TouchButton keys[] = {
    { "B", KEY_B, TOUCH_RIGHT, 0, -2.75f, -1.3f },
    { "A", KEY_A, TOUCH_RIGHT, 0, -1.15f, -1.8f },
    { "SELECT", SELECT, TOUCH_LEFT, 1, 1.65f, -3.6f },
    { "START", START, TOUCH_RIGHT, 1, -1.95f, -3.6f },
};
static const TouchLayout layout = { UP, DOWN, LEFT, RIGHT, keys, 4 };

#define W 2400
#define H 1080

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) fails++;
}

static int finger(TouchPad *t, Uint32 type, SDL_FingerID id, float x, float y, int ui_open)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.tfinger.fingerId = id;
    e.tfinger.x = x / W;
    e.tfinger.y = y / H;
    return touch_event(t, &e, W, H, ui_open, 100);
}

static int mouse(TouchPad *t, Uint32 type, Uint32 which, int x, int y, int ui_open)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.which = which;
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.which = which;
        e.button.x = x;
        e.button.y = y;
    }
    return touch_event(t, &e, W, H, ui_open, 100);
}

/* Centro de un botón de la tabla, en pixeles */
static void key_at(int i, float *x, float *y)
{
    touch_key_center(&keys[i], W, H, x, y);
}

int main(void)
{
    TouchPad t;
    float u = TOUCH_U(W, H), dx, dy, ax, ay, bx, by, sx, sy;
    touch_dpad_center(W, H, &dx, &dy);
    key_at(0, &bx, &by);
    key_at(1, &ax, &ay);
    key_at(3, &sx, &sy);
    float r = TOUCH_DPAD_R * u;

    touch_init(&t, &layout);
    check(!t.shown, "en escritorio empieza escondido");
    check(touch_buttons(&t) == 0, "sin toques, nada apretado");

    check(finger(&t, SDL_FINGERDOWN, 1, dx + 0.7f * r, dy, 0) == 1 && t.shown, "un toque lo muestra y es suyo");
    check(touch_buttons(&t) == RIGHT, "cruz: derecha");
    check(finger(&t, SDL_FINGERDOWN, 2, ax, ay, 0) == 1, "segundo dedo");
    check(touch_buttons(&t) == (RIGHT | KEY_A), "derecha y A a la vez");
    finger(&t, SDL_FINGERMOTION, 1, dx - 0.6f * r, dy - 0.6f * r, 0);
    check(touch_buttons(&t) == (UP | LEFT | KEY_A), "la cruz en diagonal (arriba a la izquierda)");
    finger(&t, SDL_FINGERMOTION, 1, dx + 0.1f * r, dy, 0);
    check(touch_buttons(&t) == KEY_A, "en el centro de la cruz, ninguna dirección");
    finger(&t, SDL_FINGERMOTION, 1, dx + 2.5f * r, dy - 1.5f * r, 0);
    check((touch_buttons(&t) & (UP | RIGHT)) == (UP | RIGHT), "el dedo de la cruz sigue en ella aunque salga");
    finger(&t, SDL_FINGERUP, 1, 0, 0, 0);
    check(touch_buttons(&t) == KEY_A, "al soltar la cruz queda A");

    finger(&t, SDL_FINGERMOTION, 2, bx, by, 0);
    check(touch_buttons(&t) == KEY_B, "deslizar de A a B");
    finger(&t, SDL_FINGERMOTION, 2, (ax + bx) / 2, (ay + by) / 2, 0);
    check(touch_buttons(&t) == (KEY_A | KEY_B), "entre A y B, los dos");
    finger(&t, SDL_FINGERMOTION, 2, sx + 0.5f * u, sy, 0);
    check(touch_buttons(&t) == START, "deslizar a START (botón ancho, cerca de una punta)");
    finger(&t, SDL_FINGERUP, 2, 0, 0, 0);
    check(touch_buttons(&t) == 0 && t.fingers == 0, "sin dedos, nada apretado");

    check(finger(&t, SDL_FINGERDOWN, 3, 0.6f * u, 0.6f * u, 0) == 1 && t.menu, "el botón del menú");
    finger(&t, SDL_FINGERUP, 3, 0, 0, 0);
    t.menu = 0;
    check(finger(&t, SDL_FINGERDOWN, 4, W / 2.0f, H / 2.0f, 0) == 1 && touch_buttons(&t) == 0,
          "un toque sobre el juego es del mando pero no aprieta nada");
    finger(&t, SDL_FINGERUP, 4, 0, 0, 0);
    check(finger(&t, SDL_FINGERDOWN, 5, ax, ay, 1) == 0 && t.fingers == 0, "con un menú abierto, los toques son suyos");

    /* El ratón que SDL inventa con el primer dedo */
    check(mouse(&t, SDL_MOUSEBUTTONDOWN, SDL_TOUCH_MOUSEID, (int)ax, (int)ay, 0) == 1, "ratón del toque sobre A: no pasa");
    check(mouse(&t, SDL_MOUSEBUTTONUP, SDL_TOUCH_MOUSEID, (int)ax, (int)ay, 0) == 1, "y su soltar tampoco");
    check(mouse(&t, SDL_MOUSEMOTION, SDL_TOUCH_MOUSEID, W / 2, 40, 0) == 0, "en la franja de arriba pasa (la barra)");
    check(mouse(&t, SDL_MOUSEBUTTONDOWN, SDL_TOUCH_MOUSEID, W / 2, 40, 0) == 0, "el clic arriba también");
    check(mouse(&t, SDL_MOUSEBUTTONUP, SDL_TOUCH_MOUSEID, W / 2, 600, 0) == 0, "y su soltar, aunque se haya movido");
    check(mouse(&t, SDL_MOUSEBUTTONDOWN, SDL_TOUCH_MOUSEID, 50, 50, 0) == 1, "arriba pero en el botón del menú: no pasa");
    check(mouse(&t, SDL_MOUSEBUTTONDOWN, SDL_TOUCH_MOUSEID, (int)ax, (int)ay, 1) == 0, "con un menú abierto pasa todo");
    check(mouse(&t, SDL_MOUSEBUTTONDOWN, 0, (int)ax, (int)ay, 0) == 0, "un ratón de verdad pasa siempre");

    finger(&t, SDL_FINGERDOWN, 6, ax, ay, 0);
    SDL_Event pad;
    memset(&pad, 0, sizeof pad);
    pad.type = SDL_CONTROLLERBUTTONDOWN;
    touch_event(&t, &pad, W, H, 0, 100);
    check(!t.shown && touch_buttons(&t) == 0, "un mando físico lo esconde");
    finger(&t, SDL_FINGERDOWN, 7, bx, by, 0);
    check(t.shown && touch_buttons(&t) == (KEY_A | KEY_B), "un toque lo vuelve a mostrar");

    /* Dibujo en un renderer de software: la cruz y A se ven; el centro de la pantalla no */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *ren = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    check(ren != NULL, "renderer de software");
    if (ren) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        touch_draw(&t, ren, W, H);
        SDL_RenderPresent(ren);
        const Uint32 *px = s->pixels;
        int pitch = s->pitch / 4;
        Uint32 up = px[(int)(dy - 0.7f * r) * pitch + (int)dx] & 0xFFFFFF;
        Uint32 a = px[(int)(ay - 0.4f * u) * pitch + (int)ax] & 0xFFFFFF;
        Uint32 mid = px[(H / 2) * pitch + W / 2] & 0xFFFFFF;
        check(up != 0 && a != 0 && mid == 0, "dibuja la cruz y los botones, y nada en medio");
        SDL_DestroyRenderer(ren);
    }
    SDL_FreeSurface(s);

    printf("%d fallas\n", fails);
    return fails ? 1 : 0;
}
