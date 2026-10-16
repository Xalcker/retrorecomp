/*
 * inputtest: las entradas grabadas (inputs.h) y las opciones comunes de botones (cli.h): leer, escribir
 * y volver a leer un guion, los nombres de los 10 botones, la forma $hex y el mando aleatorio de --seed
 * (el mismo para la misma semilla).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "console.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    console_utf8();
    u16 p;
    CHECK(inputs_parse_pad("START", &p) && p == NEO_START, "START");
    CHECK(inputs_parse_pad("down+a", &p) && p == (NEO_DOWN | NEO_A), "down+a");
    CHECK(inputs_parse_pad("B+C+D+SELECT", &p) && p == (NEO_B | NEO_C | NEO_D | NEO_SELECT), "B C D SELECT");
    CHECK(inputs_parse_pad("$110", &p) && p == (NEO_A | NEO_START), "forma $hex");
    CHECK(inputs_parse_pad("-", &p) && p == 0, "ninguno");
    CHECK(!inputs_parse_pad("MODE", &p), "MODE no es un botón del Neo Geo");
    char buf[96];
    inputs_format(buf, sizeof buf, NEO_UP | NEO_B | NEO_SELECT);
    CHECK(!strcmp(buf, "UP+B+SELECT"), "formato (%s)", buf);

    /* Grabar y volver a leer */
    const char *path = "build/tests/inputtest.txt";
    FILE *f = fopen(path, "w");
    if (!f) { printf("FALLA: no se pudo crear %s\n", path); return 1; }
    int last[2] = { -1, -1 };
    u16 seq[6][2] = { { 0, 0 }, { NEO_START, 0 }, { NEO_START, 0 }, { NEO_RIGHT | NEO_C, NEO_A }, { 0, 0 }, { NEO_D, 0 } };
    for (int i = 0; i < 6; i++) inputs_record(f, i * 10, seq[i], last);
    fclose(f);
    static InputScript s;
    const char *err = inputs_load(&s, path);
    CHECK(!err, "leer lo grabado: %s", err ? err : "");
    CHECK(s.n == 5, "una línea por cambio (%zu)", s.n);
    for (int i = 0; i < 6; i++)
        CHECK(inputs_pad(&s, i * 10 + 3, 0) == seq[i][0] && inputs_pad(&s, i * 10 + 3, 1) == seq[i][1], "cuadro %d", i * 10 + 3);

    /* --seed: la misma secuencia con la misma semilla, con A/B/derecha y sin C, D ni Select */
    static Cli a, b;
    char *argv[] = { "x", "rom", "--seed", "7" };
    cli_parse(&a, 4, argv, 1, 600);
    cli_parse(&b, 4, argv, 1, 600);
    int same = 1, any = 0;
    for (long fr = 0; fr < 2000; fr++) { u16 x = cli_pad(&a, fr, 0), y = cli_pad(&b, fr, 0); same &= x == y; any |= x; }
    CHECK(same && (any & (NEO_A | NEO_B | NEO_RIGHT)) && !(any & (NEO_C | NEO_D | NEO_SELECT)), "--seed reproducible y en los botones esperados");

    /* boton:desde-hasta */
    static Cli h;
    char *argv2[] = { "x", "rom", "start:5-6", "d:8-8" };
    CHECK(!cli_parse(&h, 4, argv2, 1, 600), "boton:desde-hasta");
    CHECK(cli_pad(&h, 5, 0) == NEO_START && cli_pad(&h, 7, 0) == 0 && cli_pad(&h, 8, 0) == NEO_D, "boton:desde-hasta en sus cuadros");
    if (!fails) printf("inputtest: todo bien\n");
    return fails != 0;
}
