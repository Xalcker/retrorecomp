/*
 * Prueba diferencial: corre la ROM con el código recompilado y con el
 * intérprete puro, con las mismas entradas, y compara el estado completo
 * (CPU, ciclos, RAM, VRAM, OAM, I/O, timer, PPU, RAM del cartucho) al final de
 * cada cuadro. Si algo difiere, regresa al inicio del cuadro y avanza
 * instrucción por instrucción para reportar la primera diferencia exacta.
 *
 * Uso: difftest rom.gb [--frames N] [--seed S | --input archivo] [--save archivo.sav]
 *                     [--uncovered archivo]
 *                     [--screens carpeta [--every N]] [--bench] [--executed archivo]
 *   --input reproduce entradas grabadas (ver inputs.h) en lugar de aleatorias.
 *   --save carga un .sav en ambas máquinas antes de empezar (para partidas
 *   grabadas que empezaron desde una partida guardada).
 *   --save-out escribe la RAM del cartucho (de la máquina recompilada) al terminar.
 *   --screens guarda una captura (PNG) cada N cuadros, para ver hasta dónde
 *   llegaron las entradas aleatorias.
 *   --uncovered escribe, en formato de traza de gbrecomp, las direcciones de
 *   ROM que ejecutó el intérprete (para alimentar gbrecomp --trace).
 *   --executed escribe qué bytes de ROM se ejecutaron (los registra la máquina
 *   de referencia; formato de gbrecomp --codemap).
 * Código de salida: 0 sin diferencias, 1 con diferencias.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console.h"
#include "inputs.h"
#include "recomp.h"
#include "sha1.h"

static GB rec, ref, snap_rec, snap_ref;

/* --- Cobertura: instrucciones de ROM que no corrieron recompiladas --- */

#define MAX_BANKS 512
static u32 *interp_hits[MAX_BANKS];

/* --- Bytes de ROM ejecutados (por la máquina de referencia) --- */

static u8 *executed[MAX_BANKS];

static int insn_length(u8 op)
{
    switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x08:
    case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA:
    case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC:
    case 0xEA: case 0xFA:
        return 3;
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
    case 0xE0: case 0xF0: case 0xE8: case 0xF8: case 0xCB: case 0x10:
        return 2;
    default:
        return 1;
    }
}

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

/* Instrucciones del intérprete por zona: ROM, RAM del cartucho, WRAM, HRAM.
 * Lo que corre en RAM nunca se puede recompilar (el juego lo copia ahí). */
static u64 interp_zone[4];

static void count_interp(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 bank;
    u16 off;
    interp_zone[pc < 0x8000 ? 0 : pc < 0xC000 ? 1 : pc < 0xFF80 ? 2 : 3]++;
    if (pc < 0x4000) { bank = g->mbc.bank0; off = pc; }
    else if (pc < 0x8000) { bank = g->mbc.bankx; off = (u16)(pc - 0x4000); }
    else return;
    if (bank >= MAX_BANKS) return;
    if (!interp_hits[bank]) interp_hits[bank] = calloc(0x4000, sizeof(u32));
    interp_hits[bank][off]++;
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

/* --- Capturas en PNG (gris, sin compresión) --- */

static u32 crc_table[256];

static u32 crc32_buf(u32 crc, const u8 *p, size_t n)
{
    if (!crc_table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static void png_chunk(FILE *f, const char *type, const u8 *data, u32 len)
{
    u8 hdr[8], crc[4];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    put_be32(crc, crc32_buf(crc32_buf(0, hdr + 4, 4), data, len));
    fwrite(crc, 1, 4, f);
}

/* Captura en PNG RGB sin compresión (bloques "stored" de deflate) */
static void save_screen(GB *g, const char *path)
{
    static const u8 shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };
    /* En SGB se guarda la imagen completa de la SNES, con el borde */
    enum { MAX_RAW = (SGB_SCREEN_W * 3 + 1) * SGB_SCREEN_H,
           MAX_BLOCKS = (MAX_RAW + 65534) / 65535 };
    static u8 raw[MAX_RAW];
    static u8 z[2 + MAX_RAW + MAX_BLOCKS * 5 + 4];
    const u16 *fb = g->sgb ? gb_sgb_screen(g) : gb_framebuffer(g);
    const int W = g->sgb ? SGB_SCREEN_W : GB_SCREEN_W, H = g->sgb ? SGB_SCREEN_H : GB_SCREEN_H;
    const u32 STRIDE = (u32)W * 3 + 1, RAW = STRIDE * (u32)H;
    const size_t zlen = 2 + RAW + (RAW + 65534) / 65535 * 5 + 4;

    for (int y = 0; y < H; y++) {
        u8 *row = raw + y * STRIDE;
        row[0] = 0;     /* sin filtro */
        for (int x = 0; x < W; x++) {
            u16 v = fb[y * W + x];
            u8 *px = row + 1 + x * 3;
            if (!g->cgb_hw && !g->sgb) {
                px[0] = px[1] = px[2] = shade[v & 3];
            } else {
                for (int c = 0; c < 3; c++) {
                    unsigned ch = (v >> (5 * c)) & 31;
                    px[c] = (u8)((ch << 3) | (ch >> 2));
                }
            }
        }
    }

    u8 *p = z;
    u32 a = 1, b = 0;
    *p++ = 0x78; *p++ = 0x01;
    for (u32 done = 0; done < RAW;) {
        u32 n = RAW - done > 65535 ? 65535 : RAW - done;
        *p++ = done + n == RAW ? 1 : 0;
        *p++ = (u8)n; *p++ = (u8)(n >> 8);
        *p++ = (u8)~n; *p++ = (u8)(~n >> 8);
        for (u32 i = 0; i < n; i++) {
            u8 v = raw[done + i];
            *p++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        done += n;
    }
    put_be32(p, b << 16 | a);

    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    u8 ihdr[13] = { 0 };
    fwrite(sig, 1, 8, f);
    put_be32(ihdr, (u32)W);
    put_be32(ihdr + 4, (u32)H);
    ihdr[8] = 8;
    ihdr[9] = 2;    /* RGB */
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (u32)zlen);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
}

/* --- Entradas pseudoaleatorias reproducibles --- */

static u32 rng_state;

static u32 rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static InputScript script;   /* --input: entradas grabadas en lugar de aleatorias */
static int use_script;
static u8 pad_now;
static long pad_until;

static void reset_pad(u32 seed)
{
    rng_state = seed ? seed : 1;
    pad_now = 0;
    pad_until = 0;
    script.pos = 0;
}

static u8 next_pad(long frame)
{
    u8 pad = pad_now;
    long until = pad_until;
    if (frame >= until) {
        u32 r = rng();
        until = frame + 4 + (long)(r % 40);
        /* Start poco frecuente: en muchos juegos pausa la partida */
        if ((r >> 24) % 24 == 0) {
            pad_now = PAD_START;
            pad_until = until;
            return PAD_START;
        }
        switch ((r >> 8) % 6) {
        case 0: pad = 0; break;
        case 1: pad = PAD_B; break;
        case 2: pad = PAD_A; break;
        default: pad = (u8)(1u << (4 + ((r >> 12) & 3))) | ((r >> 16) & 1 ? PAD_A : 0); break;
        }
    }
    pad_now = pad;
    pad_until = until;
    return pad;
}

static u8 pad_for(long frame)
{
    return use_script ? inputs_pad(&script, frame) : next_pad(frame);
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
            printf("Primera diferencia en el cuadro %ld, instrucción %ld del cuadro,\n", frame, step);
            printf("  tras ejecutar %02X:%04X (opcode $%02X, %s)\n", bank, pc, op,
                   recomp ? "recompilada" : "intérprete");
            printf("  %s\n", diff_msg);
            return;
        }
    }
    printf("No se pudo reproducir la diferencia instrucción por instrucción.\n");
}

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)*size);
    if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *rom_path = NULL, *uncovered = NULL, *screens = NULL;
    const char *executed_path = NULL, *input_path = NULL, *save_path = NULL;
    const char *save_out = NULL;
    int frames_given = 0;
    long frames = 3600, every = 600;
    int bench = 0, model = GB_MODEL_AUTO;
    const char *palette = NULL;
    u32 seed = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames = atol(argv[++i]); frames_given = 1; }
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_path = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else if (!strcmp(argv[i], "--save-out") && i + 1 < argc) save_out = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (u32)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--uncovered") && i + 1 < argc) uncovered = argv[++i];
        else if (!strcmp(argv[i], "--executed") && i + 1 < argc) executed_path = argv[++i];
        else if (!strcmp(argv[i], "--screens") && i + 1 < argc) screens = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--bench")) bench = 1;
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = model_from_name(argv[++i]);
            if (model < GB_MODEL_AUTO) { fprintf(stderr, "modelo desconocido: %s\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc) palette = argv[++i];
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "uso: difftest rom.gb [--frames N] [--seed S] [--uncovered archivo]\n");
        return 2;
    }

    long size;
    unsigned char *rom = read_file(rom_path, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", rom_path); return 2; }
    char sha1[41];
    sha1_hex(rom, (size_t)size, sha1);
    if (strcmp(sha1, recomp_rom_sha1) != 0) {
        fprintf(stderr, "la ROM no coincide: SHA1 %s, se esperaba %s\n", sha1, recomp_rom_sha1);
        return 2;
    }

    /* La grabación puede decir con qué modelo y paleta se hizo */
    if (input_path) {
        const char *ierr = inputs_load(&script, input_path);
        if (ierr) { fprintf(stderr, "%s\n", ierr); return 2; }
        use_script = 1;
        /* Sin --frames: hasta 10 segundos después del último cambio */
        if (!frames_given) frames = inputs_last_frame(&script) + 600;
        if (model == GB_MODEL_AUTO) model = script.model;
        if (!palette && script.palette[0]) palette = script.palette;
    }

    const char *err;
    if ((err = gb_init_model(&rec, rom, (u32)size, model)) || (err = gb_init_model(&ref, rom, (u32)size, model)) ||
        (err = gb_init_model(&snap_rec, rom, (u32)size, model)) || (err = gb_init_model(&snap_ref, rom, (u32)size, model))) {
        fprintf(stderr, "%s\n", err);
        return 2;
    }
    free(rom);
    if (palette && !(gb_compat_palette(&rec, palette) && gb_compat_palette(&ref, palette))) {
        fprintf(stderr, "paleta \"%s\": solo existe en el modo de compatibilidad del GBC\n", palette);
        return 2;
    }
    rec.recomp_lookup = recomp_lookup;
    rec.trace = count_interp;
    if (executed_path) ref.trace = mark_executed;
    if (save_path) {
        /* Misma partida en ambas máquinas; hora 0: el reloj no se adelanta */
        if (!gb_save_read(&rec, save_path, 0) || !gb_save_read(&ref, save_path, 0)) {
            fprintf(stderr, "no se pudo cargar %s (¿el cartucho no tiene batería?)\n", save_path);
            return 2;
        }
    }
    reset_pad(seed);

    printf("%s: %ld cuadros, semilla %u\n", recomp_rom_title, frames, seed);
    if (bench) {
        /* Velocidad: cada máquina por separado, con las mismas entradas */
        GB *m[2] = { &rec, &ref };
        const char *name[2] = { "recompilado", "intérprete" };
        for (int k = 0; k < 2; k++) {
            reset_pad(seed);
            clock_t t0 = clock();
            for (long frame = 0; frame < frames; frame++) {
                gb_set_pad(m[k], pad_for(frame));
                gb_run_frame(m[k]);
            }
            double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
            printf("  %-12s %.2f s  (%.0fx tiempo real)\n", name[k], s,
                   s > 0 ? frames * ((double)GB_CYCLES_FRAME / GB_CPU_HZ) / s : 0.0);
        }
        return 0;
    }
    int ok = 1;
    for (long frame = 0; frame < frames; frame++) {
        u8 pad = pad_for(frame);
        gb_copy_state(&snap_rec, &rec);
        gb_copy_state(&snap_ref, &ref);
        gb_set_pad(&rec, pad);
        gb_set_pad(&ref, pad);
        u64 target = rec.cyc + GB_CYCLES_FRAME;
        gb_run(&rec, target);
        gb_run(&ref, target);
        if (!same_state(&rec, &ref)) {
            printf("Diferencia al final del cuadro %ld: %s\n", frame, diff_msg);
            /* Repetir el cuadro con las mismas entradas */
            gb_copy_state(&rec, &snap_rec);
            gb_copy_state(&ref, &snap_ref);
            gb_set_pad(&rec, pad);
            gb_set_pad(&ref, pad);
            gb_copy_state(&snap_rec, &rec);
            gb_copy_state(&snap_ref, &ref);
            locate(frame);
            ok = 0;
            break;
        }
        if (screens && every > 0 && (frame + 1) % every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/cuadro_%06ld.png", screens, frame + 1);
            save_screen(&rec, path);
        }
    }

    u64 total = rec.instr_recomp + rec.instr_interp;
    printf("Instrucciones: %llu recompiladas, %llu en el intérprete (%.3f%% recompilado)\n",
           (unsigned long long)rec.instr_recomp, (unsigned long long)rec.instr_interp,
           total ? 100.0 * rec.instr_recomp / total : 0.0);
    if (rec.instr_interp)
        printf("Intérprete por zona: ROM %.1f%%, RAM de cartucho %.1f%%, WRAM %.1f%%, HRAM %.1f%%\n",
               100.0 * interp_zone[0] / rec.instr_interp, 100.0 * interp_zone[1] / rec.instr_interp,
               100.0 * interp_zone[2] / rec.instr_interp, 100.0 * interp_zone[3] / rec.instr_interp);

    /* Direcciones de ROM más ejecutadas por el intérprete */
    unsigned shown = 0, distinct = 0;
    FILE *uf = uncovered ? fopen(uncovered, "w") : NULL;
    for (u32 b = 0; b < MAX_BANKS; b++) {
        if (!interp_hits[b]) continue;
        for (u32 off = 0; off < 0x4000; off++) {
            if (!interp_hits[b][off]) continue;
            u16 addr = (u16)(b == 0 ? off : 0x4000 + off);
            distinct++;
            if (uf) fprintf(uf, "%02X:%04X\n", b, addr);
            if (shown < 10) {
                printf("  interpretada en ROM: %02X:%04X (%u veces)\n", b, addr, interp_hits[b][off]);
                shown++;
            }
        }
    }
    if (uf) fclose(uf);

    if (executed_path) {
        /* Mismo formato que gbrecomp --codemap: C = ejecutado */
        FILE *ef = fopen(executed_path, "w");
        u32 nbanks = rec.rom_banks;
        for (u32 b = 0; ef && b < nbanks && b < MAX_BANKS; b++)
            for (u32 off = 0; off < 0x4000; off++) {
                fputc(executed[b] && executed[b][off] ? 'C' : '.', ef);
                if (off % 64 == 63) fputc('\n', ef);
            }
        if (ef) fclose(ef);
    }
    if (distinct) printf("Direcciones de ROM interpretadas: %u\n", distinct);

    if (save_out && !gb_save_write(&rec, save_out, 0))
        fprintf(stderr, "no se pudo escribir %s\n", save_out);

    printf(ok ? "Sin diferencias.\n" : "HAY DIFERENCIAS.\n");
    return ok ? 0 : 1;
}
