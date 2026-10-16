/*
 * inputtest: las entradas grabadas (inputs.h) y las opciones comunes de botones (cli.h): leer, escribir y volver
 * a leer un guion, los nombres de los 8 botones, la forma $hex y el mando aleatorio de --seed (el mismo para la
 * misma semilla, sin la Pausa); también --model, que las herramientas sacan antes de cli_parse.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "console.h"
#include "romload.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    console_utf8();
    u8 p;
    CHECK(inputs_parse_pad("START", &p) && p == SMS_START, "START");
    CHECK(inputs_parse_pad("down+a", &p) && p == (SMS_DOWN | SMS_A), "down+a");
    CHECK(inputs_parse_pad("B+PAUSE", &p) && p == (SMS_B | SMS_PAUSE), "B y la Pausa");
    CHECK(inputs_parse_pad("$50", &p) && p == (SMS_A | SMS_START), "forma $hex");
    CHECK(inputs_parse_pad("-", &p) && p == 0, "ninguno");
    CHECK(!inputs_parse_pad("SELECT", &p), "SELECT no es un botón del SMS");
    CHECK(!inputs_parse_pad("C", &p), "C no es un botón del SMS");
    char buf[96];
    inputs_format(buf, sizeof buf, SMS_UP | SMS_B | SMS_PAUSE);
    CHECK(!strcmp(buf, "UP+B+PAUSE"), "formato (%s)", buf);

    /* Grabar y volver a leer */
    const char *path = "build/tests/inputtest.txt";
    FILE *f = fopen(path, "w");
    if (!f) { printf("FALLA: no se pudo crear %s\n", path); return 1; }
    int last[2] = { -1, -1 };
    u8 seq[6][2] = { { 0, 0 }, { SMS_START, 0 }, { SMS_START, 0 }, { SMS_RIGHT | SMS_B, SMS_A }, { 0, 0 }, { SMS_PAUSE, 0 } };
    for (int i = 0; i < 6; i++) inputs_record(f, i * 10, seq[i], last);
    fclose(f);
    static InputScript s;
    const char *err = inputs_load(&s, path);
    CHECK(!err, "leer lo grabado: %s", err ? err : "");
    CHECK(s.n == 5, "una línea por cambio (%zu)", s.n);
    for (int i = 0; i < 6; i++)
        CHECK(inputs_pad(&s, i * 10 + 3, 0) == seq[i][0] && inputs_pad(&s, i * 10 + 3, 1) == seq[i][1], "cuadro %d", i * 10 + 3);

    /* --seed: la misma secuencia con la misma semilla, con A/B/derecha y sin la Pausa (pausaría el juego) */
    static Cli a, b;
    char *argv[] = { "x", "rom", "--seed", "7" };
    cli_parse(&a, 4, argv, 1, 600);
    cli_parse(&b, 4, argv, 1, 600);
    int same = 1, any = 0;
    for (long fr = 0; fr < 2000; fr++) { u8 x = cli_pad(&a, fr, 0), y = cli_pad(&b, fr, 0); same &= x == y; any |= x; }
    CHECK(same && (any & (SMS_A | SMS_B | SMS_RIGHT)) && !(any & SMS_PAUSE), "--seed reproducible y en los botones esperados");

    /* boton:desde-hasta */
    static Cli h;
    char *argv2[] = { "x", "rom", "start:5-6", "pause:8-8" };
    CHECK(!cli_parse(&h, 4, argv2, 1, 600), "boton:desde-hasta");
    CHECK(cli_pad(&h, 5, 0) == SMS_START && cli_pad(&h, 7, 0) == 0 && cli_pad(&h, 8, 0) == SMS_PAUSE, "boton:desde-hasta en sus cuadros");

    /* --model: se saca de los argumentos antes de cli_parse */
    char *argv3[] = { "x", "rom", "--model", "gg", "--frames", "5", NULL };
    int n3 = 6;
    CHECK(take_model(&n3, argv3) == MODEL_GG && n3 == 4 && !strcmp(argv3[2], "--frames"), "--model gg");
    char *argv4[] = { "x", "rom", "--model", "sg", NULL };
    int n4 = 4;
    CHECK(take_model(&n4, argv4) == -2, "--model sg es un error");
    if (!fails) printf("inputtest: todo bien\n");
    return fails != 0;
}
