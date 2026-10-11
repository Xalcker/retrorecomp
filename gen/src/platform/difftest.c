/*
 * Prueba diferencial: corre el código recompilado y el intérprete lado a lado y se detiene en la
 * primera diferencia (68000, RAM, VDP, Z80, YM2612, PSG, mandos, cartucho...).
 *
 *   difftest rom.bin [--frames N] [--input archivo] [--seed S] [--uncovered archivo]
 *                    [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: difftest rom.bin cuadros [boton:desde-hasta ...])
 * Las mismas opciones que difftest de NESRecomp y GBRecomp.
 *
 * --seed S       suma a las entradas dadas un mando aleatorio (reproducible con la semilla).
 * --uncovered f  escribe, en formato de traza de genrecomp ("OFFSET:DIRECCION"), las instrucciones
 *                de ROM que la máquina B tuvo que interpretar: sirven como --trace para recompilar
 *                y cubrirlas.
 * --screens d    guarda una captura PNG (cuadro_NNNNNN.png) cada --every N cuadros (600), para ver
 *                hasta dónde llegaron las entradas.
 * --bench        mide la velocidad del código recompilado y la del intérprete por separado, con las
 *                mismas entradas, sin comparar.
 * --executed f   escribe qué bytes de ROM se ejecutaron (los registra la máquina A): una letra por
 *                byte, C = ejecutado, 64 por línea.
 *
 * La máquina B corre función por función (recompilada, o el intérprete donde no hay código). Tras
 * cada función, la máquina A (solo intérprete) avanza hasta el mismo ciclo y se comparan. Las dos
 * cuentan los ciclos igual, así que coinciden en cada límite de instrucción.
 */
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp.h"
#include "cli.h"
#include "console.h"
#include "png.h"
#include "sha1.h"

static Gen A, B;

/* Captura del cuadro actual de g en PNG */
static int save_screen(const Gen *g, const char *path)
{
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

/* --executed: cada instrucción que registró la traza de g marca sus bytes (opcode y extensión) */
static int write_executed(const Gen *g, const char *path)
{
    size_t size = g->cart.rom_size;
    const u8 *rom = g->cart.rom;
    u8 *done = calloc(size ? size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!done || !f) { free(done); if (f) fclose(f); return 0; }
    for (size_t off = 0; off + 1 < size; off += 2) {
        if (!g->trace[off >> 1]) continue;
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

/* --bench: el recompilado y el intérprete por separado, con las mismas entradas */
static void bench(Cli *c)
{
    Gen *m[2] = { &B, &A };
    const char *name[2] = { "recompilado", "intérprete" };
    for (int k = 0; k < 2; k++) {
        cli_rewind(c);
        gen_force_interp = k;
        clock_t t0 = clock();
        for (long f = 0; f < c->frames; f++) {
            m[k]->pad[0] = cli_pad(c, f, 0);
            m[k]->pad[1] = cli_pad(c, f, 1);
            gen_run_frame(m[k]);
            m[k]->audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  %-12s %.2f s  (%.1fx tiempo real)\n", name[k], s, s > 0 ? c->frames / 59.92 / s : 0.0);
    }
    gen_force_interp = 0;
}

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz > 0 ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = (size_t)sz;
    return b;
}

#define CMP(field, what) do { if (memcmp(&A.field, &B.field, sizeof A.field)) { puts("  difiere: " what); bad = 1; } } while (0)

/* Compara todo menos los búferes de salida (full = también la imagen y el audio listo) */
static int compare(int full)
{
    int bad = 0;
    CMP(cpu, "registros del 68000");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    CMP(ram, "RAM del 68000");
    if (memcmp(&A.vdp, &B.vdp, offsetof(Vdp, fb))) { puts("  difiere: VDP (registros, VRAM, CRAM, VSRAM o líneas)"); bad = 1; }
    CMP(io, "E/S (puertos de los mandos)");
    CMP(eeprom, "EEPROM serie");
    CMP(cart.bank, "bancos del mapper");
    if (A.cart.a130f1 != B.cart.a130f1 || A.cart.map_ver != B.cart.map_ver) { puts("  difiere: control de la SRAM o del mapper"); bad = 1; }
    if (A.cart.sram_size && memcmp(A.cart.sram_data, B.cart.sram_data, A.cart.sram_size)) { puts("  difiere: SRAM/EEPROM del cartucho"); bad = 1; }
    if (A.z80_busreq != B.z80_busreq || A.z80_reset != B.z80_reset || A.z80_bank != B.z80_bank ||
        A.tmss_reg != B.tmss_reg || A.tmss_locked != B.tmss_locked) { puts("  difiere: control del Z80 o TMSS"); bad = 1; }
    CMP(z80, "registros del Z80");
    if (A.z80_mclk != B.z80_mclk || A.z80_irq_from != B.z80_irq_from || A.z80_irq_until != B.z80_irq_until) {
        puts("  difiere: tiempo o interrupción del Z80"); bad = 1;
    }
    CMP(zram, "RAM del Z80");
    CMP(psg, "PSG");
    CMP(ym, "YM2612");
    if (memcmp(&A.audio, &B.audio, offsetof(Audio, buf))) { puts("  difiere: mezcla de audio"); bad = 1; }
    if (A.open_bus != B.open_bus) { puts("  difiere: bus abierto"); bad = 1; }
    CMP(exc_count, "excepciones atendidas");
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
            B.jb = &jb;
            if (setjmp(jb) == 0) r = fn(&B);
            else { gen_after_step(&B); r = 1; }     /* un error de dirección terminó la instrucción */
            B.jb = NULL;
            if (r) return 1;
        }
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

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 300)) {
        fprintf(stderr, "uso: difftest rom.bin [--frames N] [--input archivo] [--seed S] [--uncovered archivo]\n"
                        "                      [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]\n");
        return 2;
    }
    size_t len;
    u8 *rom = slurp(c.pos[0], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    const char *err = NULL;
    if (gen_load(&A, rom, len, &err) || gen_load(&B, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    if (strcmp(B.cart.sha1, recomp_rom_sha1)) {
        fprintf(stderr, "esta ROM no es la que se recompiló (SHA1 %s, esperaba %s)\n", B.cart.sha1, recomp_rom_sha1);
        return 2;
    }
    long frames = c.frames;
    if (c.bench) {
        printf("%s: %ld cuadros\n", recomp_name, frames);
        bench(&c);
        return 0;
    }
    if (c.uncovered) gen_trace_start(&B);   /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c.executed) gen_trace_start(&A);    /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */

    unsigned long calls = 0, recomp_calls = 0;
    u64 recomp_cycles = 0;
    for (long f = 0; f < frames; f++) {
        A.pad[0] = B.pad[0] = cli_pad(&c, f, 0);
        A.pad[1] = B.pad[1] = cli_pad(&c, f, 1);
        A.vdp.frame_done = B.vdp.frame_done = 0;
        while (!B.vdp.frame_done) {
            u32 pc0 = B.cpu.pc;
            u64 c0 = B.cyc;
            int native = step_b();
            if (native) { recomp_calls++; recomp_cycles += B.cyc - c0; }
            calls++;
            while (A.cyc < B.cyc && !A.vdp.frame_done) { m68k_step(&A); gen_after_step(&A); }
            if (compare(0) || A.vdp.frame_done != B.vdp.frame_done) {
                printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%06X (ciclo %llu, %s)\n",
                       f, pc0, (unsigned long long)B.cyc, native ? "recompilado" : "intérprete");
                print_cpu("A (intérprete) ", &A.cpu);
                print_cpu("B (recompilado)", &B.cpu);
                return 1;
            }
        }
        audio_end_frame(&A);
        audio_end_frame(&B);
        if (compare(1)) { printf("DIFERENCIA al final del cuadro %ld\n", f); return 1; }
        A.audio.n = B.audio.n = 0;
        if (c.screens && c.every > 0 && (f + 1) % c.every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/cuadro_%06ld.png", c.screens, f + 1);
            if (!save_screen(&B, path)) fprintf(stderr, "no pude escribir %s\n", path);
        }
    }
    printf("difftest: %ld cuadros iguales (%lu llamadas, %lu a código recompilado, %.1f%% de los ciclos)\n",
           frames, calls, recomp_calls, B.cyc ? 100.0 * (double)recomp_cycles / (double)B.cyc : 0.0);
    if (c.uncovered) {
        size_t cnt = gen_trace_count(&B);
        if (!gen_trace_write(&B, c.uncovered)) { fprintf(stderr, "no pude escribir %s\n", c.uncovered); return 2; }
        printf("Instrucciones de ROM interpretadas: %zu\n", cnt);
    }
    if (c.executed && !write_executed(&A, c.executed)) { fprintf(stderr, "no pude escribir %s\n", c.executed); return 2; }
    return 0;
}
