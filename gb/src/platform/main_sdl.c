/*
 * Capa de plataforma con SDL2 para un juego recompilado: ventana, controles,
 * audio y ritmo de 59.7275 cuadros por segundo.
 *
 * Uso: juego [rom.gb] [--scale N] [--interp] [--force] [--record archivo | --play archivo]
 *   Sin argumentos busca "rom.gb" en la carpeta actual.
 *   --interp  ejecuta todo con el intérprete (para comparar)
 *   --force   acepta una ROM con otro SHA1 (solo con el intérprete)
 *   --record  graba los botones en un archivo de texto (ver inputs.h), para
 *             reproducirlos con --play o con difftest --input
 *   --play    reproduce un archivo grabado (el teclado se ignora)
 *
 * Controles: flechas, X = A, Z = B, Enter = Start, Retroceso = Select,
 * Esc = salir.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inputs.h"
#include "recomp.h"
#include "sha1.h"

static void fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, recomp_rom_title, msg, NULL);
    exit(1);
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
    const char *rom_path = "rom.gb";
    int scale = 4, interp_only = 0, force = 0;
    const char *record_path = NULL, *play_path = NULL;
    static GB gb;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interp")) interp_only = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_path = argv[++i];
        else rom_path = argv[i];
    }
    if (scale < 1) scale = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) fail(SDL_GetError());

    long size;
    unsigned char *rom = read_file(rom_path, &size);
    char msg[512];
    if (!rom) {
        snprintf(msg, sizeof msg, "No se encontró la ROM \"%s\".\n"
                 "Copia tu ROM de %s junto al ejecutable con el nombre rom.gb o pásala como argumento.",
                 rom_path, recomp_rom_title);
        fail(msg);
    }
    char sha1[41];
    sha1_hex(rom, (size_t)size, sha1);
    if (strcmp(sha1, recomp_rom_sha1) != 0) {
        if (!force) {
            snprintf(msg, sizeof msg, "La ROM \"%s\" no es la esperada.\nSHA1: %s\nSe esperaba: %s",
                     rom_path, sha1, recomp_rom_sha1);
            fail(msg);
        }
        interp_only = 1;    /* el código recompilado es de otra ROM */
    }

    const char *err = gb_init(&gb, rom, (u32)size);
    free(rom);
    if (err) fail(err);
    if (!interp_only) gb.recomp_lookup = recomp_lookup;
    gb.apu.enabled = 1;

    InputScript script;
    int play = 0, last_pad = -1;
    long frame = 0;
    FILE *record = NULL;
    if (play_path) {
        const char *ierr = inputs_load(&script, play_path);
        if (ierr) fail(ierr);
        play = 1;
    }
    if (record_path && !(record = fopen(record_path, "w"))) fail("no se pudo crear el archivo de grabación");

    SDL_Window *win = SDL_CreateWindow(recomp_rom_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       GB_SCREEN_W * scale, GB_SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) fail(SDL_GetError());
    SDL_RenderSetLogicalSize(ren, GB_SCREEN_W, GB_SCREEN_H);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         GB_SCREEN_W, GB_SCREEN_H);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = GB_SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
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
        }

        u8 pad = play ? inputs_pad(&script, frame) : read_pad();
        if (record) inputs_record(record, frame, pad, &last_pad);
        gb_set_pad(&gb, pad);
        frame++;
        gb_run_frame(&gb);

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
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    gb_free(&gb);
    return 0;
}
