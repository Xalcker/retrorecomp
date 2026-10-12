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
    { "samsho2", "Samurai Shodown II / Shin Samurai Spirits", "SNK", 1994, 0x0063, 9, 0, "83af9153f655527de0f24c9bd1fa1543cb4d68c3", {
        { 0x22368892, 0x200000, PART_P, 1 },        /* 063-p1.p1, 2 MB, mitades cambiadas */
        { 0x64A5CD66, 0x020000, PART_S, 1 },        /* 063-s1.s1 */
        { 0x56675098, 0x020000, PART_M, 1 },        /* 063-m1.m1 */
        { 0x37703F91, 0x200000, PART_VA, 1 },       /* 063-v1.v1 */
        { 0x0142BDE8, 0x200000, PART_VA, 2 },       /* 063-v2.v2 */
        { 0xD07FA5CA, 0x200000, PART_VA, 3 },       /* 063-v3.v3 */
        { 0x24AAB4BB, 0x100000, PART_VA, 4 },       /* 063-v4.v4 */
        { 0x86CD307C, 0x200000, PART_C, 1 },        /* 063-c1.c1 */
        { 0xCDFCC4CA, 0x200000, PART_C, 2 },        /* 063-c2.c2 */
        { 0x7A63CCC7, 0x200000, PART_C, 3 },        /* 063-c3.c3 */
        { 0x751025CE, 0x200000, PART_C, 4 },        /* 063-c4.c4 */
        { 0x20D3A475, 0x200000, PART_C, 5 },        /* 063-c5.c5 */
        { 0xAE4C0A88, 0x200000, PART_C, 6 },        /* 063-c6.c6 */
        { 0x2DF3CBCF, 0x200000, PART_C, 7 },        /* 063-c7.c7 */
        { 0x1FFC6DFA, 0x200000, PART_C, 8 },        /* 063-c8.c8 */
        { 0, 0, 0, 0 } } },
    { "turfmast", "Neo Turf Masters / Big Tournament Golf", "Nazca", 1996, 0x0200, 3, 0, "6383d852c17c0eb770cd51c4fa8cb9b29874bbb2", {
        { 0x28C83048, 0x200000, PART_P, 1 },        /* 200-p1.p1, 2 MB, mitades cambiadas */
        { 0x9A5402B2, 0x020000, PART_S, 1 },        /* 200-s1.s1 */
        { 0x9994AC00, 0x020000, PART_M, 1 },        /* 200-m1.m1 */
        { 0x00FD48D2, 0x200000, PART_VA, 1 },       /* 200-v1.v1 */
        { 0x082ACB31, 0x200000, PART_VA, 2 },       /* 200-v2.v2 */
        { 0x7ABCA053, 0x200000, PART_VA, 3 },       /* 200-v3.v3 */
        { 0x6C7B4902, 0x200000, PART_VA, 4 },       /* 200-v4.v4 */
        { 0x8E7BF41A, 0x400000, PART_C, 1 },        /* 200-c1.c1 */
        { 0x5A65A8CE, 0x400000, PART_C, 2 },        /* 200-c2.c2 */
        { 0, 0, 0, 0 } } },
    { "shocktro", "Shock Troopers", "Saurus", 1997, 0x0238, 1, DB_MVS_ONLY, "c06c42f8999fa5c284aa7e728574f15434e2a09a", {
        { 0xEFEDF8DC, 0x100000, PART_P, 1 },        /* 238-pg1.p1 */
        { 0x5B4A09C5, 0x400000, PART_P, 2 },        /* 238-p2.sp2: 4 bancos de 1 MB en $200000 */
        { 0x1F95CEDB, 0x020000, PART_S, 1 },        /* 238-s1.s1 */
        { 0x075B9518, 0x020000, PART_M, 1 },        /* 238-m1.m1 */
        { 0x260C0BEF, 0x400000, PART_VA, 1 },       /* 238-v1.v1 */
        { 0x4AD7D59E, 0x200000, PART_VA, 2 },       /* 238-v2.v2 */
        { 0x90C6A181, 0x400000, PART_C, 1 },        /* 238-c1.c1 */
        { 0x888720F0, 0x400000, PART_C, 2 },        /* 238-c2.c2 */
        { 0x2C393AA3, 0x400000, PART_C, 3 },        /* 238-c3.c3 */
        { 0xB9E909EB, 0x400000, PART_C, 4 },        /* 238-c4.c4 */
        { 0xC22C68EB, 0x400000, PART_C, 5 },        /* 238-c5.c5 */
        { 0x119323CD, 0x400000, PART_C, 6 },        /* 238-c6.c6 */
        { 0xA72CE7ED, 0x400000, PART_C, 7 },        /* 238-c7.c7 */
        { 0x1C7C2EFB, 0x400000, PART_C, 8 },        /* 238-c8.c8 */
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
