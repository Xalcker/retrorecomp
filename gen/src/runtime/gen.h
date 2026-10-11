/*
 * GENRecomp: estado completo de la consola y su interfaz.
 * Fase 1: cartucho (formatos, cabecera, vectores) y 68000. El bus, el VDP, el
 * Z80 y el audio se agregan en las fases siguientes (ver docs/PLAN.md).
 */
#ifndef GEN_H
#define GEN_H

#include <stddef.h>
#include <stdint.h>

#include "m68k.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* El 68000 es big-endian: la ROM y la RAM se guardan tal cual y se leen con estas */
static inline u16 rd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static inline u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

#define ROM_LINEAR (4u << 20)       /* $000000-$3FFFFF: lo que se ve sin mapper */
#define ROM_LIMIT  (16u << 20)      /* más grande, solo con el mapper de SSF2 (bancos de 512 KB) */

enum { ROMFMT_BIN, ROMFMT_SMD };

/* Vectores de excepción del 68000 (índices en la tabla de $000000) */
enum {
    VEC_SSP = 0, VEC_RESET = 1, VEC_BUS = 2, VEC_ADDRESS = 3, VEC_ILLEGAL = 4, VEC_ZERODIV = 5,
    VEC_CHK = 6, VEC_TRAPV = 7, VEC_PRIV = 8, VEC_TRACE = 9, VEC_LINEA = 10, VEC_LINEF = 11,
    VEC_SPURIOUS = 24, VEC_AUTO1 = 25,  /* nivel n: VEC_AUTO1 + n - 1; en la Mega Drive: 2 = EXT, 4 = HINT, 6 = VINT */
    VEC_TRAP0 = 32, VEC_COUNT = 64
};

/* Cabecera de $100-$1FF (los textos sin los espacios del final) */
typedef struct {
    char system[17];            /* $100: "SEGA MEGA DRIVE", "SEGA GENESIS"... */
    char copyright[17];         /* $110 */
    char name_dom[49];          /* $120: nombre en Japón */
    char name_int[49];          /* $150: nombre en el resto del mundo */
    char serial[15];            /* $180: "GM XXXXXXXX-XX" */
    u16  checksum;              /* $18E: la que declara la cabecera */
    u16  checksum_calc;         /* suma de las palabras de $200 al final de la ROM */
    char io[17];                /* $190: periféricos ("J" = mando de 3 botones, "6" = de 6...) */
    u32  rom_start, rom_end;    /* $1A0 */
    u32  ram_start, ram_end;    /* $1A8 */
    char region[4];             /* $1F0: "JUE", o el formato nuevo de un dígito hex */
    int  sega;                  /* 1 si $100 empieza con "SEGA" (o " SEGA") */
} GenHeader;

typedef struct {
    u8    *rom;  size_t rom_size;       /* imagen normalizada (big-endian, sin cabecera de SMD) */
    int    format;                      /* ROMFMT_* del archivo original */
    char   sha1[41];                    /* SHA1 de la imagen normalizada */
    GenHeader hdr;
    /* RAM o EEPROM del cartucho, según la cabecera ("RA" en $1B0) */
    int    sram;                        /* 1 si la cabecera la declara */
    u8     sram_type;                   /* $1B2: bit 6 = con batería; bits 4-3: 10 = bytes pares, 11 = impares, 00 = palabras */
    u32    sram_start, sram_end;
} Cart;

typedef struct Gen {
    M68k cpu;
    u64  cyc;                   /* ciclos del 68000 desde el reset */
    void *jb;                   /* jmp_buf del paso actual (el error de dirección vuelve ahí) */
    Cart cart;
    u8   ram[0x10000];          /* RAM del 68000: $FF0000-$FFFFFF (y sus espejos desde $E00000) */
    u8   zram[0x2000];          /* RAM del Z80 */
} Gen;

/* Carga una ROM (.bin, .md, .gen o .smd). 0 si todo bien; mensaje en *err si falla. */
int  gen_load(Gen *g, const u8 *data, size_t len, const char **err);
void gen_free(Gen *g);

/* Normaliza el archivo a la imagen que ve el 68000 (deshace el entrelazado de SMD, rellena a
 * tamaño par). malloc; NULL y *err si no se reconoce. */
u8  *cart_normalize(const u8 *data, size_t len, size_t *out_len, int *format, const char **err);
void cart_parse_header(Cart *c);
u16  cart_checksum(const u8 *rom, size_t len);
u32  cart_vector(const Cart *c, int n);         /* vector n de la tabla de $000000 */
const char *cart_vector_name(int n);            /* "reset", "VINT"...; NULL si no tiene nombre propio */

#endif
