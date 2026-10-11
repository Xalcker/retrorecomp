/*
 * NESRecomp: estado completo de la consola y su interfaz.
 * Etapa 1: CPU 6502 (2A03, sin decimal) por instrucción, bus y mappers.
 * Los ciclos se cuentan por instrucción; cuando entre la PPU se pasará a
 * contar por acceso al bus (como hace GBRecomp con los M-ciclos).
 */
#ifndef NES_H
#define NES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_I 0x04
#define FLAG_D 0x08
#define FLAG_B 0x10
#define FLAG_U 0x20
#define FLAG_V 0x40
#define FLAG_N 0x80

typedef struct {
    u16 pc;
    u8  a, x, y, sp, p;
    int jammed;                 /* opcode KIL: el CPU se detiene */
} Cpu;

enum { MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_SINGLE0, MIRROR_SINGLE1, MIRROR_FOUR, MIRROR_CUSTOM };

/* Mapper fijo en tiempo de compilación: el proyecto que genera nesrecomp se compila con -DNES_MAPPER=<número> y el
 * compilador elimina todo el código de los demás mappers. Sin esa definición (el intérprete `nes`) se decide al cargar. */
#ifdef NES_MAPPER
#define MAPPER(c)          NES_MAPPER
#define MAPPER_IS_MMC3(m)  ((m) == 4 || (m) == 118 || (m) == 119 || (m) == 115)
#define CART_MMC3(c)       MAPPER_IS_MMC3(NES_MAPPER)
#define CART_MMC5(c)       (NES_MAPPER == 5)
#define CART_CHRHOOK(c)    (NES_MAPPER == 9 || NES_MAPPER == 10)
#define CART_LOWREGS(c)    (NES_MAPPER == 34 || NES_MAPPER == 79 || NES_MAPPER == 87 || NES_MAPPER == 140 || NES_MAPPER == 5 || NES_MAPPER == 19 || NES_MAPPER == 184 || NES_MAPPER == 115)
#define CART_EXGFX(c)      (NES_MAPPER == 5 ? (c)->exgfx : 0)
#else
#define MAPPER(c)          ((c)->mapper)
#define CART_MMC3(c)       ((c)->mmc3)
#define CART_MMC5(c)       ((c)->mmc5)
#define CART_CHRHOOK(c)    ((c)->chr_hook)
#define CART_LOWREGS(c)    ((c)->low_regs)
#define CART_EXGFX(c)      ((c)->exgfx)
#endif

typedef struct {
    int  mapper;
    int  mirroring;
    int  battery;
    int  nes2;
    u8  *prg;  size_t prg_size;       /* PRG ROM */
    u8  *chr;  size_t chr_size;       /* CHR ROM o RAM */
    int  chr_is_ram;
    u8   prg_ram[0x2000];
    u8   sram_saved[0x2000];    /* copia de lo último guardado en disco (para ver si cambió) */
    /* estado de los mappers con bancos */
    u32  reg[24];
    size_t prg_map[4];          /* desplazamiento de PRG de cada ventana de 8 KB ($8000, $A000, $C000, $E000) */
    u8  *chr_page[8];           /* puntero a cada página de 1 KB de CHR con los bancos actuales */
    u32  chr_mask;              /* tamaño de CHR - 1 si es potencia de 2 (si no, 0 y se usa el módulo) */
    u8  *prg_ptr[4];            /* puntero a cada ventana de 8 KB de $8000-$FFFF (ROM, o RAM en el MMC5) */
    u8  *chr_page_spr[8];       /* MMC5: páginas de CHR para las búsquedas de sprites (con sprites 8x16) */
    u8   exgfx;                 /* MMC5: ExRAM en modo 1 (ExGrafix: banco de CHR y paleta por mosaico) */
    u8   mmc5, m5_spr16, m5_inframe, m5_pending, m5_counter;
    u8   m5[0x60];              /* registros del MMC5: $5100-$5130 -> 0x00-0x30; $5200-$5206 -> 0x50-0x56 */
    u16  m5_prod;               /* resultado del multiplicador */
    u8   exram[0x400];          /* MMC5: ExRAM */
    u8   wram_hi[7 * 0x2000];   /* MMC5: bancos 1-7 de PRG-RAM (el banco 0 es prg_ram) */
    u8   chr_ram[0x2000];       /* TQROM (119): 8 KB de RAM de CHR junto a la ROM */
    u8   chr_wr[8];             /* cada página de 1 KB de CHR: ¿se puede escribir? */
    u8   mmc3_alt;              /* MMC3 revisión vieja (rev. A / MMC6): variable de entorno NES_MMC3_ALT=1 */
    u8   mmc3;                  /* MMC3 o derivado: IRQ por flanco de A12 */
    u8   chr_hook;              /* MMC2/MMC4: las lecturas de ciertos patrones cambian el banco */
    u8   nt_map[4];             /* MIRROR_CUSTOM: página de CIRAM (0 o 1) de cada nametable */
    u64  irq_t0, irq_cycle;     /* FME-7: IRQ por ciclos de CPU (irq_cycle = ciclo en que se levanta, o ~0) */
    u8   low_regs;              /* el mapper tiene registros por debajo de $8000 (la PPU debe ir al día al escribirlos) */
    u8   submapper;             /* NES 2.0 (0 si la cabecera no lo dice) */
    u32  map_ver;               /* sube con cada escritura a $8000-$FFFF (¿cambió el mapeo?) */
    u64  a12_low;               /* MMC3: reloj de la PPU cuando A12 pasó a 0 */
} Cart;
#define CART_VRC(c) (MAPPER(c) == 21 || MAPPER(c) == 22 || MAPPER(c) == 23 || MAPPER(c) == 25)
#define CART_VRC6(c) (MAPPER(c) == 24 || MAPPER(c) == 26)

typedef struct {
    u8  spr_y, spr_tile, spr_attr, spr_x;      /* (sin uso: la OAM se lee cruda) */
} OamEntry;

typedef struct {
    u8   x, attr, lo, hi;
} SprSlot;

typedef struct {
    u8   ctrl, mask, status, oam_addr;
    u16  v, t;                  /* direcciones "loopy" */
    u8   fine_x, w;
    u8   read_buf, latch;       /* búfer de $2007 y bus abierto */
    u8   vram[0x1000];          /* 4 nametables (2 KB con espejo, 4 KB en four-screen) */
    u8   pal[32];
    u8   oam[256];
    int  scanline, dot;         /* 0-261 (261 = pre-render), 0-340 */
    int  odd;
    u64  frame;
    int  frame_done;
    /* fondo */
    u8   nt_id, nt_attr, nt_lo, nt_hi, ex_byte;     /* ex_byte: ExRAM del mosaico (MMC5 ExGrafix) */
    u16  bg_lo, bg_hi, at_lo, at_hi;
    /* sprites de la línea actual */
    SprSlot spr[8];
    int  spr_count, spr0_possible;
    u8   suppress_vbl;          /* lectura de $2002 justo antes de que salga el vblank */
    u8   sec[32];               /* OAM secundaria de la línea */
    int  sec_count, sec_sprite0;
    u16  ovf_dot;               /* punto de la línea en que se levantará el desbordamiento de sprites (0: ninguno) */
    u8   oam_snap;              /* OAMADDR en el punto 65 de la línea (donde empieza la evaluación de sprites) */
    u8   spr_a12[8];            /* A12 de la búsqueda de cada ranura de sprite */
    u64  clock;                 /* puntos de reloj de la PPU ya ejecutados (puede ir detrás de 3 × ciclos de CPU) */
    u64  event_dot;             /* cuenta de puntos en que algo visible desde fuera (vblank, fin del cuadro) puede pasar */
    u16  fb[256 * 240];         /* por pixel: índice de color 0-63 y, en los bits 6-8, el énfasis ($2001 bits 5-7) */
} Ppu;

typedef struct { u8 on, duty, step, len, halt, cst, vol, env_start, env_div, env_decay;
                 u8 sw_en, sw_per, sw_neg, sw_shift, sw_div, sw_reload; u16 period, timer;
                 u8 lhalt, hnew, rel, prev; } Pulse;      /* lhalt: halt del contador de longitud (se aplica al final del ciclo); rel/prev: carga pendiente */
typedef struct { u8 on, ctrl, lin_val, lin, lin_reload, step, len; u16 period, timer; u8 lhalt, hnew, rel, prev; } Triangle;
typedef struct { u8 on, halt, cst, vol, env_start, env_div, env_decay, mode, len; u16 period, timer, shift;
                 u8 lhalt, hnew, rel, prev; } Noise;
typedef struct { u8 on, irq_en, loop, out, buf, buf_empty, shift, bits, silence, load_delay;
                 u16 rate, timer, addr, cur, remaining, length; } Dmc;

/* Audio del VRC6 (mappers 24 y 26): dos pulsos con 8 anchos de pulso y un diente de sierra */
typedef struct {
    u8  duty[2], vol[2], mode[2], step[2];      /* pulsos: step baja de 15 a 0 */
    u8  rate, acc, sstep;                       /* diente de sierra: acumulador de 8 bits, paso 0-13 */
    u8  en[3], ctrl;                            /* habilitado por canal; $9003: bit 0 = detener, bits 1-2 = frecuencia x16/x256 */
    u16 period[3], timer[3];
} Vrc6;

#define APU_BUF 16384
typedef struct {
    Pulse p[4]; Triangle t; Noise nz; Dmc d;   /* p[2] y p[3]: pulsos del MMC5 (sin barrido) */
    u8   m5;                            /* hay audio de MMC5 */
    u8   m5_pcm, m5_pcm_read;           /* PCM del MMC5: valor de salida y modo lectura ($5010 bit 0) */
    u32  m5_acc;                        /* reloj de 240 Hz del MMC5, en décimas de ciclo de CPU */
    u8   mode5, inhibit, frame_irq;
    int  fcc, delay, pending;           /* contador del frame counter y escritura de $4017 retrasada */
    u64  cycle;                         /* ciclos de CPU que el APU ya ejecutó (puede ir detrás de nes.cyc) */
    u64  deadline;                      /* ciclo en que algo visible desde fuera (IRQ, DMA del DMC) puede pasar */
    /* salida */
    double acc, acc_n, next, cps;       /* promedio entre muestras; ciclos por muestra */
    u8   mute;                  /* bits 0-4: silencia pulso 1, pulso 2, triángulo, ruido, DMC */
    u8   dmc_delay, dmc_next;   /* el bit del DMC escrito en $4015 (dmc_next) se aplica dmc_delay ciclos después */
    u8   dmc_implicit;          /* $4015 encendió el DMC justo antes de su reloj: en el próximo byte sale una DMA de un ciclo */
    u8   vrc6;                  /* el cartucho tiene el audio del VRC6 (estado en NES.vrc6) */
    double rate_mul;                    /* ajuste fino del ritmo (sincronía con el video) */
    double hp1, hp2, lp, in1, in2;      /* filtros del hardware */
    int16_t buf[APU_BUF]; int nbuf;
} Apu;

enum { BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8,
       BTN_UP = 16, BTN_DOWN = 32, BTN_LEFT = 64, BTN_RIGHT = 128 };

typedef struct NES {
    Cpu  cpu;
    u64  cyc;                   /* ciclos de CPU desde el reset */
    u8   ram[0x800];
    Cart cart;
    Ppu  ppu;
    Apu  apu;
    int  nmi_line, need_nmi, prev_need_nmi;   /* NMI: borde detectado por ciclo */
    int  run_irq, prev_run_irq;               /* IRQ visto en el penúltimo ciclo */
    int  irq_seen;              /* irq_line un ciclo antes (el IRQ llega al CPU con un ciclo de retraso) */
    int  irq_line;              /* bit 0 = mapper, 1 = APU (frame), 2 = DMC */
    int  stall;                 /* DMA pendiente: bit 0 = el DMC pidió un byte, bit 1 = escritura a $4014 (OAM DMA, página en oam_page), bit 2 = la del DMC ya no está detenida (sobrante de un aborto); el CPU los cumple en su próxima lectura */
    u8   oam_page;
    u8   bus;                   /* último valor en el bus de datos del CPU (lo que leen las direcciones sin nada) */
    u64  cyc_recomp;            /* ciclos ejecutados por código recompilado (estadística) */
    u32 *trace;                 /* NULL, o por cada byte de PRG: 0 = no ejecutado, 0x10000|dirección */
    u8   pad[4];                /* botones pulsados: jugadores 1-4 (3 y 4 solo con el Four Score) */
    u32  pad_shift[2];          /* registros de los puertos 1 y 2 (los bits que ya se leyeron se rellenan con 1) */
    u8   fourscore;             /* 1: Four Score (multitap de 4 jugadores) en los dos puertos */
    u8   zapper_on;             /* 1: Zapper en el puerto 2 ($4017) en lugar de un control */
    int  zap_x, zap_y;          /* posición de la mira en la imagen (fuera de 0-255 / 0-239: apunta fuera de la pantalla) */
    u8   zap_trigger;           /* gatillo apretado */
    u8   zap_radius;            /* radio de visión del cañón en pixeles (0 = 4) */
    int  pad_strobe;
    int  ppu_eager;             /* 1: la PPU avanza en cada ciclo (MMC3, DMA de OAM); 0: solo cuando hace falta */
    u64  dma_end;               /* ciclo en que terminó la última DMA (la lectura del CPU que detuvo empieza ahí). Al final: en
                                   medio desplaza campos que se usan en cada ciclo y el emulador va más lento */
    u8   dmc_left;              /* DMA del DMC abortada por $4015 que sigue pendiente: bit 0 = hay, bit 1 = ya no está detenida.
                                   Vuelve a stall cuando el DMC se enciende */
    u64  dmc_get_cyc;           /* ciclo en que la última DMA del DMC leyó su byte (en ese ciclo no sale otra) */
    u64  dmc_implicit_cyc;      /* ciclo en que una DMA del aborto implícito puede detener al CPU (solo ese) */
    u64  pad_pend;              /* $4016 = 1 escrito en un ciclo put: el control se latchea en el get siguiente (ciclo
                                   guardado aquí) si el strobe sigue en 1; 0 = nada pendiente */
    Vrc6 vrc6;                  /* audio del VRC6 (al final: en medio desplazaría campos de cada ciclo) */
} NES;

/* iNES: 0 si todo bien; mensaje en *err si falla */
int  nes_load(NES *n, const u8 *data, size_t len, const char **err);
void nes_free(NES *n);
void nes_reset(NES *n);

/* Colores: paletas de 64 entradas ARGB. kind: 0 predeterminada, 1 grises, 2 saturada */
void ppu_make_palette(int kind, u32 out[64]);
int  ppu_load_palette(const u8 *data, size_t len, u32 out[64]);   /* archivo .pal (192 bytes RGB o más) */
u32  ppu_argb(const u32 pal[64], u16 px);                          /* un pixel de fb, con énfasis */

/* Estados guardados: solo los lee la misma versión con la misma ROM. malloc; NULL si falla. */
u8  *nes_state_save(NES *n, size_t *size);
int  nes_state_load(NES *n, const u8 *data, size_t size);          /* 1 si se cargó */
int  nes_trace_load(NES *n, const char *path);                     /* suma líneas de una traza anterior */
extern int nes_force_interp;                                       /* 1: nada de código recompilado */

/* RAM con batería ($6000-$7FFF): solo si la cabecera la declara. 1 = hecho, 0 = no aplica o falló. */
int  nes_sram_load(NES *n, const char *path);
int  nes_sram_save(NES *n, const char *path);
int  nes_sram_dirty(const NES *n);

/* Bus sin efecto de reloj (DMA, depuración) */
u8   nes_read(NES *n, u16 addr);
void nes_write(NES *n, u16 addr, u8 v);
/* Acceso del CPU: avanza un ciclo de la consola (3 puntos de PPU y 1 de APU) */
u8   nes_cpu_read(NES *n, u16 addr);
void nes_cpu_write(NES *n, u16 addr, u8 v);

/* CPU: ejecuta una instrucción (y la interrupción que le siga); devuelve los ciclos */
int  cpu_step(NES *n);

/* Mappers */
int  mapper_supported(int id);
void mapper_reset(NES *n);
void mapper_remap(Cart *c);
void mapper_ppu_ctrl(NES *n, u8 v);              /* MMC5: tamaño de los sprites */
void mapper_scanline(NES *n, int line);          /* MMC5: la PPU empieza una línea con dibujo */
void mapper_chr_latch(Cart *c, u16 addr);        /* MMC2/MMC4: tras leer un patrón */                      /* recalcula prg_map tras un cambio de bancos */
u8   mapper_cpu_read(NES *n, u16 addr);          /* $4020-$FFFF */
long mapper_prg_offset(const Cart *c, u16 addr);   /* rápida: usa prg_map */
/* Consultas para el recompilador */
typedef struct { u16 start; u32 size; size_t off; int fixed; } PrgWin;
int    mapper_prg_window(const Cart *c, u16 addr, PrgWin *w);
u16    mapper_prg_canon(const Cart *c, u16 addr);
size_t mapper_prg_granule(const Cart *c);
void   nes_cpu_tick(NES *n);
void   nes_cpu_ticks(NES *n, int k);            /* k ciclos seguidos sin acceso al bus */                    /* un ciclo sin acceso al bus (captura de ROM en código recompilado) */
   /* desplazamiento en PRG con el mapeo actual; -1 si no es PRG */
void mapper_cpu_write(NES *n, u16 addr, u8 v);
void mapper_a12(NES *n, int a12);               /* nivel de A12 del bus de la PPU */
u8   mapper_ppu_read(NES *n, u16 addr);          /* $0000-$1FFF */
void mapper_ppu_write(NES *n, u16 addr, u8 v);

/* PPU 2C02 */
void ppu_reset(NES *n);
void ppu_tick(NES *n);                           /* un punto de reloj de la PPU */
void ppu_sync(NES *n, u64 target);               /* ejecuta puntos hasta que clock == target */
void ppu_flush(NES *n);
int  ppu_light_at(const NES *n, int x, int y);    /* ¿hay luz en (x, y)? (para el Zapper) */                          /* al día con el CPU (3 × ciclos) */
u8   ppu_reg_read(NES *n, u16 addr);
void ppu_reg_write(NES *n, u16 addr, u8 v);

/* APU 2A03 */
#define APU_RATE 44100
void apu_reset(NES *n);
void apu_sync(NES *n);                           /* ejecuta lo pendiente hasta el ciclo actual de la consola */
void apu_dmc_dma_done(NES *n, u8 v);             /* el DMA del DMC entregó su byte */
u16  apu_dmc_addr(const NES *n);                 /* dirección que el DMC va a leer */
void apu_vrc6_write(NES *n, u16 reg, u8 v);      /* $9000-$9003, $A000-$A002, $B000-$B002 (ya decodificados) */
int  apu_dmc_dma_live(NES *n);                   /* ¿sigue en pie la DMA del DMC? (no la abortó $4015) */
void apu_check(NES *n);                          /* al final de cada ciclo de CPU: avanza solo si hay un evento visible */
u8   apu_read(NES *n, u16 addr);                 /* $4015 */
void apu_write(NES *n, u16 addr, u8 v);          /* $4000-$4013, $4015, $4017 */
void apu_mmc5_write(NES *n, u16 addr, u8 v);     /* $5000-$5007, $5010, $5011 y $5015 */
void apu_mmc5_pcm(NES *n, u8 v);                 /* el CPU leyó un byte de $8000-$BFFF con el PCM en modo lectura */
u8   apu_mmc5_status(NES *n);                    /* lectura de $5015 */
int  apu_take(NES *n, int16_t *dst, int max);    /* saca muestras mono de 44100 Hz */

/* Traza de ejecución (para guiar al recompilador) */
void nes_trace_start(NES *n);
void nes_trace_note(NES *n, u16 pc);
int  nes_trace_write(const NES *n, const char *path);     /* líneas "OFFSET:DIRECCION" en hexadecimal */

/* Consola: un instante de CPU más PPU, o un cuadro completo */
int  nes_step(NES *n);
void nes_run_frame(NES *n);

#endif
