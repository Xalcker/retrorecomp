/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos,
 * excepciones por vector y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de
 * espera). Sirve para ver hasta dónde llega un juego mientras falta el resto de la consola.
 * Las opciones comunes (--frames, --watch, --sram, --shot, --screens, --wav, botones) son las de run_comun.h.
 *
 *   genrun rom.bin [--frames N] [--watch DIR ...] [--sram ARCHIVO] [--tmss] [boton:desde-hasta ...]
 *
 * Botones: up down left right a b c start x y z mode (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $FF0000-$FFFFFF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la SRAM o EEPROM del cartucho al empezar (si existe) y la guarda al terminar.
 * --tmss: consola con TMSS (el juego debe escribir "SEGA" en $A14000 antes de tocar el VDP).
 */
#include "gen.h"
#include "png.h"
#include "run_comun.h"

static int tmss;

static int opcion(const char *a, const char *v)
{
    (void)v;
    if (!strcmp(a, "--tmss")) { tmss = 1; return 1; }
    return 0;
}

static const char *cargar(void **pg, const Cli *c)
{
    static char msg[200];
    size_t len;
    u8 *data = run_leer(c->pos[0], &len);
    if (!data) return "no se pudo leer";
    Gen *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || gen_load(g, data, len, &err)) {
        snprintf(msg, sizeof msg, "%s", err ? err : "sin memoria");
        free(data); free(g);
        return msg;
    }
    free(data);
    *pg = g;
    return NULL;
}

static void inicio(void *p)
{
    Gen *g = p;
    if (tmss) { g->tmss = 1; gen_reset(g); }
}

static void liberar(void *g) { gen_free(g); free(g); }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((Gen *)g)->pad[0] = (u16)p1; ((Gen *)g)->pad[1] = (u16)p2; }

/* Después de cada instrucción: VDP al día, cuenta y (en el último cuadro) histograma del PC */
static RunCtx *rc;
static int after(Gen *g)
{
    gen_after_step(g);
    run_paso(rc, g->cpu.pc);
    return g->vdp.frame_done || g->cpu.halted;
}

static void cuadro(void *p, RunCtx *r)
{
    Gen *g = p;
    rc = r;
    g->vdp.frame_done = 0;
    m68k_run(g, after);
    audio_end_frame(g);
}

static const char *parado(void *p)
{
    Gen *g = p;
    if (!g->cpu.halted) return NULL;
    return g->tmss_locked ? "el 68000 tocó el VDP sin \"SEGA\" en $A14000 (TMSS) y se colgó" : "el CPU se detuvo (doble falta)";
}

static int audio(void *g, int16_t *dst, int max) { return gen_audio_take(g, dst, max); }

/* La imagen del cuadro (el tamaño que tiene en ese modo) como PNG */
static int captura(void *p, const char *path, int *pw, int *ph)
{
    const Gen *g = p;
    const Vdp *v = &g->vdp;
    int w = v->width, h = v->height;
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = vdp_argb(v->fb[y * FB_W + x]);
            unsigned char *q = rgb + ((size_t)y * w + x) * 3;
            q[0] = (unsigned char)(c >> 16); q[1] = (unsigned char)(c >> 8); q[2] = (unsigned char)c;
        }
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    *pw = w; *ph = h;
    return ok;
}

static int ram(const void *g, unsigned off) { return ((const Gen *)g)->ram[off]; }
static u64 cuadros(const void *g) { return ((const Gen *)g)->vdp.frame; }
static u64 ciclos(const void *g) { return ((const Gen *)g)->cyc; }

static void resumen(void *p, const char *rom)
{
    (void)rom;
    Gen *g = p;
    M68k *c = &g->cpu;
    printf("%s\n", g->cart.hdr.name_int);
    printf("PC $%06X  SR $%04X%s%s\n", c->pc, m68k_get_sr(c), c->stopped ? "  (STOP)" : "", c->halted ? "  (detenido)" : "");
    if (g->cart.mapper == MAPPER_SSF2) printf("mapper SSF2:   %u cambios de mapeo\n", g->cart.map_ver);
    if (g->cart.has_eeprom) printf("EEPROM:        %u bytes escritos\n", g->eeprom.writes);
    printf("excepciones:\n");
    for (int n = 0; n < 64; n++)
        if (g->exc_count[n]) {
            const char *nm = cart_vector_name(n);
            char buf[16];
            if (!nm) { snprintf(buf, sizeof buf, "vector %d", n); nm = buf; }
            printf("  %-22s %u\n", nm, g->exc_count[n]);
        }
}

static int partida_cargar(void *g, const char *path) { return gen_sram_load(g, path); }
static int partida_guardar(void *g, const char *path) { return gen_sram_save(g, path); }
static int partida_sucia(const void *g) { return gen_sram_dirty((Gen *)g); }

int main(int argc, char **argv)
{
    RunOps o = {
        .prog = "genrun", .uso_rom = "rom.bin", .uso_extra = " [--tmss]", .ram_base = 0xFF0000, .ram_tam = 0x10000,
        .dir_dig = 6, .pc_dig = 6, .rate = AUDIO_RATE, .canales = 2,
        .opcion = opcion, .cargar = cargar, .inicio = inicio, .liberar = liberar, .pad = poner_pad, .cuadro = cuadro, .parado = parado,
        .audio = audio, .captura = captura, .ram = ram, .cuadros = cuadros, .ciclos = ciclos, .resumen = resumen,
        .partida_cargar = partida_cargar, .partida_guardar = partida_guardar, .partida_sucia = partida_sucia, .partida_nombre = "partida",
    };
    return run_main(argc, argv, &o);
}
