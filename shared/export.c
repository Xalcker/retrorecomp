#include "export.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "png.h"
#include "wav.h"

int screenshot_write(const char *path, const uint32_t *argb, int w, int h)
{
    unsigned char *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return 0;
    for (int i = 0; i < w * h; i++) {
        rgb[i * 3] = (unsigned char)(argb[i] >> 16);
        rgb[i * 3 + 1] = (unsigned char)(argb[i] >> 8);
        rgb[i * 3 + 2] = (unsigned char)argb[i];
    }
    png_open_file = u8_fopen;
    int ok = png_write_rgb(path, rgb, w, h);
    free(rgb);
    return ok;
}

static void wav_header(WavRecorder *w)
{
    wav_put_header(w->f, (uint32_t)w->rate, 1, w->bytes);   /* mono */
}

int wav_start(WavRecorder *w, const char *path, int rate)
{
    memset(w, 0, sizeof *w);
    w->f = u8_fopen(path, "wb");
    if (!w->f) return 0;
    w->rate = rate;
    wav_header(w);
    return 1;
}

void wav_samples(WavRecorder *w, const int16_t *s, int n)
{
    if (!w->f || n <= 0) return;
    fwrite(s, 2, (size_t)n, w->f);
    w->bytes += (uint32_t)n * 2;
}

void wav_stop(WavRecorder *w)
{
    if (!w->f) return;
    wav_header(w);
    fclose(w->f);
    w->f = NULL;
}

int wav_active(const WavRecorder *w) { return w->f != NULL; }
