/*
 * Base de datos de juegos: lo que la cabecera no dice (el mapper de los juegos sin cabecera o con la de
 * Codemasters rota, el modelo que necesitan, el modo SMS de algunos cartuchos del Game Gear). Se agregan
 * solo juegos comprobados con un archivo real, por el SHA1 de la imagen normalizada (cart.c), como neodb.c
 * en NEORecomp. Empieza vacía: las entradas llegan con las fases 2 y 7 (docs/PLAN.md).
 */
#include "sms.h"

#include <string.h>

static const SmsDbGame games[] = {
    { NULL, NULL, -1, MODEL_AUTO, 0 }
};

const SmsDbGame *smsdb_find(const char *sha1)
{
    for (const SmsDbGame *g = games; g->sha1; g++)
        if (!strcmp(g->sha1, sha1)) return g;
    return NULL;
}
