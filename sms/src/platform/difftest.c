/*
 * Prueba diferencial de Master System y Game Gear: corre el código recompilado y el intérprete lado a lado y se detiene en la
 * primera diferencia (Z80, RAM, VDP, PSG, mandos, cartucho...). Las opciones, la carga, el bench y el final son los de
 * difftest_comun.h (en ../shared); aquí va lo propio de la consola.
 *
 * La máquina B corre función por función (recompilada, o una instrucción del intérprete donde no hay código). Tras cada paso, la
 * máquina A (solo intérprete: z80_interp_only) avanza hasta el mismo ciclo y se comparan. Las dos cuentan los ciclos igual, así que
 * coinciden en cada límite de instrucción. La línea común «Métrica: ...» (metrica.h) separa el HALT saltado (espera) de lo nativo
 * y lo interpretado.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp.h"
#include "difftest_comun.h"
#include "png.h"
#include "z80_ops.h"

static Sms A, B;

/* Captura de lo que se ve de g en PNG */
static int save_screen(const Sms *g, const char *path)
{
    int x0, y0, w, h;
    vdp_view(g, &x0, &y0, &w, &h);
    static unsigned char rgb[FB_W * FB_H * 3];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 v = vdp_argb(g->vdp.fb[(y + y0) * FB_W + x + x0]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (u8)(v >> 16); p[1] = (u8)(v >> 8); p[2] = (u8)v;
        }
    return png_write_rgb(path, rgb, w, h);
}

/* --executed: cada instrucción que registró la traza de g marca sus bytes */
static int write_executed(const Sms *g, const char *path)
{
    size_t size = g->cart.rom_size;
    const u8 *rom = g->cart.rom;
    u8 *done = calloc(size ? size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!done || !f) { free(done); if (f) fclose(f); return 0; }
    for (size_t off = 0; off < size; off++) {
        if (!g->cart.trace[off]) continue;
        u8 b[4];
        Z80Insn in;
        for (int i = 0; i < 4; i++) b[i] = off + (size_t)i < size ? rom[off + (size_t)i] : 0;
        z80_decode(b, (u16)off, &in);
        for (int k = 0; k < in.len && off + (size_t)k < size; k++) done[off + (size_t)k] = 1;
    }
    for (size_t off = 0; off < size; off++) {
        fputc(done[off] ? 'C' : '.', f);
        if (off % 64 == 63) fputc('\n', f);
    }
    free(done);
    return fclose(f) == 0;
}

/* Compara todo menos los búferes de salida (full = también la imagen y el audio listo) */
static int compare(int full)
{
    int bad = 0;
    DT_CMP(z80, "registros del Z80");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    DT_CMP(ram, "RAM");
    DT_CMP(map, "mapper");
    if (memcmp(&A.vdp, &B.vdp, offsetof(Vdp, fb))) { puts("  difiere: VDP (registros, VRAM, CRAM, contadores o líneas)"); bad = 1; }
    DT_CMP(io, "E/S (control, mandos, puertos del Game Gear)");
    DT_CMP(psg, "PSG");
    if (memcmp(&A.audio, &B.audio, offsetof(Audio, buf))) { puts("  difiere: mezcla de audio"); bad = 1; }
    if (memcmp(A.cart.ram, B.cart.ram, CART_RAM_SIZE) || A.cart.ram_used != B.cart.ram_used) { puts("  difiere: RAM del cartucho"); bad = 1; }
    if (A.nmi_count != B.nmi_count || A.irq_count != B.irq_count) { puts("  difiere: interrupciones atendidas"); bad = 1; }
    if (full) {
        if (memcmp(A.vdp.fb, B.vdp.fb, sizeof A.vdp.fb)) { puts("  difiere: imagen"); bad = 1; }
        if (A.audio.n != B.audio.n || memcmp(A.audio.buf, B.audio.buf, (size_t)A.audio.n * 2 * sizeof A.audio.buf[0])) {
            puts("  difiere: audio"); bad = 1;
        }
    }
    return bad;
}

static void print_cpu(const char *who, const Z80 *z)
{
    printf("  %s PC=$%04X SP=$%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X IFF1=%d\n", who,
           z->pc, z->sp, z->a, z->f, z->b, z->c, z->d, z->e, z->h, z->l, z->ix, z->iy, z->iff1);
}

/* Un cuadro: B avanza paso a paso y A lo alcanza tras cada uno; se comparan en cada límite de instrucción */
static int cuadro(DiffCtx *x, long f)
{
    Cli *c = x->c;
    A.pad[0] = B.pad[0] = cli_pad(c, f, 0);
    A.pad[1] = B.pad[1] = cli_pad(c, f, 1);
    sms_frame_begin(&A);
    sms_frame_begin(&B);
    while (!B.vdp.frame_done) {
        u16 pc0 = B.z80.pc;
        u64 c0 = B.cyc;
        int zone = bus_rom_offset(&B, pc0) >= 0 ? 0 : pc0 >= 0x8000 ? 1 : 2;
        u64 sk0 = z80_stat.skipped;
        int native = sms_recomp_step(&B);
        if (c->estricto && metrica_estricto(f, 4, NULL)) return DT_ESTRICTO;
        if (native) x->recompiladas++;
        else if (z80_stat.skipped != sk0) {}     /* HALT saltado: va en la métrica (espera) */
        else {
            x->zona[zone] += B.cyc - c0;
            if (zone == 0) dt_golpe(x, (u32)bus_rom_offset(&B, pc0));
        }
        x->llamadas++;
        while (A.cyc < B.cyc && !A.vdp.frame_done) sms_step(&A);
        if (compare(0) || A.vdp.frame_done != B.vdp.frame_done) {
            printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%04X (ciclo %llu, %s)\n",
                   f, pc0, (unsigned long long)B.cyc, native ? "recompilado" : "intérprete");
            print_cpu("A (intérprete) ", &A.z80);
            print_cpu("B (recompilado)", &B.z80);
            return DT_DIFERENCIA;
        }
    }
    audio_end_frame(&A);
    audio_end_frame(&B);
    if (compare(1)) { printf("DIFERENCIA al final del cuadro %ld\n", f); return DT_DIFERENCIA; }
    A.audio.n = B.audio.n = 0;
    return DT_IGUAL;
}

static const char *cargar(const u8 *rom, size_t len, const Cli *c)
{
    static char msg[200];
    const char *err = NULL;
    if (!sms_load(&A, rom, len, c->pos[0], c->model, &err) && !sms_load(&B, rom, len, c->pos[0], c->model, &err)) return NULL;
    snprintf(msg, sizeof msg, "ROM: %s", err);
    return msg;
}

static const char *sha1(const u8 *rom, size_t len) { (void)rom; (void)len; return B.cart.sha1; }

static const char *preparar(const Cli *c)
{
    (void)c;
    z80_stat_of = &B;                       /* cuánto del Z80 de B corre nativo */
    z80_interp_only = &A;                   /* A es la referencia: solo con el intérprete */
    return NULL;
}

static int con_cuentas;                     /* --counts: --uncovered con cuántas veces interpretó cada instrucción */

static void trazas(const Cli *c)
{
    con_cuentas = c->counts;
    if (c->uncovered) sms_trace_start(&B);  /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c->executed) sms_trace_start(&A);   /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */
}

static int captura(const char *path) { return save_screen(&B, path); }
static int executed(const char *path) { return write_executed(&A, path); }
static void reiniciar(void) { metrica_reiniciar(&B); }          /* la métrica común: la de B (metrica.h) */
static void *maquina(int k) { return k ? (void *)&A : (void *)&B; }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((Sms *)g)->pad[0] = (u8)p1; ((Sms *)g)->pad[1] = (u8)p2; }
static void cuadro_solo(void *g) { sms_run_frame(g); }
static void vaciar(void *g) { ((Sms *)g)->audio.n = 0; }
static u64 ciclos(void) { return B.cyc; }
static void extra(void) { z80_stat_print(); }
static void golpe(char *out, size_t size, u32 clave) { snprintf(out, size, "ROM $%05X", clave); }

static size_t uncovered(const char *path, int *ok)
{
    size_t cnt = sms_trace_count(&B);
    *ok = con_cuentas ? sms_trace_write_counts(&B, path) : sms_trace_write(&B, path);
    if (con_cuentas && *ok) sms_trace_top(&B, stdout, 10, "Las instrucciones de ROM que más interpretó el recompilado:");
    return cnt;
}

static void ram(unsigned *fijas, unsigned *variables, unsigned *varias, unsigned *perdidas) { sms_ram_summary(&B, fijas, variables, varias, perdidas); }
/* La partida del Master System y del Game Gear es la RAM del cartucho */
static int cargar_partida(const char *path) { return sms_sram_load(&A, path) && sms_sram_load(&B, path); }
static int guardar_partida(const char *path) { return sms_sram_save(&B, path); }

int main(int argc, char **argv)
{
    DiffOps o = {
        .titulo = recomp_name, .uso_rom = "rom.sms", .uso_extra = " [--model sms|gg]", .cuadros = 300, .fps = 59.92, .unidad = "ROM",
        .zonas = { "ROM", "RAM", "otra" },
        .cargar = cargar, .sha1 = sha1, .preparar = preparar, .cargar_partida = cargar_partida, .guardar_partida = guardar_partida,
        .trazas = trazas, .reiniciar = reiniciar, .maquina = maquina, .pad = poner_pad, .cuadro_solo = cuadro_solo,
        .vaciar = vaciar, .cuadro = cuadro, .ciclos = ciclos, .captura = captura, .extra = extra, .golpe = golpe, .uncovered = uncovered, .ram = ram,
        .executed = executed,
    };
    return difftest_main(argc, argv, &o);
}
