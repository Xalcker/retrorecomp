/*
 * NEORecomp: estado completo de la consola y su interfaz.
 * Por ahora: el cartucho (formatos, imagen normalizada, cabecera, vectores), la BIOS (validación por
 * SHA1, tabla L0), el 68000 y el Z80. Crece con cada fase de docs/PLAN.md.
 */
#ifndef NEO_H
#define NEO_H

#include <stddef.h>
#include <stdint.h>

#include "m68k.h"
#include "z80.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/*
 * Imagen normalizada: las regiones en el orden del .neo, con la forma que tienen ahí.
 *   P     programa del 68000, en el orden de bytes de los chips (palabras con los bytes invertidos:
 *         el byte de la dirección a del 68000 está en a ^ 1) y en orden lineal: el primer MB es
 *         $000000-$0FFFFF (las mitades de un P1 de 2 MB ya en su lugar) y el MB n + 1 es el banco n
 *         de la ventana $200000-$2FFFFF
 *   S     capa fija (8x8)
 *   M     programa del Z80 (M1)
 *   VA    muestras del bus A del YM2610 (ADPCM-A; también las de ADPCM-B si el juego usa un solo bus)
 *   VB    muestras del bus B (ADPCM-B), solo en los juegos viejos con los dos buses separados
 *   C     sprites: cada par de chips (C1/C2, C3/C4...) intercalado byte a byte, el impar en los pares
 * El SHA1 de la imagen completa es la identidad del juego, igual en cualquier formato: coincide con
 * el de la parte de datos (después de la cabecera de 4096 bytes) de un .neo.
 */
enum { REG_P, REG_S, REG_M, REG_VA, REG_VB, REG_C, REG_COUNT };
extern const char *const neo_region_name[REG_COUNT];

enum { ROMFMT_NEO, ROMFMT_ZIP, ROMFMT_DIR };

#define P_BANK_SIZE   0x100000u     /* ventanas de 1 MB: $000000 y los bancos de $200000 */
#define TRACE_SHIFT   20            /* trace_line.h: banco = desplazamiento >> 20 */
#define BIOS_SIZE     0x20000u      /* 128 KB en $C00000 (con espejos hasta $CFFFFF) */
#define BIOS_SPACE    0xFF00000u    /* desplazamiento de la BIOS en las trazas (banco FF) */
#define L0_SIZE       0x20000u      /* tabla de reducción vertical (000-lo.lo; solo se usa la primera mitad) */

/* Cabecera del programa en $100 (la lee la BIOS) */
typedef struct {
    int neo;                        /* "NEO-GEO" en $100 */
    u8  sysver;                     /* $107: 0 en los cartuchos */
    u16 ngh;                        /* $108: número NGH en BCD ($0201) */
    u32 p_size;                     /* $10A: tamaño del programa que declara */
    u32 backup_ptr;                 /* $10E: bloque de la RAM que la BIOS guarda (RAM de respaldo, MVS) */
    u16 backup_size;                /* $112 */
    u8  eyecatch;                   /* $114: pantalla de SNK: 0 la hace la BIOS, 1 el juego, 2 ninguna */
    u8  eyecatch_bank;              /* $115: banco de sprites del logo */
    u32 dip[3];                     /* $116/$11A/$11E: DIP por software de Japón, EE. UU. y Europa */
    u32 entry[4];                   /* destinos de los JMP de $122 (USER), $128 (PLAYER_START), $12E
                                       (DEMO_END) y $134 (COIN_SOUND); 0 si no hay JMP absoluto */
    u32 security;                   /* $182: puntero al código de seguridad */
} NeoHeader;

enum {
    VEC_SSP = 0, VEC_RESET = 1, VEC_BUS = 2, VEC_ADDRESS = 3, VEC_ILLEGAL = 4, VEC_ZERODIV = 5,
    VEC_CHK = 6, VEC_TRAPV = 7, VEC_PRIV = 8, VEC_TRACE = 9, VEC_LINEA = 10, VEC_LINEF = 11,
    VEC_SPURIOUS = 24, VEC_AUTO1 = 25,  /* nivel n: VEC_AUTO1 + n - 1; 1 = VBLANK, 2 = temporizador, 3 = arranque */
    VEC_TRAP0 = 32, VEC_COUNT = 64
};
enum { ENTRY_USER, ENTRY_PLAYER_START, ENTRY_DEMO_END, ENTRY_COIN_SOUND, ENTRY_COUNT };
extern const char *const neo_entry_name[ENTRY_COUNT];

struct NeoDbGame;

typedef struct {
    u8    *data;                    /* imagen normalizada (malloc) */
    size_t size;
    size_t off[REG_COUNT], len[REG_COUNT];
    char   sha1[41];                /* de la imagen completa */
    char   reg_sha1[REG_COUNT][41];
    int    format;                  /* ROMFMT_* */
    char   files[REG_COUNT][160];   /* de qué archivos salió cada región (para --info) */
    int    p1_swapped;              /* el P1 de 2 MB traía las mitades cambiadas */
    /* De la cabecera del .neo o de la base de datos (vacíos si no se sabe) */
    char   title[64], maker[32], set[16];
    u32    year, genre;
    const struct NeoDbGame *db;
    NeoHeader hdr;
    u32    vec[VEC_COUNT];          /* tabla de vectores del juego ($000000-$0000FF) */
    int    p_banks;                 /* bancos de 1 MB en la ventana $200000: 0 sin P2, 1 fijo, más con latch */
} Cart;

/* Versiones aceptadas de la BIOS */
typedef struct {
    const char *sha1;
    const char *file;               /* nombre en MAME */
    const char *desc;
} BiosVersion;
extern const BiosVersion bios_versions[];
extern const int bios_version_count;

typedef struct {
    u8   rom[BIOS_SIZE];            /* en el orden de bytes del chip, como P */
    char sha1[41];
    const BiosVersion *ver;         /* NULL: no se cargó */
    char path[512];                 /* archivo (y miembro del zip) de donde salió */
    int  l0_checked;                /* había un 000-lo.lo junto a la BIOS: 1 igual a la calculada, -1 distinta */
} Bios;

typedef struct Neo {
    M68k cpu;
    u64  cyc;                       /* ciclos del 68000 desde el reset */
    void *jb;                       /* jmp_buf del paso actual (el error de dirección vuelve ahí) */
    Cart cart;
    Bios bios;
    u8   p2_bank;                   /* banco de P2 que se ve en $200000 */
    Z80  z80;
    u32  exc_count[64];             /* excepciones atendidas por vector (estadística) */
    u32 *trace;                     /* NULL, o por cada palabra de P y de la BIOS (neo_trace_index): 0 = no
                                       ejecutada; si no, $80000000 | dirección */
    u8   l0[L0_SIZE];
} Neo;

#if defined(__GNUC__) || defined(__clang__)
#define NEO_INL static inline __attribute__((always_inline))
#else
#define NEO_INL static inline
#endif

/* Ciclos del 68000 sin acceso al bus */
NEO_INL void m68k_idle(Neo *g, uint32_t n) { g->cyc += n; }

/*
 * Identidad del código: el desplazamiento en la imagen P de lo que se ve en pc ($000000-$0FFFFF,
 * con espejos si P1 es más chico; el banco actual de $200000-$2FFFFF), o BIOS_SPACE más el
 * desplazamiento en la BIOS ($C00000-$CFFFFF). -1 si ahí no hay ROM (RAM, registros).
 */
long neo_code_offset(const Neo *g, u32 pc);
/* Posición en g->trace de un desplazamiento de neo_code_offset: las palabras de P y después las de la BIOS */
NEO_INL size_t neo_trace_index(const Neo *g, long off)
{
    return (size_t)(off < (long)BIOS_SPACE ? off : (long)g->cart.len[REG_P] + (off - (long)BIOS_SPACE)) >> 1;
}
NEO_INL size_t neo_trace_words(const Neo *g) { return (g->cart.len[REG_P] + BIOS_SIZE) >> 1; }
NEO_INL void neo_trace_mark(Neo *g, u32 pc)
{
    long off = neo_code_offset(g, pc);
    if (off < 0) return;
    size_t i = neo_trace_index(g, off);
    if (!g->trace[i]) g->trace[i] = 0x80000000u | pc;
}

/* Lectura de 16 bits del programa o de la BIOS (palabras con los bytes invertidos) */
static inline u16 rdw_le(const u8 *p) { return (u16)(p[1] << 8 | p[0]); }

/* Una palabra o un largo del 68000 en el desplazamiento off de P (par) */
static inline u16 cart_p16(const Cart *c, u32 off) { return rdw_le(c->data + c->off[REG_P] + off); }
static inline u32 cart_p32(const Cart *c, u32 off) { return (u32)cart_p16(c, off) << 16 | cart_p16(c, off + 2); }

/* --- Carga (cart.c) --- */

/* Lectura de archivos: por omisión fopen; la plataforma pone la que entiende rutas UTF-8 en Windows */
extern unsigned char *(*neo_read_file)(const char *path, size_t *size);

/* Un archivo de un zip o de una carpeta */
typedef struct {
    char   name[128];               /* sin la carpeta */
    u8    *data;
    size_t size;
    u32    crc;
} RomPart;

/* El juego desde un archivo .neo o .zip o una carpeta con los chips. 0 si todo bien; si no, *err */
int  cart_load_path(Cart *c, const char *path, const char **err);
/* Desde un .neo en memoria */
int  cart_load_neo(Cart *c, const u8 *data, size_t len, const char **err);
/* Desde los chips sueltos (de un zip o una carpeta): la base de datos o los sufijos dicen qué es cada uno */
int  cart_load_parts(Cart *c, RomPart *parts, int n, int format, const char **err);
void cart_free(Cart *c);
/* Escribe el juego como .neo (cabecera de 4096 bytes y la imagen normalizada). 1 si todo bien */
int  cart_write_neo(const Cart *c, const char *path);
/* Mensaje si el juego necesita algo que todavía no está (cifrado, protección, otro mando), o NULL.
 * *fatal = 1 si no se puede correr */
const char *cart_unsupported(const Cart *c, int *fatal);
const char *cart_vector_name(int n);

/* --- BIOS (bios.c) --- */

/* Carga la BIOS desde un archivo o un zip (aes.zip de MAME): la primera versión aceptada que haya */
int  bios_load(Bios *b, const char *path, const u8 *l0, const char **err);
/* Busca la BIOS: $NEO_BIOS, la carpeta del juego (aes.zip, neo-epo.bin, neo-po.bin), roms/ */
int  bios_find(Bios *b, const char *rom_path, const u8 *l0, const char **err);
/* La tabla L0 (reducción vertical de los sprites), calculada */
void l0_generate(u8 out[L0_SIZE]);
#define L0_SHA1 "5992277debadeb64d1c1c64b0a92d9293eaf7e4a"

/* --- Zip (zip.c) --- */

u32  crc32_buf(u32 crc, const u8 *p, size_t n);
/* Los archivos de un zip (stored o deflate), con su CRC comprobado. Devuelve cuántos o -1 (*err) */
int  zip_read_all(const u8 *zip, size_t len, RomPart **parts, const char **err);
void parts_free(RomPart *parts, int n);
/* Descomprime un flujo deflate (RFC 1951) en out, que mide exactamente out_len. 0 si todo bien */
int  inflate_raw(const u8 *in, size_t in_len, u8 *out, size_t out_len);

#endif
