/*
 * Corre una ROM sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos, interrupciones,
 * cambios de mapeo, el estado del VDP y dónde se le fue el tiempo al CPU en el último cuadro (los bucles de
 * espera). Sirve para ver hasta dónde llega un juego mientras falta el resto de la consola, como genrun.
 * Las opciones comunes (--frames, --watch, --sram, --shot, --screens, --wav, botones) son las de run_comun.h.
 *
 *   smsrun rom.sms [--frames N] [--model sms|gg] [--watch DIR ...] [--sram ARCHIVO] [--sram-text] [--sdsc]
 *                  [boton:desde-hasta ...]
 *
 * Botones: up down left right a b start pause (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $C000-$DFFF) imprime el byte cada vez que cambia.
 * --sram ARCHIVO carga la RAM del cartucho al empezar (si existe) y la guarda al terminar (si el juego la usa).
 * --sram-text imprime al final el texto ASCII de la RAM del cartucho (ZEXALL deja ahí su resultado).
 * --sdsc imprime lo que el juego escribe en la consola de depuración SDSC (puerto $FD, solo emuladores).
 * La imagen (--shot, --screens) es lo que se ve: 256×192 (o 224) en el Master System y la ventana de 160×144 en el
 * Game Gear en modo GG.
 */
#include "sms.h"
#include "png.h"
#include "run_comun.h"

static int sram_text, sdsc;

static int opcion(const char *a, const char *v)
{
    (void)v;
    if (!strcmp(a, "--sram-text")) { sram_text = 1; return 1; }
    if (!strcmp(a, "--sdsc")) { sdsc = 1; return 1; }
    return 0;
}

/* La consola SDSC: los caracteres que se pueden imprimir y los saltos de línea */
static void sdsc_print(Sms *g, u8 v)
{
    (void)g;
    if (v == 10 || v == 13 || (v >= 32 && v < 127)) putchar(v == 13 ? '\n' : v);
}

static const char *cargar(void **pg, const Cli *c)
{
    static char msg[200];
    size_t len;
    u8 *data = run_leer(c->pos[0], &len);
    if (!data) return "no se pudo leer";
    Sms *g = calloc(1, sizeof *g);
    const char *err = NULL;
    if (!g || sms_load(g, data, len, c->pos[0], c->model, &err)) {
        snprintf(msg, sizeof msg, "%s", err ? err : "sin memoria");
        free(data); free(g);
        return msg;
    }
    free(data);
    *pg = g;
    return NULL;
}

static void inicio(void *p) { if (sdsc) ((Sms *)p)->sdsc_out = sdsc_print; }
static void liberar(void *g) { sms_free(g); free(g); }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((Sms *)g)->pad[0] = (u8)p1; ((Sms *)g)->pad[1] = (u8)p2; }

static void cuadro(void *p, RunCtx *r)
{
    Sms *g = p;
    sms_frame_begin(g);
    while (!g->vdp.frame_done) { run_paso(r, g->z80.pc); sms_step(g); }
    audio_end_frame(g);
}

static int audio(void *g, int16_t *dst, int max) { return sms_audio_take(g, dst, max); }

/* La imagen del cuadro, lo que se ve, como PNG */
static int captura(void *p, const char *path, int *pw, int *ph)
{
    const Sms *g = p;
    int x0, y0, w, h;
    vdp_view(g, &x0, &y0, &w, &h);
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = vdp_argb(g->vdp.fb[(y0 + y) * FB_W + x0 + x]);
            unsigned char *q = rgb + ((size_t)y * w + x) * 3;
            q[0] = (unsigned char)(c >> 16); q[1] = (unsigned char)(c >> 8); q[2] = (unsigned char)c;
        }
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    *pw = w; *ph = h;
    return ok;
}

static int ram(const void *g, unsigned off) { return ((const Sms *)g)->ram[off]; }
static u64 cuadros(const void *g) { return ((const Sms *)g)->vdp.frame; }
static u64 ciclos(const void *g) { return ((const Sms *)g)->cyc; }

static void resumen(void *p, const char *rom)
{
    Sms *g = p;
    const Cart *c = &g->cart;
    const Z80 *z = &g->z80;
    const Vdp *v = &g->vdp;
    if (sdsc) printf("\n");
    printf("%s (%s%s, %s)\n", c->sdsc.found && c->sdsc.name[0] ? c->sdsc.name : rom, model_name(g->model),
           g->model == MODEL_GG && g->sms_mode ? " en modo SMS" : "", mapper_name(c->mapper));
    printf("PC $%04X  SP $%04X  AF $%02X%02X  IFF1 %d  IM %d%s\n", z->pc, z->sp, z->a, z->f, z->iff1, z->im,
           z->halted ? "  (HALT)" : "");
    printf("interrupciones: %u (y %u NMI)\n", g->irq_count, g->nmi_count);
    printf("mapeo:          %u cambios; ranuras en $%05X $%05X $%05X%s%s\n", g->map.map_ver, g->map.slot[0], g->map.slot[1],
           g->map.slot[2], g->map.ram_slot2 || g->map.ram_a000 ? ", RAM del cartucho en la ranura 2" : "",
           g->map.ram_high ? ", RAM del cartucho en $C000" : "");
    printf("VDP:            pantalla %s, interrupción de cuadro %s, de línea %s, %u líneas;",
           v->reg[1] & 0x40 ? "encendida" : "apagada", v->reg[1] & 0x20 ? "sí" : "no", v->reg[0] & 0x10 ? "sí" : "no", v->height);
    for (int i = 0; i <= 10; i++) printf(" %02X", v->reg[i]);
    printf("\n");
}

static void final(void *p)
{
    Sms *g = p;
    if (!sram_text) return;
    printf("texto de la RAM del cartucho:\n");
    int col = 0;
    for (u32 a = 0; a < CART_RAM_SIZE; a++) {
        u8 ch = g->cart.ram[a];
        if (ch == 10 || ch == 13) { if (col) putchar('\n'); col = 0; }
        else if (ch >= 32 && ch < 127) { putchar(ch); col++; }
    }
    if (col) putchar('\n');
}

static int partida_cargar(void *g, const char *path) { return sms_sram_load(g, path); }
static int partida_guardar(void *g, const char *path) { return sms_sram_save(g, path); }
static int partida_sucia(const void *g) { return sms_sram_dirty(g); }

int main(int argc, char **argv)
{
    RunOps o = {
        .prog = "smsrun", .uso_rom = "rom.sms", .uso_extra = " [--model sms|gg] [--sram-text] [--sdsc]", .ram_base = 0xC000, .ram_tam = 0x2000,
        .dir_dig = 4, .pc_dig = 4, .rate = AUDIO_RATE, .canales = 2,
        .opcion = opcion, .cargar = cargar, .inicio = inicio, .liberar = liberar, .pad = poner_pad, .cuadro = cuadro,
        .audio = audio, .captura = captura, .ram = ram, .cuadros = cuadros, .ciclos = ciclos, .resumen = resumen, .final = final,
        .partida_cargar = partida_cargar, .partida_guardar = partida_guardar, .partida_sucia = partida_sucia, .partida_nombre = "partida",
    };
    return run_main(argc, argv, &o);
}
