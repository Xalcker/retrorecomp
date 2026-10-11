/*
 * Lo mínimo de la API de libretro que usa retro_run.c: las constantes y estructuras públicas de la
 * interfaz (los núcleos las exportan con estos nombres y valores). No es una copia de libretro.h.
 */
#ifndef LIBRETRO_MIN_H
#define LIBRETRO_MIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RETRO_API_VERSION 1

#define RETRO_DEVICE_JOYPAD 1
enum {
    RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R,
    RETRO_DEVICE_ID_JOYPAD_COUNT = 16
};

#define RETRO_MEMORY_SYSTEM_RAM 2

#define RETRO_ENVIRONMENT_GET_CAN_DUPE             3
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY     9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT        10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS   11
#define RETRO_ENVIRONMENT_GET_VARIABLE            15
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE     17
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE       27
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY      31
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION 52
#define RETRO_ENVIRONMENT_SET_VARIABLES           16
#define RETRO_ENVIRONMENT_EXPERIMENTAL            0x10000

enum retro_pixel_format { RETRO_PIXEL_FORMAT_0RGB1555 = 0, RETRO_PIXEL_FORMAT_XRGB8888 = 1, RETRO_PIXEL_FORMAT_RGB565 = 2 };

struct retro_variable { const char *key; const char *value; };
struct retro_input_descriptor { unsigned port, device, index, id; const char *description; };
struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_system_info { const char *library_name, *library_version, *valid_extensions; bool need_fullpath, block_extract; };
struct retro_game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct retro_system_timing { double fps, sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geometry; struct retro_system_timing timing; };

enum retro_log_level { RETRO_LOG_DEBUG, RETRO_LOG_INFO, RETRO_LOG_WARN, RETRO_LOG_ERROR };
typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
struct retro_log_callback { retro_log_printf_t log; };

typedef bool (*retro_environment_t)(unsigned cmd, void *data);
typedef void (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#endif
