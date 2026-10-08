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
