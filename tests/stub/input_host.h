/* Un input_host.h de mentira para tests/comun_test.c: 12 botones en 16 bits, como el Genesis, con una
 * opción y un suelto propios como los de nesshot */
#ifndef INPUT_HOST_H
#define INPUT_HOST_H

#include <stdint.h>

#include "random_pad.h"

typedef uint16_t InputPad;
#define INPUT_BUTTONS 12
#define INPUT_NAMES "UP", "DOWN", "LEFT", "RIGHT", "B", "C", "A", "START", "Z", "Y", "X", "MODE"

static inline InputPad input_from_rp(unsigned rp)
{
    InputPad p = 0;
    if (rp & RP_A) p |= 1 << 6;
    if (rp & RP_B) p |= 1 << 4;
    if (rp & RP_START) p |= 1 << 7;
    if (rp & RP_UP) p |= 1 << 0;
    if (rp & RP_DOWN) p |= 1 << 1;
    if (rp & RP_LEFT) p |= 1 << 2;
    if (rp & RP_RIGHT) p |= 1 << 3;
    return p;
}

#define CLI_HOST_FIELDS int propia;
#define CLI_HOST_OPTION(c, a) (!strcmp((a), "--propia") ? ((c)->propia = 1) : 0)
#define CLI_HOST_EXTRA(a) (!strncmp((a), "zapper:", 7))

#endif
