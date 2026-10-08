/*
 * Estado completo de un Game Boy (DMG) para el runtime de GBRecomp.
 *
 * Todo el estado vive en un struct GB y cada función recibe un puntero a él:
 * así la prueba diferencial puede tener dos máquinas en el mismo proceso (una
 * con código recompilado y otra con el intérprete) sobre el mismo runtime.
 *
 * Temporización: g->cyc cuenta T-ciclos del CPU (siempre múltiplo de 4). El
 * hardware (timer, PPU, APU, serial, DMA) es "perezoso": solo avanza cuando
 * alguien lo observa o lo modifica (gb_sync), o cuando el CPU llega a
 * g->next_event, el primer instante en que el hardware podría cambiar algo que
 * el CPU ve sin acceder al bus (en la práctica, IF).
 */
#ifndef GB_H
#define GB_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;

#define GB_CPU_HZ        4194304
#define GB_CYCLES_FRAME  70224
#define GB_SCREEN_W      160
#define GB_SCREEN_H      144
#define GB_SAMPLE_RATE   48000

/* Bits de IF / IE */
#define INT_VBLANK 0x01
#define INT_STAT   0x02
#define INT_TIMER  0x04
#define INT_SERIAL 0x08
#define INT_JOYPAD 0x10

/* Botones (mismo formato que PADF_* de hardware.inc) */
#define PAD_A      0x01
#define PAD_B      0x02
#define PAD_SELECT 0x04
#define PAD_START  0x08
#define PAD_RIGHT  0x10
#define PAD_LEFT   0x20
#define PAD_UP     0x40
#define PAD_DOWN   0x80

/* Índices dentro de io[] ($FF00 + n) */
enum {
    IO_P1 = 0x00, IO_SB = 0x01, IO_SC = 0x02, IO_DIV = 0x04, IO_TIMA = 0x05,
    IO_TMA = 0x06, IO_TAC = 0x07, IO_IF = 0x0F,
    IO_LCDC = 0x40, IO_STAT = 0x41, IO_SCY = 0x42, IO_SCX = 0x43, IO_LY = 0x44,
    IO_LYC = 0x45, IO_DMA = 0x46, IO_BGP = 0x47, IO_OBP0 = 0x48, IO_OBP1 = 0x49,
    IO_WY = 0x4A, IO_WX = 0x4B,
};

typedef struct {
    u8 a, f, b, c, d, e, h, l;
    u16 sp, pc;
    u8 ime;
    u8 ei_pend;     /* EI: IME se activa después de la instrucción siguiente */
    u8 halted;
    u8 halt_bug;    /* la siguiente lectura de opcode no incrementa PC */
    u8 stopped;
    u8 locked;      /* opcode ilegal: el CPU se cuelga para siempre */
} Cpu;

typedef enum {
    MBC_NONE, MBC_1, MBC_2, MBC_3, MBC_5,
} MbcType;

typedef struct {
    MbcType type;
    int has_ram, has_battery, has_rtc;
    u8 ram_enable;
    u16 rom_bank;       /* registro de banco de ROM (sin enmascarar) */
    u8 ram_bank;        /* MBC1: registro de 2 bits; MBC3/5: banco de RAM o RTC */
    u8 mode;            /* MBC1: modo de banco */
    u32 bank0, bankx;   /* bancos de ROM efectivos en $0000 y $4000 */
    int sram_bank;      /* banco de RAM efectivo, -1 si no hay acceso */
    /* RTC del MBC3 (fase 4: por ahora solo registros) */
    u8 rtc[5], rtc_latched[5], rtc_latch_prev;
    int rtc_select;     /* -1 o 0..4 si $A000 apunta a un registro del RTC */
} Mbc;

typedef enum { TIMA_NORMAL, TIMA_OVERFLOW, TIMA_RELOADING } TimaState;

typedef struct {
    u16 sys;            /* contador interno de 16 bits; DIV = sys >> 8 */
    u8 tima, tma, tac;
    TimaState state;
} Timer;

typedef struct {
    u32 dot;            /* punto dentro de la línea (0..455) */
    u8 ly;              /* línea que procesa la PPU */
    u8 ly_reg;          /* valor de LY visible (difiere en la línea 153) */
    u8 mode;            /* modo visible en STAT */
    u32 mode3_end;      /* punto en que termina el modo 3 de esta línea */
    u8 stat_line;       /* línea de interrupción STAT (para el flanco) */
    u8 window_line;     /* contador interno de líneas de la ventana */
    u8 window_active;   /* WY coincidió con LY en este cuadro */
    u8 lcd_first_line;  /* primera línea tras encender el LCD (sin modo 2) */
    u8 sprites[10];     /* índices de OAM elegidos para la línea */
    u8 nsprites;
    u8 *back;           /* cuadro en construcción */
    u8 buf[2][GB_SCREEN_W * GB_SCREEN_H];
    int front;          /* índice de buf con el último cuadro completo */
    u64 frames;         /* cuadros completos (VBlank) */
} Ppu;

typedef struct {
    int on, dac;
    int length, length_enable;
    int timer, freq;
    int volume, env_period, env_timer, env_up;
    int duty, step;
    int sweep_period, sweep_negate, sweep_shift, sweep_timer, sweep_enabled, shadow_freq;
    int sweep_negated;  /* se calculó con resta desde el último disparo */
    int position, volume_code;
    u8 sample_buffer;
    u16 lfsr;
    int clock_shift, width7, divisor;
} ApuChannel;

#define APU_RING 16384  /* pares estéreo */

typedef struct {
    u8 regs[0x30];      /* $FF10-$FF3F */
    int power;
    ApuChannel ch[4];
    int seq_step;
    double sample_acc;
    double sum[4];
    double sum_cycles;
    float hp_l, hp_r;
    /* Cola de muestras estéreo intercaladas para la plataforma */
    float ring[APU_RING * 2];
    u32 ring_head, ring_tail;
    int enabled;        /* 0 = no generar muestras (pruebas sin audio) */
} Apu;

typedef struct {
    int active;         /* hay DMA programado o en curso (bus lento) */
    int running;        /* copiando: OAM y el bus de origen ocupados */
    u8 source;          /* byte alto del origen */
    u16 index;          /* siguiente byte a copiar (0..159) */
    int start_delay;    /* M-ciclos antes de empezar a copiar */
    u8 pending_source;  /* reinicio solicitado mientras otra corría */
    int pending_delay;
    u8 last_byte;       /* lo que hay en el bus de origen */
} Dma;

typedef struct {
    int active;
    int bits_left;      /* bits por transferir (uno por flanco del reloj) */
    u16 last_sys;       /* contador del timer en el M-ciclo anterior */
    u8 *out;            /* bytes enviados por el juego (para pruebas) */
    size_t out_len, out_cap;
} Serial;

typedef struct GB GB;

/* Función recompilada: ejecuta desde g->cpu.pc y regresa al despachador */
typedef void (*RecompFn)(GB *g);

struct GB {
    Cpu cpu;
    u64 cyc;            /* T-ciclos ejecutados por el CPU */
    u64 hw_cyc;         /* hasta dónde avanzó el hardware */
    u64 next_event;     /* el CPU debe llamar a gb_check al llegar aquí */
    u64 stop_at;        /* fin de la corrida actual (gb_run) */

    /* Cartucho */
    const u8 *rom;
    u32 rom_size, rom_banks;
    u8 *sram;
    u32 sram_size;
    int sram_dirty;
    Mbc mbc;

    /* Memoria interna */
    u8 vram[0x2000];
    u8 wram[0x2000];
    u8 oam[0xA0];
    u8 hram[0x7F];
    u8 io[0x80];
    u8 ie;

    /* Mapa de lectura rápida: puntero a la página de 256 bytes, o NULL si
     * la página tiene efectos (I/O, VRAM, OAM, MBC, DMA en curso). */
    const u8 *rpage[256];
    u8 *wpage[256];

    Timer timer;
    Ppu ppu;
    Apu apu;
    Dma dma;
    Serial serial;
    u8 pad;             /* botones presionados (PAD_*) */
    u8 p1_lines;        /* líneas de entrada de P1 en el último cálculo */

    /* Opciones */
    int block_vram;     /* respetar el bloqueo de VRAM/OAM por modo de PPU */

    /* Código recompilado (NULL = solo intérprete) */
    RecompFn (*recomp_lookup)(GB *g, u16 pc);

    /* Métricas */
    u64 instr_interp;   /* instrucciones ejecutadas por el intérprete */
    u64 instr_recomp;   /* instrucciones ejecutadas por código recompilado */

    /* Depuración: se ejecutó LD B,B (fin de las pruebas de Mooneye) */
    int ld_bb_hit;
    /* Gancho opcional antes de cada instrucción del intérprete */
    void (*trace)(GB *g);
};

/* --- Ciclo de vida --- */

/* Prepara la máquina con la ROM dada (no se copia: debe seguir viva).
 * Devuelve 0 si todo bien o un mensaje de error. */
const char *gb_init(GB *g, const u8 *rom, u32 size);
void gb_free(GB *g);

/* Ejecuta hasta que g->cyc >= hasta (en un límite de instrucción). */
void gb_run(GB *g, u64 hasta);

/* Ejecuta un cuadro (70224 ciclos) */
void gb_run_frame(GB *g);

/* Último cuadro completo: 160×144 bytes, tonos 0 (claro) a 3 (oscuro). */
const u8 *gb_framebuffer(const GB *g);

/* Cambia los botones presionados (puede disparar la interrupción de joypad) */
void gb_set_pad(GB *g, u8 pad);

/* Saca hasta max pares estéreo de la cola de audio. */
int gb_audio_pull(GB *g, float *out, int max);

/* --- Usado por el CPU (intérprete y código recompilado) --- */

/* Avanza el hardware hasta g->cyc */
void gb_sync(GB *g);

/* Llamado cuando g->cyc >= g->next_event, en un límite de instrucción con
 * g->cpu.pc ya apuntando a la siguiente. Devuelve 1 si el código recompilado
 * debe regresar al despachador (interrupción por atender, fin de corrida). */
int gb_check(GB *g);

/* HALT (ya con pc después del opcode) */
void gb_halt(GB *g);
/* STOP */
void gb_stop(GB *g);
/* Opcode ilegal */
void gb_lockup(GB *g);

u8   gb_read_slow(GB *g, u16 addr);
void gb_write_slow(GB *g, u16 addr, u8 value);

/* Lectura y escritura sin consumir ciclos (depuración, verificación) */
u8   gb_peek(GB *g, u16 addr);

/* Intérprete: ejecuta una instrucción (interp.c) */
void gb_interp_step(GB *g);

/* --- Subsistemas (internos del runtime) --- */
void mbc_init(GB *g);
void mbc_write(GB *g, u16 addr, u8 value);
u8   mbc_sram_read(GB *g, u16 addr);
void mbc_sram_write(GB *g, u16 addr, u8 value);
void gb_remap(GB *g);

void timer_reset(GB *g);
void timer_mcycle(GB *g);
u8   timer_read(GB *g, u8 reg);
void timer_write(GB *g, u8 reg, u8 value);
u64  timer_next_event(GB *g);

void ppu_reset(GB *g);
void ppu_mcycle(GB *g);
void ppu_write(GB *g, u8 reg, u8 value);
u8   ppu_read(GB *g, u8 reg);
u64  ppu_next_event(GB *g);
int  ppu_vram_blocked(GB *g);
int  ppu_oam_blocked(GB *g);

void apu_reset(GB *g);
void apu_mcycle(GB *g);
void apu_div_event(GB *g);
u8   apu_read(GB *g, u16 addr);
void apu_write(GB *g, u16 addr, u8 value);

#define IF_REG(g) ((g)->io[IO_IF])

#endif
