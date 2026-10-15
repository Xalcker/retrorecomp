/*
 * smsrecomp: recompilador estático de juegos de Sega Master System y Game Gear a C99.
 *
 * Uso: smsrecomp juego.sms [--info] [--model sms|gg]
 *
 * Hoy (fase 0) solo lee el cartucho y muestra lo que sabe de él: formato, SHA1 y CRC32, las cabeceras
 * (TMR SEGA, Codemasters, SDSC), el mapper y el modelo con que correría. El recompilador (descubrimiento,
 * funciones, emisión del proyecto, -o, --trace, --viz, --why...) llega en la fase 5 (docs/PLAN.md), con
 * la misma línea de comandos que nesrecomp, gbrecomp, genrecomp y neorecomp.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "console.h"

#ifndef SMSRECOMP_VERSION
#define SMSRECOMP_VERSION "dev"
#endif

static const char usage_text[] =
    "smsrecomp " SMSRECOMP_VERSION ": recompila una ROM de Sega Master System o Game Gear a un proyecto C99 con SDL2.\n"
    "\n"
    "uso: smsrecomp juego.sms [--info] [--model sms|gg]\n"
    "\n"
    "  --info           lo que se sabe del cartucho (también sin opciones)\n"
    "  --model M        sms (Master System II) o gg (Game Gear); por omisión, el de la extensión (.sms, .gg)\n"
    "                   o el de la región de la cabecera\n"
    "\n"
    "Recompilar (-o carpeta, --name, --trace, --viz, --why, --coverage, --html, --codemap) llega en la fase 5.\n";

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "smsrecomp: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

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

/* ------------------------------------------------------------------ */
/* --info                                                              */
/* ------------------------------------------------------------------ */

static void print_size(size_t bytes)
{
    if (bytes % 1024) printf("%zu bytes", bytes);
    else printf("%zu KB", bytes / 1024);
}

static void print_info(const char *path, const Sms *s)
{
    const Cart *c = &s->cart;
    printf("%s: ", path);
    print_size(c->rom_size);
    printf(", %s\n", c->copier ? "con cabecera de copiador (512 bytes, quitada)" : "binario");
    printf("SHA1:        %s\n", c->sha1);
    printf("CRC32:       %08X\n", c->crc32);
    if (c->hdr.found) {
        const SmsHeader *h = &c->hdr;
        u32 declared = size_code_bytes(h->size_code);
        printf("cabecera:    TMR SEGA en $%04X\n", h->at);
        printf("producto:    %s\n", h->product);
        printf("versión:     %u\n", h->version);
        printf("región:      %u (%s)\n", h->region, region_name(h->region));
        printf("tamaño:      $%X (", h->size_code);
        if (declared) print_size(declared); else printf("la BIOS no lo admite");
        printf(")\n");
        if (h->checksum == 0 && h->checksum_calc != 0)
            printf("suma:        sin suma ($0000; la BIOS del Game Gear y la japonesa no la comprueban)\n");
        else if (h->checksum_ok)
            printf("suma:        $%04X (calculada $%04X, %s)\n", h->checksum, h->checksum_calc,
                   h->checksum == h->checksum_calc ? "coincide" : "no coincide");
        else
            printf("suma:        $%04X (no se puede comprobar con ese tamaño)\n", h->checksum);
    } else {
        printf("cabecera:    no hay \"TMR SEGA\" (en $7FF0, $3FF0 ni $1FF0)\n");
    }
    if (c->cm.found)
        printf("Codemasters: %u páginas, %02X/%02X/%02X %02X:%02X, suma $%04X (calculada $%04X, %s)\n",
               c->cm.banks, c->cm.day, c->cm.month, c->cm.year, c->cm.hour, c->cm.minute, c->cm.checksum,
               c->cm.checksum_calc, c->cm.checksum == c->cm.checksum_calc ? "coincide" : "no coincide");
    if (c->sdsc.found)
        printf("SDSC:        %s%s%s, versión %X.%02X, %02X/%02X/%04X\n", c->sdsc.name[0] ? c->sdsc.name : "(sin nombre)",
               c->sdsc.author[0] ? " de " : "", c->sdsc.author, c->sdsc.major, c->sdsc.minor,
               c->sdsc.day, c->sdsc.month, c->sdsc.year);
    printf("mapper:      %s\n", mapper_name(c->mapper));
    printf("modelo:      %s (%s%s)\n", model_name(s->model), s->model == MODEL_GG ? "Game Gear" : "Master System II",
           s->model == MODEL_GG && s->sms_mode ? ", en su modo SMS" : "");
    printf("juego:       %s\n", c->db ? c->db->name : "no está en la base de datos (se usa lo que dicen la cabecera y el tamaño)");
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *path = NULL;
    int model = MODEL_AUTO;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--info")) continue;
        if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = model_from_name(argv[++i]);
            if (model < MODEL_AUTO) die("modelo desconocido: %s (sms o gg)", argv[i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fputs(usage_text, stdout);
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1]) {
            die("opción desconocida o todavía no disponible: %s (ver smsrecomp sin argumentos)", argv[i]);
        } else if (!path) {
            path = argv[i];
        } else {
            die("sobra un argumento: %s", argv[i]);
        }
    }
    if (!path) {
        fputs(usage_text, stderr);
        return 2;
    }
    size_t len;
    u8 *data = read_file(path, &len);
    if (!data) die("no se pudo leer %s", path);
    static Sms s;
    const char *err = NULL;
    if (sms_load(&s, data, len, path, model, &err)) die("%s: %s", path, err);
    free(data);
    print_info(path, &s);
    sms_free(&s);
    return 0;
}
