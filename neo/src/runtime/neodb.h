/*
 * Base de datos de juegos para los zips al estilo MAME: qué es cada chip (por su CRC32, no por su
 * nombre) y cómo se carga. Un zip que no está aquí se arma por los sufijos de los archivos.
 */
#ifndef NEODB_H
#define NEODB_H

#include "neo.h"

/* Papel de un chip: la región y su posición dentro de ella (P1 = PART_P, 1; las C van por número) */
enum { PART_NONE, PART_P, PART_S, PART_M, PART_VA, PART_VB, PART_C };

enum {
    DB_P1_LINEAR = 1,               /* el P1 de 2 MB viene ya en orden lineal (sin las mitades cambiadas) */
    DB_MVS_ONLY  = 2,               /* salió solo en MVS (se prueba con la BIOS del AES de todos modos) */
};

typedef struct { u32 crc, size; u8 role, idx; } NeoDbChip;

typedef struct NeoDbGame {
    const char *set;                /* nombre en MAME */
    const char *title, *maker;
    u16 year, ngh;                  /* NGH como en la cabecera ($0201) */
    u8  genre;                      /* el de la cabecera del .neo */
    u8  flags;                      /* DB_* */
    const char *sha1;               /* de la imagen normalizada */
    NeoDbChip chips[24];            /* termina con size = 0 */
} NeoDbGame;

extern const NeoDbGame neodb_games[];
extern const int neodb_count;

/* El juego cuyos chips están todos entre los archivos, o NULL */
const NeoDbGame *neodb_match(const RomPart *parts, int n);
/* El juego con ese SHA1 de la imagen normalizada, o NULL */
const NeoDbGame *neodb_by_sha1(const char *sha1);

#endif
