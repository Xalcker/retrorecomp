/*
 * Prueba del bus, los mappers, los puertos y el VDP sin el dibujo (make test), con ROMs hechas aquí:
 *   - memoria: la RAM y su espejo, sin mapper (32 y 48 KB)
 *   - mapper Sega: las ranuras al encender, el primer KB fijo, los registros reflejados en la RAM, las páginas
 *     que dan la vuelta, la RAM del cartucho en la ranura 2 (con sus dos bancos) y en $C000
 *   - mapper Codemasters: las ranuras al encender (0, 1, 0), sin el KB fijo, la RAM en $A000
 *   - puertos: $3E y $3F, los mandos ($DC/$DD y sus espejos), la detección de consola de exportación por TH,
 *     START como botón 1 en el SMS, la E/S apagada; en el Game Gear, $00 (Start y región), $06 y los espejos
 *     que no tiene; en su modo SMS, sin $00-$06
 *   - VDP: el comando de dos bytes, el búfer de lectura, los registros, la CRAM del SMS y la del GG con su
 *     latch, el estado que se borra al leerlo
 *   - tiempos: el contador V en cada línea (192 y 224 líneas), la bandera de cuadro en la línea $C1, la
 *     interrupción de línea cada R10 + 1 líneas, la línea IRQ del Z80 (2 y 3 ciclos después del cambio de línea),
 *     el contador H congelado cuando TH pasa de 0 a 1, con los valores de la tabla del VDPTEST de FluBBa
 *   - programas de Z80: una interrupción IM 1 por cuadro (EI y HALT, el manejador lee el estado) y la NMI de
 *     la Pausa, una por cada vez que se aprieta
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sms.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

/* Una ROM de pages páginas: cada byte vale su número de página (lo que se ve en una ranura dice qué
 * página hay); el código va al principio de la página 0 */
static u8 *make_rom(u32 pages, const u8 *code, size_t len)
{
    u8 *r = malloc(pages * PAGE_SIZE);
    for (u32 p = 0; p < pages; p++) memset(r + p * PAGE_SIZE, (int)p, PAGE_SIZE);
    if (code) memcpy(r, code, len);
    return r;
}

static void load(Sms *g, const u8 *rom, size_t size, const char *name, int model)
{
    const char *err = NULL;
    if (sms_load(g, rom, size, name, model, &err)) { printf("FALLA: no carga (%s)\n", err); fallas++; exit(1); }
}

/* Un puerto, como lo haría una instrucción en el ciclo cyc */
static u8 in_at(Sms *g, u8 port, u64 cyc) { g->cyc = g->io_cyc = cyc; return z80_in(g, port); }
static void out_at(Sms *g, u8 port, u8 v, u64 cyc) { g->cyc = g->io_cyc = cyc; z80_out(g, port, v); }

#define LINE 228u

static void memory(void)
{
    static Sms g;
    u8 *r = make_rom(2, NULL, 0);
    load(&g, r, 2 * PAGE_SIZE, "a.sms", MODEL_AUTO);
    CHECK(g.cart.mapper == MAPPER_NONE, "32 KB con mapper");
    z80_write(&g, 0xC123, 0x5A);
    CHECK(z80_read(&g, 0xE123) == 0x5A && z80_read(&g, 0xC123) == 0x5A, "RAM y su espejo en $E000");
    z80_write(&g, 0xFFFF, 0x07);       /* sin mapper: solo la RAM */
    CHECK(z80_read(&g, 0xDFFF) == 0x07 && z80_read(&g, 0x8000) == 0x00, "sin mapper, $FFFF es solo RAM (ranura 2: página %u)", z80_read(&g, 0x8000));
    CHECK(z80_read(&g, 0x4000) == 1, "sin mapper, la ranura 1");
    z80_write(&g, 0x1000, 0x99);
    CHECK(z80_read(&g, 0x1000) == 0, "la ROM se escribió");
    sms_free(&g); free(r);
    r = make_rom(3, NULL, 0);
    load(&g, r, 3 * PAGE_SIZE, "b.sms", MODEL_AUTO);
    CHECK(g.cart.mapper == MAPPER_NONE && z80_read(&g, 0x8000) == 2, "48 KB: la página 2 en la ranura 2");
    sms_free(&g); free(r);
    /* Al encender: SP en $DFF0, IM 1, $C000 con lo que la BIOS escribió en $3E */
    r = make_rom(2, NULL, 0);
    load(&g, r, 2 * PAGE_SIZE, "a.sms", MODEL_AUTO);
    CHECK(g.z80.sp == 0xDFF0 && g.z80.im == 1 && !g.z80.iff1 && g.ram[0] == 0xAB && g.vdp.reg[1] == 0xA0,
          "estado tras la BIOS: SP $%04X IM %d $C000=$%02X R1=$%02X", g.z80.sp, g.z80.im, g.ram[0], g.vdp.reg[1]);
    sms_free(&g); free(r);
}

static void sega(void)
{
    static Sms g;
    u8 *r = make_rom(8, NULL, 0);
    r[0x0100] = 0xEE;                  /* para ver el primer KB fijo */
    load(&g, r, 8 * PAGE_SIZE, "s.sms", MODEL_AUTO);
    CHECK(g.cart.mapper == MAPPER_SEGA, "128 KB sin mapper Sega");
    CHECK(z80_read(&g, 0x0400) == 0 && z80_read(&g, 0x4000) == 1 && z80_read(&g, 0x8000) == 2, "ranuras al encender");
    z80_write(&g, 0xFFFE, 5);
    z80_write(&g, 0xFFFD, 3);
    CHECK(z80_read(&g, 0x4000) == 5 && z80_read(&g, 0x0400) == 3, "$FFFE y $FFFD");
    CHECK(z80_read(&g, 0x0100) == 0xEE && z80_read(&g, 0x03FF) == 0, "el primer KB no es siempre la página 0");
    CHECK(z80_read(&g, 0xDFFE) == 5 && z80_read(&g, 0xFFFD) == 3, "los registros no se reflejan en la RAM");
    z80_write(&g, 0xFFFF, 9);
    CHECK(z80_read(&g, 0x8000) == 1, "la página 9 de 8 no dio la vuelta (%u)", z80_read(&g, 0x8000));
    u32 v0 = g.map.map_ver;
    z80_write(&g, 0xFFFC, 0x08);       /* la RAM del cartucho en la ranura 2, banco 0 */
    z80_write(&g, 0x8000, 0x11);
    z80_write(&g, 0xBFFF, 0x22);
    z80_write(&g, 0xFFFC, 0x0C);       /* banco 1 */
    CHECK(z80_read(&g, 0x8000) == 0x00, "el banco 1 de la RAM no está vacío");
    z80_write(&g, 0x8000, 0x33);
    z80_write(&g, 0xFFFC, 0x08);
    CHECK(z80_read(&g, 0x8000) == 0x11 && z80_read(&g, 0xBFFF) == 0x22, "RAM del cartucho, banco 0");
    CHECK(g.cart.ram[0x4000] == 0x33 && g.cart.ram_used && g.map.map_ver > v0, "banco 1, ram_used o map_ver");
    z80_write(&g, 0xFFFC, 0x00);
    CHECK(z80_read(&g, 0x8000) == 1, "la ROM no volvió a la ranura 2");
    z80_write(&g, 0xFFFC, 0x10);       /* la RAM del cartucho en $C000-$FFFF */
    z80_write(&g, 0xC010, 0x44);
    CHECK(g.cart.ram[0x0010] == 0x44 && g.ram[0x0010] != 0x44, "RAM del cartucho en $C000");
    z80_write(&g, 0xFFFC, 0x00);
    CHECK(z80_read(&g, 0xC010) != 0x44, "la RAM de trabajo no volvió a $C000");
    sms_free(&g); free(r);
}

static void codemasters(void)
{
    static Sms g;
    u8 *r = make_rom(8, NULL, 0);
    u8 *h = r + 0x7FE0;
    memset(h, 0, 16);
    h[0] = 8;
    u16 sum = 0;
    for (u32 a = 0; a < 8 * PAGE_SIZE; a += 2) if (a < 0x7FF0 || a >= 0x8000) sum = (u16)(sum + (r[a] | r[a + 1] << 8));
    h[6] = (u8)sum; h[7] = (u8)(sum >> 8); h[8] = (u8)(0x10000 - sum); h[9] = (u8)((0x10000 - sum) >> 8);
    load(&g, r, 8 * PAGE_SIZE, "c.sms", MODEL_AUTO);
    CHECK(g.cart.mapper == MAPPER_CODEMASTERS, "sin mapper Codemasters");
    CHECK(z80_read(&g, 0x0000) == 0 && z80_read(&g, 0x4000) == 1 && z80_read(&g, 0x8000) == 0, "ranuras al encender (0, 1, 0)");
    z80_write(&g, 0x8000, 3);
    z80_write(&g, 0x0000, 2);
    CHECK(z80_read(&g, 0x8000) == 3 && z80_read(&g, 0x0000) == 2, "ranuras 2 y 0 (sin KB fijo)");
    z80_write(&g, 0x4000, 0x85);       /* página 5 y la RAM en $A000 */
    CHECK(z80_read(&g, 0x4000) == 5 && g.map.ram_a000, "$4000 con el bit 7");
    z80_write(&g, 0xA000, 0x77);
    CHECK(z80_read(&g, 0xA000) == 0x77 && z80_read(&g, 0x8000) == 3 && z80_read(&g, 0xBFFF) == 0, "RAM en $A000-$BFFF");
    z80_write(&g, 0x4000, 0x05);
    CHECK(z80_read(&g, 0xA000) == 3, "la RAM no se fue de $A000");
    sms_free(&g); free(r);
}

static void ports(void)
{
    static Sms g;
    u8 *r = make_rom(2, NULL, 0);
    load(&g, r, 2 * PAGE_SIZE, "p.sms", MODEL_AUTO);
    CHECK(z80_in(&g, 0xDC) == 0xFF && z80_in(&g, 0xDD) == 0xFF, "mandos sueltos: $DC=$%02X $DD=$%02X", z80_in(&g, 0xDC), z80_in(&g, 0xDD));
    g.pad[0] = SMS_UP | SMS_A;
    g.pad[1] = SMS_DOWN | SMS_LEFT | SMS_B;
    CHECK(z80_in(&g, 0xDC) == (u8)~(0x01 | 0x10 | 0x80), "$DC con arriba, A y abajo de B: $%02X", z80_in(&g, 0xDC));
    CHECK(z80_in(&g, 0xDD) == (u8)~(0x01 | 0x08), "$DD con izquierda y B de B: $%02X", z80_in(&g, 0xDD));
    CHECK(z80_in(&g, 0xC0) == z80_in(&g, 0xDC) && z80_in(&g, 0xFF) == z80_in(&g, 0xDD), "espejos de $DC/$DD");
    g.pad[0] = SMS_START; g.pad[1] = 0;
    CHECK(z80_in(&g, 0xDC) == (u8)~0x10, "START no aprieta el botón 1 en el SMS");
    g.pad[0] = 0;
    /* Exportación: TH de los dos puertos como salida, se lee lo que se escribió */
    z80_out(&g, 0x3F, 0x55);           /* TH en 0 (A y B), como salidas */
    CHECK((z80_in(&g, 0xDD) & 0xC0) == 0x00, "TH en 0 se lee $%02X", z80_in(&g, 0xDD));
    z80_out(&g, 0x3F, 0xF5);           /* TH en 1 */
    CHECK((z80_in(&g, 0xDD) & 0xC0) == 0xC0, "TH en 1 se lee $%02X", z80_in(&g, 0xDD));
    z80_out(&g, 0x3F, 0xFF);
    CHECK(z80_in(&g, 0x3E) == 0xFF && z80_in(&g, 0x00) == 0xFF, "$00-$3F no leen $FF");
    z80_out(&g, 0x3E, 0xAF);           /* la E/S apagada */
    CHECK(z80_in(&g, 0xDC) == 0xFF && g.io.mem_ctrl == 0xAF, "con la E/S apagada");
    z80_out(&g, 0x3E, 0xAB);
    z80_out(&g, 0x7F, 0x9A);           /* PSG: volumen del tono 0 en 10 (en un espejo) */
    z80_out(&g, 0x41, 0xE5);           /* ruido */
    CHECK(g.psg.vol[0] == 10 && g.psg.period[3] == 5, "las escrituras al PSG por $7F y $41");
    sms_free(&g);
    /* Game Gear */
    load(&g, r, 2 * PAGE_SIZE, "p.gg", MODEL_AUTO);
    CHECK(g.model == MODEL_GG && !g.sms_mode && g.ram[0] == 0xA8, "GG: modelo, modo o $C000");
    CHECK(z80_in(&g, 0x00) == 0xC0 && z80_in(&g, 0x01) == 0x7F && z80_in(&g, 0x06) == 0xFF, "GG: $00-$06 al encender");
    g.pad[0] = SMS_START | SMS_A;
    CHECK(z80_in(&g, 0x00) == 0x40 && z80_in(&g, 0xDC) == (u8)~0x10, "GG: Start en $00 ($%02X) y A en $DC", z80_in(&g, 0x00));
    g.pad[0] = 0;
    z80_out(&g, 0x06, 0x0F);
    CHECK(g.io.gg[6] == 0x0F, "GG: $06 (estéreo)");
    z80_out(&g, 0x08, 0xA0);           /* $07-$3F: control de memoria y de E/S */
    CHECK(g.io.mem_ctrl == 0xA0, "GG: $08 no fue a $3E");
    z80_out(&g, 0x08, 0xA8);
    CHECK(z80_in(&g, 0xC2) == 0xFF && z80_in(&g, 0xDC) == 0xFF, "GG: solo $C0/$C1/$DC/$DD");
    sms_free(&g);
    /* Un juego de SMS en el Game Gear: sin $00-$06 */
    load(&g, r, 2 * PAGE_SIZE, "p.sms", MODEL_GG);
    CHECK(g.model == MODEL_GG && g.sms_mode && z80_in(&g, 0x00) == 0xFF, "GG en modo SMS: $00");
    sms_free(&g); free(r);
}

static void vdp(void)
{
    static Sms g;
    u8 *r = make_rom(2, NULL, 0);
    load(&g, r, 2 * PAGE_SIZE, "v.sms", MODEL_AUTO);
    /* Escribir VRAM en $1234 */
    z80_out(&g, 0xBF, 0x34); z80_out(&g, 0xBF, 0x40 | 0x12);
    z80_out(&g, 0xBE, 0xAA); z80_out(&g, 0xBE, 0xBB);
    CHECK(g.vdp.vram[0x1234] == 0xAA && g.vdp.vram[0x1235] == 0xBB && g.vdp.addr == 0x1236, "escritura en VRAM");
    /* Leer: el código 0 carga el búfer y sube la dirección */
    z80_out(&g, 0xBF, 0x34); z80_out(&g, 0xBF, 0x12);
    CHECK(z80_in(&g, 0xBE) == 0xAA && z80_in(&g, 0xBE) == 0xBB, "lectura de VRAM por el búfer");
    /* Escribir carga el búfer */
    z80_out(&g, 0xBE, 0x5C);
    CHECK(z80_in(&g, 0xBE) == 0x5C, "escribir no carga el búfer");
    /* Registros: el 1 con $E0; el 11 no hace nada */
    z80_out(&g, 0xBF, 0xE0); z80_out(&g, 0xBF, 0x81);
    z80_out(&g, 0xBF, 0x12); z80_out(&g, 0xBF, 0x8B);
    CHECK(g.vdp.reg[1] == 0xE0 && g.vdp.reg[11] == 0, "registros");
    /* Primer byte suelto: leer el estado lo borra */
    z80_out(&g, 0xBF, 0x00);
    z80_in(&g, 0xBF);
    z80_out(&g, 0xBF, 0x00); z80_out(&g, 0xBF, 0xC0);
    z80_out(&g, 0xBE, 0x3F); z80_out(&g, 0xBE, 0xFF);
    CHECK(g.vdp.cram[0] == 0x3F && g.vdp.cram[1] == 0x3F, "CRAM del SMS (o el primer byte no se borró)");
    z80_out(&g, 0xBF, 0x1F); z80_out(&g, 0xBF, 0xC0);
    z80_out(&g, 0xBE, 0x01); z80_out(&g, 0xBE, 0x02);
    CHECK(g.vdp.cram[31] == 0x01 && g.vdp.cram[0] == 0x02, "la CRAM del SMS da la vuelta en $1F");
    sms_free(&g);
    /* Game Gear: el ejemplo de msvdp (latch en el par, se escribe con el impar) */
    load(&g, r, 2 * PAGE_SIZE, "v.gg", MODEL_AUTO);
    z80_out(&g, 0xBF, 0x00); z80_out(&g, 0xBF, 0xC0);
    z80_out(&g, 0xBE, 0xFF);
    CHECK(g.vdp.cram[0] == 0 && g.vdp.cram[1] == 0, "GG: el byte par escribió la CRAM");
    z80_out(&g, 0xBF, 0x21); z80_out(&g, 0xBF, 0xC0);
    z80_out(&g, 0xBE, 0x0F);
    CHECK(g.vdp.cram[0x20] == 0xFF && g.vdp.cram[0x21] == 0x0F && g.vdp.cram[0] == 0, "GG: la palabra en $20 no es $0FFF");
    sms_free(&g); free(r);
}

static void timing(void)
{
    static Sms g;
    u8 *r = make_rom(2, NULL, 0);
    load(&g, r, 2 * PAGE_SIZE, "t.sms", MODEL_AUTO);
    /* Contador V (192 líneas): $00 en la línea 0, $C0 en la 192, $DA en la 218, $D5 en la 219, $FF en la 261 */
    static const struct { u32 line; u8 v; } vc[] = { { 0, 0x00 }, { 192, 0xC0 }, { 218, 0xDA }, { 219, 0xD5 }, { 261, 0xFF } };
    for (unsigned i = 0; i < 5; i++) {
        u8 v = in_at(&g, 0x7E, vc[i].line * LINE + 10);
        CHECK(v == vc[i].v, "contador V en la línea %u: $%02X (esperado $%02X)", vc[i].line, v, vc[i].v);
    }
    in_at(&g, 0xBF, 262 * LINE + 5);   /* cuadro siguiente; borra el estado */
    CHECK(g.vdp.frame == 1, "no terminó el cuadro (%llu)", (unsigned long long)g.vdp.frame);
    /* La bandera de cuadro: no en la línea 192, sí en la 193 ($C1) */
    u64 f0 = 262 * LINE;
    CHECK(!(in_at(&g, 0xBF, f0 + 192 * LINE + 100) & 0x80), "la bandera de cuadro sube en la línea 192");
    u8 st = in_at(&g, 0xBF, f0 + 193 * LINE + 1);
    CHECK(st & 0x80, "la bandera de cuadro no sube en la línea $C1 (estado $%02X)", st);
    CHECK(!(in_at(&g, 0xBF, f0 + 193 * LINE + 2) & 0x80), "leer el estado no borra la bandera");
    /* La línea IRQ: bandera de cuadro con el registro 1, bit 5 */
    out_at(&g, 0xBF, 0xE0, f0 + 200 * LINE); out_at(&g, 0xBF, 0x81, f0 + 200 * LINE);
    in_at(&g, 0xBF, f0 + 262 * LINE + 193 * LINE + 5);      /* la del cuadro 2 ya se leyó: borrada */
    CHECK(!g.z80.irq, "IRQ con la bandera borrada");
    vdp_sync(&g, 3 * 262 * LINE + 193 * LINE + 1);
    CHECK(!g.z80.irq, "la IRQ de cuadro subió antes de 2 ciclos");
    vdp_sync(&g, 3 * 262 * LINE + 193 * LINE + 2);
    CHECK(g.z80.irq, "sin IRQ con la bandera de cuadro y el registro 1, bit 5 (2 ciclos después del cambio de línea)");
    out_at(&g, 0xBF, 0x80, 3 * 262 * LINE + 194 * LINE); out_at(&g, 0xBF, 0x81, 3 * 262 * LINE + 194 * LINE);
    CHECK(!g.z80.irq, "IRQ con el registro 1, bit 5 en 0");
    sms_free(&g);
    /* Interrupción de línea con R10 = 3: en las líneas 3, 7, 11... (cada R10 + 1) */
    load(&g, r, 2 * PAGE_SIZE, "t.sms", MODEL_AUTO);
    out_at(&g, 0xBF, 0x03, 0); out_at(&g, 0xBF, 0x8A, 0);
    out_at(&g, 0xBF, 0x36 | 0x10, 0); out_at(&g, 0xBF, 0x80, 0);
    in_at(&g, 0xBF, 262 * LINE - 10);                       /* el R10 nuevo entra en las líneas de fuera */
    u64 base = 262 * LINE;
    int hits[16], nh = 0;
    for (u32 l = 0; l < 16; l++) {
        vdp_sync(&g, base + l * LINE + 3);  /* la de línea sube 3 ciclos después del cambio de línea */
        if (g.vdp.line_irq) { if (nh < 16) hits[nh++] = (int)l; in_at(&g, 0xBF, base + l * LINE + 4); }
    }
    CHECK(nh == 4 && hits[0] == 3 && hits[1] == 7 && hits[2] == 11 && hits[3] == 15, "interrupción de línea: %d veces, la primera en la línea %d",
          nh, nh ? hits[0] : -1);
    sms_free(&g);
    /* 224 líneas: $EA en la línea 234, $E5 en la 235, la bandera en la línea $E1 */
    load(&g, r, 2 * PAGE_SIZE, "t.sms", MODEL_AUTO);
    out_at(&g, 0xBF, 0x06, 0); out_at(&g, 0xBF, 0x80, 0);
    out_at(&g, 0xBF, 0xB0, 0); out_at(&g, 0xBF, 0x81, 0);
    in_at(&g, 0xBF, 262 * LINE);
    base = 262 * LINE;
    CHECK(g.vdp.height == 224, "no hay 224 líneas (%u)", g.vdp.height);
    CHECK(in_at(&g, 0x7E, base + 234 * LINE) == 0xEA && in_at(&g, 0x7E, base + 235 * LINE) == 0xE5, "contador V con 224 líneas");
    sms_free(&g);
    load(&g, r, 2 * PAGE_SIZE, "t.sms", MODEL_AUTO);
    out_at(&g, 0xBF, 0x06, 0); out_at(&g, 0xBF, 0x80, 0);
    out_at(&g, 0xBF, 0xB0, 0); out_at(&g, 0xBF, 0x81, 0);
    in_at(&g, 0xBF, 262 * LINE);
    base = 262 * LINE;
    CHECK(!(in_at(&g, 0xBF, base + 224 * LINE + 5) & 0x80) && (in_at(&g, 0xBF, base + 225 * LINE + 5) & 0x80),
          "la bandera de cuadro con 224 líneas no sube en $E1");
    /* Contador H, en el cuadro siguiente (el tiempo solo avanza): se congela cuando TH pasa de 0 a 1, no de 1 a 0.
     * Como la tabla del VDPTEST de FluBBa: $F4 cuando cambia el contador V, +3/4 por ciclo, y de $93 salta a $E9 */
    base += 262 * LINE;
    static const struct { u32 c; u8 h; } hc[] = { { 0, 0xF4 }, { 100, 0x3F }, { 200, 0x8A }, { 212, 0x93 }, { 213, 0xE9 }, { 227, 0xF3 } };
    for (unsigned i = 0; i < sizeof hc / sizeof *hc; i++) {
        u64 l = base + (10 + 2 * i) * LINE;
        out_at(&g, 0x3F, 0x55, l);                         /* TH de A y B como salidas en 0 */
        out_at(&g, 0x3F, 0xF5, l + hc[i].c);               /* TH en 1: congela */
        u8 h = in_at(&g, 0x7F, l + LINE + 5);
        CHECK(h == hc[i].h, "contador H a los %u ciclos: $%02X (esperado $%02X)", hc[i].c, h, hc[i].h);
    }
    out_at(&g, 0x3F, 0x55, base + 40 * LINE + 50);         /* de 1 a 0: no congela */
    CHECK(in_at(&g, 0x7F, base + 41 * LINE) == 0xF3, "TH de 1 a 0 congeló el contador H");
    sms_free(&g); free(r);
}

/* Programas de Z80 */
static void programs(void)
{
    static Sms g;
    /* $0000: DI; IM 1; LD SP,$DFF0; R1 = $E0 (pantalla e interrupción de cuadro); EI; bucle: HALT; JR bucle
     * $0038: PUSH AF; IN A,($BF); LD A,($C100); INC A; LD ($C100),A; POP AF; EI; RETI
     * $0066: PUSH AF; LD A,($C101); INC A; LD ($C101),A; POP AF; RETN */
    static const u8 code[] = {
        0xF3, 0xED, 0x56, 0x31, 0xF0, 0xDF,
        0x3E, 0xE0, 0xD3, 0xBF, 0x3E, 0x81, 0xD3, 0xBF,
        0xFB, 0x76, 0x18, 0xFD,
    };
    static const u8 irq[] = { 0xF5, 0xDB, 0xBF, 0x3A, 0x00, 0xC1, 0x3C, 0x32, 0x00, 0xC1, 0xF1, 0xFB, 0xED, 0x4D };
    static const u8 nmi[] = { 0xF5, 0x3A, 0x01, 0xC1, 0x3C, 0x32, 0x01, 0xC1, 0xF1, 0xED, 0x45 };
    u8 *r = make_rom(2, code, sizeof code);
    memcpy(r + 0x38, irq, sizeof irq);
    memcpy(r + 0x66, nmi, sizeof nmi);
    load(&g, r, 2 * PAGE_SIZE, "prog.sms", MODEL_AUTO);
    for (int f = 0; f < 60; f++) sms_run_frame(&g);
    CHECK(g.ram[0x100] == 60 || g.ram[0x100] == 59, "interrupciones de cuadro en 60 cuadros: %u", g.ram[0x100]);
    CHECK(g.irq_count == g.ram[0x100], "irq_count %u y el contador %u", g.irq_count, g.ram[0x100]);
    /* La Pausa: una NMI por cada vez que se aprieta (sostenida 3 cuadros, dos veces) */
    for (int f = 0; f < 20; f++) { g.pad[0] = (f >= 2 && f < 5) || (f >= 10 && f < 13) ? SMS_PAUSE : 0; sms_run_frame(&g); }
    CHECK(g.ram[0x101] == 2 && g.nmi_count == 2, "NMI de la Pausa: %u (esperado 2)", g.ram[0x101]);
    /* En el Game Gear en modo GG, Start no da la NMI */
    sms_free(&g);
    load(&g, r, 2 * PAGE_SIZE, "prog.gg", MODEL_AUTO);
    for (int f = 0; f < 10; f++) { g.pad[0] = f == 3 ? SMS_START : 0; sms_run_frame(&g); }
    CHECK(g.ram[0x101] == 0, "GG: Start dio la NMI en modo GG");
    sms_free(&g);
    load(&g, r, 2 * PAGE_SIZE, "prog.sms", MODEL_GG);
    for (int f = 0; f < 10; f++) { g.pad[0] = f == 3 ? SMS_START : 0; sms_run_frame(&g); }
    CHECK(g.ram[0x101] == 1, "GG en modo SMS: Start no dio la NMI (%u)", g.ram[0x101]);
    sms_free(&g); free(r);
}

int main(void)
{
    console_utf8();
    memory();
    sega();
    codemasters();
    ports();
    vdp();
    timing();
    programs();
    if (fallas) { printf("bustest: %d fallas\n", fallas); return 1; }
    printf("bustest: memoria, mappers Sega y Codemasters, RAM del cartucho, puertos del SMS y del GG, VDP, contadores e interrupciones bien\n");
    return 0;
}
