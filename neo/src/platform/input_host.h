/*
 * Lo que inputs.h y cli.h comunes (tools/retrorecomp/shared, los mismos de NESRecomp y GENRecomp) toman
 * del Neo Geo: los botones del mando (NEO_* de neo.h).
 */
#ifndef NEO_INPUT_HOST_H
#define NEO_INPUT_HOST_H

#include "neo.h"
#include "random_pad.h"

typedef u16 InputPad;
#define INPUT_BUTTONS 10
#define INPUT_NAMES "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "D", "START", "SELECT"     /* orden de NEO_* */

/* El mando aleatorio común (RP_*, los bits de la NES) en los botones del Neo Geo: A y B del NES son
 * A y B, Start es Start; C, D y Select no los pulsa (como en el Genesis no pulsa C, X, Y, Z ni Mode) */
static inline InputPad input_from_rp(unsigned rp)
{
    InputPad p = 0;
    if (rp & RP_A) p |= NEO_A;
    if (rp & RP_B) p |= NEO_B;
    if (rp & RP_START) p |= NEO_START;
    if (rp & RP_UP) p |= NEO_UP;
    if (rp & RP_DOWN) p |= NEO_DOWN;
    if (rp & RP_LEFT) p |= NEO_LEFT;
    if (rp & RP_RIGHT) p |= NEO_RIGHT;
    return p;
}

#endif
