/*
 * Guardado de la RAM del cartucho (batería) y del RTC del MBC3.
 *
 * Formato compatible con BGB y VBA-M: primero la RAM tal cual; si el cartucho
 * tiene RTC, siguen 48 bytes en little endian:
 *   5 × u32  registros actuales  (S, M, H, DL, DH)
 *   5 × u32  registros congelados por el latch
 *   u64      hora Unix del guardado
 * Al cargar, el reloj se adelanta el tiempo real que pasó desde entonces, como
 * un cartucho con su pila.
 *
 * gb_save_serialize/gb_save_deserialize trabajan en memoria (la plataforma
 * decide cómo escribir el archivo); gb_save_write/gb_save_read son atajos con
 * stdio para las herramientas.
 */
#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gb_has_battery(const GB *g)
{
    return g->mbc.has_battery && (g->sram_size || g->mbc.has_rtc);
}

static void put_le(u8 *p, u64 v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i));
}

static u64 get_le(const u8 *p, int n)
{
    u64 v = 0;
    for (int i = 0; i < n; i++) v |= (u64)p[i] << (8 * i);
    return v;
}

u8 *gb_save_serialize(GB *g, long long now, size_t *size)
{
    size_t n = g->sram_size + (g->mbc.has_rtc ? 48 : 0);
    u8 *data = malloc(n ? n : 1);
    *size = 0;
    if (!data) return NULL;
    if (g->sram_size) memcpy(data, g->sram, g->sram_size);
    if (g->mbc.has_rtc) {
        u8 *rtc = data + g->sram_size;
        for (int i = 0; i < 5; i++) {
            put_le(rtc + i * 4, g->mbc.rtc[i], 4);
            put_le(rtc + 20 + i * 4, g->mbc.rtc_latched[i], 4);
        }
        put_le(rtc + 40, (u64)now, 8);
    }
    *size = n;
    return data;
}

int gb_save_deserialize(GB *g, const u8 *data, size_t size, long long now)
{
    if (!gb_has_battery(g)) return 0;
    size_t ram = g->sram_size < size ? g->sram_size : size;
    if (ram) memcpy(g->sram, data, ram);

    /* RTC: 48 bytes (hora de 64 bits) o 44 (hora de 32 bits, versiones viejas) */
    long rest = (long)size - (long)g->sram_size;
    if (g->mbc.has_rtc && (rest == 48 || rest == 44)) {
        const u8 *p = data + g->sram_size;
        for (int i = 0; i < 5; i++) {
            g->mbc.rtc[i] = (u8)get_le(p + i * 4, 4);
            g->mbc.rtc_latched[i] = (u8)get_le(p + 20 + i * 4, 4);
        }
        long long saved = (long long)get_le(p + 40, rest == 48 ? 8 : 4);
        if (now > saved) mbc_rtc_advance(g, (u64)(now - saved));
        g->mbc.rtc_last_cyc = g->cyc;
        g->mbc.rtc_sub = 0;
    }
    g->sram_dirty = 0;
    return 1;
}

int gb_save_write(GB *g, const char *path, long long now)
{
    if (!gb_has_battery(g)) return 1;
    size_t size;
    u8 *data = gb_save_serialize(g, now, &size);
    if (!data) return 0;
    /* Escribe a un temporal y lo renombra: un corte a la mitad no daña la
     * partida anterior */
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = 0;
    free(data);
    if (!ok) {
        remove(tmp);
        return 0;
    }
    remove(path);   /* en Windows rename no reemplaza */
    if (rename(tmp, path) != 0) return 0;
    g->sram_dirty = 0;
    return 1;
}

int gb_save_read(GB *g, const char *path, long long now)
{
    if (!gb_has_battery(g)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *data = malloc(size > 0 ? (size_t)size : 1);
    int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (ok) ok = gb_save_deserialize(g, data, (size_t)size, now);
    free(data);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Estados guardados                                                   */
/* ------------------------------------------------------------------ */

/*
 * Un estado es la máquina completa en un instante: el struct GB tal cual, más
 * la RAM del cartucho. No es un formato portable: solo lo lee el mismo
 * ejecutable (el encabezado lleva el tamaño del struct y la cabecera de la
 * ROM, y si no coinciden se rechaza).
 *
 *   "GBRS", versión (u32), sizeof(GB) (u32), tamaño de la RAM (u32),
 *   cuadro en construcción (u32), $134-$14F de la ROM (28 bytes),
 *   struct GB, RAM del cartucho
 */
enum { STATE_VERSION = 1, STATE_HEADER = 4 + 4 * 4 + 28 };

u8 *gb_state_save(const GB *g, size_t *size)
{
    *size = STATE_HEADER + sizeof *g + g->sram_size;
    u8 *data = malloc(*size);
    if (!data) return NULL;
    memcpy(data, "GBRS", 4);
    put_le(data + 4, STATE_VERSION, 4);
    put_le(data + 8, sizeof *g, 4);
    put_le(data + 12, g->sram_size, 4);
    put_le(data + 16, g->ppu.back == g->ppu.buf[0] ? 0 : 1, 4);
    memcpy(data + 20, g->rom + 0x134, 28);
    memcpy(data + STATE_HEADER, g, sizeof *g);
    if (g->sram_size) memcpy(data + STATE_HEADER + sizeof *g, g->sram, g->sram_size);
    return data;
}

int gb_state_load(GB *g, const u8 *data, size_t size)
{
    if (size < STATE_HEADER || memcmp(data, "GBRS", 4) != 0) return 0;
    if (get_le(data + 4, 4) != STATE_VERSION || get_le(data + 8, 4) != sizeof *g) return 0;
    u32 sram_size = (u32)get_le(data + 12, 4);
    if (sram_size != g->sram_size || size != STATE_HEADER + sizeof *g + sram_size) return 0;
    if (memcmp(data + 20, g->rom + 0x134, 28) != 0) return 0;

    GB *tmp = malloc(sizeof *tmp);
    if (!tmp) return 0;
    memcpy(tmp, data + STATE_HEADER, sizeof *tmp);
    /* Los punteros guardados son de otra corrida: se reemplazan antes de
     * copiar (gb_copy_state conserva los de g y copia la RAM desde aquí) */
    tmp->sram = sram_size ? (u8 *)(data + STATE_HEADER + sizeof *g) : NULL;
    tmp->serial.out = NULL;
    tmp->serial.out_len = 0;
    tmp->ppu.back = tmp->ppu.buf[get_le(data + 16, 4) & 1];
    int sram_dirty = g->sram_dirty;
    gb_copy_state(g, tmp);
    free(tmp);
    /* El audio pendiente era de la corrida anterior */
    g->apu.ring_tail = g->apu.ring_head;
    g->sram_dirty = sram_dirty || sram_size;   /* la RAM cambió: que se guarde el .sav */
    return 1;
}
