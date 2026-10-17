/*
 * inputtest: las entradas grabadas (inputs.h) y las opciones comunes de botones (cli.h): leer, escribir y volver
 * a leer un guion (con una sola columna, la del Game Boy), los nombres de los 8 botones, la forma $hex, las líneas
 * `modelo` y `paleta` del encabezado, el mando aleatorio de --seed (el mismo para la misma semilla) y --model.
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
    u8 p;
    CHECK(inputs_parse_pad("START", &p) && p == PAD_START, "START");
    CHECK(inputs_parse_pad("down+a", &p) && p == (PAD_DOWN | PAD_A), "down+a");
    CHECK(inputs_parse_pad("$11", &p) && p == (PAD_A | PAD_RIGHT), "forma $hex");
    CHECK(inputs_parse_pad("-", &p) && p == 0, "ninguno");
    CHECK(!inputs_parse_pad("C", &p), "C no es un botón del Game Boy");
    CHECK(!inputs_parse_pad("PAUSE", &p), "PAUSE no es un botón del Game Boy");
    char buf[96];
    inputs_format(buf, sizeof buf, PAD_UP | PAD_B | PAD_SELECT);
    CHECK(!strcmp(buf, "B+SELECT+UP"), "formato (%s)", buf);

    /* Grabar (una sola columna) y volver a leer */
    const char *path = "build/tests/inputtest.txt";
    FILE *f = fopen(path, "w");
    if (!f) { printf("FALLA: no se pudo crear %s\n", path); return 1; }
    int last[2] = { -1, -1 };
    u8 seq[6] = { 0, PAD_START, PAD_START, PAD_RIGHT | PAD_B, 0, PAD_A };
    for (int i = 0; i < 6; i++) { InputPad pads[2] = { seq[i], 0 }; inputs_record(f, i * 10, pads, last); }
    fclose(f);
    static InputScript s;
    const char *err = inputs_load(&s, path);
    CHECK(!err, "leer lo grabado: %s", err ? err : "");
    CHECK(s.n == 5, "una línea por cambio (%zu)", s.n);
    CHECK(s.model == GB_MODEL_AUTO && !s.palette[0], "sin encabezado, modelo y paleta sin fijar");
    for (int i = 0; i < 6; i++)
        CHECK(inputs_pad(&s, i * 10 + 3, 0) == seq[i] && inputs_pad(&s, i * 10 + 3, 1) == 0, "cuadro %d", i * 10 + 3);

    /* Las líneas modelo y paleta del encabezado */
    f = fopen(path, "w");
    fprintf(f, "modelo cgb\npaleta up+a\n0 START\n");
    fclose(f);
    static InputScript h;
    err = inputs_load(&h, path);
    CHECK(!err && h.model == GB_MODEL_CGB && !strcmp(h.palette, "up+a") && h.n == 1, "modelo y paleta del encabezado (%s)", err ? err : "");
    f = fopen(path, "w");
    fprintf(f, "modelo gbc\n0 START\n");
    fclose(f);
    static InputScript bad;
    CHECK(inputs_load(&bad, path) != NULL, "un modelo desconocido es un error");

    /* --seed: la misma secuencia con la misma semilla */
    static Cli a, b;
    char *argv[] = { "x", "rom", "--seed", "7" };
    cli_parse(&a, 4, argv, 1, 600);
    cli_parse(&b, 4, argv, 1, 600);
    int same = 1, any = 0;
    for (long fr = 0; fr < 2000; fr++) { u8 x = cli_pad(&a, fr, 0), y = cli_pad(&b, fr, 0); same &= x == y; any |= x; }
    CHECK(same && (any & (PAD_A | PAD_B | PAD_RIGHT)), "--seed reproducible y en los botones esperados");

    /* boton:desde-hasta */
    static Cli hold;
    char *argv2[] = { "x", "rom", "start:5-6", "select:8-8" };
    CHECK(!cli_parse(&hold, 4, argv2, 1, 600), "boton:desde-hasta");
    CHECK(cli_pad(&hold, 5, 0) == PAD_START && cli_pad(&hold, 7, 0) == 0 && cli_pad(&hold, 8, 0) == PAD_SELECT, "boton:desde-hasta en sus cuadros");

    /* --model: lo resuelve cli_parse (CLI_HOST_DONE de input_host.h) */
    static Cli m;
    char *argv3[] = { "x", "rom", "--model", "cgb", "--frames", "5" };
    CHECK(!cli_parse(&m, 6, argv3, 1, 600) && m.model == GB_MODEL_CGB && m.frames == 5, "--model cgb");
    char *argv4[] = { "x", "rom", "--model", "gbc" };
    CHECK(cli_parse(&m, 4, argv4, 1, 600) == 2, "--model gbc es un error");
    char *argv5[] = { "x", "rom" };
    CHECK(!cli_parse(&m, 2, argv5, 1, 600) && m.model == GB_MODEL_AUTO, "sin --model, GB_MODEL_AUTO");
    if (!fails) printf("inputtest: todo bien\n");
    return fails != 0;
}
