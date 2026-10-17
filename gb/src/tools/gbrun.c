/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: ciclos, el estado del CPU y del mapper del
 * cartucho y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de espera). Sirve para ver hasta dónde
 * llega un juego, como genrun, neorun, smsrun y nesrun. Las opciones comunes (--frames, --watch, --sram, --shot,
 * --screens, --wav, botones) son las de run_comun.h.
 *
 *   gbrun rom.gb [--frames N] [--model dmg|cgb|sgb] [--watch DIR ...] [--sram ARCHIVO] [boton:desde-hasta ...]
 *
 * Botones: a b select start right left up down (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la WRAM: $C000-$DFFF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la RAM del cartucho (y el reloj, si lo tiene) al empezar si existe y la guarda al terminar.
 * El sonido de --wav sale a 48 000 Hz.
 */
#include <time.h>

#include "gb.h"
#include "png.h"
#include "romload.h"
#include "run_comun.h"

static const char *cargar(void **pg, const Cli *c)
{
    GB *g = calloc(1, sizeof *g);
    if (!g) return "sin memoria";
    if (load_gb(g, c->pos[0], c->model)) { free(g); return "no se pudo cargar"; }
    g->apu.enabled = 1;                 /* el audio no se genera si nadie lo pide */
    *pg = g;
    return NULL;
}

static void liberar(void *g) { gb_free(g); free(g); }
static void poner_pad(void *g, unsigned p1, unsigned p2) { (void)p2; gb_set_pad(g, (u8)p1); }

/* Un cuadro. En el último, una instrucción por vez para el histograma de PC (gb_run para en cuanto pasa del límite) */
static void cuadro(void *p, RunCtx *r)
{
    GB *g = p;
    if (!r->ultimo) { gb_run_frame(g); return; }
    u64 fin = g->cyc + gb_frame_cycles(g);
    while (g->cyc < fin && !g->cpu.locked) {
        run_paso(r, g->cpu.pc);
        gb_run(g, g->cyc + 1);
    }
}

static const char *parado(void *g) { return ((GB *)g)->cpu.locked ? "el CPU se detuvo (opcode ilegal)" : NULL; }

/* Estéreo de 48 000 Hz en coma flotante → 16 bits */
static int audio(void *g, int16_t *dst, int max)
{
    static float f[4096 * 2];
    int n = gb_audio_pull(g, f, max > 4096 ? 4096 : max);
    for (int i = 0; i < n * 2; i++) {
        float v = f[i] * 32767.0f;
        dst[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
    }
    return n;
}

/* La imagen del cuadro (160 × 144) como PNG: tonos de gris en el DMG, el color real en el Game Boy Color */
static int captura(void *p, const char *path, int *w, int *h)
{
    static const unsigned char shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };
    const GB *g = p;
    static unsigned char rgb[GB_SCREEN_W * GB_SCREEN_H * 3];
    const u16 *fb = gb_framebuffer(g);
    for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) {
        u16 v = fb[i];
        for (int k = 0; k < 3; k++) {
            if (g->cgb_hw) { unsigned x = (v >> (5 * k)) & 31; rgb[i * 3 + k] = (unsigned char)((x << 3) | (x >> 2)); }
            else rgb[i * 3 + k] = shade[v & 3];
        }
    }
    *w = GB_SCREEN_W; *h = GB_SCREEN_H;
    return png_write_rgb(path, rgb, GB_SCREEN_W, GB_SCREEN_H);
}

/* La WRAM como la ve el CPU en $C000-$DFFF: el banco 0 y el elegido (en el DMG, el 1) */
static int ram(const void *p, unsigned off)
{
    const GB *g = p;
    if (off < 0x1000) return g->wram[off];
    unsigned bank = g->cgb_hw && g->wram_bank ? g->wram_bank : 1;
    return g->wram[bank * 0x1000 + off - 0x1000];
}

static u64 cuadros(const void *g) { return ((const GB *)g)->ppu.frames; }
static u64 ciclos(const void *g) { return ((const GB *)g)->cyc; }

static void resumen(void *p, const char *rom)
{
    GB *g = p;
    const Cpu *c = &g->cpu;
    char titulo[17] = "";
    for (int i = 0; i < 16 && 0x134 + i < (int)g->rom_size; i++) {
        u8 ch = g->rom[0x134 + i];
        titulo[i] = ch >= 32 && ch < 127 ? (char)ch : 0;
        if (!ch) break;
    }
    printf("%s (%s)\n", titulo[0] ? titulo : rom, model_name(g));
    printf("PC $%04X  SP $%04X  AF $%02X%02X  BC $%02X%02X  DE $%02X%02X  HL $%02X%02X  IME %d%s%s%s\n", c->pc, c->sp, c->a, c->f, c->b,
           c->c, c->d, c->e, c->h, c->l, c->ime, c->halted ? "  (HALT)" : "", c->stopped ? "  (STOP)" : "",
           c->locked ? "  (detenido)" : "");
    printf("cartucho:      MBC%d, ROM %u KB (%u bancos)%s%s%s, banco actual $%03X\n", (int)g->mbc.type, g->rom_size / 1024, g->rom_banks,
           g->mbc.has_ram ? ", con RAM" : "", g->mbc.has_battery ? " y batería" : "", g->mbc.has_rtc ? ", con reloj" : "", g->mbc.rom_bank);
}

/* La partida de Game Boy es el .sav de BGB y VBA-M: la RAM del cartucho y, si hay, el reloj */
static int partida_cargar(void *g, const char *path) { return gb_has_battery(g) && gb_save_read(g, path, (long long)time(NULL)); }
static int partida_guardar(void *g, const char *path) { return gb_save_write(g, path, (long long)time(NULL)); }
static int partida_sucia(const void *g) { return gb_has_battery(g) && ((const GB *)g)->sram_dirty; }

int main(int argc, char **argv)
{
    RunOps o = {
        .prog = "gbrun", .uso_rom = "rom.gb", .uso_extra = " [--model dmg|cgb|sgb]", .ram_base = 0xC000, .ram_tam = 0x2000,
        .dir_dig = 4, .pc_dig = 4, .rate = GB_SAMPLE_RATE, .canales = 2,
        .cargar = cargar, .liberar = liberar, .pad = poner_pad, .cuadro = cuadro, .parado = parado, .audio = audio, .captura = captura,
        .ram = ram, .cuadros = cuadros, .ciclos = ciclos, .resumen = resumen,
        .partida_cargar = partida_cargar, .partida_guardar = partida_guardar, .partida_sucia = partida_sucia, .partida_nombre = "partida",
    };
    return run_main(argc, argv, &o);
}
