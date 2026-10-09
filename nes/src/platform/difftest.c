/*
 * Prueba diferencial: corre el código recompilado y el intérprete lado a lado y
 * se detiene en la primera diferencia (registros, RAM, PPU, APU, mapper...).
 *
 *   difftest rom.nes [cuadros] [boton:desde-hasta ...]
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
#include "recomp.h"
#include "sha1.h"

static NES A, B;

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
    if (argc < 2) { fprintf(stderr, "uso: difftest rom.nes [cuadros] [boton:desde-hasta ...]\n"); return 2; }
    size_t len; u8 *rom = slurp(argv[1], &len);
    if (!rom) { fprintf(stderr, "no pude leer %s\n", argv[1]); return 2; }
    char sha[41]; sha1_hex(rom, len, sha);
    if (strcmp(sha, recomp_rom_sha1)) {
        fprintf(stderr, "esta ROM no es la que se recompiló (SHA1 %s, esperaba %s)\n", sha, recomp_rom_sha1);
        return 2;
    }
    const char *err = NULL;
    if (nes_load(&A, rom, len, &err) || nes_load(&B, rom, len, &err)) { fprintf(stderr, "ROM: %s\n", err); return 2; }
    int frames = argc > 2 ? atoi(argv[2]) : 300;

    static const char *nm[] = { "a", "b", "select", "start", "up", "down", "left", "right" };
    struct { int b, f, t; } h[64]; int nh = 0;
    for (int i = 3; i < argc && nh < 64; i++) {
        char name[16]; int a, b;
        if (sscanf(argv[i], "%15[a-z]:%d-%d", name, &a, &b) == 3)
            for (int k = 0; k < 8; k++) if (!strcmp(name, nm[k])) { h[nh].b = 1 << k; h[nh].f = a; h[nh].t = b; nh++; }
    }

    unsigned long calls = 0, recomp_calls = 0;
    u64 recomp_cycles = 0;
    for (int f = 0; f < frames; f++) {
        u8 pad = 0;
        for (int i = 0; i < nh; i++) if (f >= h[i].f && f <= h[i].t) pad |= h[i].b;
        A.pad[0] = B.pad[0] = pad;
        A.ppu.frame_done = B.ppu.frame_done = 0;
        while (!B.ppu.frame_done) {
            u16 pc0 = B.cpu.pc; u64 c0 = B.cyc;
            RecompFn fn = recomp_lookup(&B, B.cpu.pc);
            if (fn && !B.cpu.jammed) { fn(&B); recomp_calls++; recomp_cycles += B.cyc - c0; }
            else cpu_step(&B);
            calls++;
            while (A.cyc < B.cyc) cpu_step(&A);
            if (compare(0) || A.ppu.frame_done != B.ppu.frame_done) {
                printf("DIFERENCIA en el cuadro %d, tras ejecutar desde $%04X (ciclo %llu, %s)\n"
                       "  A (intérprete) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n"
                       "  B (recompilado) PC=$%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n",
                       f, pc0, (unsigned long long)B.cyc, fn ? "recompilado" : "intérprete",
                       A.cpu.pc, A.cpu.a, A.cpu.x, A.cpu.y, A.cpu.p, A.cpu.sp,
                       B.cpu.pc, B.cpu.a, B.cpu.x, B.cpu.y, B.cpu.p, B.cpu.sp);
                return 1;
            }
        }
        if (compare(1)) { printf("DIFERENCIA al final del cuadro %d\n", f); return 1; }
        A.apu.nbuf = B.apu.nbuf = 0;
    }
    printf("difftest: %d cuadros iguales (%lu llamadas, %lu a código recompilado, %.1f%% de los ciclos)\n",
           frames, calls, recomp_calls, B.cyc ? 100.0 * recomp_cycles / B.cyc : 0.0);
    return 0;
}
