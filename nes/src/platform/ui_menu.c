#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "ui_font.h"

/* Colores al estilo de un menú de escritorio claro */
static const SDL_Color c_bar      = { 0xF0, 0xF0, 0xF0, 0xFF };
static const SDL_Color c_bar_line = { 0xD0, 0xD0, 0xD0, 0xFF };
static const SDL_Color c_text     = { 0x10, 0x10, 0x10, 0xFF };
static const SDL_Color c_disabled = { 0xA0, 0xA0, 0xA0, 0xFF };
static const SDL_Color c_title_hl = { 0xCC, 0xE4, 0xF7, 0xFF };
static const SDL_Color c_item_hl  = { 0x91, 0xC9, 0xF7, 0xFF };
static const SDL_Color c_panel    = { 0xFF, 0xFF, 0xFF, 0xFF };
static const SDL_Color c_border   = { 0x80, 0x80, 0x80, 0xFF };
static const SDL_Color c_shade    = { 0x00, 0x00, 0x00, 0x60 };

#define PAD 4           /* margen interno, en pixeles de la fuente */
#define ROW (UI_GLYPH_H + 4)

static void fill(SDL_Renderer *ren, SDL_Rect r, SDL_Color c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}

static void frame(SDL_Renderer *ren, SDL_Rect r, SDL_Color c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}

static int inside(SDL_Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ---------------------------------------------------------------------- */
/* Barra                                                                   */
/* ---------------------------------------------------------------------- */

int menubar_height(int scale)
{
    return ROW * scale;
}

int menubar_is_open(const MenuBar *mb)
{
    return mb->open >= 0;
}

void menubar_close(MenuBar *mb)
{
    mb->open = -1;
    mb->hover = -1;
}

static SDL_Rect title_rect(const MenuBar *mb, int index, int scale)
{
    int x = PAD * scale;
    for (int i = 0; i < index; i++)
        x += ui_text_width(mb->menus[i].title, scale) + 2 * PAD * scale;
    return (SDL_Rect){ x, 0, ui_text_width(mb->menus[index].title, scale) + 2 * PAD * scale,
                       menubar_height(scale) };
}

/* Ancho del menú desplegable: marca + etiqueta + atajo */
static int dropdown_width(const Menu *m, int scale)
{
    int label = 0, shortcut = 0;
    for (int i = 0; i < m->count; i++) {
        int w = ui_text_width(m->items[i].label, scale);
        if (w > label) label = w;
        if (m->items[i].shortcut) {
            w = ui_text_width(m->items[i].shortcut, scale);
            if (w > shortcut) shortcut = w;
        }
    }
    return (3 * PAD + UI_ADVANCE * 2) * scale + label + (shortcut ? shortcut + 4 * PAD * scale : 0)
           + PAD * scale;
}

static int item_height(const MenuItem *it, int scale)
{
    return (it->kind == ITEM_SEPARATOR ? 5 : ROW + 2) * scale;
}

static SDL_Rect dropdown_rect(const MenuBar *mb, int scale)
{
    const Menu *m = &mb->menus[mb->open];
    SDL_Rect t = title_rect(mb, mb->open, scale);
    int h = 2 * scale;
    for (int i = 0; i < m->count; i++)
        h += item_height(&m->items[i], scale);
    return (SDL_Rect){ t.x, t.y + t.h, dropdown_width(m, scale), h };
}

static SDL_Rect item_rect(const MenuBar *mb, int index, int scale)
{
    const Menu *m = &mb->menus[mb->open];
    SDL_Rect d = dropdown_rect(mb, scale);
    int y = d.y + scale;
    for (int i = 0; i < index; i++)
        y += item_height(&m->items[i], scale);
    return (SDL_Rect){ d.x + scale, y, d.w - 2 * scale, item_height(&m->items[index], scale) };
}

static int item_enabled(const MenuBar *mb, const MenuItem *it, int *checked)
{
    int c = 0;
    int enabled = mb->state ? mb->state(mb->ctx, it->action, &c) : 1;
    if (checked) *checked = c;
    return enabled;
}

void menubar_draw(MenuBar *mb, SDL_Renderer *ren, int width, int scale)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int h = menubar_height(scale);
    fill(ren, (SDL_Rect){ 0, 0, width, h }, c_bar);
    fill(ren, (SDL_Rect){ 0, h - scale, width, scale }, c_bar_line);

    for (int i = 0; i < mb->count; i++) {
        SDL_Rect t = title_rect(mb, i, scale);
        if (i == mb->open)
            fill(ren, (SDL_Rect){ t.x, t.y, t.w, t.h - scale }, c_title_hl);
        ui_draw_text(ren, t.x + PAD * scale, 2 * scale, scale, mb->menus[i].title, c_text);
    }

    if (mb->open < 0) return;

    const Menu *m = &mb->menus[mb->open];
    SDL_Rect d = dropdown_rect(mb, scale);
    fill(ren, (SDL_Rect){ d.x + 2 * scale, d.y + 2 * scale, d.w, d.h }, c_shade);
    fill(ren, d, c_panel);
    frame(ren, d, c_border);

    for (int i = 0; i < m->count; i++) {
        const MenuItem *it = &m->items[i];
        SDL_Rect r = item_rect(mb, i, scale);
        if (it->kind == ITEM_SEPARATOR) {
            fill(ren, (SDL_Rect){ r.x + PAD * scale, r.y + 2 * scale, r.w - 2 * PAD * scale, scale },
                 c_bar_line);
            continue;
        }
        int checked;
        int enabled = item_enabled(mb, it, &checked);
        if (i == mb->hover && enabled)
            fill(ren, r, c_item_hl);
        SDL_Color c = enabled ? c_text : c_disabled;
        int ty = r.y + 3 * scale;
        if (checked)
            ui_draw_text(ren, r.x + PAD * scale, ty, scale,
                         it->kind == ITEM_RADIO ? "\xE2\x97\x8F" : "\xE2\x9C\x93", c);
        int lx = r.x + (2 * PAD + UI_ADVANCE * 2) * scale;
        ui_draw_text(ren, lx, ty, scale, it->label, c);
        if (it->shortcut) {
            int sw = ui_text_width(it->shortcut, scale);
            ui_draw_text(ren, r.x + r.w - sw - 2 * PAD * scale, ty, scale, it->shortcut, c);
        }
    }
}

/* Mueve el resaltado a la siguiente opción seleccionable */
static void move_hover(MenuBar *mb, int dir)
{
    const Menu *m = &mb->menus[mb->open];
    int i = mb->hover;
    for (int n = 0; n < m->count; n++) {
        i = (i + dir + m->count) % m->count;
        if (m->items[i].kind != ITEM_SEPARATOR && item_enabled(mb, &m->items[i], NULL)) {
            mb->hover = i;
            return;
        }
    }
}

static void open_menu(MenuBar *mb, int index)
{
    mb->open = index;
    mb->hover = -1;
}

static int choose(MenuBar *mb, int *action)
{
    if (mb->open < 0 || mb->hover < 0) return 0;
    const MenuItem *it = &mb->menus[mb->open].items[mb->hover];
    if (it->kind == ITEM_SEPARATOR || !item_enabled(mb, it, NULL)) return 0;
    *action = it->action;
    menubar_close(mb);
    return 1;
}

int menubar_event(MenuBar *mb, const SDL_Event *e, int scale, int *action)
{
    *action = 0;
    switch (e->type) {
    case SDL_MOUSEMOTION: {
        int x = e->motion.x, y = e->motion.y;
        if (mb->open < 0) return 0;
        for (int i = 0; i < mb->count; i++)
            if (inside(title_rect(mb, i, scale), x, y)) {
                if (i != mb->open) open_menu(mb, i);
                return 1;
            }
        mb->hover = -1;
        if (inside(dropdown_rect(mb, scale), x, y)) {
            const Menu *m = &mb->menus[mb->open];
            for (int i = 0; i < m->count; i++)
                if (inside(item_rect(mb, i, scale), x, y) && m->items[i].kind != ITEM_SEPARATOR)
                    mb->hover = i;
        }
        return 1;
    }
    case SDL_MOUSEBUTTONDOWN: {
        int x = e->button.x, y = e->button.y;
        for (int i = 0; i < mb->count; i++)
            if (inside(title_rect(mb, i, scale), x, y)) {
                if (mb->open == i) menubar_close(mb);
                else open_menu(mb, i);
                return 1;
            }
        if (mb->open >= 0) {
            if (!inside(dropdown_rect(mb, scale), x, y))
                menubar_close(mb);
            return 1;
        }
        return y < menubar_height(scale);
    }
    case SDL_MOUSEBUTTONUP:
        if (mb->open >= 0 && inside(dropdown_rect(mb, scale), e->button.x, e->button.y)) {
            choose(mb, action);
            return 1;
        }
        return mb->open >= 0;
    case SDL_KEYDOWN: {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_LALT || k == SDLK_RALT) {
            if (!e->key.repeat) mb->armed = 1;
            return 1;
        }
        mb->armed = 0;
        if (k == SDLK_F10) {
            if (mb->open >= 0) menubar_close(mb);
            else open_menu(mb, 0);
            return 1;
        }
        if (mb->open < 0) return 0;
        switch (k) {
        case SDLK_ESCAPE: menubar_close(mb); break;
        case SDLK_LEFT:   open_menu(mb, (mb->open + mb->count - 1) % mb->count); move_hover(mb, 1); break;
        case SDLK_RIGHT:  open_menu(mb, (mb->open + 1) % mb->count); move_hover(mb, 1); break;
        case SDLK_UP:     move_hover(mb, -1); break;
        case SDLK_DOWN:   move_hover(mb, 1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:  choose(mb, action); break;
        default: break;
        }
        return 1;       /* con un menú abierto, el teclado es suyo */
    }
    case SDL_KEYUP: {
        SDL_Keycode k = e->key.keysym.sym;
        if ((k == SDLK_LALT || k == SDLK_RALT) && mb->armed) {
            mb->armed = 0;
            if (mb->open >= 0) menubar_close(mb);
            else {
                open_menu(mb, 0);
                move_hover(mb, 1);
            }
            return 1;
        }
        return mb->open >= 0;
    }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------------- */
/* Diálogos                                                                */
/* ---------------------------------------------------------------------- */

void dialog_open(Dialog *d, DialogKind kind, int tag, const char *title, const char *text)
{
    d->kind = kind;
    d->tag = tag;
    d->hover = -1;
    snprintf(d->title, sizeof d->title, "%s", title);
    snprintf(d->text, sizeof d->text, "%s", text);
    if (kind == DIALOG_INPUT) SDL_StartTextInput();
}

typedef struct {
    SDL_Rect box, title, input, ok, cancel;
    int lines, line_w;
} Layout;

static Layout layout(const Dialog *d, int width, int height, int scale)
{
    Layout L;
    memset(&L, 0, sizeof L);
    /* Medir el texto línea por línea */
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", d->text);
    int widest = ui_text_width(d->title, scale);
    L.lines = 0;
    for (char *line = buf, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        int w = ui_text_width(line, scale);
        if (w > widest) widest = w;
        L.lines++;
    }
    if (d->kind == DIALOG_INPUT && widest < 40 * UI_ADVANCE * scale)
        widest = 40 * UI_ADVANCE * scale;

    int ok_w = ui_text_width(tr(T_OK), scale), cancel_w = ui_text_width(tr(T_CANCEL), scale);
    int button_w = (ok_w > cancel_w ? ok_w : cancel_w) + 6 * PAD * scale;
    int w = widest + 6 * PAD * scale;
    if (w < 2 * button_w + 6 * PAD * scale) w = 2 * button_w + 6 * PAD * scale;
    int h = (ROW + 2) * scale                       /* título */
          + (3 * PAD + L.lines * ROW) * scale
          + (d->kind == DIALOG_INPUT ? (ROW + 4 * PAD) * scale : 0)
          + (ROW + 6 * PAD) * scale;                /* botones */
    if (w > width - 8) w = width - 8;
    L.box = (SDL_Rect){ (width - w) / 2, (height - h) / 2, w, h };
    L.title = (SDL_Rect){ L.box.x, L.box.y, w, (ROW + 2) * scale };

    int y = L.title.y + L.title.h + (3 * PAD + L.lines * ROW) * scale;
    if (d->kind == DIALOG_INPUT) {
        L.input = (SDL_Rect){ L.box.x + 3 * PAD * scale, y, w - 6 * PAD * scale, (ROW + 2) * scale };
        y += (ROW + 4 * PAD) * scale;
    }
    int bh = (ROW + 2) * scale, by = L.box.y + h - bh - 2 * PAD * scale;
    if (d->kind == DIALOG_MESSAGE) {
        L.ok = (SDL_Rect){ L.box.x + (w - button_w) / 2, by, button_w, bh };
    } else if (d->kind == DIALOG_WAIT) {
        L.cancel = (SDL_Rect){ L.box.x + (w - button_w) / 2, by, button_w, bh };
    } else {
        L.ok = (SDL_Rect){ L.box.x + w / 2 - button_w - PAD * scale, by, button_w, bh };
        L.cancel = (SDL_Rect){ L.box.x + w / 2 + PAD * scale, by, button_w, bh };
    }
    return L;
}

static void draw_button(SDL_Renderer *ren, SDL_Rect r, const char *text, int hot, int scale)
{
    fill(ren, r, hot ? c_item_hl : c_bar);
    frame(ren, r, c_border);
    int tw = ui_text_width(text, scale);
    ui_draw_text(ren, r.x + (r.w - tw) / 2, r.y + 2 * scale, scale, text, c_text);
}

void dialog_draw(const Dialog *d, SDL_Renderer *ren, int width, int height, int scale)
{
    if (d->kind == DIALOG_NONE) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    fill(ren, (SDL_Rect){ 0, 0, width, height }, (SDL_Color){ 0, 0, 0, 0x80 });

    Layout L = layout(d, width, height, scale);
    fill(ren, (SDL_Rect){ L.box.x + 3 * scale, L.box.y + 3 * scale, L.box.w, L.box.h }, c_shade);
    fill(ren, L.box, c_panel);
    fill(ren, L.title, c_bar);
    frame(ren, L.box, c_border);
    ui_draw_text(ren, L.title.x + 3 * PAD * scale, L.title.y + 3 * scale, scale, d->title, c_text);

    char buf[1024];
    snprintf(buf, sizeof buf, "%s", d->text);
    int y = L.title.y + L.title.h + 2 * PAD * scale;
    for (char *line = buf, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ui_draw_text(ren, L.box.x + 3 * PAD * scale, y, scale, line, c_text);
        y += ROW * scale;
    }

    if (d->kind == DIALOG_INPUT) {
        fill(ren, L.input, c_panel);
        frame(ren, L.input, c_border);
        ui_draw_text(ren, L.input.x + PAD * scale, L.input.y + 3 * scale, scale, d->input, c_text);
        /* Cursor parpadeante */
        if ((SDL_GetTicks() / 500) % 2) {
            int cx = L.input.x + PAD * scale + ui_text_width(d->input, scale);
            fill(ren, (SDL_Rect){ cx, L.input.y + 3 * scale, scale, (UI_GLYPH_H - 1) * scale }, c_text);
        }
    }

    if (L.ok.w) draw_button(ren, L.ok, tr(T_OK), d->hover == 0, scale);
    if (L.cancel.w) draw_button(ren, L.cancel, tr(T_CANCEL), d->hover == 1, scale);
}

static DialogResult finish(Dialog *d, DialogResult r)
{
    if (d->kind == DIALOG_INPUT) SDL_StopTextInput();
    d->kind = DIALOG_NONE;
    return r;
}

DialogResult dialog_event(Dialog *d, const SDL_Event *e, int width, int height, int scale)
{
    if (d->kind == DIALOG_NONE) return DIALOG_PENDING;
    Layout L = layout(d, width, height, scale);

    switch (e->type) {
    case SDL_MOUSEMOTION:
        d->hover = (L.ok.w && inside(L.ok, e->motion.x, e->motion.y)) ? 0
                 : (L.cancel.w && inside(L.cancel, e->motion.x, e->motion.y)) ? 1 : -1;
        break;
    case SDL_MOUSEBUTTONUP:
        if (L.ok.w && inside(L.ok, e->button.x, e->button.y)) return finish(d, DIALOG_OK);
        if (L.cancel.w && inside(L.cancel, e->button.x, e->button.y)) return finish(d, DIALOG_CANCEL);
        break;
    case SDL_TEXTINPUT:
        if (d->kind == DIALOG_INPUT && strlen(d->input) + strlen(e->text.text) < sizeof d->input - 1)
            strcat(d->input, e->text.text);
        break;
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (d->kind == DIALOG_WAIT) break;
            return finish(d, DIALOG_OK);
        case SDLK_ESCAPE:
            return finish(d, d->kind == DIALOG_MESSAGE ? DIALOG_OK : DIALOG_CANCEL);
        case SDLK_BACKSPACE:
            if (d->kind == DIALOG_INPUT) {
                size_t n = strlen(d->input);
                /* Borra un carácter UTF-8 completo */
                while (n > 0 && ((unsigned char)d->input[n - 1] & 0xC0) == 0x80) n--;
                if (n > 0) n--;
                d->input[n] = 0;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return DIALOG_PENDING;
}
