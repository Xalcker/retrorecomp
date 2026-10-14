#include "gbexport.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "png.h"

/* ------------------------------------------------------------------ */
/* Música: VGM                                                         */
/* ------------------------------------------------------------------ */

/*
 * VGM 1.71: encabezado de 256 bytes; el reloj del Game Boy va en $80. Los
 * datos son comandos: $B3 rr dd escribe dd en el registro $FF10 + rr; $61 nn nn
 * espera nn muestras de 44.1 kHz ($62 y $63: un cuadro de 60 y 50 Hz; $7n:
 * n + 1 muestras); $66 termina. Al final va la etiqueta GD3 (textos en
 * UTF-16).
 */
enum { VGM_RATE = 44100, VGM_HEADER = 0x100 };

static void put_le32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static void vgm_wait_until(MusicRecorder *m, u64 target)
{
    while (m->vgm_samples < target) {
        u64 left = target - m->vgm_samples;
        u32 n = left > 65535 ? 65535 : (u32)left;
        if (n == 735) fputc(0x62, m->vgm);
        else if (n == 882) fputc(0x63, m->vgm);
        else if (n <= 16) fputc(0x70 + (int)(n - 1), m->vgm);
        else {
            u8 cmd[3] = { 0x61, (u8)n, (u8)(n >> 8) };
            fwrite(cmd, 1, 3, m->vgm);
        }
        m->vgm_samples += n;
    }
}

static void vgm_write_reg(MusicRecorder *m, u16 addr, u8 value)
{
    u8 cmd[3] = { 0xB3, (u8)(addr - 0xFF10), value };
    fwrite(cmd, 1, 3, m->vgm);
}

/* Estado actual del chip, para que la grabación empiece donde va el juego */
static void vgm_initial_state(MusicRecorder *m, GB *g)
{
    const u8 *regs = g->apu.regs;   /* $FF10-$FF3F */
    vgm_write_reg(m, 0xFF26, g->apu.power ? 0x80 : 0x00);
    if (!g->apu.power) return;
    vgm_write_reg(m, 0xFF1A, 0x00);     /* canal 3 apagado para escribir la onda */
    for (u16 a = 0xFF30; a <= 0xFF3F; a++) vgm_write_reg(m, a, regs[a - 0xFF10]);
    for (u16 a = 0xFF10; a <= 0xFF25; a++) {
        u8 v = regs[a - 0xFF10];
        /* Sin disparar los canales: las notas que ya sonaban empiezan en la
         * siguiente que toque el juego */
        if (a == 0xFF14 || a == 0xFF19 || a == 0xFF1E || a == 0xFF23) v &= 0x7F;
        vgm_write_reg(m, a, v);
    }
}

/* Texto ASCII como UTF-16LE terminado en 0 */
static size_t gd3_string(u8 *out, const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        out[n++] = (u8)*s;
        out[n++] = 0;
    }
    out[n++] = 0;
    out[n++] = 0;
    return n;
}

static void vgm_finish(MusicRecorder *m, const char *title)
{
    fputc(0x66, m->vgm);
    long gd3_pos = ftell(m->vgm);

    /* GD3: pista, juego, sistema y autor (en inglés y japonés), fecha,
     * quién lo grabó y notas */
    const char *fields[11] = { "", "", title, "", "Nintendo Game Boy", "", "", "", "",
                               "GBRecomp", "" };
    static u8 body[4096];
    size_t len = 0;
    for (int i = 0; i < 11; i++) len += gd3_string(body + len, fields[i]);
    u8 head[12];
    memcpy(head, "Gd3 ", 4);
    put_le32(head + 4, 0x100);
    put_le32(head + 8, (u32)len);
    fwrite(head, 1, 12, m->vgm);
    fwrite(body, 1, len, m->vgm);
    long end = ftell(m->vgm);

    u8 h[VGM_HEADER] = { 0 };
    memcpy(h, "Vgm ", 4);
    put_le32(h + 0x04, (u32)(end - 4));             /* fin del archivo */
    put_le32(h + 0x08, 0x171);                      /* versión */
    put_le32(h + 0x14, (u32)(gd3_pos - 0x14));      /* GD3 */
    put_le32(h + 0x18, (u32)m->vgm_samples);        /* muestras totales */
    put_le32(h + 0x34, VGM_HEADER - 0x34);          /* inicio de los datos */
    put_le32(h + 0x80, GB_CPU_HZ);                  /* Game Boy (DMG) */
    fseek(m->vgm, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, m->vgm);
    fclose(m->vgm);
    m->vgm = NULL;
}

/* ------------------------------------------------------------------ */
/* Música: WAV                                                         */
/* ------------------------------------------------------------------ */

static void wav_header(FILE *f, u32 frames)
{
    u8 h[44];
    u32 bytes = frames * 4;
    memcpy(h, "RIFF", 4);
    put_le32(h + 4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16);
    h[20] = 1; h[21] = 0;                       /* PCM */
    h[22] = 2; h[23] = 0;                       /* estéreo */
    put_le32(h + 24, GB_SAMPLE_RATE);
    put_le32(h + 28, GB_SAMPLE_RATE * 4);
    h[32] = 4; h[33] = 0;                       /* bytes por par */
    h[34] = 16; h[35] = 0;                      /* bits */
    memcpy(h + 36, "data", 4);
    put_le32(h + 40, bytes);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
}

/* ------------------------------------------------------------------ */

int music_start(MusicRecorder *m, GB *g, const char *base)
{
    char path[1100];
    memset(m, 0, sizeof *m);
    snprintf(m->base, sizeof m->base, "%s", base);
    snprintf(path, sizeof path, "%s.vgm", base);
    m->vgm = u8_fopen(path, "wb");
    snprintf(path, sizeof path, "%s.wav", base);
    m->wav = u8_fopen(path, "wb");
    if (!m->vgm || !m->wav) {
        if (m->vgm) fclose(m->vgm);
        if (m->wav) fclose(m->wav);
        m->vgm = m->wav = NULL;
        return 0;
    }
    static const u8 zeros[VGM_HEADER];
    fwrite(zeros, 1, VGM_HEADER, m->vgm);
    fwrite(zeros, 1, 44, m->wav);
    m->start_rt = g->rt;
    vgm_initial_state(m, g);
    return 1;
}

void music_register(MusicRecorder *m, GB *g, u16 addr, u8 value)
{
    if (!m->vgm) return;
    vgm_wait_until(m, (g->rt - m->start_rt) * VGM_RATE / GB_CPU_HZ);
    vgm_write_reg(m, addr, value);
}

void music_samples(MusicRecorder *m, const float *samples, int frames)
{
    if (!m->wav) return;
    static short pcm[4096 * 2];
    if (frames > 4096) frames = 4096;
    for (int i = 0; i < frames * 2; i++) {
        float v = samples[i] * 32767.0f;
        pcm[i] = (short)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
    }
    fwrite(pcm, sizeof pcm[0], (size_t)frames * 2, m->wav);
    m->wav_frames += (u32)frames;
}

void music_stop(MusicRecorder *m, GB *g, const char *title)
{
    if (m->vgm) {
        vgm_wait_until(m, (g->rt - m->start_rt) * VGM_RATE / GB_CPU_HZ);
        vgm_finish(m, title);
    }
    if (m->wav) {
        wav_header(m->wav, m->wav_frames);
        fclose(m->wav);
        m->wav = NULL;
    }
}

int music_active(const MusicRecorder *m)
{
    return m->vgm != NULL;
}

/* ------------------------------------------------------------------ */
/* Gráficos                                                            */
/* ------------------------------------------------------------------ */

static u32 tile_hash(const u8 *t)
{
    u32 h = 2166136261u;    /* FNV-1a */
    for (int i = 0; i < 16; i++) h = (h ^ t[i]) * 16777619u;
    return h;
}

static void table_insert(TileCollector *t, int index)
{
    u32 mask = (u32)t->table_size - 1;
    u32 i = tile_hash(t->tiles[index]) & mask;
    while (t->table[i]) i = (i + 1) & mask;
    t->table[i] = (u32)index + 1;
}

static int table_grow(TileCollector *t)
{
    int size = t->table_size ? t->table_size * 2 : 4096;
    u32 *table = calloc((size_t)size, sizeof *table);
    if (!table) return 0;
    free(t->table);
    t->table = table;
    t->table_size = size;
    for (int i = 0; i < t->count; i++) table_insert(t, i);
    return 1;
}

void tiles_start(TileCollector *t)
{
    free(t->tiles);
    free(t->table);
    memset(t, 0, sizeof *t);
    t->active = 1;
}

static void add_tile(TileCollector *t, const u8 *data)
{
    static const u8 empty[16];
    if (!memcmp(data, empty, 16)) return;
    if (!t->table_size || (t->count + 1) * 2 > t->table_size) {
        if (!table_grow(t)) return;
    }
    u32 mask = (u32)t->table_size - 1;
    for (u32 i = tile_hash(data) & mask; t->table[i]; i = (i + 1) & mask)
        if (!memcmp(t->tiles[t->table[i] - 1], data, 16)) return;    /* ya está */
    if (t->count == t->cap) {
        int cap = t->cap ? t->cap * 2 : 1024;
        u8 (*tiles)[16] = realloc(t->tiles, (size_t)cap * 16);
        if (!tiles) return;
        t->tiles = tiles;
        t->cap = cap;
    }
    memcpy(t->tiles[t->count], data, 16);
    table_insert(t, t->count);
    t->count++;
}

void tiles_scan(TileCollector *t, const GB *g)
{
    if (!t->active) return;
    /* 384 mosaicos en $8000-$97FF; en modo color también los del banco 1 */
    for (int bank = 0; bank < (g->cgb ? 2 : 1); bank++)
        for (int i = 0; i < 384; i++) add_tile(t, &g->vram[bank * 0x2000 + i * 16]);
}

int tiles_stop(TileCollector *t, const char *base)
{
    enum { COLS = 16, ROWS = 64, PER_SHEET = COLS * ROWS };
    static const u8 shade[4] = { 0xFF, 0xAA, 0x55, 0x00 };
    int sheets = 0;
    t->active = 0;
    png_open_file = u8_fopen;
    for (int first = 0; first < t->count; first += PER_SHEET) {
        int n = t->count - first < PER_SHEET ? t->count - first : PER_SHEET;
        int rows = (n + COLS - 1) / COLS;
        int w = COLS * 8, h = rows * 8;
        u8 *px = malloc((size_t)w * h);
        if (!px) break;
        memset(px, 0xFF, (size_t)w * h);
        for (int k = 0; k < n; k++) {
            const u8 *tile = t->tiles[first + k];
            int tx = (k % COLS) * 8, ty = (k / COLS) * 8;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int b = 7 - x;
                    int v = ((tile[y * 2] >> b) & 1) | (((tile[y * 2 + 1] >> b) & 1) << 1);
                    px[(ty + y) * w + tx + x] = shade[v];
                }
        }
        char path[1100];
        snprintf(path, sizeof path, "%s_%d.png", base, sheets + 1);
        if (png_write_gray(path, px, w, h)) sheets++;
        free(px);
    }
    free(t->tiles);
    free(t->table);
    memset(t, 0, sizeof *t);
    return sheets;
}
