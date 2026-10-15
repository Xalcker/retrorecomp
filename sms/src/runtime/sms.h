/*
 * SMSRecomp: estado de la consola y su interfaz (Sega Master System y Game Gear de EE. UU., NTSC).
 * Por ahora (fase 0) solo el cartucho: formatos, cabeceras (TMR SEGA, Codemasters, SDSC), mapper y
 * modelo. El Z80, el bus, el VDP y el PSG llegan en las fases 1 a 3 (docs/PLAN.md).
 */
#ifndef SMS_H
#define SMS_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* El Z80 es little-endian */
static inline u16 rd16le(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

#define PAGE_SIZE  0x4000u          /* páginas de 16 KB: lo que eligen los mappers */
#define ROM_LIMIT  (4u << 20)       /* 4 MB: más de lo que direcciona el mapper Sega (256 páginas) */

/* Reloj NTSC: el maestro es de 53.693175 MHz (el mismo del Genesis); el Z80 va a /15 y una línea
 * son 342 pixeles de 10 ciclos maestros (228 del Z80); 262 líneas por cuadro */
#define MCLK_PER_Z80   15
#define MCLK_PER_LINE  3420
#define LINES_NTSC     262

/* Modelos (--model): el hardware que se emula. El modo del juego (SMS o GG) sale del cartucho */
enum { MODEL_AUTO = -1, MODEL_SMS, MODEL_GG, MODEL_COUNT };

/* Mappers del cartucho */
enum { MAPPER_NONE, MAPPER_SEGA, MAPPER_CODEMASTERS, MAPPER_COUNT };

/* Cabecera "TMR SEGA" (en $7FF0, $3FF0 o $1FF0) */
typedef struct {
    int  found;
    u32  at;                    /* dónde está (desplazamiento en la ROM) */
    u16  checksum;              /* la que declara (palabra little-endian en +$A) */
    u16  checksum_calc;         /* suma de los bytes del rango que dice el código de tamaño */
    int  checksum_ok;           /* el código de tamaño tiene rango y la ROM llega hasta su final */
    char product[8];            /* código de producto: las cifras de los dos bytes BCD con la de +$E delante ("27026") */
    u8   version;               /* nibble bajo de +$E */
    u8   region;                /* nibble alto de +$F: 3 SMS Japón, 4 SMS exportación, 5 GG Japón, 6 GG exportación, 7 GG internacional */
    u8   size_code;             /* nibble bajo de +$F */
} SmsHeader;

/* Cabecera de Codemasters en $7FE0 (su propia suma: los juegos con su mapper no pasan la de la BIOS) */
typedef struct {
    int  found;                 /* la suma y su inversa suman $10000 */
    u8   banks;                 /* páginas de 16 KB que cubre la suma */
    u8   day, month, year, hour, minute;    /* BCD */
    u16  checksum, inverse;
    u16  checksum_calc;         /* suma de palabras little-endian de esas páginas, sin $7FF0-$7FFF */
} CmHeader;

/* Cabecera SDSC en $7FE0 (homebrew): versión, fecha y textos */
typedef struct {
    int  found;
    u8   major, minor;          /* BCD */
    u8   day, month;            /* BCD */
    u16  year;                  /* BCD */
    char author[64], name[64];
} SdscHeader;

/* Un juego de la base de datos (smsdb.c): lo que la cabecera no dice */
typedef struct {
    const char *sha1;           /* de la imagen normalizada */
    const char *name;
    int mapper;                 /* MAPPER_*, o -1: el que se detecte */
    int model;                  /* MODEL_AUTO, o el que necesita */
    int sms_mode;               /* en el Game Gear, corre en el modo SMS */
} SmsDbGame;

typedef struct {
    u8    *rom; size_t rom_size;        /* imagen normalizada (sin la cabecera de copiador) */
    int    copier;                      /* el archivo traía 512 bytes de cabecera de copiador */
    char   sha1[41];                    /* SHA1 de la imagen normalizada */
    u32    crc32;
    u32    pages;                       /* páginas de 16 KB (la última puede estar incompleta) */
    SmsHeader hdr;
    CmHeader cm;
    SdscHeader sdsc;
    const SmsDbGame *db;                /* NULL si el juego no está en la base */
    int    mapper;                      /* MAPPER_* */
    int    ext_gg;                      /* la extensión del archivo es .gg */
} Cart;

typedef struct Sms {
    Cart cart;
    int  model;                 /* MODEL_SMS o MODEL_GG */
    int  sms_mode;              /* el juego corre en el modo SMS (siempre en el SMS; en el GG, un cartucho de SMS) */
} Sms;

/* Carga una ROM (.sms, .gg o con cabecera de copiador). name: el nombre del archivo (la extensión
 * ayuda a elegir el modelo); model: MODEL_AUTO o el que se pide. 0 si todo bien; *err si falla. */
int  sms_load(Sms *s, const u8 *data, size_t len, const char *name, int model, const char **err);
void sms_free(Sms *s);

/* Modelos: "sms" y "gg" */
const char *model_name(int model);
int  model_from_name(const char *name);         /* MODEL_*, o -2 si no es uno */
const char *mapper_name(int mapper);

/* Cartucho (cart.c) */
u8  *cart_normalize(const u8 *data, size_t len, size_t *out_len, int *copier, const char **err);
void cart_parse_headers(Cart *c);
int  cart_detect_mapper(const Cart *c);         /* por la base de datos, la cabecera de Codemasters o el tamaño */
u32  cart_crc32(const u8 *p, size_t len);
const char *region_name(int region);
u32  size_code_bytes(int code);                 /* bytes que dice el código de tamaño; 0 si no lo admite la BIOS */

/* Base de datos (smsdb.c) */
const SmsDbGame *smsdb_find(const char *sha1);

#endif
