/* Cartucho iNES, reset y bus del CPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

int nes_load(NES *n, const u8 *d, size_t len, const char **err)
{
    Cart *c = &n->cart;
    memset(n, 0, sizeof *n);
    if (len < 16 || memcmp(d, "NES\x1A", 4)) { if (err) *err = "no es iNES"; return -1; }
    size_t prg = (size_t)d[4] * 0x4000, chr = (size_t)d[5] * 0x2000;
    int f6 = d[6], f7 = d[7];
    c->nes2 = (f7 & 0x0C) == 8;
    c->mapper = (f6 >> 4) | (f7 & 0xF0);
    c->battery = (f6 & 2) != 0;
    c->mirroring = (f6 & 8) ? MIRROR_FOUR : (f6 & 1) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    size_t off = 16 + ((f6 & 4) ? 512 : 0);
    if (!prg || prg & (prg - 1)) { if (err) *err = "tamaño de PRG no soportado (no es potencia de 2)"; return -1; }
    if (off + prg + chr > len)   { if (err) *err = "ROM truncada"; return -1; }
    if (!mapper_supported(c->mapper)) { if (err) *err = "mapper no soportado"; return -1; }
    c->prg = malloc(prg); c->prg_size = prg;
    memcpy(c->prg, d + off, prg);
    if (f6 & 4) memcpy(c->prg_ram + 0x1000, d + 16, 512);          /* el 'trainer' va a $7000-$71FF */
    if (chr) { c->chr = malloc(chr); c->chr_size = chr; memcpy(c->chr, d + off + prg, chr); }
    else     { c->chr = calloc(1, 0x2000); c->chr_size = 0x2000; c->chr_is_ram = 1; }
    nes_reset(n);
    return 0;
}

void nes_free(NES *n)
{
    free(n->trace); n->trace = NULL;
    free(n->cart.prg); free(n->cart.chr);
    n->cart.prg = n->cart.chr = NULL;
}

/* Puntos de PPU antes del acceso dentro de un ciclo de CPU (el resto, después).
 * Se ajustan con las pruebas de vblank/NMI de Blargg. */
#ifndef PPU_PRE_READ
#define PPU_PRE_READ  1
#endif
#ifndef PPU_PRE_WRITE
#define PPU_PRE_WRITE 2
#endif

/* La PPU es perezosa: no avanza en cada ciclo de CPU sino cuando algo la toca (registros, escrituras al
 * mapper, DMA) o cuando va a pasar algo que se ve desde fuera (bandera de vblank, fin de cuadro).
 * Con el MMC3 (su IRQ depende de las lecturas de la PPU) y durante el DMA de OAM avanza en cada ciclo.
 * Dentro de un ciclo de CPU, antes del acceso van PPU_PRE_* puntos y el resto después. */
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

static inline void cycle_begin(NES *n, int wr)
{
    if (n->ppu_eager) ppu_sync(n, 3 * n->cyc + (wr ? PPU_PRE_WRITE : PPU_PRE_READ));
}

static inline void cycle_end(NES *n, int wr)
{
    (void)wr;
    /* NMI e IRQ se muestrean en la segunda fase del ciclo: antes del último punto de PPU. */
    u64 t3 = 3 * (n->cyc + 1);
    int sync = n->ppu_eager || n->ppu.event_dot <= t3;
    if (sync) ppu_sync(n, t3 - 1);
    apu_check(n);
    if (UNLIKELY(n->cart.irq_cycle <= n->cyc + 1)) { n->irq_line |= 1; n->cart.irq_cycle = ~0ull; }   /* FME-7 */
    n->prev_need_nmi = n->need_nmi;
    int line = (n->ppu.status & 0x80) && (n->ppu.ctrl & 0x80);
    if (line && !n->nmi_line) n->need_nmi = 1;
    n->nmi_line = line;
    n->prev_run_irq = n->run_irq;
    n->run_irq = ((n->irq_line & 1) || (n->irq_seen & 6)) && !(n->cpu.p & FLAG_I);   /* el IRQ del APU llega con un ciclo de retraso */
    n->irq_seen = n->irq_line;
    if (sync) ppu_sync(n, t3);
    n->cyc++;
}

/* ---- DMA del DMC ----
 * Cuando el DMC necesita un byte, el CPU se detiene en su próxima lectura (las escrituras no se pueden detener): el
 * ciclo de parada y los de "alineación" repiten la lectura que el CPU iba a hacer (con sus efectos en $2007,
 * $4015...), y en un ciclo "get" (par) se lee el byte del DMC. Son 3 o 4 ciclos según la alineación. Después el CPU
 * hace su lectura de verdad. Los registros de los controles ($4016/$4017) solo se leen en el ciclo de parada:
 * lecturas seguidas de la misma dirección no vuelven a dar pulso de reloj. */
#ifndef DMA_GET_PARITY
#define DMA_GET_PARITY 0
#endif
#ifndef OAM_GET_PARITY
#define OAM_GET_PARITY 1
#endif

static void dma_dummy(NES *n, u16 a, int do_read)
{
    cycle_begin(n, 0);
    if (do_read) {
        if (a >= 0x2000 && a < 0x4000) ppu_sync(n, 3 * n->cyc + PPU_PRE_READ);
        u8 v = nes_read(n, a);
        if (a != 0x4015) n->bus = v;                    /* leer $4015 no pone nada en el bus */
    }
    cycle_end(n, 0);
}

static void dma_run(NES *n, u16 a)
{
    int dmc = n->stall & 1, spr = (n->stall & 2) != 0;
    u8 page = n->oam_page;
    n->stall = 0;
    int was_eager = n->ppu_eager;
    n->ppu_eager = 1;                                    /* la OAM se llena mientras la PPU sigue */
    int ctrl = (a == 0x4016 || a == 0x4017);
    dma_dummy(n, a, 1);                                  /* ciclo de parada */
    int need_dummy = dmc, spr_i = 0, spr_val = 0, have = 0;
    while (dmc || spr) {
        if (n->stall & 1) { dmc = 1; need_dummy = 1; n->stall &= ~1; }       /* el DMC pide un byte en medio del OAM DMA */
        if (n->stall & 2) { n->stall &= ~2; page = n->oam_page; }               /* otra escritura a $4014: cambia la página */
        int get = ((n->cyc & 1) == (dmc ? DMA_GET_PARITY : OAM_GET_PARITY));
        if (get) {
            if (dmc && !need_dummy) {                    /* ciclo "get": el byte del DMC */
                cycle_begin(n, 0);
                u8 v = nes_read(n, apu_dmc_addr(n));
                n->bus = v;
                cycle_end(n, 0);
                apu_dmc_dma_done(n, v);
                dmc = 0;
            } else if (spr) {                            /* ciclo "get": lee un byte para la OAM */
                cycle_begin(n, 0);
                spr_val = nes_read(n, (u16)((page << 8) | spr_i));
                n->bus = (u8)spr_val;
                cycle_end(n, 0);
                have = 1;
            } else {
                dma_dummy(n, a, !ctrl);
                need_dummy = 0;
            }
        } else {
            if (spr && have && (!dmc || need_dummy)) {   /* ciclo "put": escribe en la OAM */
                cycle_begin(n, 1);
                n->ppu.oam[(u8)(n->ppu.oam_addr + spr_i)] = (u8)spr_val;
                cycle_end(n, 1);
                have = 0;
                if (++spr_i == 256) spr = 0;
                need_dummy = 0;
            } else {
                dma_dummy(n, a, !ctrl);                  /* alineación o ciclo "dummy" del DMC */
                need_dummy = 0;
            }
        }
    }
    n->ppu_eager = was_eager;
}

u8 nes_cpu_read(NES *n, u16 a)
{
    if (UNLIKELY(n->stall)) dma_run(n, a);
    if (UNLIKELY(n->ppu_eager)) cycle_begin(n, 0);
    u8 v;
    if (a < 0x2000) v = n->ram[a & 0x7FF];                                           /* lo más común */
    else if (a >= 0x8000) v = n->cart.prg_ptr[(a >> 13) & 3][a & 0x1FFF];
    else {
        if (a < 0x4000 || (a == 0x4017 && n->zapper_on)) ppu_sync(n, 3 * n->cyc + PPU_PRE_READ);   /* registros de la PPU y luz del Zapper */
        v = nes_read(n, a);
    }
    if (a != 0x4015) n->bus = v;                    /* leer $4015 no pone nada en el bus */
    cycle_end(n, 0);
    return v;
}

void nes_cpu_tick(NES *n)
{
    if (UNLIKELY(n->stall)) dma_run(n, 0x8000);
    if (UNLIKELY(n->ppu_eager)) cycle_begin(n, 0);
    cycle_end(n, 0);
}

/* k ciclos seguidos sin acceso al bus (capturas de ROM en el código recompilado). Si no hay ningún
 * evento de la PPU ni del APU en ellos, tras el primero las entradas de interrupción no cambian y los
 * demás se suman de golpe. */
void nes_cpu_ticks(NES *n, int k)
{
    if (k <= 1 || n->ppu_eager || n->stall) { while (k-- > 0) nes_cpu_tick(n); return; }
    cycle_end(n, 0);
    k--;
    /* Hay que ir ciclo a ciclo si: el APU pidió un DMA de DMC (el siguiente acceso lo cumple antes de seguir),
     * en el tercer punto del ciclo subió o bajó la bandera de vblank (el NMI se ve en el siguiente ciclo), o
     * habrá un evento de la PPU o del APU dentro de estos ciclos. */
    if (n->stall || n->run_irq != (((n->irq_line & 1) || (n->irq_seen & 6)) && !(n->cpu.p & FLAG_I)) ||
        (((n->ppu.status & 0x80) && (n->ppu.ctrl & 0x80)) != n->nmi_line) ||
        n->ppu.event_dot <= 3 * (n->cyc + (u64)k) || n->cyc + (u64)k >= n->apu.deadline ||
        n->cart.irq_cycle <= n->cyc + (u64)k) {
        while (k-- > 0) nes_cpu_tick(n);
        return;
    }
    n->cyc += (u64)k;
    n->prev_need_nmi = n->need_nmi;
    n->prev_run_irq = n->run_irq;
}

void nes_cpu_write(NES *n, u16 a, u8 v)
{
    cycle_begin(n, 1);
    /* registros de la PPU, DMA de OAM y registros del mapper (cambian CHR y espejado) */
    if ((a >= 0x2000 && a < 0x4000) || a == 0x4014 || a >= 0x8000 || (n->cart.low_regs && a >= 0x4020))
        ppu_sync(n, 3 * n->cyc + PPU_PRE_WRITE);
    n->bus = v;
    nes_write(n, a, v);
    cycle_end(n, 1);
}

void nes_reset(NES *n)
{
    Cpu *c = &n->cpu;
    c->a = c->x = c->y = 0;
    c->sp = 0xFD;
    c->p = FLAG_I | FLAG_U;
    c->jammed = 0;
    mapper_reset(n);
    c->pc = nes_read(n, 0xFFFC) | (nes_read(n, 0xFFFD) << 8);
    n->cyc = 0;
    n->nmi_line = n->need_nmi = n->prev_need_nmi = 0;
    n->run_irq = n->prev_run_irq = 0;
    n->irq_line = n->stall = 0;
    ppu_reset(n);
    apu_reset(n);
    n->ppu_eager = 0;
    for (int i = 0; i < 7; i++) { cycle_begin(n, 0); cycle_end(n, 0); }   /* secuencia de reset */
}

/* Lo que carga un puerto al hacer strobe. Control normal: 8 botones y luego unos. Four Score: jugador 1 (o 2), jugador
 * 3 (o 4) y un byte de firma (el bit 3 del primer puerto, el 2 del segundo), y luego unos. */
static u32 pad_latch(const NES *n, int port)
{
    if (!n->fourscore) return 0xFFFFFF00u | n->pad[port];
    u32 sig = port == 0 ? 0x08 : 0x04;
    return 0xFF000000u | (sig << 16) | ((u32)n->pad[port + 2] << 8) | n->pad[port];
}

u8 nes_read(NES *n, u16 a)
{
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a < 0x4000) return ppu_reg_read(n, a);
    if (a == 0x4017 && n->zapper_on) {                       /* Zapper: bit 3 = luz (0 = hay), bit 4 = gatillo */
        int light = ppu_light_at(n, n->zap_x, n->zap_y);
        return (u8)((n->bus & 0xE0) | (light ? 0 : 8) | (n->zap_trigger ? 0x10 : 0));
    }
    if (a == 0x4016 || a == 0x4017) {
        int i = a & 1;
        u8 bit = n->pad_shift[i] & 1;
        if (!n->pad_strobe) n->pad_shift[i] = (n->pad_shift[i] >> 1) | 0x80000000u;
        else n->pad_shift[i] = pad_latch(n, i);
        return bit | (n->bus & 0xE0);                        /* los bits 5-7 son el bus abierto (casi siempre $40) */
    }
    if (a == 0x4015) return (apu_read(n, a) & 0xDF) | (n->bus & 0x20);    /* el bit 5 es el bus abierto */
    if (a < 0x4020) return n->bus;                            /* $4000-$4014: solo escritura */
    if (a < 0x6000 && !n->cart.mmc5) return n->bus;          /* sin dispositivo */
    return mapper_cpu_read(n, a);
}

void nes_write(NES *n, u16 a, u8 v)
{
    if (a < 0x2000) n->ram[a & 0x7FF] = v;
    else if (a < 0x4000) ppu_reg_write(n, a, v);
    else if (a == 0x4014) { n->oam_page = v; n->stall |= 2; }   /* OAM DMA: se cumple en la próxima lectura del CPU */
    else if (a == 0x4016) {
        n->pad_strobe = v & 1;
        if (n->pad_strobe) { n->pad_shift[0] = pad_latch(n, 0); n->pad_shift[1] = pad_latch(n, 1); }
    }
    else if (a < 0x4020) apu_write(n, a, v);
    else mapper_cpu_write(n, a, v);
}

int nes_step(NES *n)
{
    return cpu_step(n);
}

int nes_force_interp;

#ifdef NES_RECOMP
#include "recomp.h"
/* Código recompilado donde lo hay; el intérprete en el resto (RAM, lo no descubierto) */
void nes_run_frame(NES *n)
{
    n->ppu.frame_done = 0;
    while (!n->ppu.frame_done) {
        RecompFn fn = nes_force_interp ? NULL : recomp_lookup(n, n->cpu.pc);
        if (fn && !n->cpu.jammed) { u64 c0 = n->cyc; fn(n); n->cyc_recomp += n->cyc - c0; }
        else cpu_step(n);
    }
    ppu_flush(n);
}
#else
void nes_run_frame(NES *n)
{
    n->ppu.frame_done = 0;
    while (!n->ppu.frame_done) nes_step(n);
    ppu_flush(n);
}
#endif

/* ---- RAM con batería ---- */

int nes_sram_load(NES *n, const char *path)
{
    if (!n->cart.battery) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t k = fread(n->cart.prg_ram, 1, sizeof n->cart.prg_ram, f);
    fclose(f);
    for (size_t i = k; i < sizeof n->cart.prg_ram; i++) n->cart.prg_ram[i] = 0;
    memcpy(n->cart.sram_saved, n->cart.prg_ram, sizeof n->cart.prg_ram);
    return k > 0;
}

int nes_sram_save(NES *n, const char *path)
{
    if (!n->cart.battery) return 0;
    /* se escribe a un temporal y se renombra: un corte a medias no destruye la partida */
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    size_t k = fwrite(n->cart.prg_ram, 1, sizeof n->cart.prg_ram, f);
    int ok = (fclose(f) == 0) && k == sizeof n->cart.prg_ram;
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return 0; }
    memcpy(n->cart.sram_saved, n->cart.prg_ram, sizeof n->cart.prg_ram);
    return 1;
}

int nes_sram_dirty(const NES *n)
{
    return n->cart.battery && memcmp(n->cart.sram_saved, n->cart.prg_ram, sizeof n->cart.prg_ram) != 0;
}

/* ---- Traza de ejecución ---- */

void nes_trace_start(NES *n)
{
    if (!n->trace) n->trace = calloc(n->cart.prg_size, sizeof *n->trace);
}

void nes_trace_note(NES *n, u16 pc)
{
    long off = mapper_prg_offset(&n->cart, pc);
    if (off >= 0 && !n->trace[off]) n->trace[off] = 0x10000u | pc;
}

int nes_trace_write(const NES *n, const char *path)
{
    if (!n->trace) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (size_t i = 0; i < n->cart.prg_size; i++)
        if (n->trace[i]) fprintf(f, "%06zX:%04X\n", i, n->trace[i] & 0xFFFF);
    return fclose(f) == 0;
}

/* Suma a la traza las líneas de un archivo anterior (para ir acumulando corridas) */
int nes_trace_load(NES *n, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f || !n->trace) { if (f) fclose(f); return 0; }
    char line[64];
    unsigned long off, addr;
    int k = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%lx:%lx", &off, &addr) == 2 && off < n->cart.prg_size && !n->trace[off]) {
            n->trace[off] = 0x10000u | (u32)addr; k++;
        }
    fclose(f);
    return k;
}

/* ---- Estados guardados ----
 * Cabecera: "NESSTATE", tamaño de NES, mapper, tamaños de PRG y CHR, ¿CHR RAM?, suma de PRG;
 * luego la estructura NES completa (con los punteros de la máquina actual al cargar) y la CHR RAM. */

static u64 fnv(const u8 *d, size_t n)
{
    u64 h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}

typedef struct { char magic[8]; u32 nes_size, mapper, prg, chr, chr_ram; u64 sum; } StateHead;

u8 *nes_state_save(NES *n, size_t *size)
{
    apu_sync(n);
    ppu_flush(n);
    const Cart *c = &n->cart;
    size_t extra = c->chr_is_ram ? c->chr_size : 0;
    size_t total = sizeof(StateHead) + sizeof(NES) + extra;
    u8 *b = malloc(total);
    if (!b) return NULL;
    StateHead h = { {'N','E','S','S','T','A','T','E'}, (u32)sizeof(NES), (u32)c->mapper,
                    (u32)c->prg_size, (u32)c->chr_size, (u32)c->chr_is_ram, fnv(c->prg, c->prg_size) };
    memcpy(b, &h, sizeof h);
    memcpy(b + sizeof h, n, sizeof(NES));
    if (extra) memcpy(b + sizeof h + sizeof(NES), c->chr, extra);
    *size = total;
    return b;
}

int nes_state_load(NES *n, const u8 *data, size_t size)
{
    Cart *c = &n->cart;
    StateHead h;
    if (size < sizeof h) return 0;
    memcpy(&h, data, sizeof h);
    size_t extra = c->chr_is_ram ? c->chr_size : 0;
    if (memcmp(h.magic, "NESSTATE", 8) || h.nes_size != sizeof(NES) || h.mapper != (u32)c->mapper ||
        h.prg != c->prg_size || h.chr != c->chr_size || h.chr_ram != (u32)c->chr_is_ram ||
        h.sum != fnv(c->prg, c->prg_size) || size != sizeof h + sizeof(NES) + extra) return 0;
    u8 *prg = c->prg, *chr = c->chr;
    u32 *trace = n->trace;
    u8 mute = n->apu.mute;
    memcpy(n, data + sizeof h, sizeof(NES));
    c->prg = prg; c->chr = chr; n->trace = trace;
    n->apu.mute = mute; n->apu.nbuf = 0;
    if (extra) memcpy(c->chr, data + sizeof h + sizeof(NES), extra);
    mapper_remap(c);                                 /* los punteros guardados eran de otra máquina */
    return 1;
}
