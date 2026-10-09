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
#define SGB_SCREEN_W     256    /* imagen de la SNES con el Super Game Boy */
#define SGB_SCREEN_H     224
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
    u8 halt_first;  /* primer M-ciclo tras HALT (muestrea IF al inicio) */
    u8 stopped;
    u8 locked;      /* opcode ilegal: el CPU se cuelga para siempre */
} Cpu;

typedef enum {
    MBC_NONE, MBC_1, MBC_2, MBC_3, MBC_5,
} MbcType;

typedef struct {
    MbcType type;
    int has_ram, has_battery, has_rtc;
    int multicart;      /* MBC1M: los bits altos del banco se desplazan 4, no 5 */
    u8 ram_enable;
    u16 rom_bank;       /* registro de banco de ROM (sin enmascarar) */
    u8 ram_bank;        /* MBC1: registro de 2 bits; MBC3/5: banco de RAM o RTC */
    u8 mode;            /* MBC1: modo de banco */
    u32 bank0, bankx;   /* bancos de ROM efectivos en $0000 y $4000 */
    int sram_bank;      /* banco de RAM efectivo, -1 si no hay acceso */
    /* RTC del MBC3: avanza con los ciclos emulados (determinista) */
    u8 rtc[5], rtc_latched[5], rtc_latch_prev;  /* S, M, H, DL, DH */
    u64 rtc_last_rt;    /* tiempo real (gb_realtime) hasta el que ya se contó */
    u64 rtc_sub;        /* tiempo real acumulado hacia el siguiente segundo */
    int rtc_select;     /* -1 o 0..4 si $A000 apunta a un registro del RTC */
} Mbc;

typedef enum { TIMA_NORMAL, TIMA_OVERFLOW, TIMA_RELOADING } TimaState;

typedef struct {
    u16 sys;            /* contador interno de 16 bits; DIV = sys >> 8 */
    u8 tima, tma, tac;
    TimaState state;
} Timer;

typedef struct {
    u8 pixel;           /* color 0-3 antes de la paleta (0 = transparente) */
    u8 attr;            /* atributos del sprite (paleta, banco, prioridad) */
    u8 index;           /* índice de OAM (prioridad en CGB) */
} ObjPixel;

typedef struct {
    u64 cyc;            /* hasta qué ciclo avanzó la PPU (lleva su propio reloj) */
    u32 dot;            /* punto dentro de la línea */
    u32 line_len;       /* puntos de la línea actual (la primera tras encender es más corta) */
    u8 ly;              /* línea que procesa la PPU */
    u8 ly_reg;          /* valor visible de LY */
    u8 mode;            /* modo visible en STAT */
    s8 mode_irq;        /* fuente de modo para la interrupción STAT (-1: ninguna) */
    int16_t ly_cmp;     /* LY para la comparación con LYC (-1: sin comparar) */
    u8 lyc_line;        /* LY == LYC (pegajoso mientras ly_cmp = -1) */
    u8 stat_line;       /* línea de interrupción STAT (para el flanco) */
    u8 lcd_first_line;  /* primera línea tras encender el LCD */
    u16 hblank_irq_dot; /* punto en que se evalúa la interrupción de HBlank */
    u8 oam_rb, oam_wb;  /* OAM bloqueada para leer / escribir desde el CPU */
    u8 vram_rb, vram_wb;

    /* Ventana */
    u8 wy_triggered;    /* WY coincidió con LY en este cuadro */
    u8 wy_pending;      /* revisar WY en el siguiente punto de la rejilla de 8 */
    u8 wy_mod;          /* puntos desde que se encendió el LCD, módulo 256 */
    u8 wx_triggered;    /* el fetcher está en la ventana */
    u8 window_fetching; /* se acaba de activar la ventana */
    u8 insert_bg_pixel; /* falla del DMG: se intercala un píxel de fondo */
    u8 wx_just_changed; /* WX se escribió en este punto */
    u8 no_win_glitch;   /* se apagó la ventana mientras se leía: sin falla de inserción */
    u8 window_tile_x;
    int16_t window_y;   /* línea interna de la ventana (-1 al empezar el cuadro) */

    /* Sprites de la línea, en orden descendente de X (el siguiente es el último) */
    u8 objs[10], objs_x[10], objs_y[10];
    u8 n_objs;

    /* Modo 3 */
    u8 rendering;       /* el modo 3 está dibujando */
    u8 pos;             /* posición en la línea (con signo de 8 bits: -16..159) */
    u8 lcd_x;           /* columna de pantalla */
    u8 resume;          /* el punto siguiente continúa tras sprite o ventana */
    u8 bg_fifo[16];
    u8 bg_attr[16];     /* CGB: atributos del mosaico de cada píxel */
    u8 bg_head, bg_size;
    ObjPixel obj_fifo[8];
    u8 obj_head, obj_size;
    u8 fetch_state;     /* 0-5: mosaico, dato bajo y dato alto (2 puntos c/u); 6: empujar */
    u16 fetch_addr;
    u8 fetch_tile, fetch_attr, fetch_data[2];
    u8 obj_state;       /* lectura de sprite en curso (ver ppu.c) */
    u8 obj_tile, obj_flags, obj_lo;

    u16 *back;          /* cuadro en construcción */
    /* Cuadros: en DMG cada valor es un tono 0-3; en CGB, color RGB555 */
    u16 buf[2][GB_SCREEN_W * GB_SCREEN_H];
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
    u8 sending;         /* byte que se está enviando (SB al empezar) */
    u8 *out;            /* bytes enviados por el juego (para pruebas) */
    size_t out_len, out_cap;
} Serial;

/* Super Game Boy: lo que hace la SNES con los paquetes que manda el juego */
typedef struct {
    /* Recepción de paquetes por P1 (bits P14/P15) */
    u8 packet[7 * 16];  /* un comando ocupa hasta 7 paquetes de 16 bytes */
    int bits;           /* bits recibidos del paquete actual */
    int receiving;      /* entre el pulso de reinicio y el bit de fin */
    int ready_for_pulse;    /* hubo una escritura con ambos en alto */
    int packets;        /* paquetes ya recibidos del comando */
    int length;         /* paquetes que ocupa el comando */
    u8 last_p1;         /* bits 4-5 de la última escritura en P1 */
    /* Multijugador (MLT_REQ) */
    u8 players, player;
    /* Transferencia de VRAM pendiente (*_TRN): se toma de la pantalla */
    u8 trn_cmd, trn_arg;
    int trn_wait;       /* cuadros que faltan */
    /* Colores */
    u16 pal[4][4];      /* paletas activas (el color 0 de todas es pal[0][0]) */
    u8 attr[20 * 18];   /* paleta de cada mosaico de 8x8 de la pantalla */
    u8 mask;            /* MASK_EN: 0 normal, 1 congelada, 2 negro, 3 color 0 */
    u16 sys_pal[512 * 4];   /* paletas del sistema (PAL_TRN) */
    u8 atf[45 * 90];        /* archivos de atributos (ATTR_TRN) */
    /* Borde */
    u8 border_chr[256 * 32];    /* mosaicos de 4 bits por píxel (formato SNES) */
    u16 border_map[32 * 28];
    u16 border_pal[4][16];      /* paletas 4-7 */
    u16 screen[SGB_SCREEN_W * SGB_SCREEN_H];   /* imagen final en RGB555 */
} Sgb;

typedef struct GB GB;

/* Función recompilada: ejecuta desde g->cpu.pc y regresa al despachador */
typedef void (*RecompFn)(GB *g);

struct GB {
    Cpu cpu;
    u64 cyc;            /* T-ciclos ejecutados por el CPU */
    u64 hw_cyc;         /* hasta dónde avanzó el hardware (ciclos del CPU) */
    u64 rt;             /* tiempo real del hardware en puntos (4,19 MHz): la PPU y
                         * el APU van a este ritmo aunque el CPU vaya al doble */
    u64 next_event;     /* el CPU debe llamar a gb_check al llegar aquí */
    u64 stop_at;        /* fin de la corrida actual (gb_run) */

    /* Cartucho */
    const u8 *rom;
    u32 rom_size, rom_banks;
    u8 *sram;
    u32 sram_size;
    int sram_dirty;
    Mbc mbc;

    /* Modelo: 0 = DMG, 1 = Game Boy Color (en modo CGB) */
    int cgb;
    /* Hardware de Game Boy Color (temporización de la PPU, framebuffer en
     * RGB555). Con cgb = 0 es el modo de compatibilidad: un juego de DMG en
     * un GBC, con las paletas que eligió la boot ROM. */
    int cgb_hw;
    int sgb;            /* Super Game Boy (siempre con cgb = 0) */
    Sgb sgbs;

    /* Memoria interna (en DMG solo se usan el banco 0 de VRAM y 8 KB de WRAM) */
    u8 vram[0x4000];    /* 2 bancos de 8 KB */
    u8 wram[0x8000];    /* 8 bancos de 4 KB:  el 0,  el elegido */
    u8 vram_bank;       /* VBK (CGB) */
    u8 wram_bank;       /* SVBK (CGB): 1-7 */
    u8 oam[0xA0];
    u8 hram[0x7F];
    u8 io[0x80];
    u8 ie;

    /* Mapa de lectura rápida: puntero a la página de 256 bytes, o NULL si
     * la página tiene efectos (I/O, VRAM, OAM, MBC, DMA en curso). */
    const u8 *rpage[256];
    u8 *wpage[256];

    /* Game Boy Color */
    u8 bg_pal[64], obj_pal[64];   /* 8 paletas x 4 colores x 2 bytes (RGB555) */
    u8 bcps, ocps;      /* índice y autoincremento de las paletas */
    u8 key1;            /* bit 0: cambio de velocidad pedido */
    int double_speed;
    u8 opri;            /* prioridad de sprites: 0 = por índice de OAM */
    u8 undoc[4];        /* registros sin documentar - */
    u16 hdma_src, hdma_dst;
    u8 hdma_left;       /* bloques de 16 bytes que faltan, menos 1 */
    int hdma_hblank;    /* DMA por HBlank en curso */
    u32 hdma_stall;     /* ciclos que el CPU debe esperar por la DMA */

    Timer timer;
    Ppu ppu;
    Apu apu;
    Dma dma;
    Serial serial;
    u8 pad;             /* botones presionados (PAD_*) */
    u8 pad_sgb[3];      /* controles 2-4 del Super Game Boy (multijugador) */
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
    /* Gancho opcional: cada escritura del juego a un registro de sonido
     * ($FF10-$FF3F), en el instante g->rt (para grabar la música en VGM) */
    void (*apu_log)(GB *g, u16 addr, u8 value);

    /* Cable link: al terminar una transferencia con reloj interno, el byte
     * recibido es lo que devuelva link_xfer(g, link_user, enviado). Sin cable
     * (NULL) se recibe $FF, como con nada conectado. */
    u8 (*link_xfer)(GB *g, void *user, u8 sent);
    void *link_user;
};

/* --- Ciclo de vida --- */

/* Prepara la máquina con la ROM dada (no se copia: debe seguir viva).
 * Devuelve 0 si todo bien o un mensaje de error. */
const char *gb_init(GB *g, const u8 *rom, u32 size);

/* Igual, eligiendo el modelo: color automático según la cabecera, o forzado */
enum { GB_MODEL_AUTO = -1, GB_MODEL_DMG = 0, GB_MODEL_CGB = 1, GB_MODEL_SGB = 2 };
const char *gb_init_model(GB *g, const u8 *rom, u32 size, int model);

/* Modo de compatibilidad del GBC: cambia las paletas como si se hubiera
 * presionado una combinación al arrancar ("up", "up+a", "left+b", ...: las
 * 12 del GBC), o "auto" para la que elige la boot ROM. Devuelve 0 si el
 * nombre no existe o no es ese modo. */
int gb_compat_palette(GB *g, const char *combo);

/* PPU → HDMA: empieza un HBlank de una línea visible (CGB) */
void gb_hdma_hblank(GB *g);
void gb_free(GB *g);

/* Copia el estado de src en dst (que ya pasó por gb_init con la misma ROM).
 * dst conserva sus propios búferes, su recomp_lookup y su trace. */
void gb_copy_state(GB *dst, const GB *src);

/* Guardado de la RAM del cartucho y del RTC (save.c), en el formato de BGB y
 * VBA-M: la RAM tal cual y, si hay RTC, 48 bytes con sus registros y la hora.
 * `now` es la hora Unix actual, para adelantar el reloj al cargar.
 * Devuelven 1 si todo bien. */
int gb_has_battery(const GB *g);
/* En memoria: devuelve un búfer (malloc) con el contenido del .sav */
u8  *gb_save_serialize(GB *g, long long now, size_t *size);
int  gb_save_deserialize(GB *g, const u8 *data, size_t size, long long now);
int gb_save_write(GB *g, const char *path, long long now);
int gb_save_read(GB *g, const char *path, long long now);

/* Estado guardado: la máquina completa (solo para el mismo ejecutable y la
 * misma ROM). gb_state_load devuelve 0 si el estado no corresponde. */
u8  *gb_state_save(const GB *g, size_t *size);
int  gb_state_load(GB *g, const u8 *data, size_t size);

/* Ejecuta hasta que g->cyc >= hasta (en un límite de instrucción). */
void gb_run(GB *g, u64 hasta);

/* Ejecuta un cuadro de pantalla (gb_frame_cycles ciclos del CPU) */
void gb_run_frame(GB *g);

/* Ciclos del CPU que dura un cuadro de pantalla: GB_CYCLES_FRAME, o el doble
 * en doble velocidad (el CPU va al doble, la pantalla no) */
u64 gb_frame_cycles(const GB *g);

/* Tiempo real del hardware hasta el ciclo actual del CPU, en puntos de
 * 4,19 MHz (GB_CPU_HZ por segundo, a cualquier velocidad del CPU) */
u64 gb_realtime(const GB *g);

/* --- Cable link --- */

/* El otro extremo del cable completó un byte con su reloj interno: devuelve lo
 * que este Game Boy tenía en SB y, si esperaba con reloj externo (SC = $80),
 * recibe `in` y pide la interrupción serial. Si no esperaba, no recibe nada. */
u8 gb_serial_clock_in(GB *g, u8 in);

/* Dos Game Boy conectados por cable en el mismo proceso. Corren por tramos
 * cortos de tiempo real, y cuando uno completa un byte el otro se adelanta
 * hasta ese instante antes del intercambio. */
typedef struct {
    GB *gb[2];
    u64 base[2];        /* gb_realtime de cada uno al conectarlos */
    int busy;           /* adelantando al otro (evita llamadas anidadas) */
    u64 bytes;          /* bytes intercambiados */
} GbLink;

void gb_link_connect(GbLink *l, GB *a, GB *b);
void gb_link_disconnect(GbLink *l);
/* Un cuadro de pantalla de los dos */
void gb_link_run_frame(GbLink *l);

/* Último cuadro completo, 160×144: en DMG tonos 0 (claro) a 3 (oscuro); en
 * CGB colores RGB555 (5 bits por canal, rojo en los bits bajos). */
const u16 *gb_framebuffer(const GB *g);

/* SGB: imagen completa de 256×224 en RGB555, con el borde y el juego
 * coloreado al centro (en 48,40). NULL si no es un Super Game Boy. */
const u16 *gb_sgb_screen(const GB *g);

/* Cambia los botones presionados (puede disparar la interrupción de joypad) */
void gb_set_pad(GB *g, u8 pad);
/* Botones del control `player` (0-3). Los controles 2-4 solo existen en el
 * Super Game Boy, cuando el juego pide varios jugadores (MLT_REQ). */
void gb_set_pad_player(GB *g, int player, u8 pad);
/* Cuántos controles está leyendo el juego: 1, o 2 o 4 en el Super Game Boy */
int gb_players(const GB *g);

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
void mbc_rtc_advance(GB *g, u64 seconds);
void gb_remap(GB *g);

void timer_reset(GB *g);
void timer_mcycle(GB *g);
u8   timer_read(GB *g, u8 reg);
void timer_write(GB *g, u8 reg, u8 value);
u64  timer_next_event(GB *g);

void ppu_reset(GB *g);
void ppu_advance(GB *g, u64 until);   /* avanza la PPU hasta el ciclo `until` */
void ppu_write(GB *g, u8 reg, u8 value);
/* Escritura del CPU a un registro de la PPU en el M-ciclo que empieza en `s`:
 * cada registro surte efecto en su punto (ver ppu.c). */
void ppu_write_timed(GB *g, u8 reg, u8 value, u64 s);
u8   ppu_read(GB *g, u8 reg);
u64  ppu_next_event(GB *g);
int  ppu_vram_blocked(GB *g, int write);
int  ppu_oam_blocked(GB *g, int write);

void apu_reset(GB *g);
void apu_mcycle(GB *g, u32 dots);   /* avanza `dots` puntos de tiempo real */
void apu_div_event(GB *g);
u8   apu_read(GB *g, u16 addr);
void apu_write(GB *g, u16 addr, u8 value);

void compat_boot(GB *g);    /* paletas del modo de compatibilidad del GBC */
int  compat_combo(const GB *g);

void sgb_reset(GB *g);
void sgb_p1_write(GB *g, u8 value);     /* bits 4-5 escritos en P1 */
int  sgb_joypad_id(GB *g);              /* jugador actual (MLT_REQ) */
void sgb_frame(GB *g, const u16 *shades);   /* cuadro terminado (tonos 0-3) */

#define IF_REG(g) ((g)->io[IO_IF])

#endif
