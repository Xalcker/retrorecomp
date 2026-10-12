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
typedef int8_t   s8;
typedef int16_t  s16;

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

/*
 * Reloj: el maestro es de 24 MHz (24,167829 MHz en el AES); el 68000 va a la mitad, el Z80 a un
 * sexto y el YM2610 a un tercio. Una línea son 384 pixeles de 4 ciclos maestros (768 ciclos del
 * 68000) y un cuadro, 264 líneas. Todo se cuenta en ciclos del 68000 (g->cyc) o maestros.
 */
#define MCLK_PER_M68K    2
#define MCLK_PER_Z80     6
#define M68K_PER_LINE    768
#define LINES_PER_FRAME  264
#define M68K_PER_FRAME   (M68K_PER_LINE * LINES_PER_FRAME)
#define MCLK_PER_FM      432          /* una muestra del FM del YM2610: 144 ciclos suyos */
#define NEVER            UINT64_MAX

/*
 * Líneas del cuadro: se cuentan desde la que empieza el VBLANK (el contador de líneas del LSPC
 * vale $1F0, la primera del borde de abajo), así un cuadro termina con la última línea visible:
 *   0-15 borde de abajo ($1F0-$1FF), 16-23 sincronía vertical ($0F8-$0FF), 24-39 borde de arriba
 *   ($100-$10F), 40-263 las 224 líneas visibles ($110-$1EF)
 */
#define FIRST_VISIBLE_LINE 40
#define SCREEN_W           320
#define SCREEN_H           224
#define VBL_IRQ_AT         85         /* ciclo de la línea 0 en que se pide el VBLANK (168 + 2 maestros) */
#define TIMER_VBL_AT       573        /* ... y en que se recarga el temporizador si así se pidió (1146 maestros) */

/* Interrupciones pendientes del 68000 (se borran escribiendo IRQACK, $3C000C, con los mismos bits) */
enum { IRQ_COLD = 1, IRQ_TIMER = 2, IRQ_VBL = 4 };     /* nivel 3 (arranque), 2 y 1 */

/* LSPC: VRAM, registros, contador de líneas, temporizador e interrupciones */
typedef struct {
    u16  vram[0x10000];             /* $0000-$7FFF la lenta; $8000-$87FF la rápida (con espejos) */
    u16  vram_addr;
    s16  vram_mod;
    u16  mode;                      /* lo escrito en LSPCMODE ($3C0006) */
    u32  timer_reload;              /* TIMERHIGH:TIMERLOW */
    u64  timer_at;                  /* ciclo del 68000 en que el temporizador llega a 0 (NEVER: no llegará) */
    u16  timer_stop;                /* $3C000E (solo PAL) */
    u8   irq;                       /* IRQ_* pendientes */
    int  line;                      /* línea del cuadro (0-263) */
    u64  line_start;                /* ciclo del 68000 en que empezó */
    u64  next_event;                /* el próximo ciclo en que hay algo que hacer (lspc_sync) */
    u8   vbl_done, reload_done;     /* en la línea 0: ya se pidió el VBLANK / ya se recargó el temporizador */
    u8   anim, anim_wait;           /* autoanimación: contador de 3 bits y cuadros hasta el próximo paso */
    u8   frame_done;                /* terminó el cuadro (lo borra quien corre el cuadro) */
    u64  frame;                     /* cuadros desde el reset */
} Lspc;

/*
 * YM2610 (ym2610.c): 4 canales FM, SSG, 6 canales de ADPCM-A y uno de ADPCM-B, más los temporizadores
 * y la IRQ al Z80. El FM es el YM2612 de GENRecomp (los canales 2, 3, 5 y 6 del OPNA: índices 1, 2, 4
 * y 5; el 0 y el 3 no existen).
 */
typedef struct {
    u32  phase;                 /* 20 bits */
    int  env;                   /* atenuación 0-1023 */
    u8   state;                 /* ataque, decaimiento, sostenido, relajación */
    u8   key;
    u8   dt, mul, tl, ks, ar, am, dr, sr, sl, rr, ssg;
    u8   ssg_inv;               /* SSG-EG: la envolvente va invertida en este ciclo (modos que alternan) */
    u8   kc;                    /* código de tecla (se recalcula tras escribir un registro) */
    u32  inc;                   /* incremento de fase sin el LFO (ídem) */
} YmOp;

typedef struct {
    YmOp op[4];                 /* S1 S2 S3 S4 (orden de modulación) */
    u16  fnum;
    u8   block, alg, fb, pan, ams, pms;
    u8   latch;                 /* $A4-$A6: bits altos de fnum y bloque, que se aplican al escribir $A0-$A2 */
    int  fb_out[2];             /* las dos últimas salidas de S1 (realimentación) */
    int  out;                   /* última salida del canal (14 bits) */
} YmCh;

typedef struct {
    u16  tcnt[3];               /* contadores de tono */
    u8   tout[3];               /* salida de cada tono (0/1) */
    u16  ncnt;
    u32  lfsr;                  /* ruido: 17 bits */
    u16  ecnt;                  /* envolvente: contador del periodo */
    u8   estep;                 /* paso (0-31) */
    u8   eatt, ehold;           /* dirección (1 = sube) y quieta */
} Ssg;

typedef struct {
    u8   on;
    u32  addr, end;             /* en nibbles: dirección actual y la última */
    int  acc, step;             /* acumulador de 12 bits e índice de la tabla de pasos */
} AdpcmA;

typedef struct {
    u8   on;
    u32  addr;                  /* en nibbles */
    u32  pos;                   /* fracción de 16 bits hacia la siguiente muestra */
    int  acc, step, prev;       /* acumulador de 16 bits, paso, muestra anterior (se interpola) */
} AdpcmB;

typedef struct {
    u8   addr[2];               /* registro elegido en cada mitad (puertos 4 y 6 del Z80) */
    u8   reg[2][256];
    u8   flags;                 /* bit 0 temporizador A, bit 1 temporizador B */
    u64  ta_at, tb_at;          /* ciclo maestro en que vence cada temporizador (NEVER: parado) */
    /* FM */
    YmCh ch[6];
    u16  fnum3[3];              /* canal 3 en modo especial: fnum de S1, S2, S3 */
    u8   block3[3], latch3;
    u8   dirty;                 /* se escribió un registro: recalcular kc e inc de los operadores */
    u8   lfo_on, lfo_freq, lfo_step;
    u16  lfo_cnt;
    u32  eg_cnt;
    u8   eg_sub;
    /* SSG, ADPCM */
    Ssg  ssg;
    AdpcmA pa[6];
    AdpcmB pb;
    u8   pa_sub;                /* el ADPCM-A va cada 3 muestras del FM */
    u8   end_flags;             /* fin de los ADPCM: bits 0-5 A, bit 7 B (se leen en el puerto 6) */
    u8   end_mask;              /* las banderas que no se levantan ($1C) */
    u64  busy_until;            /* ciclo maestro hasta el que el estado dice "ocupado" */
    u64  mclk;                  /* ciclo maestro hasta el que se generó el sonido */
} Ym2610;

/* Audio (audio.c): el YM2610 mezclado a 44 100 Hz en estéreo de 16 bits */
#define AUDIO_RATE 44100
#define AUDIO_BINS 4096
#define MCLK_HZ    24167829.0       /* el reloj maestro del AES (fija cuántas muestras salen por cuadro) */
typedef struct {
    float acc[AUDIO_BINS][2];   /* muestras de salida que se están formando (por promedio en el tiempo) */
    u64  first;                 /* número de la primera de ellas */
    int16_t buf[AUDIO_BINS * 2]; /* muestras listas (L R), para quien las tome */
    int  n;
    u8   mute;                  /* bits 0-3: FM 1-4, bit 4: SSG, bit 5: ADPCM-A, bit 6: ADPCM-B */
    float hp_x[2], hp_y[2];     /* filtro pasa-altos de la salida (el acople de la consola) */
} Audio;

/* Tarjeta de memoria (la usa la BIOS del AES para guardar las partidas) */
typedef struct {
    u8  *data;                      /* NULL: no hay tarjeta */
    u32  size;
    u8   protect;                   /* protegida contra escritura */
    u8   dirty;
} Card;

/* Botones de un mando (1 = apretado); los 8 primeros en el orden de REG_P1CNT ($300000) */
enum {
    NEO_UP = 1, NEO_DOWN = 2, NEO_LEFT = 4, NEO_RIGHT = 8, NEO_A = 16, NEO_B = 32, NEO_C = 64, NEO_D = 128,
    NEO_START = 256, NEO_SELECT = 512
};

/* Salidas del latch del sistema ($3A0001-$3A001F): bit n = salida n */
enum { LATCH_SHADOW = 1, LATCH_SWPROM = 2, LATCH_CRDLOCK1 = 4, LATCH_CRDUNLOCK2 = 8, LATCH_CRDNORMAL = 16,
       LATCH_CRTFIX = 32, LATCH_SRAMUNLOCK = 64, LATCH_PALBANK1 = 128 };

typedef struct Neo {
    M68k cpu;
    u64  cyc;                       /* ciclos del 68000 desde el encendido */
    void *jb;                       /* jmp_buf del paso actual (el error de dirección vuelve ahí) */
    Cart cart;
    Bios bios;
    u8   p2_bank;                   /* banco de P2 que se ve en $200000 (lo escrito en el latch) */
    u32  map_ver;                   /* sube con cada cambio de banco (el código generado lo vigilará) */
    Lspc lspc;
    u8   latch;                     /* LATCH_* */
    u16  pad[2];                    /* NEO_* de los mandos 1 y 2 */
    u8   poutput, crdbank;          /* $380001 y $380011 */
    Card card;
    u16  open_bus;                  /* último valor en el bus de datos (lo que leen las direcciones sin nada) */
    /* Z80 y sonido */
    Z80  z80;
    u64  z80_mclk;                  /* ciclo maestro hasta el que corrió el Z80 */
    u8   z80_bank[4];               /* bancos de las ventanas de 2, 4, 8 y 16 KB ($F000, $E000, $C000, $8000) */
    u8   sound_cmd, sound_reply;    /* $320000: comando del 68000 al Z80 y respuesta */
    u8   nmi_pending, nmi_enabled, nmi_line;
    u32  sound_cmds;                /* comandos enviados (estadística) */
    Ym2610 ym;
    Audio audio;
    u32  exc_count[64];             /* excepciones atendidas por vector (estadística) */
    u32 *trace;                     /* NULL, o por cada palabra de P y de la BIOS (neo_trace_index): 0 = no
                                       ejecutada; si no, $80000000 | dirección */
    u8   ram[0x10000];              /* RAM del 68000: $100000-$10FFFF (con espejos hasta $1FFFFF) */
    u16  pal[2][0x1000];            /* paleta: dos bancos de 256 paletas de 16 colores */
    u32  pal_argb[2][0x1000];       /* la misma, ya en ARGB (se pone al día en cada escritura) */
    u32  fb[SCREEN_W * SCREEN_H];   /* la imagen del cuadro en ARGB */
    u64 *spr;                       /* C convertida (lspc_render.c): 16 filas de 64 bits por mosaico */
    size_t spr_tiles;
    u32 *fix;                       /* S convertida: 8 filas de 32 bits por mosaico */
    size_t fix_tiles;
    u8   zram[0x800];               /* RAM del Z80: $F800-$FFFF */
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

/* --- La consola (neo.c) --- */

/* Carga el juego y la BIOS (la busca si bios_path es NULL), calcula la tabla L0 y enciende. 0 si todo
 * bien; si no, *err */
int  neo_load(Neo *g, const char *rom_path, const char *bios_path, const char **err);
/* Arma la consola con un cartucho (pasa a ser suyo: lo libera neo_free) y una BIOS ya cargados, y enciende */
void neo_assemble(Neo *g, const Cart *cart, const Bios *bios);
void neo_free(Neo *g);
void neo_power_on(Neo *g);              /* encendido: todo a cero, la interrupción de arranque pendiente */
void neo_reset(Neo *g);                 /* botón de reset */
void neo_run_frame(Neo *g);             /* corre hasta terminar el cuadro */
void neo_after_step(Neo *g);            /* LSPC y Z80 al ciclo del 68000 (tras cada instrucción) */
int  neo_card_insert(Neo *g, const char *path);  /* tarjeta de 2 KB (la carga si el archivo existe); 1 si todo bien */
int  neo_card_save(Neo *g, const char *path);

/* --- LSPC (lspc.c) --- */
void lspc_reset(Neo *g);
void lspc_sync(Neo *g);                 /* líneas, temporizador e interrupciones hasta g->cyc */
u16  lspc_read(Neo *g, u32 a);
void lspc_write(Neo *g, u32 a, u16 v);
NEO_INL u16 lspc_counter(const Lspc *l)    /* contador de líneas del LSPC ($0F8-$1FF) de la línea actual */
{
    int v = 0x1F0 + l->line;
    return (u16)(v > 0x1FF ? v - 0x200 + 0xF8 : v);
}

/* --- Dibujo (lspc_render.c) --- */
void gfx_init(Neo *g);                  /* convierte la C y la S del cartucho para dibujar */
void gfx_free(Neo *g);
u32  neo_color(u16 c);                  /* un color de la paleta en ARGB */
void pal_refresh(Neo *g);               /* toda la paleta a ARGB (tras cargar un estado, por ejemplo) */
void lspc_render_line(Neo *g, int sy);  /* dibuja la línea sy (0-223) de la pantalla */

/* --- Z80 y YM2610 (z80bus.c, ym2610.c) --- */
void z80_run_until(Neo *g, u64 mclk);   /* el Z80 corre hasta ese ciclo maestro */
NEO_INL void z80_sync(Neo *g) { u64 m = g->cyc * MCLK_PER_M68K; if (g->z80_mclk < m) z80_run_until(g, m); }
void sound_command(Neo *g, u8 v);       /* el 68000 escribe $320000 */
void ym_reset(Ym2610 *y);
void ym_update(Ym2610 *y, u64 mclk);    /* temporizadores hasta ese ciclo maestro */
void ym_run(Neo *g, u64 mclk);          /* el sonido hasta ese ciclo maestro */
u8   ym_read(Neo *g, int port, u64 mclk);
void ym_write(Neo *g, int port, u8 v, u64 mclk);
void audio_reset(Neo *g);
void audio_add(Neo *g, u64 t0, u64 t1, float l, float r);  /* un tramo constante de sonido */
void audio_end_frame(Neo *g);           /* lleva el YM2610 al final del cuadro y suelta las muestras */
int  neo_audio_take(Neo *g, int16_t *out, int max_frames);  /* muestras L R; devuelve cuántos pares */
NEO_INL u64 ym_next_event(const Ym2610 *y) { return y->ta_at < y->tb_at ? y->ta_at : y->tb_at; }

/* Tras cada instrucción del 68000 (intérprete y código generado): el LSPC solo si hay algo que hacer
 * y el Z80 solo si quedó atrás */
NEO_INL void neo_after_step_inline(Neo *g)
{
    if (g->cyc >= g->lspc.next_event) lspc_sync(g);
    z80_sync(g);
}

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
