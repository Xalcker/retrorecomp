/*
 * smstrace: corre una ROM sin ventana y guarda qué instrucciones de la ROM ejecutó (líneas "BANCO:DIRECCION"
 * en hexadecimal, trace_line.h, con bancos de 16 KB) para guiar a smsrecomp (fase 5).
 *   smstrace rom.sms traza.txt [--frames N] [--input archivo] [--seed S] [--model sms|gg] [boton:desde-hasta ...]
 * Con varias corridas se pueden juntar trazas (smsrecomp aceptará --trace varias veces).
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
    if (model < MODEL_AUTO || cli_parse(&c, argc, argv, 2, 600)) {
        fprintf(stderr, "uso: smstrace rom.sms traza.txt [--frames N] [--input archivo] [--seed S] [--model sms|gg]\n"
                        "                [boton:desde-hasta ...]\n");
        return 2;
    }
    Sms *g = load_sms(c.pos[0], model);
    if (!g) return 2;
    sms_trace_start(g);
    for (long f = 0; f < c.frames; f++) {
        g->pad[0] = cli_pad(&c, f, 0);
        g->pad[1] = cli_pad(&c, f, 1);
        sms_run_frame(g);
        g->audio.n = 0;
    }
    if (!sms_trace_write(g, c.pos[1])) { fprintf(stderr, "no se pudo escribir %s\n", c.pos[1]); return 2; }
    printf("%s: %zu instrucciones distintas en %ld cuadros\n", c.pos[1], sms_trace_count(g), c.frames);
    return 0;
}
