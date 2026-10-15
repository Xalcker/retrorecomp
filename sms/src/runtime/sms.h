/*
 * SMSRecomp: estado de la consola y su interfaz (Sega Master System II y Game Gear de EE. UU., NTSC).
 * Cartucho (formatos, cabeceras TMR SEGA, Codemasters y SDSC, mappers, RAM del cartucho), Z80 y PSG (comunes,
 * tools/retrorecomp/shared), mapa de memoria y puertos, mandos, el VDP (registros, VRAM, CRAM, contadores,
 * interrupciones y el dibujo) y la mezcla del sonido.
 */
#ifndef SMS_H
#define SMS_H

#include <stddef.h>
#include <stdint.h>

#include "z80.h"
#include "psg.h"
#include "audio.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* El Z80 es little-endian */
static inline u16 rd16le(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

#define SMS_PAGE_SIZE  0x4000u      /* páginas de 16 KB: lo que eligen los mappers */
#define ROM_LIMIT      (4u << 20)   /* 4 MB: más de lo que direcciona el mapper Sega (256 páginas) */

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

/* Botones de un mando (1 = apretado), en el orden de INPUT_NAMES (input_host.h, fase 4). A es el botón 1
 * y B el botón 2; START es el Start del Game Gear y, en el Master System, aprieta el botón 1; PAUSE es el
 * botón de la consola (la NMI). En el Game Gear en su modo SMS, Start es la Pausa */
enum {
    SMS_UP = 1, SMS_DOWN = 2, SMS_LEFT = 4, SMS_RIGHT = 8, SMS_A = 16, SMS_B = 32, SMS_START = 64, SMS_PAUSE = 128
};

typedef struct {
    u8    *rom; size_t rom_size;        /* imagen normalizada (sin la cabecera de copiador) */
    u8    *image;                       /* la imagen en páginas enteras de 16 KB (una ROM chica, repetida): lo que leen las ranuras */
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
    u8    *ram;                         /* RAM del cartucho: 32 KB (dos bancos de 16 KB del mapper Sega; Codemasters usa 8 KB) */
    u8    *ram_saved;                   /* lo último guardado en disco (para ver si cambió) */
    u8     ram_used;                    /* el juego la mapeó alguna vez: tiene partida que guardar */
    u8    *trace;                       /* NULL, o por cada byte de la ROM: 0 no se ejecutó; si no, 1 + la ranura (0-2) con que se ejecutó */
    void  *ram_code;                    /* con la traza: el código que corrió desde la RAM ($8000-$FFFF), un RcTable de ram_code.h */
} Cart;

#define CART_RAM_SIZE 0x8000

/* Mapeo del cartucho (bus.c): qué hay en cada ranura de 16 KB */
typedef struct {
    u8   reg[4];                        /* Sega: lo último escrito en $FFFC-$FFFF; Codemasters: en $0000, $4000, $8000 (y nada) */
    u32  slot[3];                       /* desplazamiento en la imagen de cada ranura ($0000, $4000, $8000) */
    u8   ram_slot2;                     /* la RAM del cartucho en $8000-$BFFF (Sega, bit 3 de $FFFC) */
    u8   ram_high;                      /* Sega, bit 4 de $FFFC: la RAM del cartucho en $C000-$FFFF */
    u8   ram_a000;                      /* Codemasters, bit 7 de $4000: 8 KB de RAM en $A000-$BFFF */
    u32  map_ver;                       /* sube con cada cambio de mapeo (el código generado lo vigilará) */
} Mapper;

/* VDP (vdp.c, vdp_render.c): registros, memorias, contadores, interrupciones y la imagen */
#define FB_W 256
#define FB_H 240
typedef struct {
    u8   reg[16];                       /* 0-10; 11-15 no tienen efecto */
    u16  addr;                          /* registro de dirección (14 bits) */
    u8   code;                          /* registro de código: 0 leer VRAM, 1 escribir VRAM, 2 registro, 3 CRAM */
    u8   pending;                       /* se escribió el primer byte de un comando */
    u8   first;                         /* ese primer byte */
    u8   buffer;                        /* búfer de lectura del puerto de datos */
    u8   status;                        /* bits 7-5: INT (cuadro), OVR (desborde de sprites), COL (colisión) */
    u8   vint;                          /* interrupción de cuadro pendiente (su línea INT sube unos ciclos después que la bandera) */
    u8   line_irq;                      /* interrupción de línea pendiente */

    u8   line_counter;                  /* contador de la interrupción de línea */
    u8   cram_latch;                    /* Game Gear: el byte par de la CRAM, que se escribe con el impar */
    u8   hcount;                        /* contador H latcheado (cuando cambia TH) */
    u8   frame_done;                    /* terminó un cuadro (lo borra quien corre el cuadro) */
    u16  height;                        /* líneas activas del cuadro: 192, 224 o 240 (en el GG en modo GG, siempre 192) */
    u8   vscroll;                       /* el registro 9 al empezar el cuadro (no cambia durante la imagen) */
    int  line;                          /* línea actual (0-261) */
    u64  line_start;                    /* ciclo del Z80 en que empezó la línea actual (cuando cambió el contador V) */
    u64  next_event;                    /* el próximo ciclo en que el VDP tiene algo que hacer */
    u64  col_at;                        /* ciclo en que se verá la colisión de sprites de esta línea (0: ninguna) */
    u64  vint_at, lint_at;              /* ciclos en que subirán la interrupción de cuadro y la de línea (0: ninguna) */
    u64  frame;                         /* cuadros terminados */
    u8   vram[0x4000];
    u8   cram[64];                      /* SMS: 32 bytes --BBGGRR; Game Gear: 32 palabras ----BBBBGGGGRRRR (little-endian) */
    u16  fb[FB_W * FB_H];               /* la imagen: el color de cada pixel en 12 bits (----BBBBGGGGRRRR) */
} Vdp;

/* Puertos de E/S que no son del VDP (io.c) */
typedef struct {
    u8   mem_ctrl;                      /* $3E: control de memoria */
    u8   io_ctrl;                       /* $3F: control de E/S (TH y TR de los dos puertos) */
    u8   gg[7];                         /* Game Gear, $00-$06: Start y región, el puerto de enlace y el estéreo */
    u8   pause_prev;                    /* la Pausa (o Start del GG en modo SMS) en el cuadro anterior: la NMI va por flanco */
} Io;

typedef struct Sms {
    Z80  z80;
    u64  cyc;                           /* ciclos del Z80 desde el reset */
    u64  io_cyc;                        /* ciclo del acceso al puerto de la instrucción en curso (Z80_IO_AT) */
    Cart cart;
    Mapper map;
    Vdp  vdp;
    Io   io;
    Psg  psg;                           /* el SN76489 (común con GENRecomp: tools/retrorecomp/shared/psg.c) */
    Audio audio;                        /* la mezcla a 44 100 Hz (audio.c, común). Bits de mute: 0-3 los canales del PSG */
    int  model;                         /* MODEL_SMS o MODEL_GG */
    int  sms_mode;                      /* el juego corre en el modo SMS (siempre en el SMS; en el GG, un cartucho de SMS) */
    u8   pad[2];                        /* botones apretados (SMS_*) de los mandos 1 y 2 */
    u32  nmi_count, irq_count;          /* NMI e interrupciones atendidas (estadística) */
    void (*sdsc_out)(struct Sms *g, u8 v);   /* si no es NULL, recibe lo que el juego escribe en la consola de depuración SDSC ($FD) */
    u8   ram[0x2000];                   /* RAM de trabajo: $C000-$DFFF (y su espejo en $E000) */
} Sms;

/* Carga una ROM (.sms, .gg o con cabecera de copiador) y hace el reset. name: el nombre del archivo (la
 * extensión ayuda a elegir el modelo); model: MODEL_AUTO o el que se pide. 0 si todo bien; *err si falla. */
int  sms_load(Sms *s, const u8 *data, size_t len, const char *name, int model, const char **err);
void sms_free(Sms *s);
void sms_reset(Sms *g);                 /* encendido, con el estado que deja la BIOS (sms.c) */
void sms_run_frame(Sms *g);             /* corre hasta terminar el cuadro */
void sms_frame_begin(Sms *g);           /* lo de sms_run_frame antes del primer paso (la NMI de la Pausa) */
void sms_step(Sms *g);                  /* una instrucción (o una interrupción) y lo que sigue a ella */
int  sms_sram_load(Sms *g, const char *path);   /* la RAM del cartucho: 1 si se cargó */
int  sms_sram_save(Sms *g, const char *path);   /* 1 si se guardó (solo si el juego la usa) */
int  sms_sram_dirty(const Sms *g);

/* Estados guardados: la consola entera. Solo los lee la misma versión con la misma ROM. */
u8  *sms_state_save(const Sms *g, size_t *size);   /* malloc; NULL si falla */
int  sms_state_load(Sms *g, const u8 *data, size_t size);  /* 1 si se cargó */

/* Traza: qué instrucciones ejecutó el intérprete (smstrace, sms --trace, difftest --uncovered; la lee
 * smsrecomp --trace): las de la ROM en líneas "BANCO:DIRECCION" de trace_line.h con bancos de 16 KB (las páginas
 * del mapper), y las que corrieron desde la RAM ($8000-$FFFF: la de trabajo, su espejo y la del cartucho) en
 * líneas "ram DIRECCION B0 B1 ..." de ram_code.h, con sus bytes */
#define SMS_TRACE_SHIFT 14
void   sms_trace_start(Sms *g);
int    sms_trace_load(Sms *g, const char *path);    /* suma lo de una traza anterior; 1 si se leyó */
int    sms_trace_write(const Sms *g, const char *path);
size_t sms_trace_count(const Sms *g);               /* instrucciones de la ROM */
void   sms_trace_free(Sms *g);
void   sms_ram_summary(const Sms *g, unsigned *fixed, unsigned *vol, unsigned *multi, unsigned *lost);
/* Cuántos bytes identifican la instrucción de b (la clave de ram_code.h): los prefijos y el opcode (DD CB d op
 * entero), y en JR y DJNZ también el desplazamiento */
unsigned sms_rc_key(const u8 *b);

/* El código recompilado (con -DSMS_RECOMP, en el proyecto que genera smsrecomp; ver recomp.h): una función
 * generada si la hay para el PC y el mapeo de ahora, o una instrucción del intérprete. 1 si corrió código nativo.
 * sms_force_interp (--interp de la ventana, --bench de difftest) lo apaga */
int    sms_recomp_step(Sms *g);
extern int sms_force_interp;

/* Sonido (psg.c y audio.c, comunes). Los tiempos son ciclos maestros */
void audio_reset(Sms *g);
void audio_add(Sms *g, u64 t0, u64 t1, float l, float r);  /* un tramo constante de un chip */
void audio_end_frame(Sms *g);                   /* lleva el PSG al final del cuadro y suelta las muestras */
int  sms_audio_take(Sms *g, int16_t *out, int max_frames);  /* muestras L R; devuelve cuántos pares */

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

/* Bus (bus.c): el mapa de memoria del Z80, los mappers y el reparto de los puertos */
void mapper_reset(Sms *g);
u8   bus_peek(const Sms *g, u16 addr);          /* lo que leería el Z80, sin efectos */
long bus_rom_offset(const Sms *g, u16 addr);    /* dónde cae addr en la ROM, o -1 si ahí no hay ROM */

/* E/S (io.c): $3E, $3F, los mandos ($DC, $DD) y los puertos del Game Gear ($00-$06) */
void io_reset(Sms *g);
u8   io_read(Sms *g, u8 port);                  /* $C0-$FF y $00-$06 */
void io_write(Sms *g, u8 port, u8 v);           /* $00-$3F */

/* VDP (vdp.c) */
void vdp_reset(Sms *g);
u8   vdp_read(Sms *g, u8 port);                 /* $40-$BF: contadores, datos y estado */
void vdp_write(Sms *g, u8 port, u8 v);          /* $80-$BF */
void vdp_sync(Sms *g, u64 cyc);                 /* líneas, contadores e interrupciones hasta el ciclo cyc */
void vdp_latch_h(Sms *g, u64 cyc);              /* TH cambió: congela el contador H */
u8   vdp_vcount(const Vdp *v);                  /* el contador V de la línea actual */
void vdp_render_line(Sms *g, int y);            /* dibuja la línea y en fb (vdp_render.c) */
void vdp_border_sprites(Sms *g, int vcount);    /* en una línea fuera de la imagen, solo la colisión de sprites */
u32  vdp_pixel_cycle(int x);                    /* ciclo de la línea en que se dibuja el pixel x */
int  vdp_extended(const Vdp *v);                /* la tabla de nombres de 32×32 (los modos de 224 y 240 líneas) */
uint32_t vdp_argb(u16 c);                       /* un pixel de fb en ARGB 8888 */
void vdp_view(const Sms *g, int *x0, int *y0, int *w, int *h);  /* lo que se ve: 256×192 (o 224) o la ventana de 160×144 del GG */

/* Después de cada instrucción (el intérprete y el código generado): el VDP solo si empezó una línea */
#if defined(__GNUC__) || defined(__clang__)
#define SMS_INL static inline __attribute__((always_inline))
#else
#define SMS_INL static inline
#endif

SMS_INL void sms_after_step(Sms *g)
{
    if (g->cyc >= g->vdp.next_event) vdp_sync(g, g->cyc);
}

#endif
