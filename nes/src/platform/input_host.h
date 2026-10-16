/*
 * Lo que inputs.h y cli.h comunes (tools/retrorecomp/shared, los mismos de GENRecomp y NEORecomp) toman de
 * la NES: los botones del mando (BTN_* de nes.h) y las opciones propias de nesshot (la Zapper y
 * --entradas-241).
 */
#ifndef NES_INPUT_HOST_H
#define NES_INPUT_HOST_H

#include "nes.h"
#include "random_pad.h"

typedef u8 InputPad;
#define INPUT_BUTTONS 8
#define INPUT_NAMES "A", "B", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT"     /* orden de BTN_* */

/* El mando aleatorio común: RP_* son los bits de la NES */
static inline InputPad input_from_rp(unsigned rp) { return (InputPad)rp; }

/* nesshot: --entradas-241 y la Zapper (`zapper:X,Y,desde-hasta[,mira]`, va a Cli.extra) */
#define CLI_HOST_FIELDS int entradas_241;
#define CLI_HOST_OPTION(c, a) (!strcmp((a), "--entradas-241") ? ((c)->entradas_241 = 1) : 0)
#define CLI_HOST_EXTRA(a) (!strncmp((a), "zapper:", 7))

#endif
