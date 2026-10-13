/*
 * retro_run: corre un juego en un núcleo libretro (un emulador de referencia: Geolith, FBNeo) sin
 * ventana, con las mismas entradas que las herramientas de NEORecomp, y da la misma salida que
 * neoramdump (o una captura PNG, o el sonido). Sirve de oráculo para comparar contra el intérprete. Es
 * el de GENRecomp con los botones, la RAM y el sonido del Neo Geo.
 *
 *   retro_run NUCLEO juego [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]
 *             [--shot captura.png] [--wav sonido.wav] [--no-ram] [--swap16] [--opt clave=valor ...]
 *             [--sys carpeta] [--verbose]
 *
 * --swap16   la RAM del núcleo está en palabras de 16 bits del orden de la máquina: se da vuelta cada
 *            par de bytes
 * --wav      guarda el sonido (estéreo, 16 bits, a la frecuencia del núcleo) e imprime su nivel por
 *            segundo como neorun ("audio, segundo N: nivel X", la raíz del promedio de los cuadrados)
 * --opt      una opción del núcleo (las demás quedan en su valor por omisión)
 * --sys      la carpeta de sistema del núcleo (build/oracles/sys si no se dice): ahí busca la BIOS
 *
 * Los botones se asignan por las descripciones que publica el núcleo ("A", "Button B", "Start",
 * "Select", "Up"...): así no importa cómo los acomode cada uno en el mando de libretro. Solo se usa la
 * interfaz pública de libretro (libretro_min.h); el núcleo se carga con dlopen y no va en el
 * repositorio (tools/oracles/oracle.py lo baja).
 */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define DLOPEN(p) (void *)LoadLibraryA(p)
#define DLSYM(h, s) (void *)GetProcAddress((HMODULE)(h), s)
#else
#include <dlfcn.h>
#define DLOPEN(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define DLSYM(h, s) dlsym(h, s)
#endif

#include "libretro_min.h"
#include "cli.h"
#include "png.h"

static int verbose;
static const char *sysdir = "build/oracles/sys";

/* Opciones del núcleo dadas con --opt */
static struct retro_variable opts[32];
static int nopts;

/* Imagen del último cuadro (XRGB8888) */
static u32 *frame_px;
static unsigned frame_w, frame_h;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;

/* Botones: bit de NEO_* para cada (puerto, id de libretro) según las descripciones del núcleo */
static u16 map_bit[2][RETRO_DEVICE_ID_JOYPAD_COUNT];
static int have_desc;
static u16 pads[2];

/* Sonido: el archivo, los bytes escritos y el nivel del segundo en curso */
static FILE *wav;
static unsigned long wav_bytes;
static double rate = 44100.0, sec_sum;
static unsigned long sec_n, sec;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (!verbose && level < RETRO_LOG_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[núcleo] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* Nombre de botón de una descripción ("D-Pad Up", "Button A", "Start"...) a su bit de NEO_*; 0 si no es uno */
static u16 desc_bit(const char *d)
{
    char s[64];
    size_t n = 0;
    for (; *d && n + 1 < sizeof s; d++) s[n++] = (char)tolower((unsigned char)*d);
    s[n] = 0;
    const char *t = s;
    if (!strncmp(t, "d-pad ", 6)) t += 6;
    if (!strncmp(t, "button ", 7)) t += 7;
    static const struct { const char *name; u16 bit; } tab[] = {
        { "up", NEO_UP }, { "down", NEO_DOWN }, { "left", NEO_LEFT }, { "right", NEO_RIGHT },
        { "a", NEO_A }, { "b", NEO_B }, { "c", NEO_C }, { "d", NEO_D },
        { "start", NEO_START }, { "select", NEO_SELECT }, { "select/coin", NEO_SELECT },
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) if (!strcmp(t, tab[i].name)) return tab[i].bit;
    return 0;
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd & ~(unsigned)RETRO_ENVIRONMENT_EXPERIMENTAL) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
        if (f > RETRO_PIXEL_FORMAT_RGB565) return false;
        pixfmt = f;
        return true; }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
        const struct retro_input_descriptor *d = data;
        for (; d->description; d++) {
            if (d->device != RETRO_DEVICE_JOYPAD || d->port > 1 || d->id >= RETRO_DEVICE_ID_JOYPAD_COUNT) continue;
            u16 b = desc_bit(d->description);
            if (verbose) fprintf(stderr, "botón: puerto %u id %u \"%s\" -> %04X\n", d->port, d->id, d->description, b);
            if (b) { map_bit[d->port][d->id] = b; have_desc = 1; }
        }
        return true; }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = NULL;
        for (int i = 0; i < nopts; i++) if (!strcmp(v->key, opts[i].key)) { v->value = opts[i].value; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;   /* opciones a la antigua */
    case RETRO_ENVIRONMENT_SET_VARIABLES:              /* con --verbose, las opciones que tiene el núcleo */
        if (verbose)
            for (const struct retro_variable *v = data; v && v->key; v++) fprintf(stderr, "opción: %s = %s\n", v->key, v->value);
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        rate = ((const struct retro_system_av_info *)data)->timing.sample_rate;
        return true;
    default:
        if (verbose) fprintf(stderr, "pedido sin atender: %u%s\n", cmd & 0xFFFF, (cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (experimental)" : "");
        return false;
    }
}

static long frames_dup, frames_new;

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) { frames_dup++; return; }                /* cuadro repetido: queda el anterior */
    frames_new++;
    if (w * h > frame_w * frame_h || !frame_px) {
        free(frame_px);
        frame_px = malloc((size_t)w * h * sizeof *frame_px);
        if (!frame_px) { fprintf(stderr, "sin memoria\n"); exit(2); }
    }
    frame_w = w;
    frame_h = h;
    for (unsigned y = 0; y < h; y++) {
        const u8 *row = (const u8 *)data + y * pitch;
        for (unsigned x = 0; x < w; x++) {
            u32 r, g, b;
            if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                u32 p = ((const u32 *)row)[x];
                r = p >> 16 & 0xFF; g = p >> 8 & 0xFF; b = p & 0xFF;
            } else {
                u16 p = ((const u16 *)row)[x];
                if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) { r = p >> 11 & 31; g = p >> 5 & 63; b = p & 31; g = g * 255 / 63; }
                else { r = p >> 10 & 31; g = p >> 5 & 31; b = p & 31; g = g * 255 / 31; }
                r = r * 255 / 31; b = b * 255 / 31;
            }
            frame_px[y * w + x] = r << 16 | g << 8 | b;
        }
    }
}

static size_t audio_batch(const int16_t *d, size_t n)
{
    if (!wav) return n;
    fwrite(d, 4, n, wav);
    wav_bytes += (unsigned long)n * 4;
    for (size_t i = 0; i < n * 2; i++) sec_sum += (double)d[i] * d[i];
    sec_n += (unsigned long)n * 2;
    if (sec_n >= (unsigned long)(rate * 2)) {
        printf("audio, segundo %3lu: nivel %5.0f\n", sec++, sqrt(sec_sum / (double)sec_n));
        sec_sum = 0; sec_n = 0;
    }
    return n;
}

static void audio_sample(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_batch(s, 1); }
static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port > 1 || device != RETRO_DEVICE_JOYPAD || id >= RETRO_DEVICE_ID_JOYPAD_COUNT) return 0;
    return (pads[port] & map_bit[port][id]) ? 1 : 0;
}

static void *sym(void *h, const char *name)
{
    void *p = DLSYM(h, name);
    if (!p) { fprintf(stderr, "el núcleo no tiene %s\n", name); exit(2); }
    return p;
}

static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* Cabecera de un WAV estéreo de 16 bits (los tamaños se completan al final) */
static void wav_header(FILE *f, u32 hz, u32 data)
{
    u8 h[44];
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + data); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    put32(h + 24, hz); put32(h + 28, hz * 4); h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); put32(h + 40, data);
    fwrite(h, 1, sizeof h, f);
}

int main(int argc, char **argv)
{
    const char *shot = NULL, *wav_path = NULL;
    int swap16 = 0, no_ram = 0;
    /* Las opciones propias se sacan antes de pasar el resto a cli_parse */
    char **rest = malloc((size_t)argc * sizeof *rest);
    int nrest = 0;
    for (int i = 0; i < argc; i++) {
        if (i && !strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (i && !strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (i && !strcmp(argv[i], "--sys") && i + 1 < argc) sysdir = argv[++i];
        else if (i && !strcmp(argv[i], "--swap16")) swap16 = 1;
        else if (i && !strcmp(argv[i], "--no-ram")) no_ram = 1;
        else if (i && !strcmp(argv[i], "--verbose")) verbose = 1;
        else if (i && !strcmp(argv[i], "--opt") && i + 1 < argc && nopts < 32) {
            char *kv = argv[++i], *eq = strchr(kv, '=');
            if (!eq) { fprintf(stderr, "--opt espera clave=valor\n"); return 2; }
            *eq = 0;
            opts[nopts++] = (struct retro_variable){ kv, eq + 1 };
        } else rest[nrest++] = argv[i];
    }
    static Cli c;
    if (cli_parse(&c, nrest, rest, 2, 60)) {
        fprintf(stderr, "uso: retro_run NUCLEO juego [--frames N] [--input archivo] [--seed S] [boton:desde-hasta ...]\n"
                        "                [--shot captura.png] [--wav sonido.wav] [--no-ram] [--swap16] [--opt clave=valor ...]\n"
                        "                [--sys carpeta] [--verbose]\n");
        return 2;
    }
    void *h = DLOPEN(c.pos[0]);
    if (!h) { fprintf(stderr, "no se pudo cargar el núcleo %s\n", c.pos[0]); return 2; }
    void (*set_env)(retro_environment_t) = (void (*)(retro_environment_t))sym(h, "retro_set_environment");
    void (*set_video)(retro_video_refresh_t) = (void (*)(retro_video_refresh_t))sym(h, "retro_set_video_refresh");
    void (*set_audio)(retro_audio_sample_t) = (void (*)(retro_audio_sample_t))sym(h, "retro_set_audio_sample");
    void (*set_batch)(retro_audio_sample_batch_t) = (void (*)(retro_audio_sample_batch_t))sym(h, "retro_set_audio_sample_batch");
    void (*set_poll)(retro_input_poll_t) = (void (*)(retro_input_poll_t))sym(h, "retro_set_input_poll");
    void (*set_state)(retro_input_state_t) = (void (*)(retro_input_state_t))sym(h, "retro_set_input_state");
    void (*init)(void) = (void (*)(void))sym(h, "retro_init");
    void (*get_info)(struct retro_system_info *) = (void (*)(struct retro_system_info *))sym(h, "retro_get_system_info");
    void (*get_av)(struct retro_system_av_info *) = (void (*)(struct retro_system_av_info *))sym(h, "retro_get_system_av_info");
    bool (*load)(const struct retro_game_info *) = (bool (*)(const struct retro_game_info *))sym(h, "retro_load_game");
    void (*run)(void) = (void (*)(void))sym(h, "retro_run");
    void *(*mem_data)(unsigned) = (void *(*)(unsigned))sym(h, "retro_get_memory_data");
    size_t (*mem_size)(unsigned) = (size_t (*)(unsigned))sym(h, "retro_get_memory_size");
    void (*unload)(void) = (void (*)(void))sym(h, "retro_unload_game");
    void (*deinit)(void) = (void (*)(void))sym(h, "retro_deinit");

    set_env(environment);
    init();
    set_video(video_refresh);
    set_audio(audio_sample);
    set_batch(audio_batch);
    set_poll(input_poll);
    set_state(input_state);
    struct retro_system_info si;
    memset(&si, 0, sizeof si);
    get_info(&si);
    if (verbose) fprintf(stderr, "núcleo: %s %s (rutas: %s)\n", si.library_name, si.library_version, si.need_fullpath ? "sí" : "no");

    /* Los núcleos de Neo Geo cargan por la ruta (FBNeo busca el set por el nombre del zip); si no la
     * piden, también se pasan los datos */
    u8 *rom = NULL;
    size_t sz = 0;
    if (!si.need_fullpath) {
        FILE *f = fopen(c.pos[1], "rb");
        if (!f) { fprintf(stderr, "no se pudo leer %s\n", c.pos[1]); return 2; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        rom = malloc(n > 0 ? (size_t)n : 1);
        if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "no se pudo leer %s\n", c.pos[1]); return 2; }
        fclose(f);
        sz = (size_t)n;
    }
    struct retro_game_info gi = { c.pos[1], rom, sz, NULL };
    if (!load(&gi)) { fprintf(stderr, "el núcleo no cargó el juego\n"); return 2; }
    struct retro_system_av_info av;
    memset(&av, 0, sizeof av);
    get_av(&av);
    if (av.timing.sample_rate > 0) rate = av.timing.sample_rate;
    if (verbose) fprintf(stderr, "%.4f cuadros/s, sonido a %.0f Hz\n", av.timing.fps, rate);
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) { fprintf(stderr, "no se pudo escribir %s\n", wav_path); return 2; }
        wav_header(wav, (u32)(rate + 0.5), 0);
    }

    for (long fr = 0; fr < c.frames; fr++) {
        pads[0] = cli_pad(&c, fr, 0);
        pads[1] = cli_pad(&c, fr, 1);
        run();
    }

    /* (FBNeo describe los botones en el primer cuadro, no al cargar) */
    if (!have_desc) fprintf(stderr, "aviso: el núcleo no describió sus botones; no se apretó ninguno\n");
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, (u32)(rate + 0.5), (u32)wav_bytes);
        fclose(wav);
    }
    if (shot) {
        if (!frame_px) { fprintf(stderr, "el núcleo no dio ninguna imagen\n"); return 2; }
        unsigned char *rgb = malloc((size_t)frame_w * frame_h * 3);
        for (unsigned i = 0; i < frame_w * frame_h; i++) {
            rgb[i * 3] = (u8)(frame_px[i] >> 16); rgb[i * 3 + 1] = (u8)(frame_px[i] >> 8); rgb[i * 3 + 2] = (u8)frame_px[i];
        }
        if (!png_write_rgb(shot, rgb, (int)frame_w, (int)frame_h)) { fprintf(stderr, "no se pudo escribir %s\n", shot); return 2; }
        free(rgb);
    }
    if (!no_ram) {                                      /* la RAM del 68000, como neoramdump */
        const u8 *ram = mem_data(RETRO_MEMORY_SYSTEM_RAM);
        size_t n = mem_size(RETRO_MEMORY_SYSTEM_RAM);
        if (!ram || n < 0x10000) { fprintf(stderr, "el núcleo no expone la RAM del 68000 (%zu bytes)\n", n); return 2; }
        for (u32 a = 0; a < 0x10000; a++) printf("%06X %02X\n", 0x100000 | a, ram[swap16 ? a ^ 1 : a]);
    }
    if (verbose) fprintf(stderr, "cuadros con imagen: %ld, repetidos: %ld\n", frames_new, frames_dup);
    unload();
    deinit();
    return 0;
}
