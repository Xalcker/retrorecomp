/*
 * Prueba diferencial: corre el código recompilado y el intérprete lado a lado y
 * se detiene en la primera diferencia (registros, RAM, PPU, APU, mapper...).
 *
 *   difftest rom.nes [--frames N] [--input archivo] [--seed S] [--uncovered archivo]
 *                    [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: difftest rom.nes cuadros [boton:desde-hasta ...])
 * Las mismas opciones que difftest de GBRecomp.
 *
 * --seed S       suma a las entradas dadas un mando aleatorio (reproducible con la semilla).
 * --uncovered f  escribe, en formato de traza de nesrecomp ("OFFSET:DIRECCION"), las
 *                instrucciones de PRG que la máquina B tuvo que interpretar: sirven como
 *                --trace para recompilar y cubrirlas.
 * --screens d    guarda una captura PNG (cuadro_NNNNNN.png) cada --every N cuadros (600), para
 *                ver hasta dónde llegaron las entradas.
 * --bench        mide la velocidad del código recompilado y la del intérprete por separado, con
 *                las mismas entradas, sin comparar.
 * --executed f   escribe qué bytes de PRG se ejecutaron (los registra la máquina A): una letra
 *                por byte, C = ejecutado, 64 por línea; el formato de nesrecomp --codemap.
 *
 * La máquina B corre función por función (recompilada, o el intérprete donde no
 * hay código). Tras cada función, la máquina A (solo intérprete) avanza hasta el
 * mismo ciclo y se comparan. Ambas cuentan los ciclos igual, así que coinciden
 * en cada límite de instrucción.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp.h"
#include "sha1.h"
#include "cli.h"
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

/* --bench: el recompilado y el intérprete por separado, con las mismas entradas */
static void bench(Cli *c)
{
    NES *m[2] = { &B, &A };
    const char *name[2] = { "recompilado", "intérprete" };
    for (int k = 0; k < 2; k++) {
        cli_rewind(c);
        nes_force_interp = k;
        clock_t t0 = clock();
        for (long f = 0; f < c->frames; f++) {
            m[k]->pad[0] = cli_pad(c, f, 0);
            m[k]->pad[1] = cli_pad(c, f, 1);
            nes_run_frame(m[k]);
            m[k]->apu.nbuf = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  %-12s %.2f s  (%.0fx tiempo real)\n", name[k], s, s > 0 ? c->frames / 60.0988 / s : 0.0);
    }
    nes_force_interp = 0;
}

static u8 *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *len = sz;
    return b;
}

#define CMP(field, what) do { if (memcmp(&A.field, &B.field, sizeof A.field)) { puts("  difiere: " what); bad = 1; } } while (0)

/* Compara todo menos los búferes grandes de salida (full = incluye el cuadro y el audio) */
static int compare(int full)
{
    int bad = 0;
    CMP(cpu, "registros del CPU");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    CMP(ram, "RAM");
    if (memcmp(&A.ppu, &B.ppu, offsetof(Ppu, fb))) {
        puts("  difiere: estado de la PPU (registros, VRAM, paletas u OAM)"); bad = 1;
    }
    if (memcmp(&A.apu, &B.apu, offsetof(Apu, buf))) { puts("  difiere: APU"); bad = 1; }
    CMP(cart.reg, "registros del mapper");
    CMP(cart.prg_ram, "RAM del cartucho");
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

int main(int argc, char **argv)
{
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 300)) {
        fprintf(stderr, "uso: difftest rom.nes [--frames N] [--input archivo] [--seed S] [--uncovered archivo]\n"
                        "                      [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]\n");
        return 2;
    }
    const char *uncovered = c.uncovered;
    const char *pos[1] = { c.pos[0] };
    size_t len; u8 *rom = slurp(pos[0], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", pos[0]); return 2; }
    char sha[41]; sha1_hex(rom, len, sha);
    if (strcmp(sha, recomp_rom_sha1)) {
        fprintf(stderr, "esta ROM no es la que se recompiló (SHA1 %s, esperaba %s)\n", sha, recomp_rom_sha1);
        return 2;
    }
    const char *err = NULL;
    if (nes_load(&A, rom, len, &err) || nes_load(&B, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    long frames = c.frames;
    if (c.bench) {
        printf("%s: %ld cuadros\n", recomp_name, frames);
        bench(&c);
        return 0;
    }
    if (uncovered) nes_trace_start(&B);     /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c.executed) nes_trace_start(&A);    /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */

    unsigned long calls = 0, recomp_calls = 0;
    u64 recomp_cycles = 0;
    for (long f = 0; f < frames; f++) {
        A.pad[0] = B.pad[0] = cli_pad(&c, f, 0);
        A.pad[1] = B.pad[1] = cli_pad(&c, f, 1);
        A.ppu.frame_done = B.ppu.frame_done = 0;
        while (!B.ppu.frame_done) {
            u16 pc0 = B.cpu.pc; u64 c0 = B.cyc;
            RecompFn fn = recomp_lookup(&B, B.cpu.pc);
            if (fn && !B.cpu.jammed) { fn(&B); recomp_calls++; recomp_cycles += B.cyc - c0; }
            else cpu_step(&B);
            calls++;
            while (A.cyc < B.cyc) cpu_step(&A);
            if (compare(0) || A.ppu.frame_done != B.ppu.frame_done) {
                printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%04X (ciclo %llu, %s)\n"
                       "  A (intérprete) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n"
                       "  B (recompilado) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n",
                       f, pc0, (unsigned long long)B.cyc, fn ? "recompilado" : "intérprete",
                       A.cpu.pc, A.cpu.a, A.cpu.x, A.cpu.y, A.cpu.p, A.cpu.sp,
                       B.cpu.pc, B.cpu.a, B.cpu.x, B.cpu.y, B.cpu.p, B.cpu.sp);
                return 1;
            }
        }
        if (compare(1)) { printf("DIFERENCIA al final del cuadro %ld\n", f); return 1; }
        A.apu.nbuf = B.apu.nbuf = 0;
        if (c.screens && c.every > 0 && (f + 1) % c.every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/cuadro_%06ld.png", c.screens, f + 1);
            if (!save_screen(&B, path)) fprintf(stderr, "no pude escribir %s\n", path);
        }
    }
    printf("difftest: %ld cuadros iguales (%lu llamadas, %lu a código recompilado, %.1f%% de los ciclos)\n",
           frames, calls, recomp_calls, B.cyc ? 100.0 * recomp_cycles / B.cyc : 0.0);
    if (uncovered) {
        size_t cnt = 0;
        for (size_t i = 0; i < B.cart.prg_size; i++) cnt += B.trace[i] != 0;
        if (!nes_trace_write(&B, uncovered)) { fprintf(stderr, "no pude escribir %s\n", uncovered); return 2; }
        printf("Instrucciones de PRG interpretadas: %zu\n", cnt);
    }
    if (c.executed && !write_executed(&A, c.executed)) { fprintf(stderr, "no pude escribir %s\n", c.executed); return 2; }
    return 0;
}
