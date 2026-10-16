/*
 * Prueba diferencial: corre el código recompilado y el intérprete lado a lado y se detiene en la primera
 * diferencia (Z80, RAM, mapper, VDP, PSG, mandos, RAM del cartucho...).
 *
 *   difftest rom.sms [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--model sms|gg]
 *                    [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]
 * (también vale la sintaxis de antes: difftest rom.sms cuadros [boton:desde-hasta ...])
 * Las mismas opciones que el difftest de los otros recompiladores.
 *
 * --seed S       suma a las entradas dadas un mando aleatorio (reproducible con la semilla).
 * --uncovered f  escribe, en el formato de la traza (smstrace: "BANCO:DIRECCION" y las líneas "ram"), lo que la
 *                máquina B tuvo que interpretar: sirve como --trace para recompilar y cubrirlo.
 * --screens d    guarda una captura PNG (cuadro_NNNNNN.png) cada --every N cuadros (600), para ver hasta dónde
 *                llegaron las entradas.
 * --bench        mide la velocidad del código recompilado y la del intérprete por separado, con las mismas
 *                entradas, sin comparar.
 * --executed f   escribe qué bytes de la ROM se ejecutaron (los registra la máquina A): una letra por byte,
 *                C = ejecutado, 64 por línea.
 *
 * La máquina B corre función por función (recompilada, o una instrucción del intérprete donde no hay código). Tras
 * cada paso, la máquina A (solo intérprete: z80_interp_only) avanza hasta el mismo ciclo y se comparan. Las dos
 * cuentan los ciclos igual, así que coinciden en cada límite de instrucción.
 *
 * El porcentaje de la línea final es de los ciclos que ejecutan instrucciones: los de un HALT que se saltan de una
 * vez (sms.c) no cuentan ni como recompilados ni como interpretados; la línea "Z80: ..." los muestra aparte.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp.h"
#include "cli.h"
#include "console.h"
#include "png.h"
#include "z80_ops.h"

static Sms A, B;

/* --model: la consola, como en las herramientas (cli.h no lo conoce: se saca antes) */
static int take_model(int *argc, char **argv)
{
    int model = MODEL_AUTO;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--model")) continue;
        if (i + 1 >= *argc || (model = model_from_name(argv[i + 1])) < MODEL_AUTO) return -2;
        for (int k = i; k + 2 <= *argc; k++) argv[k] = argv[k + 2];
        *argc -= 2;
        i--;
    }
    return model;
}

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

/* --bench: el recompilado (B) y el intérprete (A) por separado, con las mismas entradas */
static void bench(Cli *c)
{
    Sms *m[2] = { &B, &A };
    const char *name[2] = { "recompilado", "intérprete" };
    for (int k = 0; k < 2; k++) {
        cli_rewind(c);
        clock_t t0 = clock();
        for (long f = 0; f < c->frames; f++) {
            m[k]->pad[0] = cli_pad(c, f, 0);
            m[k]->pad[1] = cli_pad(c, f, 1);
            sms_run_frame(m[k]);
            m[k]->audio.n = 0;
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  %-12s %.2f s  (%.1fx tiempo real)\n", name[k], s, s > 0 ? c->frames / 59.92 / s : 0.0);
    }
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
    CMP(z80, "registros del Z80");
    if (A.cyc != B.cyc) { puts("  difiere: ciclos"); bad = 1; }
    CMP(ram, "RAM");
    CMP(map, "mapper");
    if (memcmp(&A.vdp, &B.vdp, offsetof(Vdp, fb))) { puts("  difiere: VDP (registros, VRAM, CRAM, contadores o líneas)"); bad = 1; }
    CMP(io, "E/S (control, mandos, puertos del Game Gear)");
    CMP(psg, "PSG");
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

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    int model = take_model(&argc, argv);
    if (model < MODEL_AUTO || cli_parse(&c, argc, argv, 1, 300)) {
        fprintf(stderr, "uso: difftest rom.sms [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--model sms|gg]\n"
                        "                      [--screens carpeta [--every N]] [--bench] [--executed archivo] [boton:desde-hasta ...]\n");
        return 2;
    }
    size_t len;
    u8 *rom = slurp(c.pos[0], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", c.pos[0]); return 2; }
    const char *err = NULL;
    if (sms_load(&A, rom, len, c.pos[0], model, &err) || sms_load(&B, rom, len, c.pos[0], model, &err)) {
        fprintf(stderr, "ROM: %s\n", err);
        return 2;
    }
    if (strcmp(B.cart.sha1, recomp_rom_sha1)) {
        fprintf(stderr, "esta ROM no es la que se recompiló (SHA1 %s, esperaba %s)\n", B.cart.sha1, recomp_rom_sha1);
        return 2;
    }
    long frames = c.frames;
    z80_stat_of = &B;                       /* cuánto del Z80 de B corre nativo */
    z80_interp_only = &A;                   /* A es la referencia: solo con el intérprete */
    if (c.bench) {
        printf("%s: %ld cuadros\n", recomp_name, frames);
        bench(&c);
        z80_stat_print();
        return 0;
    }
    if (c.uncovered) sms_trace_start(&B);   /* solo en B: lo que A (intérprete) ejecuta no cuenta */
    if (c.executed) sms_trace_start(&A);    /* A lo ejecuta todo con el intérprete: su traza es lo ejecutado */

    unsigned long calls = 0, recomp_calls = 0;
    u64 recomp_cycles = 0, halt_cycles = 0;
    u64 interp_zone[3] = { 0 };     /* ciclos del intérprete por zona del PC: ROM, RAM, otra */
    for (long f = 0; f < frames; f++) {
        A.pad[0] = B.pad[0] = cli_pad(&c, f, 0);
        A.pad[1] = B.pad[1] = cli_pad(&c, f, 1);
        sms_frame_begin(&A);
        sms_frame_begin(&B);
        while (!B.vdp.frame_done) {
            u16 pc0 = B.z80.pc;
            u64 c0 = B.cyc;
            int zone = bus_rom_offset(&B, pc0) >= 0 ? 0 : pc0 >= 0x8000 ? 1 : 2;
            u64 sk0 = z80_stat.skipped;
            int native = sms_recomp_step(&B);
            if (native) { recomp_calls++; recomp_cycles += B.cyc - c0; }
            else if (z80_stat.skipped != sk0) halt_cycles += B.cyc - c0;
            else interp_zone[zone] += B.cyc - c0;
            calls++;
            while (A.cyc < B.cyc && !A.vdp.frame_done) sms_step(&A);
            if (compare(0) || A.vdp.frame_done != B.vdp.frame_done) {
                printf("DIFERENCIA en el cuadro %ld, tras ejecutar desde $%04X (ciclo %llu, %s)\n",
                       f, pc0, (unsigned long long)B.cyc, native ? "recompilado" : "intérprete");
                print_cpu("A (intérprete) ", &A.z80);
                print_cpu("B (recompilado)", &B.z80);
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
           frames, calls, recomp_calls, B.cyc > halt_cycles ? 100.0 * (double)recomp_cycles / (double)(B.cyc - halt_cycles) : 0.0);
    if (c.uncovered) {
        size_t cnt = sms_trace_count(&B);
        if (!sms_trace_write(&B, c.uncovered)) { fprintf(stderr, "no pude escribir %s\n", c.uncovered); return 2; }
        printf("Instrucciones de ROM interpretadas: %zu\n", cnt);
        unsigned fixed, vol, multi, lost;
        sms_ram_summary(&B, &fixed, &vol, &multi, &lost);
        if (fixed || vol || lost)
            printf("Código en RAM: %u instrucciones fijas, %u con operandos variables, %u direcciones con varias versiones, "
                   "%u que cambian demasiado\n", fixed, vol, multi, lost);
    }
    if (c.executed && !write_executed(&A, c.executed)) { fprintf(stderr, "no pude escribir %s\n", c.executed); return 2; }
    return 0;
}
