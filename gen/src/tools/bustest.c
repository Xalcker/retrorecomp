/*
 * Prueba sintética del bus (make test): con ROMs armadas en memoria comprueba la RAM y sus
 * espejos, los espejos de la ROM, el registro de versión, el bus del Z80 (pedido, reset, acceso de
 * 8 bits), los mandos de 3 y 6 botones, la SRAM de bytes impares (y guardarla), la EEPROM serie
 * (I2C, con el mapeo de NBA Jam), el mapper de SSF2 y las interrupciones VINT/HINT del esqueleto del
 * VDP con un programa de 68000.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void wr32(u8 *p, u32 v) { wr16(p, (u16)(v >> 16)); wr16(p + 2, (u16)v); }

/* ROM de `size` bytes con cabecera; reset en $200 con un bucle, salvo que se ponga otro programa */
static u8 *make_rom(size_t size, int sram)
{
    u8 *r = calloc(1, size);
    wr32(r + 0, 0x00FFFE00);
    wr32(r + 4, 0x00000200);
    for (int n = 2; n < 64; n++) wr32(r + n * 4, 0x00000200);
    memcpy(r + 0x100, "SEGA MEGA DRIVE (C)TEST 2026.OCT", 32);
    memset(r + 0x120, ' ', 0x60);
    memcpy(r + 0x180, "GM 00000000-00", 14);
    memcpy(r + 0x190, "J               ", 16);
    wr32(r + 0x1A0, 0); wr32(r + 0x1A4, (u32)size - 1);
    if (sram) {
        r[0x1B0] = 'R'; r[0x1B1] = 'A'; r[0x1B2] = 0xF8; r[0x1B3] = 0x20;
        wr32(r + 0x1B4, 0x200001); wr32(r + 0x1B8, 0x203FFF);
    }
    memcpy(r + 0x1F0, "JUE", 3);
    wr16(r + 0x200, 0x60FE);                    /* bra.s * */
    return r;
}

static Gen *load(const u8 *rom, size_t size)
{
    Gen *g = malloc(sizeof *g);
    const char *err = NULL;
    if (gen_load(g, rom, size, &err)) { printf("FALLA: no carga: %s\n", err); exit(1); }
    return g;
}

static void test_memoria(void)
{
    u8 *rom = make_rom(0x40000, 0);
    Gen *g = load(rom, 0x40000);
    m68k_wr8(g, 0xFF1234, 0x5A);
    CHECK(m68k_rd8(g, 0xE01234) == 0x5A, "espejo de la RAM en $E00000");
    m68k_wr16(g, 0xFFFFFE, 0xBEEF);
    CHECK(m68k_rd16(g, 0xFFFFFE) == 0xBEEF && g->ram[0xFFFE] == 0xBE, "RAM big-endian");
    CHECK(m68k_rd16(g, 0x000100) == 0x5345, "ROM en $100 ('SE')");
    CHECK(m68k_rd16(g, 0x040100) == 0x5345, "espejo de la ROM de 256 KB en $40000");
    CHECK(m68k_rd8(g, 0xA10001) == 0xA0, "versión: extranjero, sin Mega CD ($%02X)", m68k_rd8(g, 0xA10001));
    CHECK(g->cpu.pc == 0x200 && g->cpu.a[7] == 0xFFFE00, "reset: PC $%06X SSP $%06X", g->cpu.pc, g->cpu.a[7]);
    gen_free(g); free(g); free(rom);
}

static void test_z80(void)
{
    u8 *rom = make_rom(0x40000, 0);
    Gen *g = load(rom, 0x40000);
    CHECK(m68k_rd16(g, 0xA11100) & 0x100, "al encender el Z80 está en reset: BUSACK = 1");
    m68k_wr16(g, 0xA11100, 0x100);
    m68k_wr16(g, 0xA11200, 0x100);
    CHECK(!(m68k_rd8(g, 0xA11100) & 1), "bus pedido y reset liberado: BUSACK = 0");
    m68k_wr16(g, 0xA00010, 0xABCD);
    CHECK(g->zram[0x10] == 0xAB, "escritura de palabra en la RAM del Z80: solo el byte alto");
    CHECK(m68k_rd16(g, 0xA00010) == 0xABAB, "lectura de palabra: el byte en las dos mitades");
    m68k_wr8(g, 0xA02011, 0x42);
    CHECK(g->zram[0x11] == 0x42, "espejo de la RAM del Z80 en $A02000");
    for (int i = 0; i < 9; i++) m68k_wr8(g, 0xA06000, (u8)(0x1A5 >> i & 1));
    CHECK(g->z80_bank == 0x1A5, "registro de banco del Z80 bit a bit ($%03X)", g->z80_bank);
    m68k_wr16(g, 0xA11100, 0x000);
    m68k_wr8(g, 0xA00012, 0x99);
    CHECK(g->zram[0x12] == 0 && m68k_rd8(g, 0xA00010) == 0xFF, "con el Z80 corriendo el 68000 no llega a su bus");
    gen_free(g); free(g); free(rom);
}

static void test_mandos(void)
{
    u8 *rom = make_rom(0x40000, 0);
    Gen *g = load(rom, 0x40000);
    g->pad6 = 0;
    g->pad[0] = GEN_UP | GEN_B | GEN_START;
    m68k_wr8(g, 0xA10009, 0x40);                /* TH de salida */
    m68k_wr8(g, 0xA10003, 0x40);
    CHECK(m68k_rd8(g, 0xA10003) == 0x6E, "3 botones, TH=1: C B R L D U ($%02X)", m68k_rd8(g, 0xA10003));
    m68k_wr8(g, 0xA10003, 0x00);
    CHECK(m68k_rd8(g, 0xA10003) == 0x12, "3 botones, TH=0: Start A 0 0 D U ($%02X)", m68k_rd8(g, 0xA10003));
    CHECK(m68k_rd8(g, 0xA10005) == 0x7F, "mando 2 sin botones, TH de entrada: $7F");

    g->pad6 = 1;
    g->pad[0] = GEN_X | GEN_MODE | GEN_C;
    g->cyc += 20000;
    u8 r[8];
    for (int i = 0; i < 8; i++) {
        m68k_wr8(g, 0xA10003, (i & 1) ? 0x00 : 0x40);
        r[i] = m68k_rd8(g, 0xA10003);
    }
    CHECK(r[5] == 0x30, "6 botones, tercer TH=0: bits 3-0 en 0 ($%02X)", r[5]);
    CHECK(r[6] == 0x53, "6 botones, cuarto TH=1: C B Mode X Y Z ($%02X)", r[6]);
    CHECK(r[7] == 0x3F, "6 botones, cuarto TH=0: 1111 ($%02X)", r[7]);
    CHECK(r[0] == 0x5F && r[2] == 0x5F, "6 botones, primeras lecturas como el de 3 ($%02X $%02X)", r[0], r[2]);
    g->cyc += 20000;
    m68k_wr8(g, 0xA10003, 0x40);
    CHECK(m68k_rd8(g, 0xA10003) == 0x5F, "6 botones: tras ~1.5 ms vuelve a empezar");
    gen_free(g); free(g); free(rom);
}

static void test_sram(void)
{
    u8 *rom = make_rom(0x40000, 1);
    Gen *g = load(rom, 0x40000);
    CHECK(g->cart.sram && g->cart.sram_size == 0x4000, "SRAM de la cabecera: $200001-$203FFF");
    CHECK(m68k_rd8(g, 0x200001) == 0xFF, "SRAM sin partida: $FF");
    m68k_wr8(g, 0x200001, 0x77);
    m68k_wr8(g, 0x200000, 0x11);
    CHECK(m68k_rd8(g, 0x200001) == 0x77, "SRAM: escritura y lectura en un byte impar");
    CHECK(g->cart.sram_data[0] == 0xFF, "SRAM de bytes impares: el par no se escribe");
    CHECK(gen_sram_dirty(g), "SRAM modificada");
    const char *path = "build/tests/bustest.sav";
    CHECK(gen_sram_save(g, path) && !gen_sram_dirty(g), "guardar la SRAM");
    m68k_wr8(g, 0x200001, 0x00);
    CHECK(gen_sram_load(g, path) && m68k_rd8(g, 0x200001) == 0x77, "cargar la SRAM");
    gen_free(g); free(g); free(rom);
}

/* I2C a mano, como NBA Jam: SDA en el bit 0 y SCL en el bit 1 de $200001; SDA se lee en el bit 1 */
static Gen *eg;
static void i2c(int scl, int sda) { m68k_wr8(eg, 0x200001, (u8)(scl << 1 | sda)); }
static int  i2c_sda(void) { return m68k_rd8(eg, 0x200001) >> 1 & 1; }
static void i2c_start(void) { i2c(1, 1); i2c(1, 0); i2c(0, 0); }
static void i2c_stop(void)  { i2c(0, 0); i2c(1, 0); i2c(1, 1); }
static int i2c_send(u8 b)       /* devuelve el ACK (0 = la EEPROM respondió) */
{
    for (int i = 7; i >= 0; i--) { i2c(0, b >> i & 1); i2c(1, b >> i & 1); i2c(0, b >> i & 1); }
    i2c(0, 1); i2c(1, 1);
    int ack = i2c_sda();
    i2c(0, 1);
    return ack;
}
static u8 i2c_recv(int last)
{
    u8 b = 0;
    for (int i = 0; i < 8; i++) { i2c(0, 1); i2c(1, 1); b = (u8)(b << 1 | i2c_sda()); i2c(0, 1); }
    i2c(0, last); i2c(1, last); i2c(0, last);
    return b;
}

static void test_eeprom(void)
{
    u8 *rom = make_rom(0x200000, 0);
    memcpy(rom + 0x180, "GM T-081326 00", 14);         /* NBA Jam (USA, Europa) */
    rom[0x1B0] = 'R'; rom[0x1B1] = 'A'; rom[0x1B2] = 0xE8; rom[0x1B3] = 0x40;
    eg = load(rom, 0x200000);
    CHECK(eg->cart.has_eeprom && !eg->cart.sram && eg->cart.sram_size == 256, "NBA Jam: EEPROM de 256 bytes en vez de SRAM");
    i2c_start();
    int a1 = i2c_send(0xA0), a2 = i2c_send(0x10), a3 = i2c_send(0x11), a4 = i2c_send(0x22), a5 = i2c_send(0x33);
    i2c_stop();
    CHECK(!a1 && !a2 && !a3 && !a4 && !a5, "escritura: la EEPROM responde ACK a cada byte");
    CHECK(eg->cart.sram_data[0x10] == 0x11 && eg->cart.sram_data[0x12] == 0x33, "escritura en $10-$12");
    i2c_start(); i2c_send(0xA0); i2c_send(0x10);
    i2c_start(); i2c_send(0xA1);                           /* START repetido: lectura aleatoria */
    u8 r0 = i2c_recv(0), r1 = i2c_recv(0), r2 = i2c_recv(1);
    i2c_stop();
    CHECK(r0 == 0x11 && r1 == 0x22 && r2 == 0x33, "lectura aleatoria: %02X %02X %02X", r0, r1, r2);
    i2c_start(); i2c_send(0xA0); i2c_send(0x22);
    for (int i = 0; i < 4; i++) i2c_send((u8)(0x80 + i));  /* 4 bytes desde $22: la página es de 4 */
    i2c_stop();
    CHECK(eg->cart.sram_data[0x22] == 0x80 && eg->cart.sram_data[0x23] == 0x81 && eg->cart.sram_data[0x20] == 0x82,
          "escritura que cruza la página: vuelve al principio de la página");
    i2c_start();
    CHECK(i2c_send(0xB0) == 1, "otro dispositivo (1011): sin ACK");
    i2c_stop();
    gen_free(eg); free(eg); free(rom);
}

static void test_ssf2(void)
{
    size_t size = 0x500000;     /* 5 MB: más de 4, necesita el mapper */
    u8 *rom = make_rom(size, 0);
    for (u32 b = 1; b < size / 0x80000; b++) wr16(rom + b * 0x80000, (u16)b);
    Gen *g = load(rom, size);
    CHECK(g->cart.mapper == MAPPER_SSF2, "ROM de 5 MB: mapper de SSF2");
    CHECK(m68k_rd16(g, 0x080000) == 1 && m68k_rd16(g, 0x380000) == 7, "bancos en su lugar al encender");
    u32 ver = g->cart.map_ver;
    m68k_wr8(g, 0xA130F3, 9);
    CHECK(m68k_rd16(g, 0x080000) == 9, "banco 9 en la ventana 1 ($%04X)", m68k_rd16(g, 0x080000));
    CHECK(g->cart.map_ver != ver, "map_ver cambia con el banco");
    m68k_wr16(g, 0xA130FE, 2);
    CHECK(m68k_rd16(g, 0x380000) == 2, "escritura de palabra en $A130FE: banco 2 en la ventana 7");
    gen_free(g); free(g); free(rom);
}

/* Programa: VINT y HINT suman 1 a contadores en la RAM. sr: el SR del bucle principal. */
static u8 *irq_rom(u16 sr)
{
    u8 *r = make_rom(0x40000, 0);
    static const u16 prog[] = {
        0x46FC, 0x2700,                             /* move.w #$2700,sr */
        0x33FC, 0x8014, 0x00C0, 0x0004,             /* reg 0 = $14: HINT habilitada */
        0x33FC, 0x8A00, 0x00C0, 0x0004,             /* reg 10 = 0: HINT en cada línea */
        0x33FC, 0x8164, 0x00C0, 0x0004,             /* reg 1 = $64: pantalla y VINT */
        0x46FC, 0x0000,                             /* move.w #sr,sr (se completa abajo) */
        0x60FE,                                     /* bra.s * */
    };
    for (size_t i = 0; i < sizeof prog / 2; i++) wr16(r + 0x200 + i * 2, prog[i]);
    wr16(r + 0x200 + 15 * 2, sr);
    static const u16 vint[] = { 0x52B9, 0x00FF, 0x0000, 0x4E73 };   /* addq.l #1,($FF0000).l; rte */
    static const u16 hint[] = { 0x52B9, 0x00FF, 0x0004, 0x4E73 };   /* addq.l #1,($FF0004).l; rte */
    for (int i = 0; i < 4; i++) { wr16(r + 0x300 + i * 2, vint[i]); wr16(r + 0x320 + i * 2, hint[i]); }
    wr32(r + (VEC_AUTO1 + 5) * 4, 0x300);
    wr32(r + (VEC_AUTO1 + 3) * 4, 0x320);
    return r;
}

static void test_interrupciones(void)
{
    u8 *rom = irq_rom(0x2000);
    Gen *g = load(rom, 0x40000);
    for (int f = 0; f < 10; f++) gen_run_frame(g);
    u32 vint = rd32(g->ram), hint = rd32(g->ram + 4);
    CHECK(vint == 10, "10 cuadros: 10 VINT (%u)", vint);
    CHECK(hint >= 2240 && hint <= 2250, "10 cuadros con el registro 10 = 0: ~225 HINT por cuadro (%u)", hint);
    CHECK(g->exc_count[VEC_AUTO1 + 5] == vint && g->exc_count[VEC_AUTO1 + 3] == hint, "estadística de excepciones");
    CHECK(g->vdp.frame == 10 && g->cyc >= 10ull * 262 * MCLK_PER_LINE / 7, "10 cuadros de 262 líneas");
    gen_free(g); free(g); free(rom);

    rom = irq_rom(0x2500);                          /* máscara 5: la HINT (4) no entra, la VINT (6) sí */
    g = load(rom, 0x40000);
    for (int f = 0; f < 5; f++) gen_run_frame(g);
    CHECK(rd32(g->ram) == 5 && rd32(g->ram + 4) == 0, "máscara 5: VINT %u (5), HINT %u (0)", rd32(g->ram), rd32(g->ram + 4));
    gen_free(g); free(g); free(rom);
}

int main(void)
{
    console_utf8();
    test_memoria();
    test_z80();
    test_mandos();
    test_sram();
    test_eeprom();
    test_ssf2();
    test_interrupciones();
    if (!fallas) printf("bustest: todo bien\n");
    return fallas != 0;
}
