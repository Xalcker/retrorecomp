/*
 * Corre un juego sin ventana con el intérprete y resume lo que pasó: instrucciones, ciclos,
 * excepciones por vector, el Z80, las variables de la BIOS y dónde se le fue el tiempo al CPU en el
 * último cuadro (los bucles de espera). Sirve para ver hasta dónde llega un juego mientras falta el
 * resto de la consola. Las opciones comunes (--frames, --watch, --shot, --screens, --wav, botones)
 * son las de run_comun.h.
 *
 *   neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR ...] [--fix]
 *                [boton:desde-hasta ...]
 *
 * Botones: up down left right a b c d start select (los cuadros en que se mantienen).
 * --watch DIR (hexadecimal, en la RAM: $100000-$10FFFF) imprime el byte cada vez que cambia.
 * --card ARCHIVO mete una tarjeta de memoria (la carga si existe) y la guarda al terminar (--sram es lo mismo).
 * --cardlog imprime cada llamada a CARD ($C00468) y CARD_ERROR ($C0046E) con sus variables y la respuesta.
 * --mute M silencia partes del sonido (bits 0-3 FM 1-4, 4 SSG, 5 ADPCM-A, 6 ADPCM-B; en hexadecimal).
 * --fix muestra la capa fija al final como texto: muchos juegos ponen su fuente en el código ASCII
 *       dentro de un bloque de 256 mosaicos (Metal Slug, en $300 + ASCII), así que se mira el byte bajo.
 */
#include "neo.h"
#include "png.h"
#include "run_comun.h"

static const char *bios;
static unsigned mute;
static int fix, cardlog;

static int opcion(const char *a, const char *v)
{
    if (!strcmp(a, "--bios") && v) { bios = v; return 2; }
    if (!strcmp(a, "--mute") && v) { mute = (unsigned)strtoul(v, NULL, 16); return 2; }
    if (!strcmp(a, "--fix")) { fix = 1; return 1; }
    if (!strcmp(a, "--cardlog")) { cardlog = 1; return 1; }
    return 0;
}

static const char *cargar(void **pg, const Cli *c)
{
    static char msg[1300];
    Neo *g = malloc(sizeof *g);
    const char *err = NULL;
    if (!g || neo_load(g, c->pos[0], bios, &err)) {
        snprintf(msg, sizeof msg, "%s", err ? err : "sin memoria");
        free(g);
        return msg;
    }
    int fatal;
    const char *u = cart_unsupported(&g->cart, &fatal);
    if (u) {
        fprintf(stderr, "%s%s\n", fatal ? "no se puede correr: " : "aviso: ", u);
        if (fatal) { neo_free(g); free(g); return "juego no soportado"; }
    }
    *pg = g;
    return NULL;
}

static void inicio(void *p) { ((Neo *)p)->audio.mute = (u8)mute; }
static void liberar(void *g) { neo_free(g); free(g); }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((Neo *)g)->pad[0] = (u16)p1; ((Neo *)g)->pad[1] = (u16)p2; }

static u8 ram8(const Neo *g, u32 a) { return g->ram[a & 0xFFFF]; }
static u16 ram16(const Neo *g, u32 a) { return (u16)(ram8(g, a) << 8 | ram8(g, a + 1)); }
static u32 ram32(const Neo *g, u32 a) { return (u32)ram16(g, a) << 16 | ram16(g, a + 2); }

/* --cardlog: la interfaz de CARD vista desde afuera (las variables de la RAM al entrar y la respuesta al volver) */
static u32 card_ret, card_sp;
static const char *card_what;
static void card_watch(const Neo *g)
{
    u32 pc = g->cpu.pc, sp = g->cpu.a[7];
    if (card_ret && pc == card_ret && sp > card_sp) {
        printf("cuadro %5llu:   %s -> respuesta $%02X, SUB $%04X\n", (unsigned long long)g->lspc.frame, card_what,
               ram8(g, 0xFDC6), ram16(g, 0xFDD0));
        card_ret = 0;
    }
    if (pc != 0xC00468 && pc != 0xC0046E) return;
    card_what = pc == 0xC00468 ? "CARD" : "CARD_ERROR";
    printf("cuadro %5llu: %s desde $%06X: comando %u, START $%06X, SIZE $%04X, FCB $%04X, SUB $%04X, respuesta $%02X\n",
           (unsigned long long)g->lspc.frame, card_what, ram32(g, sp & 0xFFFF), ram8(g, 0xFDC4), ram32(g, 0xFDC8),
           ram16(g, 0xFDCC), ram16(g, 0xFDCE), ram16(g, 0xFDD0), ram8(g, 0xFDC6));
    card_ret = ram32(g, sp & 0xFFFF); card_sp = sp;
}

static RunCtx *rc;
static int after(Neo *g)
{
    neo_after_step(g);
    run_paso(rc, g->cpu.pc);
    if (cardlog) card_watch(g);
    return g->lspc.frame_done || g->cpu.halted;
}

static void cuadro(void *p, RunCtx *r)
{
    Neo *g = p;
    rc = r;
    g->lspc.frame_done = 0;
    m68k_run(g, after);
    audio_end_frame(g);
}

static const char *parado(void *g) { return ((Neo *)g)->cpu.halted ? "el 68000 se detuvo (doble falta)" : NULL; }
static int audio(void *g, int16_t *dst, int max) { return neo_audio_take(g, dst, max); }

/* La imagen del cuadro (320 × 224) como PNG */
static int captura(void *p, const char *path, int *w, int *h)
{
    const Neo *g = p;
    unsigned char *rgb = malloc(SCREEN_W * SCREEN_H * 3);
    if (!rgb) return 0;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u32 c = g->fb[i];
        rgb[i * 3] = (unsigned char)(c >> 16); rgb[i * 3 + 1] = (unsigned char)(c >> 8); rgb[i * 3 + 2] = (unsigned char)c;
    }
    int ok = png_write_rgb(path, rgb, SCREEN_W, SCREEN_H);
    free(rgb);
    *w = SCREEN_W; *h = SCREEN_H;
    return ok;
}

static int ram(const void *g, unsigned off) { return ((const Neo *)g)->ram[off]; }
static u64 cuadros(const void *g) { return ((const Neo *)g)->lspc.frame; }
static u64 ciclos(const void *g) { return ((const Neo *)g)->cyc; }

static void resumen(void *p, const char *rom)
{
    Neo *g = p;
    M68k *c = &g->cpu;
    printf("%s\n", g->cart.title[0] ? g->cart.title : rom);
    printf("BIOS:          %s\n", g->bios.ver ? g->bios.ver->desc : "?");
    printf("PC $%06X  SR $%04X%s%s\n", c->pc, m68k_get_sr(c), c->stopped ? "  (STOP)" : "", c->halted ? "  (detenido)" : "");
    printf("vectores:      %s; paleta %d\n", (g->latch & LATCH_SWPROM) ? "del juego" : "de la BIOS", (g->latch & LATCH_PALBANK1) ? 1 : 0);
    if (g->cart.p_banks > 1) printf("bancos P2:     %u cambios, banco %u\n", g->map_ver - 1, g->p2_bank);
    printf("BIOS (RAM):    SYSTEM_MODE $%02X, USER_REQUEST %u, USER_MODE %u, MVS_FLAG %u, COUNTRY_CODE %u\n",
           ram8(g, 0xFD80), ram8(g, 0xFDAE), ram8(g, 0xFDAF), ram8(g, 0xFD82), ram8(g, 0xFD83));
    printf("Z80:           PC $%04X, %u comandos, último $%02X, respuesta $%02X, NMI %s, YM2610: temporizadores %s%s, banderas %u\n",
           g->z80.pc, g->sound_cmds, g->sound_cmd, g->sound_reply, g->nmi_enabled ? "habilitada" : "deshabilitada",
           g->ym.ta_at != NEVER ? "A" : "", g->ym.tb_at != NEVER ? "B" : (g->ym.ta_at != NEVER ? "" : "parados"), g->ym.flags);
    printf("bancos Z80:    $%02X $%02X $%02X $%02X (ventanas de 2, 4, 8 y 16 KB; M1 de %zu KB)\n",
           g->z80_bank[0], g->z80_bank[1], g->z80_bank[2], g->z80_bank[3], g->cart.len[REG_M] / 1024);
    printf("excepciones:\n");
    for (int n = 0; n < 64; n++)
        if (g->exc_count[n]) {
            const char *nm = cart_vector_name(n);
            char buf[16];
            if (!nm) { snprintf(buf, sizeof buf, "vector %d", n); nm = buf; }
            printf("  %-24s %u\n", nm, g->exc_count[n]);
        }
}

static const char *nota_pc(unsigned pc) { return pc >= 0xC00000 && pc < 0xD00000 ? "(BIOS)" : NULL; }

/* La capa fija: 40 columnas × 32 filas en $7000 de la VRAM (por columnas); las filas 2-29 se ven */
static void print_fix(const Neo *g)
{
    printf("capa fija (filas 2-29; el byte bajo de cada mosaico como ASCII, '.' si no lo es):\n");
    for (int y = 2; y < 30; y++) {
        char line[41];
        for (int x = 0; x < 40; x++) {
            u8 t = (u8)g->lspc.vram[0x7000 + x * 32 + y];
            line[x] = t >= 0x20 && t < 0x7F ? (char)t : '.';
        }
        line[40] = 0;
        printf("  |%s|\n", line);
    }
}

static void final(void *p)
{
    Neo *g = p;
    if (fix) print_fix(g);
    /* Colores distintos en la imagen del último cuadro (para ver de un vistazo si dibuja algo) */
    static u32 seen[SCREEN_W * SCREEN_H];
    int n = 0;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        int k = 0;
        while (k < n && seen[k] != g->fb[i]) k++;
        if (k == n && n < 4096) seen[n++] = g->fb[i];
    }
    printf("imagen:        %d colores distintos\n", n);
}

/* La partida de Neo Geo es la tarjeta de memoria */
static int partida_cargar(void *g, const char *path) { return neo_card_insert(g, path); }
static int partida_guardar(void *g, const char *path) { return neo_card_save(g, path); }
static int partida_sucia(const void *g) { return ((const Neo *)g)->card.dirty; }

int main(int argc, char **argv)
{
    RunOps o = {
        .prog = "neorun", .uso_rom = "juego",
        .uso_extra = " [--bios archivo] [--cardlog] [--fix] [--mute M]", .ram_base = 0x100000, .ram_tam = 0x10000,
        .dir_dig = 6, .pc_dig = 6, .rate = AUDIO_RATE, .canales = 2,
        .opcion = opcion, .cargar = cargar, .inicio = inicio, .liberar = liberar, .pad = poner_pad, .cuadro = cuadro, .parado = parado,
        .audio = audio, .captura = captura, .ram = ram, .cuadros = cuadros, .ciclos = ciclos, .resumen = resumen, .nota_pc = nota_pc,
        .final = final, .partida_cargar = partida_cargar, .partida_guardar = partida_guardar, .partida_sucia = partida_sucia,
        .partida_nombre = "tarjeta",
    };
    return run_main(argc, argv, &o);
}
