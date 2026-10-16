/*
 * Salida de sonido (SDL2) común a los cuatro recompiladores: una cola de SDL (SDL_QueueAudio) en float
 * estéreo, a las muestras por segundo de la consola. Cada main_sdl.c le pasa lo que el runtime mezcló en el
 * cuadro, ya con el volumen; la grabación WAV, el silencio por canal y el audio del cable link de GB se
 * quedan en cada consola. Va entre los #include de main_sdl.c (necesita SDL.h).
 *
 * El bucle marca el ritmo de la consola con el reloj del sistema, que nunca coincide
 * exacto con el de la tarjeta de sonido: la cola crecería o se vaciaría poco a poco. Para tenerla cerca de
 * 'target' cada cuadro se remuestrea (cúbico) con una razón que se ajusta a lo más ±1 %: un control PI sobre
 * la cola promediada, cuya parte integral ('drift') aprende la diferencia entre los dos relojes.
 *
 * Al empezar, tras player_audio_stop (pausa, menús, avance rápido, sonido apagado) o tras vaciarse la cola,
 * se pone silencio delante hasta 'target' (el pre-búfer: la tarjeta lo toca mientras llega lo demás) y el
 * sonido entra con una rampa de 5 ms; al parar baja con otra rampa desde el último par, y lo que quedaba en
 * la cola se toca hasta el final. Así no hay chasquidos al pausar ni al volver. Si la cola pasa de 'cap'
 * (el juego corrió de más) el cuadro se descarta. tests/player_audio_test.c lo prueba con una tarjeta
 * simulada.
 */
#ifndef PLAYER_AUDIO_H
#define PLAYER_AUDIO_H

#define PA_CHUNK  512         /* pares que se juntan antes de mandarlos a la cola */
#define PA_RANGE  0.01        /* la razón se mueve a lo más ±1 % */
#define PA_SMOOTH 0.02        /* promedio de la cola: ~50 cuadros */
#define PA_KP     0.02        /* parte proporcional, por unidad de error (cola vacía o al doble) */
#define PA_KI     0.00003     /* parte integral, por cuadro: con PA_KP, amortiguado sin oscilar */
/* Cuadros de 60 Hz en la cola además del bloque que pide la tarjeta: uno de margen */
#define PA_AHEAD 1

typedef struct {
    SDL_AudioDeviceID dev;  /* 0: sin sonido (sin tarjeta, o en las capturas de prueba) */
    int target, cap;        /* cola deseada y tope, en pares */
    int ramp;               /* largo de las rampas de entrada y salida, en pares */
    int playing;            /* 0: callada (lo siguiente empieza con pre-búfer y rampa) */
    int fade;               /* pares que quedan de la rampa de entrada */
    double level;           /* la cola, promediada */
    double drift;           /* parte integral: la diferencia de relojes aprendida */
    double ratio;           /* pares de salida por par de entrada */
    double pos;             /* posición del siguiente par de salida entre hist[1] y hist[2] */
    float hist[4][2];       /* los últimos cuatro pares de entrada */
    float last[2];          /* el último par mandado: de ahí baja la rampa de salida */
    float out[PA_CHUNK][2];
    int out_n;
    long underruns, drops;  /* veces que la cola se vació sonando, y cuadros descartados por el tope */
} PlayerAudio;

/* Abre la tarjeta a 'rate' muestras por segundo. Sin tarjeta, todo lo demás no hace nada */
static void player_audio_open(PlayerAudio *pa, int rate)
{
    SDL_AudioSpec want, have;
    SDL_zerop(pa);
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    pa->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    /* Lo que pide la tarjeta de una vez y uno o dos cuadros (~40 o ~57 ms a 44.1 kHz) */
    pa->target = want.samples + PA_AHEAD * rate / 60;
    pa->cap = 4 * pa->target;
    pa->ramp = rate / 200;
    pa->ratio = 1;
    if (pa->dev) SDL_PauseAudioDevice(pa->dev, 0);
}

static void player_audio_close(PlayerAudio *pa)
{
    if (pa->dev) SDL_CloseAudioDevice(pa->dev);
    pa->dev = 0;
}

static void pa_flush(PlayerAudio *pa)
{
    if (pa->out_n) SDL_QueueAudio(pa->dev, pa->out, (Uint32)(pa->out_n * sizeof pa->out[0]));
    pa->out_n = 0;
}

static void pa_put(PlayerAudio *pa, float l, float r)
{
    pa->out[pa->out_n][0] = l;
    pa->out[pa->out_n][1] = r;
    if (++pa->out_n == PA_CHUNK) pa_flush(pa);
}

/* Callarse (pausa, menús, avance rápido, sonido apagado): la rampa desde el último par hasta cero. Lo que
 * queda en la cola se toca hasta el final; lo siguiente que llegue empieza con pre-búfer */
static void player_audio_stop(PlayerAudio *pa)
{
    if (!pa->playing) return;
    for (int i = 1; i <= pa->ramp; i++) {
        float g = 1.0f - (float)i / (float)pa->ramp;
        pa_put(pa, pa->last[0] * g, pa->last[1] * g);
    }
    pa_flush(pa);
    pa->playing = 0;
}

/* Antes de cada cuadro: el pre-búfer si hace falta y la razón nueva según la cola. 0 si el cuadro no va */
static int pa_begin(PlayerAudio *pa)
{
    if (!pa->dev) return 0;
    int queued = (int)(SDL_GetQueuedAudioSize(pa->dev) / sizeof pa->out[0]);
    if (pa->playing && queued == 0) {
        pa->underruns++;    /* el juego se atrasó (ventana arrastrada, pestaña oculta...) */
        pa->playing = 0;
    }
    if (!pa->playing) {
        SDL_memset(pa->out, 0, sizeof pa->out);
        for (int k = queued; k < pa->target; k += PA_CHUNK) {
            int n = pa->target - k < PA_CHUNK ? pa->target - k : PA_CHUNK;
            SDL_QueueAudio(pa->dev, pa->out, (Uint32)(n * sizeof pa->out[0]));
        }
        if (queued < pa->target) queued = pa->target;
        SDL_memset(pa->hist, 0, sizeof pa->hist);
        pa->pos = 0;
        pa->fade = pa->ramp;
        pa->level = queued;
        pa->playing = 1;
    }
    pa->level += PA_SMOOTH * (queued - pa->level);
    double err = (pa->target - pa->level) / pa->target;
    if (err > 1) err = 1;
    if (err < -1) err = -1;
    pa->drift += PA_KI * err;
    if (pa->drift > PA_RANGE) pa->drift = PA_RANGE;
    if (pa->drift < -PA_RANGE) pa->drift = -PA_RANGE;
    pa->ratio = 1 + pa->drift + PA_KP * err;
    if (pa->ratio > 1 + PA_RANGE) pa->ratio = 1 + PA_RANGE;
    if (pa->ratio < 1 - PA_RANGE) pa->ratio = 1 - PA_RANGE;
    if (queued > pa->cap) {
        pa->drops++;
        return 0;
    }
    return 1;
}

/* Catmull-Rom entre b y c */
static float pa_cubic(float a, float b, float c, float d, float t)
{
    return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

/* Un par de entrada: sale uno, ninguno o dos según la razón */
static void pa_frame(PlayerAudio *pa, float l, float r)
{
    float (*h)[2] = pa->hist;
    SDL_memmove(h, h + 1, 3 * sizeof h[0]);
    h[3][0] = l;
    h[3][1] = r;
    double step = 1 / pa->ratio;
    for (; pa->pos < 1; pa->pos += step) {
        float g = 1;
        if (pa->fade) g = 1 - (float)pa->fade-- / (float)pa->ramp;
        for (int c = 0; c < 2; c++) {
            float x = g * pa_cubic(h[0][c], h[1][c], h[2][c], h[3][c], (float)pa->pos);
            pa->last[c] = x < -1 ? -1 : x > 1 ? 1 : x;
        }
        pa_put(pa, pa->last[0], pa->last[1]);
    }
    pa->pos -= 1;
}

/* Un cuadro en float estéreo (GB) */
static inline void player_audio_push(PlayerAudio *pa, const float *s, int n)
{
    if (!pa_begin(pa)) return;
    for (int i = 0; i < n; i++) pa_frame(pa, s[i * 2], s[i * 2 + 1]);
    pa_flush(pa);
}

/* Un cuadro en enteros de 16 bits, mono (NES) o estéreo (GEN, NEO) */
static inline void player_audio_push_s16(PlayerAudio *pa, const Sint16 *s, int n, int channels)
{
    if (!pa_begin(pa)) return;
    const float k = 1.0f / 32768;
    for (int i = 0; i < n; i++) {
        const Sint16 *p = s + i * channels;
        pa_frame(pa, p[0] * k, p[channels - 1] * k);
    }
    pa_flush(pa);
}

#endif
