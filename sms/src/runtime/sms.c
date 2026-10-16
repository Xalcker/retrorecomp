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
#include "trace_line.h"
#include "ram_code.h"
#include "z80_ops.h"     /* z80_decode */
#ifdef SMS_RECOMP
#include "recomp.h"
#endif

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
    psg_reset(&g->psg);
    audio_reset(g);
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

int sms_force_interp;

/* La traza: la instrucción que el intérprete va a ejecutar en pc */
static void trace_note(Sms *g, u16 pc)
{
    long off = bus_rom_offset(g, pc);
    if (off >= 0) {
        if (!g->cart.trace[off]) g->cart.trace[off] = (u8)(1 + (pc >> 14));
    } else if (pc >= 0x8000 && g->cart.ram_code) {
        u8 b[4];
        Z80Insn in;
        for (int i = 0; i < 4; i++) b[i] = bus_peek(g, (u16)(pc + i));
        z80_decode(b, pc, &in);
        rc_observe((RcTable *)g->cart.ram_code, pc, b, in.len, sms_rc_key(b));
    }
}

void sms_step(Sms *g)
{
    Z80 *z = &g->z80;
    if (z->nmi) g->nmi_count++;
    else if (z->irq && z->iff1 && !z->ei) g->irq_count++;
    else if (g->cart.trace && !z->halted) trace_note(g, z->pc);
    int t = z80_step(g, z);
    g->cyc += (u64)t;
    Z80_STAT(g, interp, (u64)t);
    sms_after_step(g);
}

/* Un HALT sin nada que atender: cada paso del intérprete son 4 ciclos T, sube R y borra Q, P y el retraso de EI, y
 * nada más cambia hasta que el VDP tenga algo que hacer (una línea, una interrupción). Se saltan de una vez los pasos
 * hasta el primero que llega a ese ciclo, como si se dieran uno por uno (la línea INT solo cambia en sms_after_step) */
static int halt_skip(Sms *g)
{
    Z80 *z = &g->z80;
    if (!z->halted || z->nmi || (z->irq && z->iff1)) return 0;
    u64 n = g->vdp.next_event > g->cyc ? (g->vdp.next_event - g->cyc + 3) / 4 : 1;
    g->cyc += 4 * n;
    Z80_STAT(g, skipped, 4 * n);
    z->r = (u8)((z->r & 0x80) | ((z->r + n) & 0x7F));
    z->q = 0;
    z->p = 0;
    z->ei = 0;
    sms_after_step(g);
    return 1;
}

#ifdef SMS_RECOMP
u32 sms_rc_map_ver;

int sms_recomp_step(Sms *g)
{
    Z80 *z = &g->z80;
    if (halt_skip(g)) return 0;
    if (!sms_force_interp && (const void *)g != z80_interp_only && !z->nmi && !(z->irq && z->iff1 && !z->ei) &&
        !z->halted) {
        Z80RecompFn fn = recomp_lookup(g, z->pc);
        if (fn) {
            sms_rc_map_ver = g->map.map_ver;
            if (fn(g, 0)) return 1;
        }
    }
    sms_step(g);
    return 0;
}
#else
int sms_recomp_step(Sms *g)
{
    if (!halt_skip(g)) sms_step(g);
    return 0;
}
#endif

void sms_run_frame(Sms *g)
{
    sms_frame_begin(g);
    while (!g->vdp.frame_done) sms_recomp_step(g);
    audio_end_frame(g);
}

/* --- Traza --- */

void sms_trace_start(Sms *g)
{
    if (!g->cart.trace) g->cart.trace = calloc(g->cart.rom_size, 1);
    if (!g->cart.ram_code) {
        RcTable *t = calloc(1, sizeof *t);
        if (t && rc_init(t, 0x8000, 0x8000)) g->cart.ram_code = t;
        else free(t);
    }
}

unsigned sms_rc_key(const u8 *b)
{
    if (b[0] == 0xDD || b[0] == 0xFD) {
        if (b[1] == 0xDD || b[1] == 0xFD || b[1] == 0xED) return 1;     /* el prefijo solo */
        return b[1] == 0xCB ? 4 : 2;
    }
    if (b[0] == 0xCB || b[0] == 0xED) return 2;
    if (b[0] == 0x10 || (b[0] & 0xE7) == 0x20 || b[0] == 0x18) return 2;     /* DJNZ, JR cc y JR: el destino */
    return 1;
}

void sms_ram_summary(const Sms *g, unsigned *fixed, unsigned *vol, unsigned *multi, unsigned *lost)
{
    RcSummary s = { 0, 0, 0, 0 };
    if (g->cart.ram_code) s = rc_summary((const RcTable *)g->cart.ram_code);
    *fixed = s.fixed; *vol = s.vol; *multi = s.multi; *lost = s.lost;
}

/* Libera lo de la traza (sms_free) */
void sms_trace_free(Sms *g)
{
    RcTable *t = g->cart.ram_code;
    if (t) {
        for (uint32_t i = 0; t->slot && i < t->size; i++) free(t->slot[i]);
        free(t->slot);
        free(t);
    }
    g->cart.ram_code = NULL;
    free(g->cart.trace);
    g->cart.trace = NULL;
}

int sms_trace_load(Sms *g, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    sms_trace_start(g);
    char line[256];
    unsigned long off, addr;
    RcIns in;
    while (g->cart.trace && fgets(line, sizeof line, f)) {
        if (trace_line_read(line, SMS_TRACE_SHIFT, &off, &addr)) {
            if (off < g->cart.rom_size && addr < 0xC000 && !g->cart.trace[off]) g->cart.trace[off] = (u8)(1 + (addr >> 14));
        } else if (g->cart.ram_code && rc_parse(line, &in)) {
            in.key = (uint8_t)sms_rc_key(in.bytes);
            rc_add((RcTable *)g->cart.ram_code, &in);
        }
    }
    fclose(f);
    return g->cart.trace != NULL;
}

int sms_trace_write(const Sms *g, const char *path)
{
    if (!g->cart.trace) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (size_t i = 0; i < g->cart.rom_size; i++)
        if (g->cart.trace[i])
            trace_line_write(f, SMS_TRACE_SHIFT, 4, i, (unsigned long)(g->cart.trace[i] - 1) << 14 | (i & 0x3FFF));
    if (g->cart.ram_code) rc_write((const RcTable *)g->cart.ram_code, f, 4);
    return fclose(f) == 0;
}

size_t sms_trace_count(const Sms *g)
{
    size_t n = 0;
    if (g->cart.trace) for (size_t i = 0; i < g->cart.rom_size; i++) n += g->cart.trace[i] != 0;
    return n;
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
