/*
 * Prueba diferencial de NES: corre el código recompilado y el intérprete lado a lado y se detiene en la primera diferencia
 * (CPU, RAM, PPU, APU, mapper, interrupciones...). Las opciones, la carga, el bench y el final son los de difftest_comun.h (en
 * ../shared); aquí va lo propio de la consola.
 *
 * La máquina B corre función por función (recompilada, o una instrucción del intérprete donde no hay código). Tras cada paso, la
 * máquina A (solo intérprete) avanza hasta el mismo ciclo y se comparan. Las dos cuentan los ciclos igual, así que coinciden en
 * cada límite de instrucción.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp.h"
#include "sha1.h"
#include "difftest_comun.h"
#include "png.h"

static NES A, B;

/* Captura del cuadro actual de n en PNG, con la paleta predeterminada */
static int save_screen(const NES *n, const char *path)
{
    static u32 pal[64];
    static unsigned char rgb[256 * 240 * 3];
    if (!pal[0]) ppu_make_palette(0, pal);
    for (int i = 0; i < 256 * 240; i++) {
        u32 c = ppu_argb(pal, n->ppu.fb[i]);
        rgb[i * 3] = (u8)(c >> 16); rgb[i * 3 + 1] = (u8)(c >> 8); rgb[i * 3 + 2] = (u8)c;
    }
    return png_write_rgb(path, rgb, 256, 240);
}

/* --executed: cada instrucción que registró la traza de n marca sus bytes (opcode y operandos) */
static int write_executed(const NES *n, const char *path)
{
    size_t size = n->cart.prg_size;
    u8 *done = calloc(size ? size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!done || !f) { free(done); if (f) fclose(f); return 0; }
    for (size_t off = 0; off < size; off++) {
        if (!n->trace[off]) continue;
        int len = mode_len[ins_tab[n->cart.prg[off]].mode];
        for (int k = 0; k < len && off + k < size; k++) done[off + k] = 1;
    }
    for (size_t off = 0; off < size; off++) {
        fputc(done[off] ? 'C' : '.', f);
        if (off % 64 == 63) fputc('\n', f);
    }
    free(done);
    return fclose(f) == 0;
}

/* Compara todo menos los búferes grandes de salida (full = incluye el cuadro y el audio) */
static int compare(int full)
{
    int bad = 0;
    DT_CMP(cpu, "registros del CPU");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    DT_CMP(ram, "RAM");
    if (memcmp(&A.ppu, &B.ppu, offsetof(Ppu, fb))) {
        puts("  difiere: estado de la PPU (registros, VRAM, paletas u OAM)"); bad = 1;
    }
    if (memcmp(&A.apu, &B.apu, offsetof(Apu, buf))) { puts("  difiere: APU"); bad = 1; }
    DT_CMP(cart.reg, "registros del mapper");
    DT_CMP(cart.prg_ram, "RAM del cartucho");
    if (A.cart.chr_is_ram && memcmp(A.cart.chr, B.cart.chr, A.cart.chr_size)) { puts("  difiere: CHR RAM"); bad = 1; }
    if (A.cart.mirroring != B.cart.mirroring) { puts("  difiere: espejado"); bad = 1; }
    if (A.nmi_line != B.nmi_line || A.need_nmi != B.need_nmi || A.prev_need_nmi != B.prev_need_nmi ||
        A.run_irq != B.run_irq || A.prev_run_irq != B.prev_run_irq || A.irq_line != B.irq_line ||
        A.stall != B.stall) { puts("  difiere: líneas de interrupción"); bad = 1; }
    if (full) {
        if (memcmp(A.ppu.fb, B.ppu.fb, sizeof A.ppu.fb)) { puts("  difiere: imagen"); bad = 1; }
        if (A.apu.nbuf != B.apu.nbuf || memcmp(A.apu.buf, B.apu.buf, A.apu.nbuf * sizeof A.apu.buf[0])) {
            puts("  difiere: audio"); bad = 1;
        }
    }
    return bad;
}

/* Un cuadro: B avanza paso a paso y A lo alcanza tras cada uno; se comparan en cada límite de instrucción */
static int cuadro(DiffCtx *x, long f)
{
    Cli *c = x->c;
    A.pad[0] = B.pad[0] = cli_pad(c, f, 0);
    A.pad[1] = B.pad[1] = cli_pad(c, f, 1);
    A.ppu.frame_done = B.ppu.frame_done = 0;
    while (!B.ppu.frame_done) {
        u16 pc0 = B.cpu.pc;
        u64 c0 = B.cyc;
        RecompFn fn = recomp_lookup(&B, B.cpu.pc);
        if (B.cpu.jammed) fn = NULL;
        nes_paso(&B, fn);
        if (c->estricto && metrica.motivo[MOTIVO_FALTA]) {
            char prg[24];
            snprintf(prg, sizeof prg, "PRG $%05lX", (unsigned long)mapper_prg_offset(&B.cart, pc0));
            metrica_estricto(f, 4, prg);
            return DT_ESTRICTO;
        }
        if (fn) x->recompiladas++;
        else {
            int zona = pc0 >= 0x8000 ? 0 : pc0 < 0x2000 ? 1 : pc0 >= 0x6000 ? 2 : 3;
            x->zona[zona] += B.cyc - c0;
            if (zona == 0) dt_golpe(x, (u32)mapper_prg_offset(&B.cart, pc0));
        }
        x->llamadas++;
        while (A.cyc < B.cyc) cpu_step(&A);
        if (compare(0) || A.ppu.frame_done != B.ppu.frame_done) {
            printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%04X (ciclo %llu, %s)\n"
                   "  A (intérprete) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n"
                   "  B (recompilado) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n",
                   f, pc0, (unsigned long long)B.cyc, fn ? "recompilado" : "intérprete",
                   A.cpu.pc, A.cpu.a, A.cpu.x, A.cpu.y, A.cpu.p, A.cpu.sp,
                   B.cpu.pc, B.cpu.a, B.cpu.x, B.cpu.y, B.cpu.p, B.cpu.sp);
            return DT_DIFERENCIA;
        }
    }
    if (compare(1)) { printf("DIFERENCIA al final del cuadro %ld\n", f); return DT_DIFERENCIA; }
    A.apu.nbuf = B.apu.nbuf = 0;
    return DT_IGUAL;
}

static const char *cargar(const u8 *rom, size_t len, const Cli *c)
{
    (void)c;
    static char msg[200];
    const char *err = NULL;
    if (!nes_load(&A, rom, len, &err) && !nes_load(&B, rom, len, &err)) return NULL;
    snprintf(msg, sizeof msg, "ROM: %s", err);
    return msg;
}

static const char *sha1(const u8 *rom, size_t len)
{
    static char sha[41];
    sha1_hex(rom, len, sha);
    return sha;
}

static void trazas(const Cli *c)
{
    if (c->uncovered) nes_trace_start(&B);  /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c->executed) nes_trace_start(&A);   /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */
}

static int captura(const char *path) { return save_screen(&B, path); }
static int executed(const char *path) { return write_executed(&A, path); }
static void reiniciar(void) { metrica_reiniciar(&B); }
static void *maquina(int k) { return k ? (void *)&A : (void *)&B; }
static void modo(int k) { nes_force_interp = k; }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((NES *)g)->pad[0] = (u8)p1; ((NES *)g)->pad[1] = (u8)p2; }
static void cuadro_solo(void *g) { nes_run_frame(g); }
static void vaciar(void *g) { ((NES *)g)->apu.nbuf = 0; }
static u64 ciclos(void) { return B.cyc; }
static void golpe(char *out, size_t size, u32 clave) { snprintf(out, size, "PRG $%05X", clave); }

static size_t uncovered(const char *path, int *ok)
{
    size_t cnt = 0;
    for (size_t i = 0; i < B.cart.prg_size; i++) cnt += B.trace[i] != 0;
    *ok = nes_trace_write(&B, path);
    return cnt;
}

static void ram(unsigned *fijas, unsigned *variables, unsigned *varias, unsigned *perdidas) { nes_ram_summary(&B, fijas, variables, varias, perdidas); }
/* La partida de la NES es la RAM con batería del cartucho */
static int cargar_partida(const char *path) { return nes_sram_load(&A, path) && nes_sram_load(&B, path); }
static int guardar_partida(const char *path) { return nes_sram_save(&B, path); }

int main(int argc, char **argv)
{
    DiffOps o = {
        .titulo = recomp_name, .uso_rom = "rom.nes", .uso_extra = "", .cuadros = 300, .fps = 60.0988, .unidad = "PRG",
        .zonas = { "PRG", "RAM", "RAM del cartucho", "otra" },
        .cargar = cargar, .sha1 = sha1, .cargar_partida = cargar_partida, .guardar_partida = guardar_partida,
        .trazas = trazas, .reiniciar = reiniciar, .maquina = maquina, .modo = modo, .pad = poner_pad, .cuadro_solo = cuadro_solo,
        .vaciar = vaciar, .cuadro = cuadro, .ciclos = ciclos, .captura = captura, .golpe = golpe, .uncovered = uncovered, .ram = ram,
        .executed = executed,
    };
    return difftest_main(argc, argv, &o);
}
