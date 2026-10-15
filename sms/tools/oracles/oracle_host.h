/*
 * Lo que retro_run común (tools/retrorecomp/shared, el mismo de GENRecomp y NEORecomp) toma del Master System y
 * el Game Gear: las descripciones de botón de Genesis Plus GX y Gearsystem, y la RAM de trabajo (8 KB en $C000),
 * como smsramdump.
 * Los núcleos nombran distinto los dos botones del mando: Gearsystem "1" y "2"; Genesis Plus GX usa los nombres del
 * Genesis, "B" (el botón 1) y "C" (el 2). En los dos, "Start" es la Pausa en el Master System y el Start en el Game
 * Gear; ORACLE_PAD traduce: en un juego de SMS, START es el botón 1 (como en sms.h) y PAUSE va al Start del núcleo.
 */
#ifndef SMS_ORACLE_HOST_H
#define SMS_ORACLE_HOST_H

#include <string.h>

#define ORACLE_BUTTONS \
    { "up", SMS_UP }, { "down", SMS_DOWN }, { "left", SMS_LEFT }, { "right", SMS_RIGHT }, \
    { "1", SMS_A }, { "b", SMS_A }, { "2", SMS_B }, { "c", SMS_B }, { "start", SMS_START },
#define ORACLE_RAM_BASE 0xC000
#define ORACLE_RAM_SIZE 0x2000
#define ORACLE_RAM_NAME "la RAM de trabajo"

/* ¿El juego es de Game Gear? (por la extensión, como sms_load sin --model) */
static inline int oracle_gg(const char *game)
{
    size_t n = strlen(game);
    return n >= 3 && (game[n - 3] == '.') && (game[n - 2] | 0x20) == 'g' && (game[n - 1] | 0x20) == 'g';
}

static inline InputPad oracle_pad(InputPad p, const char *game)
{
    if (oracle_gg(game)) return (InputPad)(p & ~SMS_PAUSE);
    InputPad q = (InputPad)(p & ~(SMS_START | SMS_PAUSE));
    if (p & SMS_START) q |= SMS_A;
    if (p & SMS_PAUSE) q |= SMS_START;
    return q;
}
#define ORACLE_PAD(p, game) oracle_pad((p), (game))

#endif
