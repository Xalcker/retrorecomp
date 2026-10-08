/*
 * Mete archivos dentro de un .c como arreglos de bytes, para que gbrecomp
 * lleve consigo el runtime y la plataforma y funcione desde cualquier carpeta.
 *
 * Uso: embed salida.c destino=origen [destino=origen ...]
 *   "destino" es la ruta dentro del proyecto generado (p. ej. runtime/gb.c).
 *
 * Genera:
 *   const EmbeddedFile embedded_files[];   (termina con name = NULL)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: embed salida.c destino=origen ...\n");
        return 2;
    }
    FILE *out = fopen(argv[1], "w");
    if (!out) {
        fprintf(stderr, "embed: no se pudo crear %s\n", argv[1]);
        return 1;
    }
    fprintf(out, "/* Generado por embed: no editar */\n#include \"embedded.h\"\n\n");

    for (int i = 2; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (!eq) {
            fprintf(stderr, "embed: falta '=' en %s\n", argv[i]);
            return 2;
        }
        FILE *in = fopen(eq + 1, "rb");
        if (!in) {
            fprintf(stderr, "embed: no se pudo abrir %s\n", eq + 1);
            return 1;
        }
        fprintf(out, "static const unsigned char file_%d[] = {", i);
        int c;
        long n = 0;
        while ((c = fgetc(in)) != EOF) {
            fprintf(out, "%s0x%02X,", n % 16 ? "" : "\n    ", c);
            n++;
        }
        fprintf(out, "\n    0x00\n};\n\n");
        fclose(in);
    }

    fprintf(out, "const EmbeddedFile embedded_files[] = {\n");
    for (int i = 2; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        fprintf(out, "    { \"%.*s\", file_%d, sizeof file_%d - 1 },\n",
                (int)(eq - argv[i]), argv[i], i, i);
    }
    fprintf(out, "    { 0, 0, 0 }\n};\n");
    fclose(out);
    return 0;
}
