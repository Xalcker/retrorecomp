/* Un input_host.h de mentira para tests/comun_test.c: 12 botones en 16 bits, como el Genesis, con una
 * opción y un suelto propios como los de nesshot, y una línea de encabezado y una opción con valor propias
 * como las del Game Boy ("modelo cgb", --model) */
#ifndef INPUT_HOST_H
#define INPUT_HOST_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

#define INPUT_HOST_SCRIPT_FIELDS int nivel;
#define INPUT_HOST_SCRIPT_INIT(s) ((s)->nivel = -1)
#define INPUT_HOST_LINE(s, line) stub_line((line), &(s)->nivel)

static inline int stub_line(const char *line, int *nivel)
{
    char word[16];
    if (sscanf(line, " nivel %15s", word) != 1) return 0;
    if (!isdigit((unsigned char)word[0])) return -1;
    *nivel = atoi(word);
    return 1;
}

#define CLI_HOST_FIELDS int propia, nivel; const char *valor;
#define CLI_HOST_OPTION(c, a) (!strcmp((a), "--propia") ? ((c)->propia = 1) : 0)
#define CLI_HOST_VALUE(c, a, v) (!strcmp((a), "--valor") ? ((c)->valor = (v), 1) : 0)
#define CLI_HOST_EXTRA(a) (!strncmp((a), "zapper:", 7))
#define CLI_HOST_DONE(c) ((c)->nivel = (c)->input ? (c)->script.nivel : -1, 0)

#endif
