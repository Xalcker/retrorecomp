/*
 * Lo que inputs.h y cli.h comunes (tools/retrorecomp/shared, los mismos de NESRecomp, GENRecomp y NEORecomp)
 * toman del Master System y el Game Gear: los botones del mando (SMS_* de sms.h).
 */
#ifndef SMS_INPUT_HOST_H
#define SMS_INPUT_HOST_H

#include "sms.h"
#include "random_pad.h"

typedef u8 InputPad;
#define INPUT_BUTTONS 8
#define INPUT_NAMES "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "START", "PAUSE"     /* orden de SMS_* */

/* El mando aleatorio común (RP_*, los bits de la NES) en los botones del SMS: A y B del NES son los botones 1
 * y 2; Start es el Start del Game Gear (en el Master System, el botón 1); Select no se usa */
static inline InputPad input_from_rp(unsigned rp)
{
    InputPad p = 0;
    if (rp & RP_A) p |= SMS_A;
    if (rp & RP_B) p |= SMS_B;
    if (rp & RP_START) p |= SMS_START;
    if (rp & RP_UP) p |= SMS_UP;
    if (rp & RP_DOWN) p |= SMS_DOWN;
    if (rp & RP_LEFT) p |= SMS_LEFT;
    if (rp & RP_RIGHT) p |= SMS_RIGHT;
    return p;
}

#endif
