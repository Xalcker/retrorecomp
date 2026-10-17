/*
 * Prueba diferencial de Game Boy: corre la ROM con el código recompilado y con el intérprete puro, con las mismas entradas, y
 * compara el estado completo (CPU, ciclos, RAM, VRAM, OAM, I/O, timer, PPU, RAM del cartucho) al final de cada cuadro. Si algo
 * difiere, regresa al inicio del cuadro y avanza instrucción por instrucción para reportar la primera diferencia exacta. Las
 * opciones, la carga, el bench y el final son los de difftest_comun.h (en ../shared); aquí va lo propio de la consola:
 *   --model dmg|cgb|sgb, --palette up+a (el modo de compatibilidad del GBC), y --uncovered, que además de las direcciones de ROM
 *   lleva los bytes del código que corrió desde WRAM o HRAM (ramtrace.h).
 * "--estricto" falla al final del cuadro en que el intérprete ejecutó una instrucción de ROM que no está recompilada; el código en RAM
 * y lo que el despachador interpreta a propósito (tras EI, bug de HALT) sí pasan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp.h"
#include "difftest_comun.h"
#include "png.h"
#include "ramtrace.h"
#include "romtrace.h"
#include "sha1.h"

static GB rec, ref, snap_rec, snap_ref;

/* --- Cobertura: instrucciones de ROM que no corrieron recompiladas --- */

#define MAX_BANKS ROM_MAX_BANKS
static u32 *interp_hits[ROM_SLOTS];     /* por ranura (romtrace.h) */

/* Direcciones más ejecutadas por el intérprete, para mostrarlas en el log */
#define TOP_HITS 20
typedef struct { u32 bank; u16 addr; u32 n; } Hit;

/* Orden del top: más ejecuciones primero; en empate, la de menor dirección */
static int hit_before(const Hit *a, const Hit *b)
{
    if (a->n != b->n) return a->n > b->n;
    if (a->bank != b->bank) return a->bank < b->bank;
    return a->addr < b->addr;
}

/* Mete h en el top (ordenado, de a lo más TOP_HITS entradas) si le toca */
static void top_insert(Hit *top, unsigned *ntop, Hit h)
{
    if (*ntop < TOP_HITS) (*ntop)++;
    else if (!hit_before(&h, &top[TOP_HITS - 1])) return;
    unsigned i = *ntop - 1;
    while (i > 0 && hit_before(&h, &top[i - 1])) {
        top[i] = top[i - 1];
        i--;
    }
    top[i] = h;
}

/* Instrucciones del intérprete fuera de la ROM ($8000-$FFFF): cuántas veces y los bytes que había
 * en esa dirección la primera vez, para reconocer la rutina en el log. Las versiones del código que
 * corrieron en WRAM y HRAM, para --uncovered, las lleva ramtrace.h, que comparte con el ejecutable
 * del juego */
#define RAM_BYTES 8
static struct { u32 n; u8 first[RAM_BYTES]; } ram_hits[0x8000];

/* --- Llamadas al código recompilado (el resumen de los cinco recompiladores) ---
 * El despachador del runtime llama a la función que le da recomp_lookup; esta versión la
 * envuelve para contarlas. Las llamadas son las funciones recompiladas más las
 * instrucciones del intérprete, como en NESRecomp. */

static RecompFn counted_fn;
static unsigned long recomp_calls;

static void counted(GB *g)
{
    counted_fn(g);
    recomp_calls++;
}

static RecompFn counting_lookup(GB *g, u16 pc)
{
    counted_fn = recomp_lookup(g, pc);
    return counted_fn ? counted : NULL;
}

/* --- Bytes de ROM ejecutados (por la máquina de referencia) --- */

static u8 *executed[MAX_BANKS];

static void mark_executed(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 bank;
    u16 off;
    if (pc < 0x4000) { bank = g->mbc.bank0; off = pc; }
    else if (pc < 0x8000) { bank = g->mbc.bankx; off = (u16)(pc - 0x4000); }
    else return;
    if (bank >= MAX_BANKS) return;
    if (!executed[bank]) executed[bank] = calloc(0x4000, 1);
    int len = insn_length(gb_peek(g, pc));
    for (int i = 0; i < len && off + i < 0x4000; i++) executed[bank][off + i] = 1;
}

/* Instrucciones del intérprete por zona: ROM, RAM del cartucho, WRAM, HRAM. */
static u64 interp_zone[4];

static void count_interp(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 slot;
    u16 off;
    interp_zone[pc < 0x8000 ? 0 : pc < 0xC000 ? 1 : pc < 0xFF80 ? 2 : 3]++;
    if (pc >= 0x8000) {
        if (!ram_hits[pc - 0x8000].n++)
            for (unsigned i = 0; i < RAM_BYTES; i++) ram_hits[pc - 0x8000].first[i] = gb_peek(g, (u16)(pc + i));
        ramtrace_observe(g, pc);
        return;
    }
    if (!rom_slot(g, pc, &slot, &off)) return;
    if (!interp_hits[slot]) interp_hits[slot] = calloc(0x4000, sizeof(u32));
    interp_hits[slot][off]++;
}

/* --- Comparación --- */

static char diff_msg[256];

#define CMP_FIELD(name, a, b) do {                                           \
        if ((a) != (b)) {                                                    \
            snprintf(diff_msg, sizeof diff_msg, "%s: recompilado=%llX intérprete=%llX", \
                     name, (unsigned long long)(a), (unsigned long long)(b)); \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int cmp_mem(const char *name, u16 base, const u8 *a, const u8 *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            snprintf(diff_msg, sizeof diff_msg, "%s $%04X: recompilado=$%02X intérprete=$%02X",
                     name, (unsigned)(base + i), a[i], b[i]);
            return 0;
        }
    }
    return 1;
}

static int same_state(GB *a, GB *b)
{
    CMP_FIELD("PC", a->cpu.pc, b->cpu.pc);
    CMP_FIELD("SP", a->cpu.sp, b->cpu.sp);
    CMP_FIELD("A", a->cpu.a, b->cpu.a);
    CMP_FIELD("F", a->cpu.f, b->cpu.f);
    CMP_FIELD("B", a->cpu.b, b->cpu.b);
    CMP_FIELD("C", a->cpu.c, b->cpu.c);
    CMP_FIELD("D", a->cpu.d, b->cpu.d);
    CMP_FIELD("E", a->cpu.e, b->cpu.e);
    CMP_FIELD("H", a->cpu.h, b->cpu.h);
    CMP_FIELD("L", a->cpu.l, b->cpu.l);
    CMP_FIELD("IME", a->cpu.ime, b->cpu.ime);
    CMP_FIELD("EI pendiente", a->cpu.ei_pend, b->cpu.ei_pend);
    CMP_FIELD("HALT", a->cpu.halted, b->cpu.halted);
    CMP_FIELD("ciclos", a->cyc, b->cyc);
    if (!cmp_mem("WRAM", 0xC000, a->wram, b->wram, sizeof a->wram)) return 0;
    if (!cmp_mem("HRAM", 0xFF80, a->hram, b->hram, sizeof a->hram)) return 0;
    if (!cmp_mem("VRAM", 0x8000, a->vram, b->vram, sizeof a->vram)) return 0;
    if (!cmp_mem("OAM", 0xFE00, a->oam, b->oam, sizeof a->oam)) return 0;
    if (!cmp_mem("I/O", 0xFF00, a->io, b->io, sizeof a->io)) return 0;
    CMP_FIELD("IE", a->ie, b->ie);
    CMP_FIELD("DIV interno", a->timer.sys, b->timer.sys);
    CMP_FIELD("TIMA", a->timer.tima, b->timer.tima);
    CMP_FIELD("LY", a->ppu.ly, b->ppu.ly);
    CMP_FIELD("punto de la PPU", a->ppu.dot, b->ppu.dot);
    CMP_FIELD("banco de ROM", a->mbc.bankx, b->mbc.bankx);
    if (a->sram && !cmp_mem("RAM de cartucho", 0xA000, a->sram, b->sram, a->sram_size)) return 0;
    if (a->sgb) {
        if (!cmp_mem("SGB: paletas", 0, (const u8 *)a->sgbs.pal, (const u8 *)b->sgbs.pal, sizeof a->sgbs.pal)) return 0;
        if (!cmp_mem("SGB: atributos", 0, a->sgbs.attr, b->sgbs.attr, sizeof a->sgbs.attr)) return 0;
        if (!cmp_mem("SGB: imagen", 0, (const u8 *)a->sgbs.screen, (const u8 *)b->sgbs.screen, sizeof a->sgbs.screen)) return 0;
    }
    return 1;
}

/* --- Captura del cuadro de B en PNG (en SGB, la imagen completa de la SNES, con el borde) --- */

static int captura(const char *path)
{
    static const u8 shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };
    GB *g = &rec;
    static u8 rgb[SGB_SCREEN_W * SGB_SCREEN_H * 3];
    const u16 *fb = g->sgb ? gb_sgb_screen(g) : gb_framebuffer(g);
    const int W = g->sgb ? SGB_SCREEN_W : GB_SCREEN_W, H = g->sgb ? SGB_SCREEN_H : GB_SCREEN_H;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            u16 v = fb[y * W + x];
            u8 *px = rgb + ((size_t)y * (size_t)W + (size_t)x) * 3;
            if (!g->cgb_hw && !g->sgb) {
                px[0] = px[1] = px[2] = shade[v & 3];
            } else {
                for (int c = 0; c < 3; c++) {
                    unsigned ch = (v >> (5 * c)) & 31;
                    px[c] = (u8)((ch << 3) | (ch >> 2));
                }
            }
        }
    return png_write_rgb(path, rgb, W, H);
}

/* --- Ubicar la primera diferencia --- */

static void locate(long frame)
{
    gb_copy_state(&rec, &snap_rec);
    gb_copy_state(&ref, &snap_ref);
    for (long step = 0; step < 2000000; step++) {
        u16 pc = rec.cpu.pc;
        u32 bank = pc < 0x4000 ? rec.mbc.bank0 : rec.mbc.bankx;
        u8 op = gb_peek(&rec, pc);
        int recomp = rec.recomp_lookup && rec.recomp_lookup(&rec, pc) != NULL;
        gb_run(&rec, rec.cyc + 1);
        gb_run(&ref, ref.cyc + 1);
        if (!same_state(&rec, &ref)) {
            printf("DIFERENCIA en el cuadro %ld, instrucción %ld del cuadro,\n", frame, step);
            printf("  tras ejecutar %02X:%04X (opcode $%02X, %s)\n", bank, pc, op,
                   recomp ? "recompilada" : "intérprete");
            printf("  %s\n", diff_msg);
            return;
        }
    }
    printf("No se pudo reproducir la diferencia instrucción por instrucción.\n");
}

/* Un cuadro: las dos máquinas corren el cuadro entero y se comparan al final; si difieren, se vuelve al inicio del cuadro y se avanza
 * instrucción por instrucción para dar la primera diferencia exacta */
static int cuadro(DiffCtx *x, long f)
{
    Cli *c = x->c;
    u8 pad = (u8)cli_pad(c, f, 0);
    gb_copy_state(&snap_rec, &rec);
    gb_copy_state(&snap_ref, &ref);
    gb_set_pad(&rec, pad);
    gb_set_pad(&ref, pad);
    u64 target = rec.cyc + gb_frame_cycles(&rec);
    gb_run(&rec, target);
    gb_run(&ref, target);
    if (!same_state(&rec, &ref)) {
        printf("DIFERENCIA al final del cuadro %ld: %s\n", f, diff_msg);
        /* Repetir el cuadro con las mismas entradas */
        gb_copy_state(&rec, &snap_rec);
        gb_copy_state(&ref, &snap_ref);
        gb_set_pad(&rec, pad);
        gb_set_pad(&ref, pad);
        gb_copy_state(&snap_rec, &rec);
        gb_copy_state(&snap_ref, &ref);
        locate(f);
        return DT_DIFERENCIA;
    }
    /* Las llamadas son las funciones recompiladas más las instrucciones del intérprete, como en NESRecomp */
    x->recompiladas = recomp_calls;
    x->llamadas = recomp_calls + (unsigned long)metrica.ins_int;
    for (int k = 0; k < 4; k++) x->zona[k] = interp_zone[k];
    if (c->estricto && metrica_estricto(f, 4, NULL)) return DT_ESTRICTO;
    return DT_IGUAL;
}

static const char *cargar(const u8 *rom, size_t len, const Cli *c)
{
    const char *err;
    if ((err = gb_init_model(&rec, rom, (u32)len, c->model)) || (err = gb_init_model(&ref, rom, (u32)len, c->model)) ||
        (err = gb_init_model(&snap_rec, rom, (u32)len, c->model)) || (err = gb_init_model(&snap_ref, rom, (u32)len, c->model)))
        return err;
    return NULL;
}

static const char *sha1(const u8 *rom, size_t len)
{
    static char sha[41];
    sha1_hex(rom, len, sha);
    return sha;
}

static const char *preparar(const Cli *c)
{
    static char msg[200];
    if (c->palette && !(gb_compat_palette(&rec, c->palette) && gb_compat_palette(&ref, c->palette))) {
        snprintf(msg, sizeof msg, "paleta \"%s\": solo existe en el modo de compatibilidad del GBC", c->palette);
        return msg;
    }
    rec.recomp_lookup = c->bench ? recomp_lookup : counting_lookup;
    rec.trace = count_interp;
    if (c->executed) ref.trace = mark_executed;
    return NULL;
}

static void reiniciar(void) { metrica_reiniciar(&rec); }
static void *maquina(int k) { return k ? (void *)&ref : (void *)&rec; }
static void poner_pad(void *g, unsigned p1, unsigned p2) { (void)p2; gb_set_pad(g, (u8)p1); }
static void cuadro_solo(void *g) { gb_run_frame(g); }
static void vaciar(void *g) { (void)g; }
static u64 ciclos(void) { return rec.cyc; }

/* Misma partida en ambas máquinas; hora 0: el reloj no se adelanta */
static int cargar_partida(const char *path) { return gb_save_read(&rec, path, 0) && gb_save_read(&ref, path, 0); }
static int guardar_partida(const char *path) { return gb_save_write(&rec, path, 0); }

/* --uncovered: las direcciones de ROM que ejecutó el intérprete, en orden de dirección (gbrecomp --trace no depende del orden), y el
 * código que corrió desde WRAM o HRAM: líneas "ram DIRECCION BYTES" (ramtrace.h) */
static RcSummary rs_ram;

static size_t uncovered(const char *path, int *ok)
{
    FILE *uf = fopen(path, "w");
    size_t distinct = 0;
    for (u32 s = 0; s < ROM_SLOTS; s++) {
        if (!interp_hits[s]) continue;
        u32 b = rom_slot_bank(s);
        for (u32 off = 0; off < 0x4000; off++) {
            if (!interp_hits[s][off]) continue;
            distinct++;
            if (uf) fprintf(uf, "%02X:%04X\n", b, rom_slot_addr(s, (u16)off));
        }
    }
    rs_ram = ramtrace_write(uf);
    if (uf) fclose(uf);
    *ok = uf != NULL;
    return distinct;
}

static void ram(unsigned *fijas, unsigned *variables, unsigned *varias, unsigned *perdidas)
{
    *fijas = rs_ram.fixed; *variables = rs_ram.vol; *varias = rs_ram.multi; *perdidas = rs_ram.lost;
}

/* Al terminar: las TOP_HITS direcciones que más ejecutó el intérprete, en ROM y en RAM (dirección, veces y bytes) */
static void informe(DiffCtx *x)
{
    (void)x;
    Hit top[TOP_HITS];
    unsigned ntop = 0;
    for (u32 s = 0; s < ROM_SLOTS; s++) {
        if (!interp_hits[s]) continue;
        u32 b = rom_slot_bank(s);
        for (u32 off = 0; off < 0x4000; off++) {
            if (!interp_hits[s][off]) continue;
            Hit h = { b, rom_slot_addr(s, (u16)off), interp_hits[s][off] };
            top_insert(top, &ntop, h);
        }
    }
    if (ntop) printf("Las más ejecutadas por el intérprete:\n");
    for (unsigned i = 0; i < ntop; i++)
        printf("  interpretada en ROM: %02X:%04X (%u veces)\n", top[i].bank, top[i].addr, top[i].n);
    Hit rtop[TOP_HITS];
    unsigned nrtop = 0, rdistinct = 0;
    for (u32 a = 0x8000; a < 0x10000; a++) {
        if (!ram_hits[a - 0x8000].n) continue;
        rdistinct++;
        Hit h = { 0, (u16)a, ram_hits[a - 0x8000].n };
        top_insert(rtop, &nrtop, h);
    }
    if (nrtop) printf("Las más ejecutadas en RAM:\n");
    for (unsigned i = 0; i < nrtop; i++) {
        printf("  interpretada en RAM: %04X (%u veces), bytes:", rtop[i].addr, rtop[i].n);
        for (unsigned k = 0; k < RAM_BYTES; k++) printf(" %02X", ram_hits[rtop[i].addr - 0x8000].first[k]);
        printf("\n");
    }
    if (rdistinct) printf("Direcciones de RAM interpretadas: %u\n", rdistinct);
}

/* --executed: mismo formato que gbrecomp --codemap (C = ejecutado) */
static int executed_map(const char *path)
{
    FILE *ef = fopen(path, "w");
    if (!ef) return 0;
    u32 nbanks = ref.rom_banks;
    for (u32 b = 0; b < nbanks && b < MAX_BANKS; b++)
        for (u32 off = 0; off < 0x4000; off++) {
            fputc(executed[b] && executed[b][off] ? 'C' : '.', ef);
            if (off % 64 == 63) fputc('\n', ef);
        }
    return fclose(ef) == 0;
}

int main(int argc, char **argv)
{
    DiffOps o = {
        .titulo = recomp_rom_title, .uso_rom = "rom.gb", .uso_extra = " [--model dmg|cgb|sgb] [--palette up+a]", .cuadros = 3600,
        .fps = (double)GB_CPU_HZ / GB_CYCLES_FRAME, .unidad = "ROM", .zonas = { "ROM", "RAM de cartucho", "WRAM", "HRAM" },
        .cargar = cargar, .sha1 = sha1, .preparar = preparar, .cargar_partida = cargar_partida, .guardar_partida = guardar_partida,
        .reiniciar = reiniciar, .maquina = maquina, .pad = poner_pad, .cuadro_solo = cuadro_solo, .vaciar = vaciar, .cuadro = cuadro,
        .ciclos = ciclos, .captura = captura, .uncovered = uncovered, .ram = ram, .executed = executed_map, .informe = informe,
    };
    return difftest_main(argc, argv, &o);
}
