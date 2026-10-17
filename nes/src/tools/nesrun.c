/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos, el estado del mapper y
 * dónde se le fue el tiempo al CPU en el último cuadro (los bucles de espera). Sirve para ver hasta dónde llega un
 * juego, como genrun, neorun, smsrun y gbrun. Las opciones comunes (--frames, --watch, --sram, --shot, --screens,
 * --wav, botones) son las de run_comun.h.
 *
 *   nesrun rom.nes [--frames N] [--watch DIR ...] [--sram ARCHIVO] [boton:desde-hasta ...]
 *
 * Botones: a b select start up down left right (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $0000-$07FF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la RAM con batería ($6000-$7FFF) al empezar (si existe) y la guarda al terminar.
 */
#include "nes.h"
#include "png.h"
#include "run_comun.h"

static const char *cargar(void **pg, const Cli *c)
{
    static char msg[200];
    size_t len;
    u8 *data = run_leer(c->pos[0], &len);
    if (!data) return "no se pudo leer";
    NES *n = calloc(1, sizeof *n);
    const char *err = NULL;
    if (!n || nes_load(n, data, len, &err)) {
        snprintf(msg, sizeof msg, "%s", err ? err : "sin memoria");
        free(data); free(n);
        return msg;
    }
    free(data);
    *pg = n;
    return NULL;
}

static void liberar(void *n) { nes_free(n); free(n); }
static void poner_pad(void *n, unsigned p1, unsigned p2) { ((NES *)n)->pad[0] = (u8)p1; ((NES *)n)->pad[1] = (u8)p2; }

static void cuadro(void *p, RunCtx *r)
{
    NES *n = p;
    n->ppu.frame_done = 0;
    while (!n->ppu.frame_done) { run_paso(r, n->cpu.pc); nes_step(n); }
    ppu_flush(n);
}

static const char *parado(void *n) { return ((NES *)n)->cpu.jammed ? "el CPU se detuvo (opcode KIL)" : NULL; }
static int audio(void *n, int16_t *dst, int max) { return apu_take(n, dst, max); }

/* La imagen del cuadro (256 × 240) como PNG, con la paleta predeterminada */
static int captura(void *p, const char *path, int *w, int *h)
{
    const NES *n = p;
    static u32 pal[64];
    static unsigned char rgb[256 * 240 * 3];
    if (!pal[0]) ppu_make_palette(0, pal);
    for (int i = 0; i < 256 * 240; i++) {
        u32 c = ppu_argb(pal, n->ppu.fb[i]);
        rgb[i * 3] = (u8)(c >> 16); rgb[i * 3 + 1] = (u8)(c >> 8); rgb[i * 3 + 2] = (u8)c;
    }
    *w = 256; *h = 240;
    return png_write_rgb(path, rgb, 256, 240);
}

static int ram(const void *n, unsigned off) { return ((const NES *)n)->ram[off]; }
static u64 cuadros(const void *n) { return ((const NES *)n)->ppu.frame; }
static u64 ciclos(const void *n) { return ((const NES *)n)->cyc; }

static void resumen(void *p, const char *rom)
{
    NES *n = p;
    const Cpu *c = &n->cpu;
    printf("%s\n", rom);
    printf("PC $%04X  A $%02X  X $%02X  Y $%02X  P $%02X  SP $%02X%s\n", c->pc, c->a, c->x, c->y, c->p, c->sp,
           c->jammed ? "  (detenido)" : "");
    printf("mapper:        %d, PRG %zu KB, CHR %zu KB%s%s\n", n->cart.mapper, n->cart.prg_size / 1024, n->cart.chr_size / 1024,
           n->cart.chr_is_ram ? " (RAM)" : "", n->cart.battery ? ", con batería" : "");
}

static int partida_cargar(void *n, const char *path) { return nes_sram_load(n, path); }
static int partida_guardar(void *n, const char *path) { return nes_sram_save(n, path); }
static int partida_sucia(const void *n) { return nes_sram_dirty(n); }

int main(int argc, char **argv)
{
    RunOps o = {
        .prog = "nesrun", .uso_rom = "rom.nes", .ram_base = 0, .ram_tam = 0x800, .dir_dig = 4, .pc_dig = 4, .rate = 44100, .canales = 1,
        .cargar = cargar, .liberar = liberar, .pad = poner_pad, .cuadro = cuadro, .parado = parado, .audio = audio, .captura = captura,
        .ram = ram, .cuadros = cuadros, .ciclos = ciclos, .resumen = resumen,
        .partida_cargar = partida_cargar, .partida_guardar = partida_guardar, .partida_sucia = partida_sucia, .partida_nombre = "partida",
    };
    return run_main(argc, argv, &o);
}
