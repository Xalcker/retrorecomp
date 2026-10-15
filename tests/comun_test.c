/*
 * Prueba de lo de shared/ que comparten las ventanas y las herramientas, sin consola: settings.c (las
 * preferencias), inputs.h y cli.h (las entradas grabadas y las opciones comunes) y audio.c (la mezcla).
 * Cada consola pone su settings.h, input_host.h y audio_host.h; aquí van unos de mentira (tests/stub).
 *   cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Ishared -Itests/stub -o comun_test \
 *      tests/comun_test.c shared/settings.c shared/files.c shared/audio.c -lm && ./comun_test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "cli.h"
#include "audio_host.h"

static int fails;

#define CHECK(cond) do { if (!(cond)) { printf("FALLA línea %d: %s\n", __LINE__, #cond); fails++; } } while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static void test_settings(void)
{
    const char *ini = "comun_test_settings.ini";
    Settings s;
    remove(ini);
    settings_load(&s, ini);                     /* sin archivo: los valores por omisión */
    CHECK(s.scale == 2 && s.sound == 1 && s.pad == 0 && !strcmp(s.model, "auto") && !s.language[0]);

    write_file(ini, "# comentario\nscale=9\nsound=-3\npad=6\nlanguage=english\nmodel=cgbcgbcgb\notra=1\n");
    settings_load(&s, ini);
    CHECK(s.scale == 6);                        /* al rango */
    CHECK(s.sound == 1);                        /* cualquier número distinto de 0 */
    CHECK(s.pad == 6);
    CHECK(!strcmp(s.language, "en"));           /* largo máximo */
    CHECK(!strcmp(s.model, "cgbcgbc"));

    write_file(ini, "pad=4\nscale=0\nsound=0\n");
    settings_load(&s, ini);
    CHECK(s.pad == 0 && s.scale == 1 && s.sound == 0);      /* SETTINGS_FIX */

    s.scale = 5; s.pad = 3; snprintf(s.language, sizeof s.language, "es");
    settings_save(&s, ini);
    Settings t;
    settings_load(&t, ini);
    CHECK(!memcmp(&s, &t, sizeof s));           /* ida y vuelta */
    FILE *f = fopen(ini, "r");
    char line[64];
    CHECK(fgets(line, sizeof line, f) && line[0] == '#');
    CHECK(fgets(line, sizeof line, f) && !strcmp(line, "model=auto\n"));     /* en el orden de la lista */
    fclose(f);
    remove(ini);
}

static void test_inputs(void)
{
    const char *path = "comun_test_inputs.txt";
    InputPad p;
    CHECK(inputs_parse_pad("right+a", &p) && p == ((1 << 3) | (1 << 6)));
    CHECK(inputs_parse_pad("-", &p) && p == 0);
    CHECK(inputs_parse_pad("$FFFF", &p) && p == 0xFFF);    /* solo los botones que hay */
    CHECK(!inputs_parse_pad("A+SELECT", &p));
    char out[128];
    inputs_format(out, sizeof out, (1 << 7) | (1 << 11));
    CHECK(!strcmp(out, "START+MODE"));

    write_file(path, "# guion\n0 -\n300 START\n310 - B\n400 RIGHT+A\n");
    char *argv[] = { "t", "rom", "sal", "500", "--input", (char *)path, "--propia", "a:5-6", "zapper:1,2,3-4", "q:1-2" };
    Cli c;
    CHECK(cli_parse(&c, 10, argv, 2, 60) == 0);
    CHECK(c.frames == 500 && c.npos == 2 && c.propia == 1);
    CHECK(c.nh == 1 && c.nextra == 2);          /* zapper: (CLI_HOST_EXTRA) y q:1-2 (no es un botón) */
    CHECK(cli_pad(&c, 5, 0) == (1 << 6));
    CHECK(cli_pad(&c, 300, 0) == (1 << 7));
    CHECK(cli_pad(&c, 310, 1) == (1 << 4));
    CHECK(cli_pad(&c, 450, 0) == ((1 << 3) | (1 << 6)) && cli_pad(&c, 450, 1) == 0);

    /* Grabar y volver a leer da lo mismo */
    FILE *f = fopen(path, "w");
    int last[2] = { -1, -1 };
    InputPad pads[2] = { 0, 0 };
    inputs_record(f, 0, pads, last);
    pads[0] = (1 << 7) | (1 << 11); pads[1] = 1 << 2;
    inputs_record(f, 7, pads, last);
    inputs_record(f, 8, pads, last);            /* sin cambio: no escribe */
    fclose(f);
    InputScript s;
    CHECK(inputs_load(&s, path) == NULL && s.n == 2);
    CHECK(inputs_pad(&s, 9, 0) == ((1 << 7) | (1 << 11)) && inputs_pad(&s, 9, 1) == (1 << 2));

    /* --seed: el mando aleatorio común en los botones de la consola, igual en cada corrida */
    char *argv2[] = { "t", "rom", "--seed", "7" };
    CHECK(cli_parse(&c, 4, argv2, 1, 60) == 0);
    InputPad first[100];
    for (int i = 0; i < 100; i++) first[i] = cli_pad(&c, i, 0);
    cli_rewind(&c);
    int same = 1, any = 0;
    for (int i = 0; i < 100; i++) { same &= cli_pad(&c, i, 0) == first[i]; any |= first[i] != 0; }
    CHECK(same && any);
    remove(path);
}

static void test_audio(void)
{
    static Host g;
    g.audio.mute = 5;
    audio_reset(&g);
    CHECK(g.audio.mute == 5 && g.audio.first == 0);    /* el reinicio conserva lo silenciado */
    audio_add(&g, 0, 250, 1000.0f, -1000.0f);
    audio_add(&g, 250, 400, 1000.0f, -1000.0f);         /* en dos tramos: lo mismo que en uno */
    audio_add(&g, 50, 100, 0.0f, 0.0f);                 /* silencio: no suma */
    g.now = 350;                                        /* tres muestras completas; la cuarta, no */
    audio_end_frame(&g);
    CHECK(g.audio.n == 3);
    CHECK(g.audio.buf[0] == 1000 && g.audio.buf[1] == -1000);
    CHECK(abs(g.audio.buf[2] - 998) <= 1);             /* el pasa-altos empieza a bajar la continua */
    int16_t out[8];
    CHECK(host_audio_take(&g, out, 2) == 2 && g.audio.n == 1 && out[0] == 1000);
    g.now = 400;
    audio_end_frame(&g);
    CHECK(g.audio.n == 2);
}

int main(void)
{
    test_settings();
    test_inputs();
    test_audio();
    if (fails) { printf("%d fallas\n", fails); return 1; }
    printf("ok    settings.c, inputs.h, cli.h y audio.c\n");
    return 0;
}
