/*
 * Mando en pantalla para las pantallas táctiles (Android, o una pantalla táctil en escritorio), dibujado con el
 * renderer de SDL e igual en los cuatro recompiladores; los botones de cada consola van en una tabla (TouchLayout)
 * de su main_sdl.c. Es multitáctil: cada dedo se queda con la zona donde tocó (la cruz, los botones o el botón del
 * menú) y en los botones puede deslizarse de uno a otro; entre dos botones aprieta los dos. Aparece al tocar la
 * pantalla (en Android, desde el principio) y se esconde al usar un mando o el teclado.
 * Va entre los #include de main_sdl.c, después de ui_font.h.
 */
#ifndef PLAYER_TOUCH_H
#define PLAYER_TOUCH_H

#include <math.h>

/* La unidad de medida es la séptima parte del lado corto de la pantalla (TOUCH_U). Cada botón está a (x, y)
 * unidades de una esquina de abajo o del centro de abajo (anchor); los anchos (wide) son Start, Select y parecidos */
enum { TOUCH_RIGHT, TOUCH_LEFT, TOUCH_CENTER };

typedef struct {
    const char *label;
    unsigned mask;          /* los bits de la consola */
    int anchor, wide;
    float x, y;
} TouchButton;

typedef struct {
    unsigned up, down, left, right;     /* los bits de la cruz */
    const TouchButton *buttons;
    int count;
} TouchLayout;

#define TOUCH_FINGERS 10
enum { TOUCH_NONE, TOUCH_DPAD, TOUCH_KEYS, TOUCH_MENU };

typedef struct {
    const TouchLayout *layout;
    int shown;              /* se dibuja y recibe los toques */
    int menu;               /* tocaron el botón del menú: lo atiende el bucle */
    int mouse_pass;         /* el toque que SDL también manda como ratón va a la barra de menús */
    int focus_motion;       /* Android: la ventana acaba de recibir el foco (ver touch_event) */
    int w, h;               /* la pantalla del último toque */
    struct { SDL_FingerID id; int zone; float x, y; } finger[TOUCH_FINGERS];
    int fingers;
} TouchPad;

#define TOUCH_U(w, h)   (((w) < (h) ? (w) : (h)) / 7.0f)
#define TOUCH_DPAD_R    1.3f    /* radio de la cruz */
#define TOUCH_KEY_R     0.55f   /* radio de un botón redondo */
#define TOUCH_KEY_HIT   0.85f   /* hasta dónde lo aprieta un dedo */
#define TOUCH_WIDE_W    0.45f   /* medio largo de un botón ancho, sin las puntas */
#define TOUCH_WIDE_R    0.28f
#define TOUCH_MENU_R    0.38f

static void touch_init(TouchPad *t, const TouchLayout *layout)
{
    memset(t, 0, sizeof *t);
    t->layout = layout;
#ifdef __ANDROID__
    t->shown = 1;
#endif
}

static void touch_dpad_center(int w, int h, float *x, float *y)
{
    float u = TOUCH_U(w, h);
    *x = (0.35f + TOUCH_DPAD_R) * u;
    *y = h - (0.35f + TOUCH_DPAD_R) * u;
}

static void touch_key_center(const TouchButton *b, int w, int h, float *x, float *y)
{
    float u = TOUCH_U(w, h);
    float x0 = b->anchor == TOUCH_RIGHT ? (float)w : b->anchor == TOUCH_LEFT ? 0.0f : w / 2.0f;
    *x = x0 + b->x * u;
    *y = h + b->y * u;
}

/* Distancia de (x, y) a un botón (al segmento de en medio, si es ancho), en unidades */
static float touch_key_dist(const TouchButton *b, int w, int h, float x, float y)
{
    float u = TOUCH_U(w, h), cx, cy;
    touch_key_center(b, w, h, &cx, &cy);
    float dx = fabsf(x - cx) / u, dy = (y - cy) / u;
    if (b->wide) dx = dx > TOUCH_WIDE_W ? dx - TOUCH_WIDE_W : 0;
    return sqrtf(dx * dx + dy * dy);
}

static int touch_zone(const TouchPad *t, int w, int h, float x, float y)
{
    float u = TOUCH_U(w, h), cx, cy;
    if (hypotf(x - 0.6f * u, y - 0.6f * u) < 0.7f * u) return TOUCH_MENU;
    for (int i = 0; i < t->layout->count; i++) {
        const TouchButton *b = &t->layout->buttons[i];
        if (touch_key_dist(b, w, h, x, y) < (b->wide ? TOUCH_WIDE_R + 0.25f : TOUCH_KEY_HIT)) return TOUCH_KEYS;
    }
    /* La cruz recibe también los toques un poco fuera de ella */
    touch_dpad_center(w, h, &cx, &cy);
    if (hypotf(x - cx, y - cy) < 1.6f * TOUCH_DPAD_R * u) return TOUCH_DPAD;
    return TOUCH_NONE;
}

/* Los bits de un dedo */
static unsigned touch_finger_mask(const TouchPad *t, int i)
{
    const TouchLayout *L = t->layout;
    int w = t->w, h = t->h;
    float x = t->finger[i].x, y = t->finger[i].y, u = TOUCH_U(w, h);
    unsigned m = 0;
    if (t->finger[i].zone == TOUCH_DPAD) {
        float cx, cy;
        touch_dpad_center(w, h, &cx, &cy);
        float dx = x - cx, dy = y - cy, d = hypotf(dx, dy);
        if (d < 0.25f * TOUCH_DPAD_R * u) return 0;
        /* ocho direcciones: las diagonales ocupan 45 grados, como las rectas */
        if (dx > 0.38f * d) m |= L->right;
        if (dx < -0.38f * d) m |= L->left;
        if (dy > 0.38f * d) m |= L->down;
        if (dy < -0.38f * d) m |= L->up;
    } else if (t->finger[i].zone == TOUCH_KEYS) {
        for (int k = 0; k < L->count; k++) {
            const TouchButton *b = &L->buttons[k];
            if (touch_key_dist(b, w, h, x, y) < (b->wide ? TOUCH_WIDE_R + 0.25f : TOUCH_KEY_HIT)) m |= b->mask;
        }
    }
    return m;
}

/* Los botones de la consola apretados en la pantalla */
static unsigned touch_buttons(const TouchPad *t)
{
    unsigned m = 0;
    if (!t->shown) return 0;
    for (int i = 0; i < t->fingers; i++) m |= touch_finger_mask(t, i);
    return m;
}

/* Reparte los toques. ui_open: hay un menú o un diálogo abierto (los toques son para ellos); top: alto de la franja
 * de arriba donde un toque fuera del mando abre la barra de menús. Devuelve 1 si el evento era del mando (el que
 * llama ya no lo pasa a la barra ni a los atajos) */
static int touch_event(TouchPad *t, const SDL_Event *e, int w, int h, int ui_open, int top)
{
    switch (e->type) {
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION: {
        float x = e->tfinger.x * w, y = e->tfinger.y * h;
        int i = 0;
        while (i < t->fingers && t->finger[i].id != e->tfinger.fingerId) i++;
        if (e->type == SDL_FINGERDOWN) {
            if (ui_open) return 0;
            t->shown = 1;
            if (i == t->fingers) {
                if (t->fingers == TOUCH_FINGERS) return 1;
                t->fingers++;
            }
            t->finger[i].id = e->tfinger.fingerId;
            t->finger[i].zone = touch_zone(t, w, h, x, y);
            if (t->finger[i].zone == TOUCH_MENU) t->menu = 1;
            else if (t->finger[i].zone == TOUCH_NONE) t->finger[i].zone = TOUCH_KEYS;  /* puede deslizarse a un botón */
        } else if (i == t->fingers) {
            return 0;
        }
        t->w = w;
        t->h = h;
        t->finger[i].x = x;
        t->finger[i].y = y;
        return 1;
    }
    case SDL_FINGERUP:
        for (int i = 0; i < t->fingers; i++)
            if (t->finger[i].id == e->tfinger.fingerId) {
                t->finger[i] = t->finger[--t->fingers];
                return 1;
            }
        return 0;
#ifdef __ANDROID__
    /* Al recibir el foco, SDL repite como ratón el último toque (o (0, 0) al arrancar): mostraría la barra oculta */
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) t->focus_motion = 1;
        return 0;
#endif
    case SDL_MOUSEMOTION:
        if (t->focus_motion) {
            t->focus_motion = 0;
            if (e->motion.which != SDL_TOUCH_MOUSEID && !e->motion.state) return 1;
        }
        if (e->motion.which != SDL_TOUCH_MOUSEID) return 0;
        return !(ui_open || (e->motion.y < top && touch_zone(t, w, h, (float)e->motion.x, (float)e->motion.y) == TOUCH_NONE));
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.which != SDL_TOUCH_MOUSEID) return 0;
        t->mouse_pass = ui_open || (e->button.y < top && touch_zone(t, w, h, (float)e->button.x, (float)e->button.y) == TOUCH_NONE);
        return !t->mouse_pass;
    case SDL_MOUSEBUTTONUP:
        if (e->button.which != SDL_TOUCH_MOUSEID) return 0;
        return !(ui_open || t->mouse_pass);
    case SDL_CONTROLLERBUTTONDOWN:
        t->shown = 0;
        return 0;
    case SDL_KEYDOWN:
        t->shown = 0;
        return 0;
    default:
        return 0;
    }
}

/* Un polígono convexo relleno: un círculo (half = 0) o un botón ancho (dos medios círculos unidos) */
static void touch_fill(SDL_Renderer *ren, float cx, float cy, float half, float r, Uint8 alpha)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    enum { N = 48 };
    SDL_Vertex v[N + 1];
    int idx[N * 3];
    SDL_Color c = { 255, 255, 255, alpha };
    v[0].position.x = cx;
    v[0].position.y = cy;
    v[0].color = c;
    for (int i = 0; i < N; i++) {
        float a = 6.2831853f * (i + 0.5f) / N;
        float side = cosf(a) >= 0 ? half : -half;
        v[i + 1].position.x = cx + side + r * cosf(a);
        v[i + 1].position.y = cy + r * sinf(a);
        v[i + 1].color = c;
        v[i + 1].tex_coord.x = v[i + 1].tex_coord.y = 0;
        idx[3 * i] = 0;
        idx[3 * i + 1] = i + 1;
        idx[3 * i + 2] = (i + 1) % N + 1;
    }
    v[0].tex_coord.x = v[0].tex_coord.y = 0;
    SDL_RenderGeometry(ren, NULL, v, N + 1, idx, N * 3);
#else
    SDL_FRect rect = { cx - half - r, cy - r, 2 * (half + r), 2 * r };
    SDL_SetRenderDrawColor(ren, 255, 255, 255, alpha);
    SDL_RenderFillRectF(ren, &rect);
#endif
}

static void touch_arrow(SDL_Renderer *ren, float cx, float cy, float dx, float dy, float r, Uint8 alpha)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_Color c = { 255, 255, 255, alpha };
    SDL_Vertex v[3];
    float px = -dy, py = dx;     /* perpendicular */
    v[0].position.x = cx + dx * 0.9f * r;  v[0].position.y = cy + dy * 0.9f * r;
    v[1].position.x = cx + dx * 0.4f * r + px * 0.32f * r;  v[1].position.y = cy + dy * 0.4f * r + py * 0.32f * r;
    v[2].position.x = cx + dx * 0.4f * r - px * 0.32f * r;  v[2].position.y = cy + dy * 0.4f * r - py * 0.32f * r;
    for (int i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord.x = v[i].tex_coord.y = 0; }
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
#else
    (void)ren; (void)cx; (void)cy; (void)dx; (void)dy; (void)r; (void)alpha;
#endif
}

static void touch_label(SDL_Renderer *ren, float cx, float cy, float height, const char *s)
{
    int scale = (int)(height / UI_GLYPH_H + 0.5f);
    if (scale < 1) scale = 1;
    SDL_Color c = { 255, 255, 255, 230 };
    ui_draw_text(ren, (int)(cx - ui_text_width(s, scale) / 2.0f), (int)(cy - UI_GLYPH_H * scale / 2.0f), scale, s, c);
}

static void touch_draw(const TouchPad *t, SDL_Renderer *ren, int w, int h)
{
    if (!t->shown) return;
    const TouchLayout *L = t->layout;
    float u = TOUCH_U(w, h), cx, cy;
    TouchPad now = *t;
    now.w = w;
    now.h = h;
    unsigned held = touch_buttons(&now);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* La cruz: un círculo con cuatro flechas que se iluminan */
    touch_dpad_center(w, h, &cx, &cy);
    float r = TOUCH_DPAD_R * u;
    touch_fill(ren, cx, cy, 0, r, 45);
    touch_arrow(ren, cx, cy, 0, -1, r, held & L->up ? 230 : 110);
    touch_arrow(ren, cx, cy, 0, 1, r, held & L->down ? 230 : 110);
    touch_arrow(ren, cx, cy, -1, 0, r, held & L->left ? 230 : 110);
    touch_arrow(ren, cx, cy, 1, 0, r, held & L->right ? 230 : 110);

    for (int i = 0; i < L->count; i++) {
        const TouchButton *b = &L->buttons[i];
        int on = (held & b->mask) != 0;
        touch_key_center(b, w, h, &cx, &cy);
        if (b->wide) {
            touch_fill(ren, cx, cy, TOUCH_WIDE_W * u, TOUCH_WIDE_R * u, on ? 150 : 55);
            touch_label(ren, cx, cy, 0.24f * u, b->label);
        } else {
            touch_fill(ren, cx, cy, 0, TOUCH_KEY_R * u, on ? 150 : 55);
            touch_label(ren, cx, cy, 0.4f * u, b->label);
        }
    }

    /* El botón del menú (como Atrás): tres rayas */
    touch_fill(ren, 0.6f * u, 0.6f * u, 0, TOUCH_MENU_R * u, 55);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 200);
    for (int i = -1; i <= 1; i++) {
        SDL_FRect bar = { 0.6f * u - 0.18f * u, 0.6f * u + i * 0.11f * u - 0.025f * u, 0.36f * u, 0.05f * u };
        SDL_RenderFillRectF(ren, &bar);
    }
}

#endif
