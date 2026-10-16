/*
 * smsramdump: corre una ROM sin ventana y vuelca los 8 KB de RAM de trabajo (una línea "DIRECCION VALOR" por
 * byte, de $00C000 a $00DFFF: con 6 cifras, como retro_run). Como genramdump de GENRecomp y gbramdump de GBRecomp.
 *   smsramdump rom.sms [--frames N] [--input archivo] [--seed S] [--model sms|gg] [boton:desde-hasta ...]
 */
#include <stdio.h>

#include "cli.h"
#include "console.h"
#include "romload.h"

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    int model = take_model(&argc, argv);
    if (model < MODEL_AUTO || cli_parse(&c, argc, argv, 1, 60)) {
        fprintf(stderr, "uso: smsramdump rom.sms [--frames N] [--input archivo] [--seed S] [--model sms|gg]\n"
                        "                  [boton:desde-hasta ...]\n");
        return 2;
    }
    Sms *g = load_sms(c.pos[0], model);
    if (!g) return 2;
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        sms_run_frame(g);
        g->audio.n = 0;
    }
    for (u32 a = 0; a < 0x2000; a++) printf("%06X %02X\n", 0xC000 | a, g->ram[a]);
    return 0;
}
