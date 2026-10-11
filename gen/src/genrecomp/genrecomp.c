/*
 * GENRecomp: recompilador estático de juegos de Sega Genesis.
 *
 * Fase 0: lee la ROM y muestra lo que el recompilador usará como punto de partida (formato,
 * SHA1, cabecera y la tabla de vectores, de donde sale el descubrimiento). La emisión de C y
 * del proyecto llegan en la fase 6 (docs/PLAN.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"
#include "embedded.h"

#ifndef GENRECOMP_VERSION
#define GENRECOMP_VERSION "dev"
#endif

static u8 *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

static void usage(void)
{
    fprintf(stderr,
        "GENRecomp %s: recompilador estático de Sega Genesis\n"
        "uso: genrecomp rom.bin [--info]\n"
        "  --info   formato, SHA1, cabecera y vectores de la ROM (lo que hace sin opciones)\n"
        "  (recompilar con -o carpeta llega en la fase 6: ver docs/PLAN.md)\n",
        GENRECOMP_VERSION);
}

/* Ancho en letras de un texto UTF-8 (printf cuenta bytes y los acentos desalinean las columnas) */
static int utf8_width(const char *s)
{
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static void print_info(const char *path, const Gen *g)
{
    const Cart *c = &g->cart;
    const GenHeader *h = &c->hdr;
    printf("%s: %zu KB, %s\n", path, c->rom_size / 1024, c->format == ROMFMT_SMD ? "SMD (entrelazado)" : "binario");
    printf("SHA1:        %s\n", c->sha1);
    if (!h->sega) printf("aviso: no hay \"SEGA\" en $100; puede no ser una ROM de Genesis\n");
    printf("sistema:     %s\n", h->system);
    printf("copyright:   %s\n", h->copyright);
    printf("nombre (JP): %s\n", h->name_dom);
    printf("nombre:      %s\n", h->name_int);
    printf("serie:       %s\n", h->serial);
    printf("región:      %s\n", h->region);
    printf("periféricos: %s\n", h->io);
    printf("suma:        $%04X (calculada $%04X, %s)\n", h->checksum, h->checksum_calc,
           h->checksum == h->checksum_calc ? "coincide" : "no coincide");
    printf("ROM:         $%06X-$%06X\n", h->rom_start, h->rom_end);
    if (c->sram)
        printf("SRAM:        $%06X-$%06X (tipo $%02X%s)\n", c->sram_start, c->sram_end, c->sram_type,
               c->sram_type & 0x40 ? ", con batería" : "");
    else
        printf("SRAM:        no\n");
    if (c->rom_size > ROM_LINEAR) printf("mapper:      ROM de más de 4 MB (necesita el mapper de SSF2)\n");
    printf("vectores:\n");
    for (int n = 0; n < VEC_COUNT; n++) {
        const char *name = cart_vector_name(n);
        char trap[16];
        if (!name && n >= VEC_TRAP0 && n < VEC_TRAP0 + 16) { snprintf(trap, sizeof trap, "TRAP #%d", n - VEC_TRAP0); name = trap; }
        if (name) printf("  %2d %s%*s $%06X\n", n, name, 20 - utf8_width(name), "", cart_vector(c, n));
    }
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--info")) continue;
        if (!strcmp(argv[i], "--version")) { printf("genrecomp %s\n", GENRECOMP_VERSION); return 0; }
        if (argv[i][0] == '-') { fprintf(stderr, "opción desconocida: %s\n", argv[i]); usage(); return 2; }
        if (rom) { fprintf(stderr, "argumento de más: %s\n", argv[i]); return 2; }
        rom = argv[i];
    }
    if (!rom) { usage(); return 2; }
    size_t len;
    u8 *data = read_file(rom, &len);
    if (!data) { fprintf(stderr, "no se pudo leer %s\n", rom); return 1; }
    Gen *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || gen_load(g, data, len, &err)) {
        fprintf(stderr, "%s: %s\n", rom, err ? err : "sin memoria");
        return 1;
    }
    free(data);
    print_info(rom, g);
    gen_free(g);
    free(g);
    (void)embedded_files;       /* el runtime ya va adentro; se escribe a los proyectos en la fase 6 */
    return 0;
}
