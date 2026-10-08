/*
 * Genera el cuerpo del intérprete (un case por opcode) a partir del mismo
 * emisor que usa gbrecomp. Uso: gen_interp salida.inc
 */
#include <stdio.h>

#include "../emit/sm83_emit.h"

static void interp_jump(Emit *e, const char *expr, int is_const, uint16_t target)
{
    (void)is_const;
    (void)target;
    emit_line(e, "c->pc = %s;", expr);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "uso: gen_interp salida.inc\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "w");
    if (!f) {
        perror(argv[1]);
        return 1;
    }

    fprintf(f, "/* Generado por gen_interp a partir de sm83_emit.c: no editar. */\n");
    Emit e = { 0 };
    e.f = f;
    e.recomp = 0;
    e.jump = interp_jump;

    for (int op = 0; op < 256; op++) {
        uint8_t bytes[3] = { (uint8_t)op, 0, 0 };
        char text[32];
        sm83_disasm(bytes, 0, text, sizeof text);
        fprintf(f, "case 0x%02X: { /* %s */\n", op, op == 0xCB ? "prefijo CB" : text);
        e.indent = "    ";
        emit_insn(&e, (uint8_t)op);
        fprintf(f, "    break;\n}\n");
    }
    fclose(f);
    return 0;
}
