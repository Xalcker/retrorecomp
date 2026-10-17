/*
 * Prueba diferencial de Genesis: corre el código recompilado y el intérprete lado a lado y se detiene en la primera diferencia
 * (68000, RAM, VDP, Z80, YM2612, PSG, mandos, cartucho...). Las opciones, la carga, el bench y el final son los de
 * difftest_comun.h (en ../shared); aquí va lo propio de la consola.
 *
 * La máquina B corre función por función (recompilada, o el intérprete donde no hay código). Tras cada función, la máquina A (solo
 * intérprete) avanza hasta el mismo ciclo y se comparan. Las dos cuentan los ciclos igual, así que coinciden en cada límite de
 * instrucción.
 */
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"
#include "difftest_comun.h"
#include "png.h"
#include "sha1.h"

static Gen A, B;

/* Captura del cuadro actual de B en PNG */
static int captura(const char *path)
{
    const Gen *g = &B;
    int w = g->vdp.width ? g->vdp.width : 320, h = g->vdp.height ? g->vdp.height : 224;
    static unsigned char rgb[FB_W * FB_H * 3];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 v = vdp_argb(g->vdp.fb[y * FB_W + x]);
            unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (u8)(v >> 16); p[1] = (u8)(v >> 8); p[2] = (u8)v;
        }
    return png_write_rgb(path, rgb, w, h);
}

/* --executed: cada instrucción que registró la traza de A marca sus bytes (opcode y extensión) */
static int executed(const char *path)
{
    size_t size = A.cart.rom_size;
    const u8 *rom = A.cart.rom;
    u8 *done = calloc(size ? size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!done || !f) { free(done); if (f) fclose(f); return 0; }
    for (size_t off = 0; off + 1 < size; off += 2) {
        if (!A.trace[off >> 1]) continue;
        int len = 2 + 2 * m68k_ext_words((u16)(rom[off] << 8 | rom[off + 1]));
        for (int k = 0; k < len && off + k < size; k++) done[off + k] = 1;
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
    DT_CMP(cpu, "registros del 68000");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    DT_CMP(ram, "RAM del 68000");
    if (memcmp(&A.vdp, &B.vdp, offsetof(Vdp, fb))) { puts("  difiere: VDP (registros, VRAM, CRAM, VSRAM o líneas)"); bad = 1; }
    DT_CMP(io, "E/S (puertos de los mandos)");
    DT_CMP(eeprom, "EEPROM serie");
    DT_CMP(cart.bank, "bancos del mapper");
    if (A.cart.a130f1 != B.cart.a130f1 || A.cart.map_ver != B.cart.map_ver) { puts("  difiere: control de la SRAM o del mapper"); bad = 1; }
    if (A.cart.sram_size && memcmp(A.cart.sram_data, B.cart.sram_data, A.cart.sram_size)) { puts("  difiere: SRAM/EEPROM del cartucho"); bad = 1; }
    if (A.z80_busreq != B.z80_busreq || A.z80_reset != B.z80_reset || A.z80_bank != B.z80_bank ||
        A.tmss_reg != B.tmss_reg || A.tmss_locked != B.tmss_locked) { puts("  difiere: control del Z80 o TMSS"); bad = 1; }
    DT_CMP(z80, "registros del Z80");
    if (A.z80_mclk != B.z80_mclk || A.z80_irq_from != B.z80_irq_from || A.z80_irq_until != B.z80_irq_until) {
        puts("  difiere: tiempo o interrupción del Z80"); bad = 1;
    }
    DT_CMP(zram, "RAM del Z80");
    DT_CMP(psg, "PSG");
    DT_CMP(ym, "YM2612");
    if (memcmp(&A.audio, &B.audio, offsetof(Audio, buf))) { puts("  difiere: mezcla de audio"); bad = 1; }
    if (A.open_bus != B.open_bus) { puts("  difiere: bus abierto"); bad = 1; }
    DT_CMP(exc_count, "excepciones atendidas");
    if (full) {
        if (memcmp(A.vdp.fb, B.vdp.fb, sizeof A.vdp.fb)) { puts("  difiere: imagen"); bad = 1; }
        if (A.audio.n != B.audio.n || memcmp(A.audio.buf, B.audio.buf, (size_t)A.audio.n * 2 * sizeof A.audio.buf[0])) {
            puts("  difiere: audio"); bad = 1;
        }
    }
    return bad;
}

/* Un paso de la máquina B: una función recompilada si la hay, si no una instrucción del intérprete.
 * Devuelve 1 si corrió código recompilado. */
static int step_b(void)
{
    if (m68k_native_ok(&B.cpu)) {
        RecompFn fn = recomp_lookup(&B, B.cpu.pc);
        if (fn) {
            jmp_buf jb;
            volatile int r = 0;
            u64 c0 = B.cyc, espera0 = metrica.espera;
            B.jb = &jb;
            if (setjmp(jb) == 0) r = fn(&B);
            else { gen_after_step(&B); r = 1; }     /* un error de dirección terminó la instrucción */
            B.jb = NULL;
            if (r) { METRICA(&B, nat, B.cyc - c0 - (metrica.espera - espera0)); return 1; }    /* como native() */
        }
        gen_motivo(&B, B.cpu.pc);       /* sin código recompilado para el PC: el motivo (metrica.h) */
    }
    m68k_step(&B);
    gen_after_step(&B);
    return 0;
}

static void print_cpu(const char *who, const M68k *c)
{
    printf("  %s PC=$%06X SR=%04X\n   ", who, c->pc, m68k_get_sr(c));
    for (int i = 0; i < 8; i++) printf(" D%d=%08X", i, c->d[i]);
    printf("\n   ");
    for (int i = 0; i < 8; i++) printf(" A%d=%08X", i, c->a[i]);
    printf("\n");
}

/* Un cuadro: B avanza paso a paso y A lo alcanza tras cada uno; se comparan en cada límite de instrucción */
static int cuadro(DiffCtx *x, long f)
{
    Cli *c = x->c;
    A.pad[0] = B.pad[0] = cli_pad(c, f, 0);
    A.pad[1] = B.pad[1] = cli_pad(c, f, 1);
    A.vdp.frame_done = B.vdp.frame_done = 0;
    while (!B.vdp.frame_done) {
        u32 pc0 = B.cpu.pc;
        u64 c0 = B.cyc;
        int native = step_b();
        if (c->estricto && metrica_estricto(f, 6, NULL)) return DT_ESTRICTO;
        if (native) x->recompiladas++;
        else {
            int zona = pc0 < 0x400000 ? 0 : pc0 >= 0xE00000 ? 1 : 2;
            x->zona[zona] += B.cyc - c0;
            if (zona == 0) dt_golpe(x, pc0);
        }
        x->llamadas++;
        while (A.cyc < B.cyc && !A.vdp.frame_done) { m68k_step(&A); gen_after_step(&A); }
        if (compare(0) || A.vdp.frame_done != B.vdp.frame_done) {
            printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%06X (ciclo %llu, %s)\n",
                   f, pc0, (unsigned long long)B.cyc, native ? "recompilado" : "intérprete");
            print_cpu("A (intérprete) ", &A.cpu);
            print_cpu("B (recompilado)", &B.cpu);
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
    (void)c;
    static char msg[200];
    const char *err = NULL;
    if (!gen_load(&A, rom, len, &err) && !gen_load(&B, rom, len, &err)) return NULL;
    snprintf(msg, sizeof msg, "ROM: %s", err);
    return msg;
}

static const char *sha1(const u8 *rom, size_t len) { (void)rom; (void)len; return B.cart.sha1; }

static const char *preparar(const Cli *c)
{
    (void)c;
    z80_stat_of = &B;                       /* cuánto del Z80 de B corre nativo */
    z80_interp_only = &A;                   /* A es la referencia: su Z80 va con el intérprete */
    return NULL;
}

static void trazas(const Cli *c)
{
    if (c->uncovered) gen_trace_start(&B);  /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c->executed) gen_trace_start(&A);   /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */
}

static void reiniciar(void) { metrica_reiniciar(&B); }          /* la métrica común: la de B (metrica.h) */
static void *maquina(int k) { return k ? (void *)&A : (void *)&B; }
static void modo(int k) { gen_force_interp = k; }
static void poner_pad(void *g, unsigned p1, unsigned p2) { ((Gen *)g)->pad[0] = (u16)p1; ((Gen *)g)->pad[1] = (u16)p2; }
static void cuadro_solo(void *g) { gen_run_frame(g); }
static void vaciar(void *g) { ((Gen *)g)->audio.n = 0; }
static u64 ciclos(void) { return B.cyc; }
static void extra(void) { z80_stat_print(); }
static void golpe(char *out, size_t size, u32 clave) { snprintf(out, size, "$%06X", clave); }

static size_t uncovered(const char *path, int *ok)
{
    size_t cnt = gen_trace_count(&B);
    *ok = gen_trace_write(&B, path);
    return cnt;
}

static void ram(unsigned *fijas, unsigned *variables, unsigned *varias, unsigned *perdidas) { gen_ram_summary(&B, fijas, variables, varias, perdidas); }
static int cargar_partida(const char *path) { return gen_sram_load(&A, path) && gen_sram_load(&B, path); }
static int guardar_partida(const char *path) { return gen_sram_save(&B, path); }

int main(int argc, char **argv)
{
    DiffOps o = {
        .titulo = recomp_name, .uso_rom = "rom.bin", .uso_extra = "", .cuadros = 300, .fps = 59.92, .unidad = "ROM",
        .zonas = { "ROM", "RAM", "otra" },
        .cargar = cargar, .sha1 = sha1, .preparar = preparar, .cargar_partida = cargar_partida, .guardar_partida = guardar_partida,
        .trazas = trazas, .reiniciar = reiniciar, .maquina = maquina, .modo = modo, .pad = poner_pad, .cuadro_solo = cuadro_solo,
        .vaciar = vaciar, .cuadro = cuadro, .ciclos = ciclos, .captura = captura, .extra = extra, .golpe = golpe, .uncovered = uncovered, .ram = ram,
        .executed = executed,
    };
    return difftest_main(argc, argv, &o);
}
