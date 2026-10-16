/*
 * BIOS del AES y tabla L0.
 *
 * La BIOS es de SNK y la pone el usuario (nunca va en el repositorio): un archivo suelto o el aes.zip
 * de MAME. Se acepta por su SHA1. En el AES no hay SFIX ni SM1 (la capa fija y el Z80 salen siempre
 * del cartucho), así que de la BIOS solo hace falta su ROM de 128 KB. También se acepta la BIOS propia
 * de NEORecomp (fase 10, tools/neobios/), por su firma en $100: su SHA1 cambia con cada versión. Al
 * buscarla (bios_find) la propia va primero; la de SNK, si se quiere, con --bios o NEO_BIOS.
 *
 * La tabla L0 (000-lo.lo, un chip de la placa) dice qué línea de un sprite se dibuja con cada
 * reducción vertical: 256 tablas de 256 bytes (mosaico en el nibble alto, línea en el bajo), y el
 * chip de 128 KB repite esa mitad. La tabla z tiene z + 1 líneas: las de la secuencia
 * invertir_bits(i) ^ $88 para i = 0..z, ordenadas, y el resto en $FF. Se dedujo del contenido del chip
 * y se comprueba con su SHA1 (L0_SHA1).
 */
#include "neo.h"
#include "sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* En orden de preferencia: la de exportación (textos en inglés) primero */
const BiosVersion bios_versions[] = {
    { "1b3b22092f30c4d1b2c15f04d1670eb1e9fbea07", "neo-epo.bin", "AES de exportación" },
    { "4e4a440cae46f3889d20234aebd7f8d5f522e22c", "neo-po.bin", "AES de Japón" },
};
const int bios_version_count = (int)(sizeof bios_versions / sizeof bios_versions[0]);
const BiosVersion bios_propia = { "", "neobios.bin", "BIOS propia de NEORecomp" };

/* ¿La BIOS propia? La firma "NEORECOMP BIOS" en $100 (la imagen va en el orden de los chips: cada
 * palabra al revés) */
static int is_propia(const u8 *rom, size_t len)
{
    static const char sig[] = "NEORECOMP BIOS";
    if (len != BIOS_SIZE) return 0;
    for (size_t i = 0; i < sizeof sig - 1; i++)
        if (rom[(0x100 + i) ^ 1] != (u8)sig[i]) return 0;
    return 1;
}

void l0_generate(u8 out[L0_SIZE])
{
    memset(out, 0xFF, L0_SIZE);
    for (int z = 0; z < 256; z++) {
        u8 used[256] = { 0 };
        for (int i = 0; i <= z; i++) {
            int r = 0;
            for (int k = 0; k < 8; k++) r |= ((i >> k) & 1) << (7 - k);
            used[r ^ 0x88] = 1;
        }
        u8 *t = out + z * 256;
        for (int line = 0; line < 256; line++) if (used[line]) *t++ = (u8)line;
    }
    memcpy(out + L0_SIZE / 2, out, L0_SIZE / 2);
}

static const BiosVersion *accepted(const char *sha1)
{
    for (int i = 0; i < bios_version_count; i++)
        if (!strcmp(bios_versions[i].sha1, sha1)) return &bios_versions[i];
    return NULL;
}

static char bios_err[512];

/* Mensaje con las versiones aceptadas */
static const char *not_accepted(const char *what, const char *sha1)
{
    int l = snprintf(bios_err, sizeof bios_err, "%s: no es una BIOS del AES aceptada (SHA1 %s); se aceptan:", what, sha1);
    for (int i = 0; i < bios_version_count && l > 0 && (size_t)l < sizeof bios_err; i++)
        l += snprintf(bios_err + l, sizeof bios_err - (size_t)l, " %s (%s)", bios_versions[i].file, bios_versions[i].desc);
    return bios_err;
}

int bios_load(Bios *b, const char *path, const u8 *l0, const char **err)
{
    memset(b, 0, sizeof *b);
    size_t len;
    u8 *data = neo_read_file(path, &len);
    if (!data) { *err = "no se pudo leer la BIOS"; return -1; }
    int res = -1;
    if (len >= 4 && !memcmp(data, "PK\3\4", 4)) {
        RomPart *parts;
        int n = zip_read_all(data, len, &parts, err);
        if (n >= 0) {
            /* La primera versión aceptada que esté, en el orden de bios_versions */
            const RomPart *best = NULL;
            const BiosVersion *bv = NULL;
            for (int i = 0; i < n; i++) {
                if (parts[i].size != BIOS_SIZE) continue;
                char s[41];
                sha1_hex(parts[i].data, parts[i].size, s);
                const BiosVersion *v = accepted(s);
                if (!v && is_propia(parts[i].data, parts[i].size)) v = &bios_propia;
                /* las de SNK primero, en el orden de bios_versions; la propia, si no hay otra */
                if (v && (!bv || (bv == &bios_propia && v != &bios_propia) ||
                          (v != &bios_propia && bv != &bios_propia && v < bv))) { bv = v; best = &parts[i]; }
            }
            if (best) {
                memcpy(b->rom, best->data, BIOS_SIZE);
                sha1_hex(best->data, BIOS_SIZE, b->sha1);
                b->ver = bv;
                snprintf(b->path, sizeof b->path, "%s:%s", path, best->name);
                res = 0;
                /* Si trae la tabla L0, se compara con la calculada */
                for (int i = 0; i < n && l0; i++)
                    if (!strcmp(parts[i].name, "000-lo.lo"))
                        b->l0_checked = parts[i].size == L0_SIZE && !memcmp(parts[i].data, l0, L0_SIZE) ? 1 : -1;
            } else {
                snprintf(bios_err, sizeof bios_err, "%s: no tiene ninguna BIOS del AES aceptada (neo-epo.bin o neo-po.bin)", path);
                *err = bios_err;
            }
            parts_free(parts, n);
        }
    } else if (len != BIOS_SIZE) {
        snprintf(bios_err, sizeof bios_err, "%s: la BIOS del AES mide 128 KB (este archivo, %zu bytes)", path, len);
        *err = bios_err;
    } else {
        char s[41];
        sha1_hex(data, len, s);
        const BiosVersion *v = accepted(s);
        if (!v && is_propia(data, len)) v = &bios_propia;
        if (v) {
            memcpy(b->rom, data, BIOS_SIZE);
            snprintf(b->sha1, sizeof b->sha1, "%s", s);
            b->ver = v;
            snprintf(b->path, sizeof b->path, "%s", path);
            res = 0;
        } else *err = not_accepted(path, s);
    }
    free(data);
    return res;
}

int bios_find(Bios *b, const char *rom_path, const u8 *l0, const char **err)
{
    const char *env = getenv("NEO_BIOS");
    if (env && *env) return bios_load(b, env, l0, err);
    /* La carpeta del juego y roms/ */
    char dir[512] = ".";
    if (rom_path) {
        const char *slash = NULL;
        for (const char *p = rom_path; *p; p++) if (*p == '/' || *p == '\\') slash = p;
        if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - rom_path), rom_path);
    }
    /* La propia primero (neobios.bin junto al juego, en roms/, o build/neobios.bin, donde la deja make
     * neobios); las de SNK si no está */
    static const char *const names[] = { "neobios.bin", "aes.zip", "neo-epo.bin", "neo-po.bin" };
    const char *dirs[3] = { dir, "roms", "build" };
    const char *last = NULL;
    for (int i = 0; i < 4; i++)
        for (int d = 0; d < 3; d++) {
            char path[640];
            if (d == 2 && i > 0) continue;   /* en build/ solo la propia */
            snprintf(path, sizeof path, "%s/%s", dirs[d], names[i]);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
            if (!bios_load(b, path, l0, err)) return 0;
            last = *err;
        }
    *err = last ? last : "no encontré la BIOS del AES (la propia, neobios.bin, o la de SNK: aes.zip, neo-epo.bin o "
                         "neo-po.bin) junto al juego ni en roms/ (la propia también en build/: make neobios); "
                         "también se puede dar con --bios o la variable NEO_BIOS";
    return -1;
}
