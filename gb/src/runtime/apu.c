/*
 * Unidad de audio (APU) del DMG: dos canales de pulso (el primero con sweep),
 * uno de onda y uno de ruido. El secuenciador de 512 Hz lo marca el flanco de
 * bajada del bit 4 de DIV (apu_div_event), así que escribir en DIV lo afecta
 * igual que en el hardware.
 *
 * Basado en el APU del port de Tetris, adaptado al contexto GB* y a avanzar
 * por M-ciclos.
 */
#include "gb.h"

#include <string.h>

#define REG(a) g->apu.regs[(a) - 0xFF10]

static const u8 duty_table[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 1, 1, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 0 },
};

static const int noise_divisor[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

/* Bits que siempre se leen en 1 en $FF10-$FF2F */
static const u8 read_mask[0x20] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,   /* NR10-NR14 */
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,   /* NR20-NR24 */
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,   /* NR30-NR34 */
    0xFF, 0xFF, 0x00, 0x00, 0xBF,   /* NR40-NR44 */
    0x00, 0x00, 0x70,               /* NR50-NR52 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

void apu_reset(GB *g)
{
    Apu *a = &g->apu;
    int enabled = a->enabled;
    memset(a, 0, sizeof *a);
    a->enabled = enabled;
    /* Estado tras la boot ROM del DMG */
    a->power = 1;
    REG(0xFF26) = 0x80;
    REG(0xFF24) = 0x77;
    REG(0xFF25) = 0xF3;
    REG(0xFF10) = 0x80;
    REG(0xFF11) = 0xBF;
    REG(0xFF12) = 0xF3;
    REG(0xFF14) = 0xBF;
    REG(0xFF16) = 0x3F;
    REG(0xFF19) = 0xBF;
    REG(0xFF1A) = 0x7F;
    REG(0xFF1B) = 0xFF;
    REG(0xFF1C) = 0x9F;
    REG(0xFF1E) = 0xBF;
    REG(0xFF20) = 0xFF;
    REG(0xFF23) = 0xBF;
    a->ch[0].duty = 2;
    a->ch[0].dac = 1;
    a->ch[0].on = 1;        /* el sonido de arranque deja el canal 1 activo */
    a->ch[0].volume = 0;
    a->ch[1].dac = 0;
    a->ch[3].lfsr = 0x7FFF;
}

static int sweep_calc(ApuChannel *c)
{
    int delta = c->shadow_freq >> c->sweep_shift;
    int f;
    if (c->sweep_negate) {
        f = c->shadow_freq - delta;
        c->sweep_negated = 1;
    } else {
        f = c->shadow_freq + delta;
    }
    if (f > 2047) c->on = 0;
    return f;
}

static void trigger(GB *g, int n)
{
    ApuChannel *c = &g->apu.ch[n];
    c->on = c->dac;
    if (c->length == 0)
        c->length = (n == 2) ? 256 : 64;

    switch (n) {
    case 0:
    case 1:
        c->timer = (2048 - c->freq) * 4;
        break;
    case 2:
        c->timer = (2048 - c->freq) * 2 + 6;
        c->position = 0;
        break;
    default:
        c->timer = noise_divisor[c->divisor] << c->clock_shift;
        c->lfsr = 0x7FFF;
        break;
    }

    if (n != 2) {
        u8 env = REG(0xFF12 + n * 5);
        c->volume = env >> 4;
        c->env_up = (env >> 3) & 1;
        c->env_period = env & 7;
        c->env_timer = c->env_period ? c->env_period : 8;
    }

    if (n == 0) {
        c->shadow_freq = c->freq;
        c->sweep_timer = c->sweep_period ? c->sweep_period : 8;
        c->sweep_enabled = c->sweep_period || c->sweep_shift;
        c->sweep_negated = 0;
        if (c->sweep_shift) sweep_calc(c);
    }
}

static void length_clock(ApuChannel *c)
{
    if (c->length_enable && c->length > 0 && --c->length == 0)
        c->on = 0;
}

/* Frame sequencer: duración a 256 Hz, sweep a 128 Hz, envolvente a 64 Hz */
void apu_div_event(GB *g)
{
    Apu *a = &g->apu;
    if (!a->power) return;

    int step = a->seq_step;
    a->seq_step = (step + 1) & 7;

    if ((step & 1) == 0)
        for (int i = 0; i < 4; i++) length_clock(&a->ch[i]);

    if (step == 2 || step == 6) {
        ApuChannel *c = &a->ch[0];
        if (--c->sweep_timer <= 0) {
            c->sweep_timer = c->sweep_period ? c->sweep_period : 8;
            if (c->sweep_enabled && c->sweep_period) {
                int f = sweep_calc(c);
                if (f <= 2047 && c->sweep_shift) {
                    c->shadow_freq = f;
                    c->freq = f;
                    REG(0xFF13) = (u8)f;
                    REG(0xFF14) = (u8)((REG(0xFF14) & 0xF8) | (f >> 8));
                    sweep_calc(c);
                }
            }
        }
    }

    if (step == 7) {
        static const int env_channels[3] = { 0, 1, 3 };
        for (int k = 0; k < 3; k++) {
            ApuChannel *c = &a->ch[env_channels[k]];
            if (!c->env_period) continue;
            if (--c->env_timer > 0) continue;
            c->env_timer = c->env_period;
            if (c->env_up && c->volume < 15) c->volume++;
            else if (!c->env_up && c->volume > 0) c->volume--;
        }
    }
}

static int channel_output(GB *g, int n)
{
    ApuChannel *c = &g->apu.ch[n];
    if (!c->on) return 0;
    switch (n) {
    case 0:
    case 1:
        return duty_table[c->duty][c->step] ? c->volume : 0;
    case 2: {
        static const int shift[4] = { 4, 0, 1, 2 };
        int sample = (c->position & 1) ? (c->sample_buffer & 0x0F) : (c->sample_buffer >> 4);
        return sample >> shift[c->volume_code];
    }
    default:
        return (c->lfsr & 1) ? 0 : c->volume;
    }
}

static void clock_channels(GB *g, int cycles)
{
    for (int n = 0; n < 4; n++) {
        ApuChannel *c = &g->apu.ch[n];
        c->timer -= cycles;
        while (c->timer <= 0) {
            switch (n) {
            case 0:
            case 1:
                c->timer += (2048 - c->freq) * 4;
                c->step = (c->step + 1) & 7;
                break;
            case 2:
                c->timer += (2048 - c->freq) * 2;
                c->position = (c->position + 1) & 31;
                c->sample_buffer = REG(0xFF30 + c->position / 2);
                break;
            default: {
                c->timer += noise_divisor[c->divisor] << c->clock_shift;
                int bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
                c->lfsr = (u16)((c->lfsr >> 1) | (bit << 14));
                if (c->width7)
                    c->lfsr = (u16)((c->lfsr & ~0x40) | (bit << 6));
                break;
            }
            }
        }
    }
}

static void push_sample(Apu *a, float l, float r)
{
    u32 next = (a->ring_head + 1) % APU_RING;
    if (next == a->ring_tail) return;   /* cola llena: se descarta */
    a->ring[a->ring_head * 2] = l;
    a->ring[a->ring_head * 2 + 1] = r;
    a->ring_head = next;
}

void apu_mcycle(GB *g)
{
    Apu *a = &g->apu;
    const double cycles_per_sample = (double)GB_CPU_HZ / GB_SAMPLE_RATE;

    if (a->power) {
        clock_channels(g, 4);
        if (a->enabled)
            for (int n = 0; n < 4; n++)
                a->sum[n] += channel_output(g, n) * 4;
    }
    if (!a->enabled) return;

    a->sum_cycles += 4;
    a->sample_acc += 4;
    if (a->sample_acc < cycles_per_sample) return;
    a->sample_acc -= cycles_per_sample;

    /* DAC: 0-15 -> -1..1, paneo (NR51) y volumen maestro (NR50) */
    u8 pan = REG(0xFF25), vol = REG(0xFF24);
    float left = 0, right = 0;
    for (int n = 0; n < 4; n++) {
        float level = (float)(a->sum[n] / a->sum_cycles);
        a->sum[n] = 0;
        if (!a->ch[n].dac) continue;
        float analog = 1.0f - level / 7.5f;
        if (pan & (0x10 << n)) left += analog;
        if (pan & (0x01 << n)) right += analog;
    }
    a->sum_cycles = 0;
    left *= (((vol >> 4) & 7) + 1) / 8.0f;
    right *= ((vol & 7) + 1) / 8.0f;

    /* Pasa-altos equivalente al capacitor de salida del DMG */
    const float charge = 0.996f;
    float l = left - a->hp_l, r = right - a->hp_r;
    a->hp_l = left - l * charge;
    a->hp_r = right - r * charge;
    push_sample(a, l * 0.15f, r * 0.15f);
}

u8 apu_read(GB *g, u16 addr)
{
    Apu *a = &g->apu;
    if (addr >= 0xFF30) return REG(addr);
    if (addr == 0xFF26) {
        u8 v = (u8)((a->power ? 0x80 : 0) | 0x70);
        for (int i = 0; i < 4; i++)
            if (a->ch[i].on) v |= (u8)(1 << i);
        return v;
    }
    return (u8)(REG(addr) | read_mask[addr - 0xFF10]);
}

void apu_write(GB *g, u16 addr, u8 v)
{
    Apu *a = &g->apu;
    ApuChannel *c;

    if (addr == 0xFF26) {
        int on = (v & 0x80) != 0;
        if (!on && a->power) {
            /* Apagar borra todos los registros menos la wave RAM; en DMG los
             * contadores de duración sobreviven */
            int len[4];
            for (int i = 0; i < 4; i++) len[i] = a->ch[i].length;
            memset(a->regs, 0, 0x20);
            for (int i = 0; i < 4; i++) {
                memset(&a->ch[i], 0, sizeof a->ch[i]);
                a->ch[i].length = len[i];
            }
        }
        if (on && !a->power)
            a->seq_step = 0;
        a->power = on;
        REG(addr) = v & 0x80;
        return;
    }
    if (addr >= 0xFF30) {
        REG(addr) = v;
        return;
    }
    if (!a->power) {
        /* En DMG los contadores de duración se pueden escribir apagado */
        if (addr == 0xFF11) a->ch[0].length = 64 - (v & 0x3F);
        else if (addr == 0xFF16) a->ch[1].length = 64 - (v & 0x3F);
        else if (addr == 0xFF1B) a->ch[2].length = 256 - v;
        else if (addr == 0xFF20) a->ch[3].length = 64 - (v & 0x3F);
        return;
    }

    REG(addr) = v;
    switch (addr) {
    case 0xFF10:
        c = &a->ch[0];
        c->sweep_period = (v >> 4) & 7;
        c->sweep_negate = (v >> 3) & 1;
        c->sweep_shift = v & 7;
        if (!c->sweep_negate && c->sweep_negated) c->on = 0;
        break;
    case 0xFF11: case 0xFF16:
        c = &a->ch[addr == 0xFF11 ? 0 : 1];
        c->duty = v >> 6;
        c->length = 64 - (v & 0x3F);
        break;
    case 0xFF12: case 0xFF17: case 0xFF21:
        c = &a->ch[addr == 0xFF12 ? 0 : addr == 0xFF17 ? 1 : 3];
        c->dac = (v & 0xF8) != 0;
        if (!c->dac) c->on = 0;
        break;
    case 0xFF13: case 0xFF18: case 0xFF1D:
        c = &a->ch[(addr - 0xFF13) / 5];
        c->freq = (c->freq & 0x700) | v;
        break;
    case 0xFF14: case 0xFF19: case 0xFF1E: case 0xFF23: {
        int n = (addr - 0xFF14) / 5;
        c = &a->ch[n];
        int was_enabled = c->length_enable;
        if (n != 3) c->freq = (c->freq & 0xFF) | ((v & 7) << 8);
        c->length_enable = (v >> 6) & 1;
        /* Rareza: habilitar la duración en un paso impar del secuenciador
         * cuenta un ciclo de más */
        if (!was_enabled && c->length_enable && (a->seq_step & 1) && c->length > 0) {
            if (--c->length == 0 && !(v & 0x80)) c->on = 0;
        }
        if (v & 0x80) {
            int max = n == 2 ? 256 : 64;
            trigger(g, n);
            if (c->length == max && c->length_enable && (a->seq_step & 1))
                c->length--;
        }
        break;
    }
    case 0xFF1A:
        c = &a->ch[2];
        c->dac = (v & 0x80) != 0;
        if (!c->dac) c->on = 0;
        break;
    case 0xFF1B:
        a->ch[2].length = 256 - v;
        break;
    case 0xFF1C:
        a->ch[2].volume_code = (v >> 5) & 3;
        break;
    case 0xFF20:
        a->ch[3].length = 64 - (v & 0x3F);
        break;
    case 0xFF22:
        c = &a->ch[3];
        c->clock_shift = v >> 4;
        c->width7 = (v >> 3) & 1;
        c->divisor = v & 7;
        break;
    default:
        break;
    }
}
