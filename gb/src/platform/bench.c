/*
 * bench: velocidad de emulación (CPU, PPU, APU...) sin ventana ni límite de
 * cuadros.
 *
 * Uso: gbbench rom.gb [--frames N] [--repeat N] [--model dmg|cgb|sgb] [--input archivo] [--seed S] [--hash]
 *   --frames  cuadros por corrida (por omisión 3600, un minuto de juego).
 *   --repeat  corridas; se reporta la más rápida, que es la menos afectada por
 *             otros programas en la máquina (por omisión 1, como en las otras consolas).
 *   --input   entradas grabadas (inputs.h, en ../shared).
 *   --seed    suma el mando al azar de random_pad.h (el mismo que en NESRecomp).
 *   --hash    corre una vez e imprime el hash de todos los cuadros y de todo el
 *             audio, el mismo que gbshot --hash (gb_golden.h): así
 *             tests/golden.sh --recompilado compara el código recompilado con la
 *             misma línea base.
 *   Sin --input ni --seed: Start en los cuadros 121-124 y la derecha desde el 401,
 *   el mismo guion que nesbench y genbench.
 * El audio se genera y se saca de la cola en cada cuadro, como en la ventana.
 * Compilado con -DGB_RECOMP (el bench del proyecto generado, que solo acepta la
 * ROM que se recompiló) mide el código recompilado; sin él (gbbench), el
 * intérprete. Como nesbench y el bench de los proyectos de NESRecomp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console.h"
#include "gb.h"
#include "cli.h"
#include "gb_golden.h"
#ifdef GB_RECOMP
#include "recomp.h"
#include "sha1.h"
#endif

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)*size);
    if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    console_utf8();
    static Cli c;
    if (cli_parse(&c, argc, argv, 1, 3600)) {
        fprintf(stderr, "uso: %s rom.gb [--frames N] [--repeat N] [--model dmg|cgb|sgb] [--input archivo] [--seed S] [--hash]\n", cli_prog(argv[0]));
        return 2;
    }
    const char *rom_path = c.pos[0];
    long frames = c.frames;
    int repeat = c.repeat, model = c.model, hash = c.hash;
    long size;
    unsigned char *rom = read_file(rom_path, &size);
    if (!rom) { fprintf(stderr, "no se pudo leer %s\n", rom_path); return 2; }
#ifdef GB_RECOMP
    char sha1[41];
    sha1_hex(rom, (size_t)size, sha1);
    if (strcmp(sha1, recomp_rom_sha1) != 0) {
        fprintf(stderr, "la ROM no coincide: SHA1 %s, se esperaba %s\n", sha1, recomp_rom_sha1);
        return 2;
    }
#endif
    static GB g;
    static float aud[4096 * 2];
    double best = 0;
    GoldenHash gh;
    u64 total_cyc = 0;
    if (hash) repeat = 1;
    for (int k = 0; k < repeat; k++) {
        const char *err = gb_init_model(&g, rom, (u32)size, model);
        if (err) { fprintf(stderr, "%s: %s\n", rom_path, err); return 2; }
        g.apu.enabled = 1;          /* como en la ventana: el APU mezcla y genera muestras */
#ifdef GB_RECOMP
        g.recomp_lookup = recomp_lookup;
        metrica_reiniciar(&g);      /* todas las corridas dan lo mismo: se queda la última */
#endif
        u64 cyc0 = g.cyc;
        cli_rewind(&c);
        int scripted = c.input || c.nh || c.use_seed;
        golden_init(&gh);
        clock_t t0 = clock();
        for (long f = 0; f < frames; f++) {
            u8 pad = cli_pad(&c, f, 0);
            if (!scripted) pad = (f > 120 && f < 125) ? PAD_START : (f > 400 ? PAD_RIGHT : 0);
            gb_set_pad(&g, pad);
            gb_run_frame(&g);
            if (hash) gb_golden_frame(&g, &gh);
            while (gb_audio_pull(&g, aud, 4096) > 0) {}
        }
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (k == 0 || s < best) best = s;
        total_cyc = g.cyc - cyc0;
        gb_free(&g);
    }
    /* Un cuadro dura GB_CYCLES_FRAME ciclos a GB_CPU_HZ (59.73 por segundo) */
    double real = frames * ((double)GB_CYCLES_FRAME / GB_CPU_HZ);
    double xtr = best > 0 ? real / best : 0.0;
    printf("%ld cuadros en %.3f s (mejor de %d): %.0f cuadros/s, %.1fx tiempo real\n",
           frames, best, repeat, best > 0 ? frames / best : 0.0, xtr);
    const Metrica *mres = NULL;
#ifdef GB_RECOMP
    metrica_imprimir(&metrica, total_cyc);
    mres = &metrica;
#endif
    if (hash) golden_print(&gh);
    char res[200];
    int nres = snprintf(res, sizeof res, "\"cuadros\":%ld,\"segundos\":%.3f,\"x_tiempo_real\":%.1f", frames, best, xtr);
    if (hash) snprintf(res + nres, sizeof res - (size_t)nres, ",\"hash_imagen\":\"%016llx\",\"hash_audio\":\"%016llx\"", gh.img, gh.aud);
    metrica_resumen("bench", 1, res, mres, total_cyc);     /* la última línea (metrica.h) */
    free(rom);
    return 0;
}
