/*
 * Prueba diferencial: corre el código recompilado y el intérprete lado a lado y se detiene en la
 * primera diferencia (68000, RAM, LSPC, paleta, Z80, YM2610, mandos, tarjeta...).
 *
 *   difftest juego [--frames N] [--input archivo] [--seed S] [--uncovered archivo]
 *                  [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: difftest juego cuadros [boton:desde-hasta ...])
 * Las mismas opciones que difftest de NESRecomp, GBRecomp y GENRecomp.
 *
 * --seed S       suma a las entradas dadas un mando aleatorio (reproducible con la semilla).
 * --uncovered f  escribe, en formato de traza ("BANCO:DIRECCION"), las instrucciones de ROM (del
 *                juego y de la BIOS) que la máquina B tuvo que interpretar: sirven como --trace para
 *                recompilar y cubrirlas.
 * --screens d    guarda una captura PNG (cuadro_NNNNNN.png) cada --every N cuadros (600), para ver
 *                hasta dónde llegaron las entradas.
 * --bench        mide la velocidad del código recompilado y la del intérprete por separado, con las
 *                mismas entradas, sin comparar.
 * --executed f   escribe qué bytes del programa se ejecutaron (los registra la máquina A): una letra
 *                por byte, C = ejecutado, 64 por línea.
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

static Neo A, B;

/* Captura del cuadro actual de g en PNG */
static int save_screen(const Neo *g, const char *path)
{
    static unsigned char rgb[SCREEN_W * SCREEN_H * 3];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u32 v = g->fb[i];
        rgb[i * 3] = (u8)(v >> 16); rgb[i * 3 + 1] = (u8)(v >> 8); rgb[i * 3 + 2] = (u8)v;
    }
    return png_write_rgb(path, rgb, SCREEN_W, SCREEN_H);
}

/* --executed: cada instrucción del juego que registró la traza de g marca sus bytes */
static int write_executed(const Neo *g, const char *path)
{
    size_t size = g->cart.len[REG_P];
    u8 *done = calloc(size ? size : 1, 1);
    FILE *f = fopen(path, "w");
    if (!done || !f) { free(done); if (f) fclose(f); return 0; }
    for (size_t off = 0; off + 1 < size; off += 2) {
        if (!g->trace[off >> 1]) continue;
        int len = 2 + 2 * m68k_ext_words(cart_p16(&g->cart, (u32)off));
        for (int k = 0; k < len && off + k < size; k++) done[off + k] = 1;
    }
    for (size_t off = 0; off < size; off++) {
        fputc(done[off] ? 'C' : '.', f);
        if (off % 64 == 63) fputc('\n', f);
    }
    free(done);
    return fclose(f) == 0;
}

static double fps(void) { return MCLK_HZ / (M68K_PER_FRAME * MCLK_PER_M68K); }

/* --bench: el recompilado y el intérprete por separado, con las mismas entradas */
static void bench(Cli *c)
{
    Neo *m[2] = { &B, &A };
    const char *name[2] = { "recompilado", "intérprete" };
    for (int k = 0; k < 2; k++) {
        cli_rewind(c);
        neo_force_interp = k;
        clock_t t0 = clock();
        for (long f = 0; f < c->frames; f++) {
            m[k]->pad[0] = cli_pad(c, f, 0);
            m[k]->pad[1] = cli_pad(c, f, 1);
            neo_run_frame(m[k]);
            m[k]->audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  %-12s %.2f s  (%.1fx tiempo real)\n", name[k], s, s > 0 ? c->frames / fps() / s : 0.0);
    }
    neo_force_interp = 0;
}

#define CMP(field, what) do { if (memcmp(&A.field, &B.field, sizeof A.field)) { puts("  difiere: " what); bad = 1; } } while (0)

/* Compara todo menos los búferes de salida (full = también la imagen y el audio listo) */
static int compare(int full)
{
    int bad = 0;
    CMP(cpu, "registros del 68000");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    CMP(ram, "RAM del 68000");
    CMP(lspc.vram, "VRAM");
    if (memcmp(&A.lspc.vram_addr, &B.lspc.vram_addr, sizeof A.lspc - offsetof(Lspc, vram_addr))) {
        puts("  difiere: registros del LSPC (dirección, modo, temporizador, interrupciones, líneas)"); bad = 1;
    }
    CMP(pal, "paleta");
    if (A.latch != B.latch || A.p2_bank != B.p2_bank || A.map_ver != B.map_ver || A.poutput != B.poutput || A.crdbank != B.crdbank) {
        puts("  difiere: latch del sistema, banco de P2 o salidas"); bad = 1;
    }
    if ((A.card.data != NULL) != (B.card.data != NULL) || (A.card.data && memcmp(A.card.data, B.card.data, A.card.size))) {
        puts("  difiere: tarjeta de memoria"); bad = 1;
    }
    CMP(z80, "registros del Z80");
    if (A.z80_mclk != B.z80_mclk || memcmp(A.z80_bank, B.z80_bank, sizeof A.z80_bank) || A.sound_cmd != B.sound_cmd ||
        A.sound_reply != B.sound_reply || A.nmi_pending != B.nmi_pending || A.nmi_enabled != B.nmi_enabled ||
        A.nmi_line != B.nmi_line) { puts("  difiere: tiempo, bancos o comunicación del Z80"); bad = 1; }
    CMP(zram, "RAM del Z80");
    CMP(ym, "YM2610");
    if (memcmp(&A.audio, &B.audio, offsetof(Audio, buf))) { puts("  difiere: mezcla de audio"); bad = 1; }
    if (A.open_bus != B.open_bus) { puts("  difiere: bus abierto"); bad = 1; }
    CMP(exc_count, "excepciones atendidas");
    if (full) {
        if (memcmp(A.fb, B.fb, sizeof A.fb)) { puts("  difiere: imagen"); bad = 1; }
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
            else { neo_after_step(&B); r = 1; }     /* un error de dirección terminó la instrucción */
            B.jb = NULL;
            if (r) return 1;
        }
    }
    m68k_step(&B);
    neo_after_step(&B);
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
        fprintf(stderr, "uso: difftest juego [--frames N] [--input archivo] [--seed S] [--uncovered archivo]\n"
                        "                    [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]\n");
        return 2;
    }
    const char *err = NULL;
    if (neo_load(&A, c.pos[0], NULL, &err) || neo_load(&B, c.pos[0], NULL, &err)) { fprintf(stderr, "%s: %s\n", c.pos[0], err); return 2; }
    if (strcmp(B.cart.sha1, recomp_rom_sha1)) {
        fprintf(stderr, "este juego no es el que se recompiló (SHA1 %s, esperaba %s)\n", B.cart.sha1, recomp_rom_sha1);
        return 2;
    }
    long frames = c.frames;
    z80_stat_of = &B;                       /* cuánto del Z80 de B corre nativo */
    z80_interp_only = &A;                   /* A es la referencia: su Z80 va con el intérprete */
    if (c.bench) {
        printf("%s: %ld cuadros\n", recomp_name, frames);
        bench(&c);
        z80_stat_print();
        return 0;
    }
    if (c.uncovered) neo_trace_start(&B);   /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c.executed) neo_trace_start(&A);    /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */

    unsigned long calls = 0, recomp_calls = 0;
    u64 recomp_cycles = 0;
    u64 interp_zone[3] = { 0 };     /* ciclos del intérprete por zona del PC: ROM (P y BIOS), RAM, otra */
    for (long f = 0; f < frames; f++) {
        A.pad[0] = B.pad[0] = cli_pad(&c, f, 0);
        A.pad[1] = B.pad[1] = cli_pad(&c, f, 1);
        A.lspc.frame_done = B.lspc.frame_done = 0;
        while (!B.lspc.frame_done) {
            u32 pc0 = B.cpu.pc;
            u64 c0 = B.cyc;
            int native = step_b();
            if (native) { recomp_calls++; recomp_cycles += B.cyc - c0; }
            else interp_zone[neo_code_offset(&B, pc0) >= 0 ? 0 : (pc0 & 0xFF0000) == 0x100000 ? 1 : 2] += B.cyc - c0;
            calls++;
            while (A.cyc < B.cyc && !A.lspc.frame_done) { m68k_step(&A); neo_after_step(&A); }
            if (compare(0) || A.lspc.frame_done != B.lspc.frame_done) {
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
    u64 interp = interp_zone[0] + interp_zone[1] + interp_zone[2];
    if (interp)
        printf("Intérprete por zona: ROM %.1f%%, RAM %.1f%%, otra %.1f%%\n", 100.0 * (double)interp_zone[0] / (double)interp,
               100.0 * (double)interp_zone[1] / (double)interp, 100.0 * (double)interp_zone[2] / (double)interp);
    z80_stat_print();
    printf("difftest: %ld cuadros iguales (%lu llamadas, %lu a código recompilado, %.1f%% de los ciclos)\n",
           frames, calls, recomp_calls, B.cyc ? 100.0 * (double)recomp_cycles / (double)B.cyc : 0.0);
    if (c.uncovered) {
        size_t cnt = neo_trace_count(&B);
        if (!neo_trace_write(&B, c.uncovered)) { fprintf(stderr, "no pude escribir %s\n", c.uncovered); return 2; }
        printf("Instrucciones de ROM interpretadas: %zu\n", cnt);
        unsigned fixed, vol, multi, lost;
        neo_ram_summary(&B, &fixed, &vol, &multi, &lost);
        if (fixed || vol || lost)
            printf("Código en RAM: %u instrucciones fijas, %u con operandos variables, %u direcciones con varias versiones, "
                   "%u que cambian demasiado\n", fixed, vol, multi, lost);
    }
    if (c.executed && !write_executed(&A, c.executed)) { fprintf(stderr, "no pude escribir %s\n", c.executed); return 2; }
    return 0;
}
