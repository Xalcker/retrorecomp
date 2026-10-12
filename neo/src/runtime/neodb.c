/*
 * Base de datos de juegos (neodb.h). Se agregan los juegos que se pueden comprobar con un archivo
 * real: los CRC32 y tamaños de los chips son los de los sets de MAME, y el SHA1 es el de la imagen
 * normalizada que arma cart.c.
 */
#include "neodb.h"

#include <string.h>

const NeoDbGame neodb_games[] = {
    { "mslug", "Metal Slug - Super Vehicle-001", "Nazca", 1996, 0x0201, 1, 0, "582233c4d95e4a2d80dd78a9de29b92e0f60d40f", {
        { 0x08D8DAA5, 0x200000, PART_P, 1 },        /* 201-p1.p1: 2 MB, mitades cambiadas */
        { 0x2F55958D, 0x020000, PART_S, 1 },        /* 201-s1.s1 */
        { 0xC28B3253, 0x020000, PART_M, 1 },        /* 201-m1.m1 */
        { 0x23D22ED1, 0x400000, PART_VA, 1 },       /* 201-v1.v1 */
        { 0x472CF9DB, 0x400000, PART_VA, 2 },       /* 201-v2.v2 */
        { 0x72813676, 0x400000, PART_C, 1 },        /* 201-c1.c1 */
        { 0x96F62574, 0x400000, PART_C, 2 },        /* 201-c2.c2 */
        { 0x5121456A, 0x400000, PART_C, 3 },        /* 201-c3.c3 */
        { 0xF4AD59A3, 0x400000, PART_C, 4 },        /* 201-c4.c4 */
        { 0, 0, 0, 0 } } },
};
const int neodb_count = (int)(sizeof neodb_games / sizeof neodb_games[0]);

const NeoDbGame *neodb_match(const RomPart *parts, int n)
{
    for (int g = 0; g < neodb_count; g++) {
        const NeoDbGame *d = &neodb_games[g];
        int ok = 1;
        for (const NeoDbChip *c = d->chips; c->size && ok; c++) {
            int found = 0;
            for (int i = 0; i < n && !found; i++) found = parts[i].crc == c->crc && parts[i].size == c->size;
            ok = found;
        }
        if (ok) return d;
    }
    return NULL;
}

const NeoDbGame *neodb_by_sha1(const char *sha1)
{
    for (int g = 0; g < neodb_count; g++)
        if (neodb_games[g].sha1 && !strcmp(neodb_games[g].sha1, sha1)) return &neodb_games[g];
    return NULL;
}
