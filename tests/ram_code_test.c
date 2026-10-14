/*
 * Prueba de shared/ram_code.h sin consola: anotar instrucciones, escribir las líneas "ram", leerlas
 * de vuelta, unir corridas y armar bloques.
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -Ishared -o ram_code_test tests/ram_code_test.c && ./ram_code_test
 */
#include "ram_code.h"
#include "trace_line.h"

static int fails;

#define CHECK(cond) do { if (!(cond)) { printf("FALLA línea %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* Una instrucción de 68000 de mentira: 2 bytes de clave (el opcode) y len en total */
static void obs(RcTable *t, uint32_t addr, unsigned len, uint8_t op, uint8_t operand)
{
    uint8_t b[RC_MAXLEN] = { 0x4E, op, operand, 0x34, 0, 0, 0, 0, 0, 0 };
    rc_observe(t, addr, b, len, 2);
}

int main(void)
{
    RcTable t;
    CHECK(rc_init(&t, 0xFF0000, 0x10000));

    /* Una rutina: $FF0100 (4 bytes, su operando cambia), $FF0104 (2), $FF0106 (2) */
    obs(&t, 0xFF0100, 4, 0x71, 0x10);
    obs(&t, 0xFF0100, 4, 0x71, 0x20);
    obs(&t, 0xFF0104, 2, 0x72, 0);
    obs(&t, 0xFF0106, 2, 0x75, 0);
    /* Una dirección con más claves que RC_VERSIONS: no se aprende */
    for (int k = 0; k < RC_VERSIONS + 1; k++) obs(&t, 0xFF0200, 2, (uint8_t)k, 0);
    /* Fuera de la región: se ignora */
    obs(&t, 0xFE0000, 2, 0x71, 0);

    RcSummary s = rc_summary(&t);
    CHECK(s.fixed == 2 && s.vol == 1 && s.multi == 0 && s.lost == 1);

    char buf[4096];
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (!f) return 1;
    fputs("00:000200\n", f);                 /* una línea de la traza común, en el mismo archivo */
    rc_write(&t, f, 6);
    rewind(f);
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    buf[got] = 0;
    CHECK(strstr(buf, "ram FF0100 4E 71 ?? 34\n") != NULL);
    CHECK(strstr(buf, "ram FF0104 4E 72\n") != NULL);
    CHECK(strstr(buf, "FF0200") == NULL);

    /* Leer de vuelta: trace_line_read ignora las líneas "ram" y rc_parse ignora las otras */
    rewind(f);
    RcList l = { 0 };
    char line[128];
    unsigned trace_lines = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long off, addr;
        RcIns in;
        if (trace_line_read(line, 19, &off, &addr)) { trace_lines++; continue; }
        if (!rc_parse(line, &in)) continue;
        in.key = 2;
        CHECK(rc_list_add(&l, &in));
    }
    fclose(f);
    CHECK(trace_lines == 1);
    CHECK(l.n == 3);

    /* Otra corrida vio la misma rutina con otro operando y otra versión de $FF0106 */
    RcIns more[2];
    CHECK(rc_parse("ram FF0100 4E 71 30 35", &more[0]));
    CHECK(rc_parse("ram FF0106 4E 73\n", &more[1]));
    for (int i = 0; i < 2; i++) { more[i].key = 2; CHECK(rc_list_add(&l, &more[i])); }
    CHECK(!rc_parse("ram FF0100 4E 7", &more[0]));
    CHECK(!rc_parse("00:000200", &more[0]));

    CHECK(rc_list_finish(&l) == 0);
    CHECK(l.n == 4);
    CHECK(l.ins[0].addr == 0xFF0100 && l.ins[0].vol == (1u << 2 | 1u << 3) && l.ins[0].bytes[3] == 0);

    /* $FF0104 tiene dos sucesores posibles ($FF0106 en dos versiones): el primer bloque termina ahí */
    RcBlock *b = NULL;
    size_t nb = rc_blocks(&l, &b, NULL);
    CHECK(nb == 3);
    if (nb == 3) {
        CHECK(b[0].start == 0xFF0100 && b[0].n == 2 && b[0].len == 6);
        CHECK(rc_block_has(&l, &b[0], 0xFF0104) && !rc_block_has(&l, &b[0], 0xFF0106));
        CHECK(b[1].start == 0xFF0106 && b[1].n == 1 && b[2].start == 0xFF0106);
    }

    /* Demasiadas versiones al compilar: se descartan */
    RcList many = { 0 };
    for (int k = 0; k < RC_MAXVER + 1; k++) {
        RcIns in;
        char s2[64];
        snprintf(s2, sizeof s2, "ram FF0300 4E %02X", k);
        CHECK(rc_parse(s2, &in));
        in.key = 2;
        rc_list_add(&many, &in);
    }
    CHECK(rc_list_finish(&many) == RC_MAXVER + 1 && many.n == 0);

    if (fails) { printf("%d fallas\n", fails); return 1; }
    printf("ram_code.h: sin fallas\n");
    return 0;
}
