/*
 * Capa de plataforma con SDL2 para un juego recompilado: ventana, controles,
 * audio y ritmo de 59.7275 cuadros por segundo.
 *
 * Uso: juego [rom.gb] [--scale N] [--interp] [--force] [--forget-rom]
 *            [--record archivo | --play archivo] [--uncovered archivo]
 *   --interp      ejecuta todo con el intérprete (para comparar)
 *   --force       acepta una ROM con otro SHA1 (solo con el intérprete)
 *   --forget-rom  borra la copia guardada de la ROM y la vuelve a pedir
 *   --record      graba los botones en un archivo de texto (ver inputs.h), para
 *                 reproducirlos con --play o con difftest --input
 *   --play        reproduce un archivo grabado (el teclado se ignora)
 *   --uncovered archivo  al salir, agrega a `archivo` las direcciones de ROM
 *                 que tuvo que ejecutar el intérprete (para gbrecomp --trace)
 *
 * La ROM se pide una sola vez. Se busca, en orden: el argumento, la copia
 * guardada en la carpeta de datos del juego, y "rom.gb" junto al ejecutable o
 * en la carpeta actual. Si no está, se pide con el diálogo de abrir archivo
 * (Windows) o arrastrándola a la ventana. Una ROM válida (SHA1 correcto) se
 * copia a la carpeta de datos para las siguientes veces.
 *
 * Carpeta de datos (la da SDL): en Windows %APPDATA%\GBRecomp\<juego>\, en
 * Linux ~/.local/share/GBRecomp/<juego>/, en macOS ~/Library/Application
 * Support/GBRecomp/<juego>/. Ahí van también las partidas (<juego>.sav,
 * compatible con BGB y VBA-M), que se cargan al iniciar y se guardan al salir
 * y cada 5 segundos si cambiaron.
 *
 * Controles: flechas, X = A, Z = B, Enter = Start, Retroceso = Select,
 * Esc = salir.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "files.h"
#include "inputs.h"
#include "recomp.h"
#include "sha1.h"

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, recomp_rom_title, msg, NULL);
    exit(1);
}

static void inform(const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, recomp_rom_title, msg, NULL);
}

/* ------------------------------------------------------------------ */
/* ROM                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;
    size_t size;
} Rom;

/* 1: es la ROM esperada; 0: otra ROM; -1: no se pudo leer */
static int load_rom(const char *path, Rom *rom)
{
    rom->data = u8_read_file(path, &rom->size);
    if (!rom->data) return -1;
    char sha1[41];
    sha1_hex(rom->data, rom->size, sha1);
    return strcmp(sha1, recomp_rom_sha1) == 0;
}

static void wrong_rom_message(const char *path, char *msg, size_t size)
{
    snprintf(msg, size, "\"%s\" no es la ROM de %s que se recompiló.\n"
             "Se necesita la que tiene SHA1 %s.", path, recomp_rom_title, recomp_rom_sha1);
}

/* Espera a que arrastren un archivo a la ventana (o a que la cierren) */
static int wait_for_drop(char *out, size_t size)
{
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 0;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return 0;
        if (ev.type == SDL_DROPFILE) {
            snprintf(out, size, "%s", ev.drop.file);
            SDL_free(ev.drop.file);
            return 1;
        }
    }
    return 0;
}

/* Pide la ROM hasta que den una válida. 0 si el usuario cancela. */
static int ask_for_rom(Rom *rom)
{
    char msg[4096], path[1024];
    int first = 1;
    for (;;) {
        int got = ask_rom_path(first ? "Elige tu ROM" : "Elige la ROM correcta", path, sizeof path);
        if (!got) {
            /* Sin diálogo nativo (o cancelado en Windows: se ofrece arrastrar) */
            snprintf(msg, sizeof msg,
                     "Para jugar se necesita tu ROM de %s.\n"
                     "Arrástrala a la ventana del juego (o ciérrala para salir).\n"
                     "Solo se pide una vez: se guarda una copia en la carpeta de datos del juego.",
                     recomp_rom_title);
            if (first) inform(msg);
            if (!wait_for_drop(path, sizeof path)) return 0;
        }
        first = 0;
        int r = load_rom(path, rom);
        if (r == 1) return 1;
        if (r == 0) {
            free(rom->data);
            wrong_rom_message(path, msg, sizeof msg);
        } else {
            snprintf(msg, sizeof msg, "No se pudo leer \"%s\".", path);
        }
        inform(msg);
    }
}

/* ------------------------------------------------------------------ */
/* Cobertura (--uncovered)                                             */
/* ------------------------------------------------------------------ */

#define MAX_BANKS 512
static u8 *uncovered[MAX_BANKS];

static void mark_uncovered(GB *g)
{
    u16 pc = g->cpu.pc;
    u32 bank = pc < 0x4000 ? g->mbc.bank0 : g->mbc.bankx;
    if (pc >= 0x8000 || bank >= MAX_BANKS) return;
    if (!uncovered[bank]) uncovered[bank] = calloc(0x4000, 1);
    if (uncovered[bank]) uncovered[bank][pc & 0x3FFF] = 1;
}

static void write_uncovered(const char *path)
{
    FILE *f = u8_fopen(path, "a");
    if (!f) return;
    for (u32 b = 0; b < MAX_BANKS; b++)
        for (u32 off = 0; uncovered[b] && off < 0x4000; off++)
            if (uncovered[b][off])
                fprintf(f, "%02X:%04X\n", b, (unsigned)(b == 0 ? off : 0x4000 + off));
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Partidas                                                            */
/* ------------------------------------------------------------------ */

static int save_game(GB *g, const char *path)
{
    size_t size;
    u8 *data = gb_save_serialize(g, (long long)time(NULL), &size);
    int ok = data && u8_write_file(path, data, size);
    free(data);
    if (ok) g->sram_dirty = 0;
    return ok;
}

static void load_game(GB *g, const char *path)
{
    size_t size;
    u8 *data = u8_read_file(path, &size);
    if (data) gb_save_deserialize(g, data, size, (long long)time(NULL));
    free(data);
}

/* ------------------------------------------------------------------ */

static u8 read_pad(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u8 pad = 0;
    if (k[SDL_SCANCODE_RIGHT]) pad |= PAD_RIGHT;
    if (k[SDL_SCANCODE_LEFT]) pad |= PAD_LEFT;
    if (k[SDL_SCANCODE_UP]) pad |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) pad |= PAD_DOWN;
    if (k[SDL_SCANCODE_X]) pad |= PAD_A;
    if (k[SDL_SCANCODE_Z]) pad |= PAD_B;
    if (k[SDL_SCANCODE_RETURN]) pad |= PAD_START;
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) pad |= PAD_SELECT;
    /* La cruceta física no permite opuestos a la vez */
    if ((pad & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) pad &= (u8)~(PAD_LEFT | PAD_RIGHT);
    if ((pad & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) pad &= (u8)~(PAD_UP | PAD_DOWN);
    return pad;
}

/* Tonos del LCD del DMG, del más claro al más oscuro */
static const Uint32 palette[4] = { 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820 };

int main(int argc, char *argv[])
{
    const char *rom_arg = NULL;
    int scale = 4, interp_only = 0, force = 0, forget = 0;
    const char *record_path = NULL, *play_path = NULL, *uncovered_path = NULL;
    static GB gb;
    char msg[4096];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interp")) interp_only = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--forget-rom")) forget = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_path = argv[++i];
        else if (!strcmp(argv[i], "--uncovered") && i + 1 < argc) uncovered_path = argv[++i];
        else rom_arg = argv[i];
    }
    if (scale < 1) scale = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) fail(SDL_GetError());

    /* Carpeta de datos del juego: copia de la ROM y partidas */
    char *pref = SDL_GetPrefPath("GBRecomp", recomp_name);
    if (!pref) fail("No se pudo crear la carpeta de datos del juego.");
    char cache_path[1024], save_path[1024];
    snprintf(cache_path, sizeof cache_path, "%srom.gb", pref);
    snprintf(save_path, sizeof save_path, "%s%s.sav", pref, recomp_name);
    if (forget) u8_remove(cache_path);

    SDL_Window *win = SDL_CreateWindow(recomp_rom_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       GB_SCREEN_W * scale, GB_SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) fail(SDL_GetError());
    SDL_RenderSetLogicalSize(ren, GB_SCREEN_W, GB_SCREEN_H);
    SDL_SetRenderDrawColor(ren, 0xE0, 0xF8, 0xD0, 0xFF);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);

    /* ROM: argumento, copia guardada, rom.gb junto al ejecutable o aquí, o pedirla */
    Rom rom = { 0 };
    int have = 0, from_cache = 0;
    if (rom_arg) {
        int r = load_rom(rom_arg, &rom);
        if (r < 0) {
            snprintf(msg, sizeof msg, "No se pudo leer \"%s\".", rom_arg);
            fail(msg);
        }
        if (r == 0) {
            if (!force) {
                wrong_rom_message(rom_arg, msg, sizeof msg);
                fail(msg);
            }
            interp_only = 1;    /* el código recompilado es de otra ROM */
        }
        have = 1;
    }
    if (!have && load_rom(cache_path, &rom) == 1) have = from_cache = 1;
    if (!have) {
        free(rom.data);
        char *base = SDL_GetBasePath();
        char path[1024];
        snprintf(path, sizeof path, "%srom.gb", base ? base : "");
        SDL_free(base);
        if (load_rom(path, &rom) == 1) have = 1;
        else {
            free(rom.data);
            if (load_rom("rom.gb", &rom) == 1) have = 1;
            else free(rom.data);
        }
    }
    if (!have && !ask_for_rom(&rom)) return 0;
    fprintf(stderr, "ROM: %s\n", from_cache ? cache_path : rom_arg ? rom_arg : "elegida por el usuario o rom.gb");
    /* Copia para las siguientes veces (solo si es la ROM correcta) */
    if (!from_cache && !interp_only && !u8_write_file(cache_path, rom.data, rom.size))
        fprintf(stderr, "no se pudo guardar la copia de la ROM en %s\n", cache_path);

    const char *err = gb_init(&gb, rom.data, (u32)rom.size);
    free(rom.data);
    if (err) fail(err);
    if (!interp_only) gb.recomp_lookup = recomp_lookup;
    gb.apu.enabled = 1;
    if (uncovered_path) gb.trace = mark_uncovered;
    if (gb_has_battery(&gb)) load_game(&gb, save_path);

    InputScript script;
    int play = 0, last_pad = -1;
    long frame = 0;
    FILE *record = NULL;
    if (play_path) {
        const char *ierr = inputs_load(&script, play_path);
        if (ierr) fail(ierr);
        play = 1;
    }
    if (record_path && !(record = u8_fopen(record_path, "w"))) fail("No se pudo crear el archivo de grabación.");

    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         GB_SCREEN_W, GB_SCREEN_H);
    SDL_AudioSpec want, have_spec;
    SDL_zero(want);
    want.freq = GB_SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have_spec, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    const double frame_seconds = (double)GB_CYCLES_FRAME / GB_CPU_HZ;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = SDL_GetPerformanceCounter();
    static float samples[4096 * 2];
    Uint32 pixels[GB_SCREEN_W * GB_SCREEN_H];

    for (int running = 1; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_DROPFILE) SDL_free(ev.drop.file);
        }

        u8 pad = play ? inputs_pad(&script, frame) : read_pad();
        if (record) inputs_record(record, frame, pad, &last_pad);
        gb_set_pad(&gb, pad);
        frame++;
        gb_run_frame(&gb);

        /* Guardado automático cada 5 segundos si la RAM del cartucho cambió */
        if (gb.sram_dirty && frame % 300 == 0) save_game(&gb, save_path);

        int n = gb_audio_pull(&gb, samples, 4096);
        /* Si el audio se acumula (más de ~100 ms), se descarta para no atrasarse */
        if (audio && SDL_GetQueuedAudioSize(audio) < GB_SAMPLE_RATE / 10 * 2 * sizeof(float))
            SDL_QueueAudio(audio, samples, (Uint32)(n * 2 * sizeof(float)));

        const u8 *fb = gb_framebuffer(&gb);
        for (int i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) pixels[i] = palette[fb[i] & 3];
        SDL_UpdateTexture(tex, NULL, pixels, GB_SCREEN_W * sizeof(Uint32));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        /* Ritmo: 59.7275 cuadros por segundo */
        next += (Uint64)(frame_seconds * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) {
            Uint32 ms = (Uint32)((next - now) * 1000 / freq);
            if (ms > 1) SDL_Delay(ms - 1);
            while (SDL_GetPerformanceCounter() < next) {}
        } else if (now - next > freq / 4) {
            next = now;     /* muy atrasados (ventana movida, etc.): reiniciar */
        }
    }

    if (record) fclose(record);
    if (gb_has_battery(&gb) && !save_game(&gb, save_path)) {
        snprintf(msg, sizeof msg, "No se pudo guardar la partida en\n%s", save_path);
        fail(msg);
    }
    if (uncovered_path) write_uncovered(uncovered_path);
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_free(pref);
    SDL_Quit();
    gb_free(&gb);
    return 0;
}
