/*
 * La consola: el encendido, el cuadro, la RAM del cartucho y los estados guardados.
 *
 * No hay BIOS (llega como opción en la fase 8): el encendido deja lo que deja la BIOS 1.3 de EE. UU. al
 * saltar al cartucho (su desensamblado, bios13.asm de SMS Power!): interrupciones apagadas, IM 1, SP en $DFF0,
 * la RAM en cero salvo $C000, donde guarda lo último escrito en el puerto $3E ($AB: el cartucho y la E/S
 * habilitados), y los registros del VDP como los deja. La del Game Gear (majbios.asm) escribe $A8. El mapper
 * Sega queda como al encenderse (00 00 01 02: la BIOS 1.3 no lo toca).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "sha1.h"

void sms_reset(Sms *g)
{
    z80_reset(&g->z80);
    g->z80.sp = 0xDFF0;
    g->z80.im = 1;
    g->cyc = g->io_cyc = 0;
    memset(g->ram, 0, sizeof g->ram);
    io_reset(g);
    g->ram[0] = g->io.mem_ctrl;
    mapper_reset(g);
    vdp_reset(g);
    g->nmi_count = g->irq_count = 0;
}

void sms_frame_begin(Sms *g)
{
    /* La Pausa es la NMI, por flanco: al apretarla. En el Game Gear solo en su modo SMS, con Start (en el modo
     * GG, Start se lee en el puerto $00) */
    u8 pause = g->model == MODEL_SMS ? (g->pad[0] & SMS_PAUSE) != 0 : g->sms_mode && (g->pad[0] & (SMS_START | SMS_PAUSE));
    if (pause && !g->io.pause_prev) g->z80.nmi = 1;
    g->io.pause_prev = pause;
    g->vdp.frame_done = 0;
}

void sms_step(Sms *g)
{
    Z80 *z = &g->z80;
    if (z->nmi) g->nmi_count++;
    else if (z->irq && z->iff1 && !z->ei) g->irq_count++;
    g->cyc += (u64)z80_step(g, z);
    sms_after_step(g);
}

void sms_run_frame(Sms *g)
{
    sms_frame_begin(g);
    while (!g->vdp.frame_done) sms_step(g);
}

/* --- RAM del cartucho --- */

int sms_sram_load(Sms *g, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(g->cart.ram, 1, CART_RAM_SIZE, f);
    fclose(f);
    if (n == 0) return 0;
    if (n < CART_RAM_SIZE) memcpy(g->cart.ram + n, g->cart.ram, CART_RAM_SIZE - n > n ? n : CART_RAM_SIZE - n);
    memcpy(g->cart.ram_saved, g->cart.ram, CART_RAM_SIZE);
    g->cart.ram_used = 1;
    return 1;
}

int sms_sram_dirty(const Sms *g)
{
    return g->cart.ram_used && memcmp(g->cart.ram, g->cart.ram_saved, CART_RAM_SIZE) != 0;
}

int sms_sram_save(Sms *g, const char *path)
{
    if (!g->cart.ram_used) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(g->cart.ram, 1, CART_RAM_SIZE, f) == CART_RAM_SIZE;
    if (fclose(f) != 0) ok = 0;
    if (ok) memcpy(g->cart.ram_saved, g->cart.ram, CART_RAM_SIZE);
    return ok;
}

/* --- Estados guardados --- */

#define STATE_MAGIC   "SMSRECOMP-ESTADO"
#define STATE_VERSION 1

typedef struct {
    char magic[16];
    u32  version;
    u32  sms_size;                      /* sizeof(Sms): otra versión del runtime no lo lee */
    char sha1[41];
    u8   pad[3];
} StateHeader;

u8 *sms_state_save(const Sms *g, size_t *size)
{
    *size = sizeof(StateHeader) + sizeof(Sms) + CART_RAM_SIZE;
    u8 *buf = malloc(*size);
    if (!buf) return NULL;
    StateHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, STATE_MAGIC, sizeof h.magic);
    h.version = STATE_VERSION;
    h.sms_size = (u32)sizeof(Sms);
    memcpy(h.sha1, g->cart.sha1, sizeof h.sha1);
    memcpy(buf, &h, sizeof h);
    memcpy(buf + sizeof h, g, sizeof(Sms));
    memcpy(buf + sizeof h + sizeof(Sms), g->cart.ram, CART_RAM_SIZE);
    return buf;
}

int sms_state_load(Sms *g, const u8 *data, size_t size)
{
    StateHeader h;
    if (size != sizeof h + sizeof(Sms) + CART_RAM_SIZE) return 0;
    memcpy(&h, data, sizeof h);
    if (memcmp(h.magic, STATE_MAGIC, sizeof h.magic) || h.version != STATE_VERSION || h.sms_size != sizeof(Sms) ||
        strcmp(h.sha1, g->cart.sha1)) return 0;
    /* Se conserva lo que es de esta corrida: los punteros a la ROM y a la RAM del cartucho, y quién escucha
     * la consola de depuración */
    Cart keep = g->cart;
    void (*sdsc)(Sms *, u8) = g->sdsc_out;
    memcpy(g, data + sizeof h, sizeof(Sms));
    u8 used = g->cart.ram_used;
    g->cart = keep;
    g->cart.ram_used = used;
    g->sdsc_out = sdsc;
    memcpy(g->cart.ram, data + sizeof h + sizeof(Sms), CART_RAM_SIZE);
    return 1;
}
