/*
 * GENRecomp: estado completo de la consola y su interfaz.
 * Cartucho (formatos, cabecera, vectores, SRAM, EEPROM, mapper de SSF2), 68000, bus,
 * E/S (mandos), VDP, Z80 y audio (YM2612 y PSG). Ver docs/PLAN.md.
 */
#ifndef GEN_H
#define GEN_H

#include <stddef.h>
#include <stdint.h>

#include "m68k.h"
#include "z80.h"

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
enum { MAPPER_NONE, MAPPER_SSF2 };

/* Botones de un mando (1 = apretado). Los 6 primeros están en el orden de los bits del puerto
 * con TH = 1 (arriba, abajo, izquierda, derecha, B, C) y Z Y X MODE en el de la cuarta lectura del
 * mando de 6 botones. */
enum {
    GEN_UP = 1, GEN_DOWN = 2, GEN_LEFT = 4, GEN_RIGHT = 8, GEN_B = 16, GEN_C = 32, GEN_A = 64,
    GEN_START = 128, GEN_Z = 256, GEN_Y = 512, GEN_X = 1024, GEN_MODE = 2048
};

/* Reloj NTSC: el maestro es de 53.693175 MHz; el 68000 va a /7 y una línea son 3420 ciclos
 * maestros (488.57 del 68000); 262 líneas por cuadro */
#define MCLK_PER_M68K  7
#define MCLK_PER_LINE  3420
#define LINES_NTSC     262

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
    u16  checksum_calc;         /* suma de las palabras de $200 al fin de ROM de la cabecera (o al final) */
    char io[17];                /* $190: periféricos ("J" = mando de 3 botones, "6" = de 6...) */
    u32  rom_start, rom_end;    /* $1A0 */
    u32  ram_start, ram_end;    /* $1A8 */
    char region[4];             /* $1F0: "JUE", o el formato nuevo de un dígito hex */
    int  sega;                  /* 1 si $100 empieza con "SEGA" (o " SEGA") */
} GenHeader;

/* EEPROM serie (I2C) de algunos cartuchos: dónde están sus líneas sale de una base de datos por
 * juego (eeprom.c), porque la cabecera no lo dice */
typedef struct {
    u32 sda_in_addr, scl_addr, sda_out_addr;    /* direcciones de byte donde se escriben SDA y SCL y se lee SDA */
    u8  sda_in_bit, scl_bit, sda_out_bit;
    u8  addr_bits;              /* 7: X24C01 (la dirección va en el primer byte); 8: 24C01-24C16; 16: 24C32 o más */
    u16 size_mask, page_mask;   /* tamaño - 1 y página de escritura - 1 */
} EepromCfg;

typedef struct {
    u8  scl, sda;               /* líneas que pone la consola */
    u8  out;                    /* SDA que pone la EEPROM (1 = suelta: la línea es el AND de las dos) */
    u8  state, bit, shift;
    u8  sending;                /* la EEPROM está mandando un byte (lectura) */
    u16 addr;
    u32 writes;                 /* bytes escritos (estadística) */
} Eeprom;

typedef struct {
    u8    *rom;  size_t rom_size;       /* imagen normalizada (big-endian, sin cabecera de SMD) */
    int    format;                      /* ROMFMT_* del archivo original */
    char   sha1[41];                    /* SHA1 de la imagen normalizada */
    GenHeader hdr;
    /* RAM del cartucho, según la cabecera ("RA" en $1B0) */
    int    sram;                        /* 1 si la cabecera la declara */
    u8     sram_type;                   /* $1B2: bit 6 = con batería; bits 4-3: 10 = bytes pares, 11 = impares, 00 = palabras */
    u32    sram_start, sram_end;
    int    has_eeprom;                  /* en vez de SRAM, una EEPROM serie (sram_data es su contenido) */
    EepromCfg eeprom;
    u8    *sram_data;                   /* todo el rango (también los bytes que el cartucho no usa) */
    u8    *sram_saved;                  /* lo último guardado en disco (para ver si cambió) */
    size_t sram_size;
    /* Mapeo */
    int    mapper;                      /* MAPPER_* */
    u32    rom_mask;                    /* potencia de 2 menos 1 que cubre la ROM (sus espejos) */
    u32    bank[8];                     /* desplazamiento en la ROM de cada ventana de 512 KB */
    u8     a130f1;                      /* $A130F1: bit 0 = la SRAM se ve en vez de la ROM, bit 1 = protegida */
    u32    map_ver;                     /* sube con cada cambio de banco (el código generado lo vigila) */
} Cart;

/* VDP (vdp.c, vdp_render.c) */
#define FB_W 320                /* ancho del búfer de imagen (320 en H40; en H32 se usan 256) */
#define FB_H 240
typedef struct {
    u8   reg[24];
    u16  addr;                  /* dirección del puerto de datos */
    u8   code;                  /* CD5-CD0: destino (VRAM, CRAM, VSRAM), lectura/escritura, DMA */
    u8   pending;               /* se escribió la primera palabra de un comando de dirección */
    u8   fill_pending;          /* DMA de relleno: espera la escritura en el puerto de datos */
    u8   vint_pending, hint_pending;
    u8   f_flag;                /* bit 7 del estado: hubo VINT (se borra al atenderla) */
    u8   spr_overflow, spr_collision;   /* bits 6 y 5 del estado (se borran al leerlo) */
    u8   odd;                   /* cuadro impar (entrelazado) */
    int  line;                  /* línea actual (0-261) */
    int  hint_counter;
    u64  next_line;             /* ciclo maestro en que empieza la línea siguiente */
    u64  dma_busy_until;        /* ciclo maestro en que termina un DMA de relleno o de copia */
    u64  frame;                 /* cuadros terminados */
    u8   frame_done;            /* terminó un cuadro (lo borra quien corre el cuadro) */
    u16  width, height;         /* del cuadro: 256 o 320 por 224 (o 240 en PAL) */
    u8   vram[0x10000];
    u16  cram[64];
    u16  vsram[40];
    u16  fb[FB_W * FB_H];       /* por pixel: color de 9 bits (B G R, 3 bits cada uno) | modo << 9
                                   (0 normal, 1 sombra, 2 resalte) */
} Vdp;

/* PSG SN76489 (psg.c): 3 tonos y ruido, al reloj del Z80 / 16 */
typedef struct {
    u16  period[4];             /* tonos: 10 bits; el 3 es el registro de ruido (3 bits) */
    u8   vol[4];                /* atenuación 0-15 (15 = silencio) */
    u8   latch;                 /* registro elegido por el último byte con el bit 7 */
    u16  counter[4];
    u8   out[4];                /* salida actual de cada canal (0/1) */
    u8   noise_ff;              /* biestable del ruido (el LFSR avanza en cada subida) */
    u16  lfsr;
    u64  mclk;                  /* ciclo maestro hasta el que va */
} Psg;

/* YM2612 (ym2612.c): 6 canales FM de 4 operadores, DAC, LFO y temporizadores */
typedef struct {
    u32  phase;                 /* 20 bits */
    int  env;                   /* atenuación 0-1023 */
    u8   state;                 /* ataque, decaimiento, sostenido, relajación */
    u8   key;
    u8   dt, mul, tl, ks, ar, am, dr, sr, sl, rr, ssg;
    u8   ssg_inv;               /* SSG-EG: la envolvente va invertida en este ciclo (modos que alternan) */
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
    YmCh ch[6];
    u8   addr[2];               /* registro elegido en cada puerto */
    u16  fnum3[3];              /* canal 3 en modo especial: fnum de S1, S2, S3 */
    u8   block3[3], latch3;
    u8   lfo_on, lfo_freq, lfo_step;
    u16  lfo_cnt;
    u16  ta, ta_cnt;
    u8   tb, tb_cnt, tb_sub;
    u8   timer_ctl;             /* $27 */
    u8   status;                /* bits 0-1: desbordaron los temporizadores A y B */
    u8   dac_on, dac;
    u32  eg_cnt;
    u8   eg_sub;
    u64  busy_until;            /* ciclo maestro hasta el que la lectura dice "ocupado" */
    u64  mclk;                  /* ciclo maestro hasta el que va */
} Ym2612;

/* Audio (audio.c): los dos chips mezclados a 44100 Hz en estéreo de 16 bits */
#define AUDIO_RATE 44100
#define AUDIO_BINS 4096
typedef struct {
    float acc[AUDIO_BINS][2];   /* muestras de salida que se están formando (por promedio en el tiempo) */
    u64  first;                 /* índice de la primera de acc */
    int16_t buf[AUDIO_BINS * 2]; /* muestras listas (L R), para quien las tome */
    int  n;
    u8   mute;                  /* bits 0-5: canales FM 1-6, bit 6: PSG */
    float hp_x[2], hp_y[2];     /* filtro pasa-altos de la salida (el acople de la consola) */
} Audio;

/* E/S (io.c): los tres puertos de la consola */
typedef struct {
    u8   data[3], ctrl[3];      /* $A10003/5/7 (datos) y $A10009/B/D (dirección de cada pin: 1 = salida) */
    u8   txdata[3], sctrl[3];   /* registros serie (no hay nada conectado) */
    u8   th_edges[2];           /* mando de 6 botones: bajadas de TH seguidas */
    u64  th_time[2];            /* ciclo de la última bajada de TH */
} Io;

typedef struct Gen {
    M68k cpu;
    u64  cyc;                   /* ciclos del 68000 desde el reset */
    void *jb;                   /* jmp_buf del paso actual (el error de dirección vuelve ahí) */
    Cart cart;
    Vdp  vdp;
    Io   io;
    Eeprom eeprom;
    u16  pad[2];                /* botones apretados (GEN_*) de los mandos 1 y 2 */
    u8   pad6;                  /* bit 0/1: el mando 1/2 es de 6 botones */
    u8   version;               /* registro de versión ($A10001): $80 extranjero, $40 PAL, $20 sin Mega CD, bits 0-3 versión */
    u8   tmss;                  /* 1: consola con TMSS (versión 1; apagado por omisión) */
    u32  tmss_reg;              /* lo escrito en $A14000: con TMSS, el VDP solo responde si es "SEGA" */
    u8   tmss_locked;           /* el 68000 tocó el VDP sin "SEGA" en $A14000 y quedó colgado */
    u8   z80_busreq;            /* el 68000 pidió el bus del Z80 ($A11100) */
    u8   z80_reset;             /* el Z80 está en reset ($A11200 = 0) */
    u16  z80_bank;              /* banco de 32 KB que ve el Z80 en $8000 (9 bits, $A06000) */
    Z80  z80;
    u64  z80_mclk;              /* ciclo maestro hasta el que corrió el Z80 */
    u64  z80_irq_from, z80_irq_until;   /* INT del Z80: una línea desde el VBLANK */
    Psg  psg;
    Ym2612 ym;
    Audio audio;
    u16  open_bus;              /* último valor en el bus de datos (lo que leen las direcciones sin nada) */
    u32  exc_count[64];         /* excepciones atendidas por vector (estadística) */
    u32 *trace;                 /* NULL, o por cada palabra de ROM: 0 = no ejecutada; si no, $80000000 | dirección */
    u8   ram[0x10000];          /* RAM del 68000: $FF0000-$FFFFFF (y sus espejos desde $E00000) */
    u8   zram[0x2000];          /* RAM del Z80 */
} Gen;

/* Carga una ROM (.bin, .md, .gen o .smd) y hace el reset. 0 si todo bien; mensaje en *err si falla. */
int  gen_load(Gen *g, const u8 *data, size_t len, const char **err);
void gen_free(Gen *g);
void gen_reset(Gen *g);                 /* reset de la consola */
void gen_run_frame(Gen *g);             /* corre hasta terminar el cuadro (con el intérprete) */
void gen_after_step(Gen *g);            /* VDP y Z80 al ciclo del 68000 (tras cada instrucción) */
int  gen_sram_load(Gen *g, const char *path);  /* 1 si se cargó; 0 si no aplica o falló */
int  gen_sram_save(Gen *g, const char *path);
int  gen_sram_dirty(const Gen *g);

/* Traza: qué instrucciones de ROM ejecutó el intérprete (líneas "OFFSET:DIRECCION" en hexadecimal,
 * lo que lee genrecomp --trace) */
void gen_trace_start(Gen *g);
int  gen_trace_load(Gen *g, const char *path);  /* suma lo de una traza anterior; 1 si se leyó */
int  gen_trace_write(const Gen *g, const char *path);
size_t gen_trace_count(const Gen *g);

/* Estados guardados: la consola entera. Solo los lee la misma versión con la misma ROM. */
u8  *gen_state_save(const Gen *g, size_t *size);   /* malloc; NULL si falla */
int  gen_state_load(Gen *g, const u8 *data, size_t size);  /* 1 si se cargó */

/* Mapeo del cartucho */
void cart_setup(Cart *c);               /* mapper, espejos y SRAM según el tamaño y la cabecera */
void cart_map_reset(Cart *c);           /* bancos de SSF2 en su lugar, SRAM según su valor de inicio */
u32  cart_rom_offset(const Cart *c, u32 addr);   /* desplazamiento en la ROM de una dirección < $400000 */

/* EEPROM serie (eeprom.c) */
int  eeprom_detect(Cart *c);            /* 1 si el juego está en la base de datos (configura c->eeprom) */
void eeprom_reset(Gen *g);
int  eeprom_owns(const Cart *c, u32 addr);
u8   eeprom_read(Gen *g, u32 addr);
void eeprom_write(Gen *g, u32 addr, u8 v);

/* VDP (vdp.c) */
void vdp_reset(Gen *g);
u16  vdp_read(Gen *g, u32 addr);        /* $C00000-$C0001F (sin los espejos) */
void vdp_write(Gen *g, u32 addr, u16 v);
void vdp_sync(Gen *g);                  /* líneas e interrupciones hasta g->cyc */
void vdp_int_ack(Gen *g, int level);
void vdp_render_line(Gen *g, int y);    /* dibuja la línea y en fb (vdp_render.c) */
uint32_t vdp_argb(u16 px);              /* un pixel de fb en ARGB 8888 */

/* Z80 en la consola (z80bus.c): corre hasta el ciclo maestro dado (o hasta el del 68000) */
void z80_run_until(Gen *g, u64 mclk);
void z80_sync(Gen *g);
void z80_set_reset(Gen *g, int on);     /* $A11200: el reset del Z80 también reinicia el YM2612 */
void z80_set_busreq(Gen *g, int on);
u8   bus_read8(Gen *g, u32 addr);       /* el bus del 68000 sin ciclos (para el Z80 y el DMA) */
void bus_write8(Gen *g, u32 addr, u8 v);

/* Sonido (psg.c, ym2612.c, audio.c). Los tiempos son ciclos maestros. */
void psg_reset(Psg *p);
void psg_write(Gen *g, u8 v, u64 mclk);
void psg_run(Gen *g, u64 mclk);
void ym_reset(Ym2612 *y);
u8   ym_read(Gen *g, u64 mclk);
void ym_write(Gen *g, int a, u8 v, u64 mclk);     /* a: 0-3 ($4000-$4003) */
void ym_run(Gen *g, u64 mclk);
void audio_reset(Gen *g);
void audio_add(Gen *g, u64 t0, u64 t1, float l, float r);  /* un tramo constante de un chip */
void audio_end_frame(Gen *g);                   /* lleva los chips al final del cuadro y suelta las muestras */
int  gen_audio_take(Gen *g, int16_t *out, int max_frames);  /* muestras L R; devuelve cuántos pares */

/* E/S (io.c): registros de $A10000-$A1001F, de 0 a 15 */
void io_reset(Gen *g);
u8   io_read(Gen *g, int reg);
void io_write(Gen *g, int reg, u8 v);

/* Normaliza el archivo a la imagen que ve el 68000 (deshace el entrelazado de SMD, rellena a
 * tamaño par). malloc; NULL y *err si no se reconoce. */
u8  *cart_normalize(const u8 *data, size_t len, size_t *out_len, int *format, const char **err);
void cart_parse_header(Cart *c);
u16  cart_checksum(const u8 *rom, size_t len);
u32  cart_vector(const Cart *c, int n);         /* vector n de la tabla de $000000 */
const char *cart_vector_name(int n);            /* "reset", "VINT"...; NULL si no tiene nombre propio */

#endif
