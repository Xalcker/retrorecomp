/*
 * neorecomp: recompilador estático de juegos de Neo Geo a C99.
 *
 * Uso: neorecomp juego.zip [--info] [--bios archivo] [--neo salida.neo]
 *
 * Fase 0 (docs/PLAN.md): lee el juego (.neo, zip al estilo MAME o carpeta con los chips), lo
 * normaliza y muestra sus regiones, sus SHA1, la cabecera de $100 y los vectores; busca y valida la
 * BIOS del AES y comprueba la tabla L0 calculada. El descubrimiento y la emisión de código llegan en
 * la fase 6.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "neodb.h"
#include "sha1.h"
#include "console.h"

#ifndef NEORECOMP_VERSION
#define NEORECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "neorecomp " NEORECOMP_VERSION ": recompila un juego de Neo Geo a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: neorecomp juego [--info] [opciones]\n"
    "  juego              un .neo, un .zip al estilo MAME (mslug.zip) o una carpeta con los chips\n"
    "  --bios archivo     la BIOS del AES: neo-epo.bin, neo-po.bin o el aes.zip de MAME (por omisión\n"
    "                     se busca junto al juego y en roms/, o en la variable NEO_BIOS)\n"
    "  --neo archivo      escribe el juego como .neo (el formato de Geolith y de otros emuladores)\n"
    "  --info             formato, regiones, SHA1, cabecera y vectores (lo que hace sin otras opciones)\n"
    "  -o carpeta         el proyecto recompilado (llega en la fase 6)\n"
    "  --version, --help\n";

/* Ancho en letras de un texto UTF-8 (printf cuenta bytes y los acentos desalinean las columnas) */
static int utf8_width(const char *s)
{
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static const char *format_name(int f)
{
    return f == ROMFMT_NEO ? ".neo" : f == ROMFMT_ZIP ? "zip al estilo MAME" : "carpeta con los chips";
}

/* Tamaño legible: KB o MB */
static void size_text(char *out, size_t n, size_t bytes)
{
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) snprintf(out, n, "%zu MB", bytes >> 20);
    else snprintf(out, n, "%zu KB", (bytes + 1023) >> 10);
}

static void print_info(const char *path, const Cart *c, const Bios *b, const char *bios_err, const u8 *l0)
{
    const NeoHeader *h = &c->hdr;
    char sz[32];
    size_text(sz, sizeof sz, c->size);
    printf("%s: %s, %s\n", path, format_name(c->format), sz);
    if (c->title[0] || c->set[0]) {
        printf("juego:       %s", c->title[0] ? c->title : c->set);
        if (c->set[0] || c->maker[0] || c->year) {
            printf(" (");
            const char *sep = "";
            if (c->set[0]) { printf("%s", c->set); sep = ", "; }
            if (c->maker[0]) { printf("%s%s", sep, c->maker); sep = " "; }
            if (c->year) printf("%s%u", c->maker[0] ? " " : sep, c->year);
            printf(")");
        }
        printf("%s\n", c->db ? "" : " (no está en la base de datos)");
    } else if (c->format != ROMFMT_NEO) printf("juego:       no está en la base de datos (armado por los nombres de los archivos)\n");
    printf("SHA1:        %s\n", c->sha1);
    printf("regiones:\n");
    for (int r = 0; r < REG_COUNT; r++) {
        size_text(sz, sizeof sz, c->len[r]);
        printf("  %-4s %8s  %s", neo_region_name[r], c->len[r] ? sz : "-", c->len[r] ? c->reg_sha1[r] : "");
        if (r == REG_VB && !c->len[r]) printf("(ADPCM-B usa el bus A)");
        if (c->files[r][0]) printf("  %s", c->files[r]);
        if (r == REG_P && c->p1_swapped) printf(" (mitades cambiadas)");
        printf("\n");
    }
    if (!h->neo) printf("aviso: no hay \"NEO-GEO\" en $100\n");
    else {
        size_text(sz, sizeof sz, h->p_size);
        printf("cabecera:    NEO-GEO, NGH $%04X, programa de %s (declarado)%s\n", h->ngh, sz,
               h->p_size != c->len[REG_P] ? ", distinto del real" : "");
        if (c->p_banks > 1) printf("P2:          %d bancos de 1 MB en $200000\n", c->p_banks);
        else if (c->p_banks) printf("P2:          fijo en $200000 (sin bancos)\n");
        printf("respaldo:    $%06X, %u bytes\n", h->backup_ptr, h->backup_size);
        static const char *const eye[] = { "la hace la BIOS", "la hace el juego", "ninguna" };
        printf("logo de SNK: %s\n", h->eyecatch < 3 ? eye[h->eyecatch] : "valor desconocido");
        printf("DIP:         Japón $%06X, EE. UU. $%06X, Europa $%06X\n", h->dip[0], h->dip[1], h->dip[2]);
        printf("entradas:\n");
        for (int i = 0; i < ENTRY_COUNT; i++)
            if (h->entry[i]) printf("  %-13s $%06X\n", neo_entry_name[i], h->entry[i]);
            else printf("  %-13s (sin JMP)\n", neo_entry_name[i]);
    }
    printf("vectores:\n");
    for (int n = 0; n < VEC_COUNT; n++) {
        const char *name = cart_vector_name(n);
        char trap[16];
        if (!name && n >= VEC_TRAP0 && n < VEC_TRAP0 + 16) { snprintf(trap, sizeof trap, "TRAP #%d", n - VEC_TRAP0); name = trap; }
        if (name) printf("  %2d %s%*s $%06X\n", n, name, 24 - utf8_width(name), "", c->vec[n]);
    }
    int fatal;
    const char *u = cart_unsupported(c, &fatal);
    if (u) printf("%s %s\n", fatal ? "no se puede:" : "aviso:      ", u);
    if (b->ver) {
        /* $400: 0 AES, $80 MVS; $401: región que ven los juegos (no hay BIOS del AES de EE. UU.) */
        static const char *const region[] = { "Japón", "EE. UU.", "Europa" };
        u8 reg = b->rom[0x401 ^ 1];
        printf("BIOS:        %s, %s, región %s (SHA1 %s)\n", b->path, b->ver->desc, reg < 3 ? region[reg] : "desconocida", b->sha1);
    }
    else printf("BIOS:        %s\n", bios_err);
    char l0sha[41];
    sha1_hex(l0, L0_SIZE, l0sha);
    printf("tabla L0:    calculada, SHA1 %s (%s)%s\n", l0sha, strcmp(l0sha, L0_SHA1) ? "NO es la del chip" : "la del chip",
           b->l0_checked > 0 ? "; igual a la 000-lo.lo de la BIOS" : b->l0_checked < 0 ? "; DISTINTA de la 000-lo.lo de la BIOS" : "");
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *bios_path = NULL, *neo_out = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--version")) { printf("neorecomp %s\n", NEORECOMP_VERSION); return 0; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { fputs(usage_text, stdout); return 0; }
        if (!strcmp(a, "--info")) continue;
        if (i + 1 < argc) {
            if (!strcmp(a, "--bios")) { bios_path = argv[++i]; continue; }
            if (!strcmp(a, "--neo")) { neo_out = argv[++i]; continue; }
            if (!strcmp(a, "-o")) { out = argv[++i]; continue; }
        }
        if (a[0] == '-') { fprintf(stderr, "opción desconocida o sin valor: %s\n\n%s", a, usage_text); return 2; }
        if (rom_path) { fprintf(stderr, "argumento de más: %s\n", a); return 2; }
        rom_path = a;
    }
    if (!rom_path) { fputs(usage_text, stderr); return 2; }
    if (out) { fprintf(stderr, "neorecomp: -o todavía no: el recompilador llega en la fase 6 (docs/PLAN.md)\n"); return 2; }

    static Neo neo;
    const char *err = NULL;
    if (cart_load_path(&neo.cart, rom_path, &err)) { fprintf(stderr, "%s: %s\n", rom_path, err); return 1; }
    l0_generate(neo.l0);
    const char *bios_err = NULL;
    if (bios_path ? bios_load(&neo.bios, bios_path, neo.l0, &bios_err) : bios_find(&neo.bios, rom_path, neo.l0, &bios_err))
        neo.bios.ver = NULL;
    if (neo_out) {
        if (!cart_write_neo(&neo.cart, neo_out)) { fprintf(stderr, "no se pudo escribir %s\n", neo_out); return 1; }
        printf("%s escrito (SHA1 de la imagen %s)\n", neo_out, neo.cart.sha1);
        return 0;
    }
    print_info(rom_path, &neo.cart, &neo.bios, bios_err, neo.l0);
    cart_free(&neo.cart);
    return 0;
}
