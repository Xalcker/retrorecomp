/*
 * Lo que inputs.h y cli.h comunes (tools/retrorecomp/shared, los mismos de NESRecomp y NEORecomp) toman
 * del Genesis: los botones del mando de 6 botones (GEN_* de gen.h).
 */
#ifndef GEN_INPUT_HOST_H
#define GEN_INPUT_HOST_H

#include "gen.h"
#include "random_pad.h"

typedef u16 InputPad;
#define INPUT_BUTTONS 12
#define INPUT_NAMES "UP", "DOWN", "LEFT", "RIGHT", "B", "C", "A", "START", "Z", "Y", "X", "MODE"     /* orden de GEN_* */

/* El mando aleatorio común (RP_*, los bits de la NES) en los botones del Genesis: A y B del NES son
 * A y B; Select no se usa */
static inline InputPad input_from_rp(unsigned rp)
{
    InputPad p = 0;
    if (rp & RP_A) p |= GEN_A;
    if (rp & RP_B) p |= GEN_B;
    if (rp & RP_START) p |= GEN_START;
    if (rp & RP_UP) p |= GEN_UP;
    if (rp & RP_DOWN) p |= GEN_DOWN;
    if (rp & RP_LEFT) p |= GEN_LEFT;
    if (rp & RP_RIGHT) p |= GEN_RIGHT;
    return p;
}

#endif
