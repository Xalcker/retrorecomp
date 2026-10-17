/*
 * Perfil por muestreo (`--profile` de *bench y del bench del proyecto generado): en qué parte de la consola
 * se va el tiempo. El runtime marca la zona en la que está (prof_zone, un byte: una escritura al entrar y
 * otra al salir, que devuelve la zona de antes) y un hilo aparte la lee cada ~20 µs y cuenta. No cambia lo
 * que se ejecuta ni los ciclos; el hilo corre en otro núcleo. Lo que no está marcado cae en "68000 y lo
 * demás" (el 68000, interpretado o recompilado, el bus, las E/S).
 *
 * Común a los cinco recompiladores (retrorecomp/shared). Cada consola pone PROF_DEFINE en un .c de su runtime
 * y marca sus zonas con PROF_ENTER/PROF_LEAVE; solo bench.c define PROF_SAMPLER antes de incluirlo, para
 * tener el hilo (prof_start, prof_stop, prof_print), y fuera de Windows también _POSIX_C_SOURCE 200809L
 * (nanosleep con -std=c99).
 */
#ifndef PROF_H
#define PROF_H

enum { PROF_OTRO, PROF_Z80, PROF_SONIDO, PROF_VIDEO, PROF_ZONAS };

extern volatile unsigned char prof_zone;
#define PROF_DEFINE volatile unsigned char prof_zone
/* Marca la zona hasta PROF_LEAVE(), en el mismo bloque (se anidan: el YM dentro del Z80 cuenta como sonido) */
#define PROF_ENTER(z) unsigned char prof_prev_ = prof_zone; prof_zone = (unsigned char)(z)
#define PROF_LEAVE() (prof_zone = prof_prev_)

#ifdef PROF_SAMPLER
#include <stdio.h>

static unsigned long long prof_count[PROF_ZONAS];
static volatile int prof_running;

#ifdef _WIN32
/* Prototipos de kernel32 y de la CRT, para no incluir windows.h (como console.h) */
#include <process.h>
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *count);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *freq);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void *h, unsigned long ms);
__declspec(dllimport) int __stdcall CloseHandle(void *h);
static void *prof_thread;

static unsigned __stdcall prof_loop(void *arg)
{
    (void)arg;
    long long f, t, next;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&next);
    while (prof_running) {
        prof_count[prof_zone]++;
        next += f / 50000;                  /* 20 µs: Sleep no baja de 1 ms, así que espera activa */
        do QueryPerformanceCounter(&t); while (t < next && prof_running);
    }
    return 0;
}

static int prof_start(void)
{
    prof_running = 1;
    prof_thread = (void *)_beginthreadex(NULL, 0, prof_loop, NULL, 0, NULL);
    return prof_thread != NULL;
}

static void prof_stop(void)
{
    prof_running = 0;
    if (prof_thread) { WaitForSingleObject(prof_thread, 0xFFFFFFFFu); CloseHandle(prof_thread); prof_thread = NULL; }
}
#else
#include <pthread.h>
#include <time.h>
static pthread_t prof_thread;

static void *prof_loop(void *arg)
{
    (void)arg;
    struct timespec ts = { 0, 20000 };      /* 20 µs */
    while (prof_running) {
        prof_count[prof_zone]++;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static int prof_start(void)
{
    prof_running = 1;
    return pthread_create(&prof_thread, NULL, prof_loop, NULL) == 0;
}

static void prof_stop(void)
{
    prof_running = 0;
    pthread_join(prof_thread, NULL);
}
#endif

/* "Perfil (N muestras): [Z80 x%, ]sonido y%, video z%, <cpu> y lo demás w%". `cpu` es el nombre del procesador principal (el que
 * no marca zona: «68000», «6502»...) y `con_z80`, si la consola marca PROF_Z80 (la segunda CPU, la del sonido) */
static void prof_print(const char *cpu, int con_z80)
{
    unsigned long long n = 0;
    for (int i = 0; i < PROF_ZONAS; i++) n += prof_count[i];
    if (!n) { puts("Perfil: sin muestras"); return; }
    double d = (double)n;
    printf("Perfil (%llu muestras): ", n);
    if (con_z80) printf("Z80 %.1f%%, ", 100.0 * (double)prof_count[PROF_Z80] / d);
    printf("sonido %.1f%%, video %.1f%%, %s y lo demás %.1f%%\n", 100.0 * (double)prof_count[PROF_SONIDO] / d,
           100.0 * (double)prof_count[PROF_VIDEO] / d, cpu, 100.0 * (double)prof_count[PROF_OTRO] / d);
}
#endif

#endif
