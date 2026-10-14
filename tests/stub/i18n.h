/* Lo poco de i18n.h (que es de cada recompilador) que usa shared/ui_menu.c, para las pruebas de este repo */
#ifndef I18N_H
#define I18N_H

typedef enum { T_OK, T_CANCEL } TextId;

static inline const char *tr(TextId id)
{
    return id == T_OK ? "Aceptar" : "Cancelar";
}

#endif
