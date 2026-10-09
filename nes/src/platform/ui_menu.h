/*
 * (Viene de Moskva 89, el port de Tetris del mismo autor, con licencia MIT.)
 *
 * Barra de menús y diálogos dibujados dentro de la ventana (SDL2 no tiene
 * menús nativos, y así se ven igual en todos los sistemas).
 */
#ifndef UI_MENU_H
#define UI_MENU_H

#include <SDL.h>

typedef enum { ITEM_ACTION, ITEM_CHECK, ITEM_RADIO, ITEM_SEPARATOR } ItemKind;

typedef struct {
    const char *label;
    const char *shortcut;   /* solo texto, p. ej. "Ctrl+R"; puede ser NULL */
    int action;
    ItemKind kind;
} MenuItem;

typedef struct {
    const char *title;
    const MenuItem *items;
    int count;
} Menu;

/* Estado de una opción: devuelve si está habilitada y dice si va marcada */
typedef int (*MenuStateFn)(void *ctx, int action, int *checked);

typedef struct {
    const Menu *menus;
    int count;
    MenuStateFn state;
    void *ctx;

    int open;       /* menú abierto, o -1 */
    int hover;      /* opción resaltada del menú abierto, o -1 */
    int armed;      /* Alt presionado solo: al soltarlo se abre la barra */
} MenuBar;

int  menubar_height(int scale);
int  menubar_is_open(const MenuBar *mb);
void menubar_close(MenuBar *mb);
void menubar_draw(MenuBar *mb, SDL_Renderer *ren, int width, int scale);

/* Procesa un evento (coordenadas del ratón ya en pixeles del renderizador).
 * Devuelve 1 si el evento era para la barra; si se eligió una opción, la deja
 * en *action. */
int menubar_event(MenuBar *mb, const SDL_Event *e, int scale, int *action);

/* ---------------------------------------------------------------------- */

typedef enum { DIALOG_NONE, DIALOG_MESSAGE, DIALOG_INPUT, DIALOG_WAIT, DIALOG_CONFIRM } DialogKind;
typedef enum { DIALOG_PENDING, DIALOG_OK, DIALOG_CANCEL } DialogResult;

typedef struct {
    DialogKind kind;
    char title[64];
    char text[1024];        /* varias líneas separadas por \n */
    char input[128];        /* para DIALOG_INPUT */
    int tag;                /* para que quien lo abrió sepa cuál es */
    int hover;              /* botón bajo el ratón: 0 Aceptar, 1 Cancelar */
} Dialog;

void dialog_open(Dialog *d, DialogKind kind, int tag, const char *title, const char *text);
void dialog_draw(const Dialog *d, SDL_Renderer *ren, int width, int height, int scale);
DialogResult dialog_event(Dialog *d, const SDL_Event *e, int width, int height, int scale);

#endif
