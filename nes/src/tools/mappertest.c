/*
 * mappertest: comprueba el mapeo de bancos de los mappers nuevos escribiendo en sus registros sobre
 * una cartucho sintético (cada banco de PRG y de CHR lleva su número como contenido).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

static NES n;
static int fails;

static void make(int mapper, int prg_kb, int chr_kb, int mirroring)
{
    memset(&n, 0, sizeof n);
    Cart *c = &n.cart;
    c->mapper = mapper; c->mirroring = mirroring;
    c->prg_size = (size_t)prg_kb * 1024; c->prg = malloc(c->prg_size);
    for (size_t i = 0; i < c->prg_size; i++) c->prg[i] = (u8)(i >> 13);         /* número de banco de 8 KB */
    if (!chr_kb) { c->chr_is_ram = 1; chr_kb = 8; }                              /* sin CHR: 8 KB de RAM */
    c->chr_size = (size_t)chr_kb * 1024; c->chr = malloc(c->chr_size);
    for (size_t i = 0; i < c->chr_size; i++) c->chr[i] = (u8)(i >> 10);         /* número de banco de 1 KB */
    mapper_reset(&n);
}

#define EXPECT_PRG(addr, want) do { u8 g_ = mapper_cpu_read(&n, (addr)); if (g_ != (want)) { \
    printf("FALLA mapper %d: PRG $%04X = banco8K %d, esperaba %d (línea %d)\n", n.cart.mapper, (addr), g_, (want), __LINE__); fails++; } } while (0)
#define EXPECT_CHR(addr, want) do { u8 g_ = mapper_ppu_read(&n, (addr)); if (g_ != (want)) { \
    printf("FALLA mapper %d: CHR $%04X = banco1K %d, esperaba %d (línea %d)\n", n.cart.mapper, (addr), g_, (want), __LINE__); fails++; } } while (0)
#define W(addr, v) mapper_cpu_write(&n, (addr), (v))

int main(void)
{
    /* 11 Color Dreams: PRG 32K (bits 0-1) y CHR 8K (bits 4-7) */
    make(11, 128, 64, MIRROR_VERTICAL);
    W(0x8000, 0x32);               /* PRG 2 (8K 8..11), CHR 3 (1K 24..31) */
    EXPECT_PRG(0x8000, 8); EXPECT_PRG(0xE000, 11); EXPECT_CHR(0x0000, 24); EXPECT_CHR(0x1C00, 31);

    /* 66 GxROM: PRG bits 4-5, CHR bits 0-1 */
    make(66, 128, 32, MIRROR_VERTICAL);
    W(0x8000, 0x21);               /* PRG 2, CHR 1 */
    EXPECT_PRG(0x8000, 8); EXPECT_CHR(0x0000, 8);

    /* 34 BNROM (CHR RAM): PRG 32K con cualquier escritura */
    make(34, 128, 0, MIRROR_VERTICAL);
    W(0x8000, 3); EXPECT_PRG(0x8000, 12); EXPECT_PRG(0xA000, 13);

    /* 71 Camerica: 16K en $8000, último fijo; espejado en $9000 */
    make(71, 128, 0, MIRROR_VERTICAL);
    W(0xC000, 3); EXPECT_PRG(0x8000, 6); EXPECT_PRG(0xA000, 7); EXPECT_PRG(0xC000, 14);
    W(0x9000, 0x10); if (n.cart.mirroring != MIRROR_SINGLE1) { puts("FALLA mapper 71: espejado"); fails++; }

    /* 180: el primero fijo en $8000, el variable en $C000 */
    make(180, 128, 0, MIRROR_VERTICAL);
    W(0x8000, 5); EXPECT_PRG(0x8000, 0); EXPECT_PRG(0xC000, 10); EXPECT_PRG(0xE000, 11);

    /* 79 NINA-03: registros en $4100 (bit 3 PRG, bits 0-2 CHR) */
    make(79, 64, 32, MIRROR_VERTICAL);
    W(0x4100, 0x0A);               /* PRG 1, CHR 2 */
    EXPECT_PRG(0x8000, 4); EXPECT_CHR(0x0000, 16);
    W(0x4000, 0x07);               /* fuera de la máscara: no cambia */
    EXPECT_PRG(0x8000, 4);

    /* 87: CHR con los bits 0 y 1 intercambiados, en $6000 */
    make(87, 32, 32, MIRROR_VERTICAL);
    W(0x6000, 0x01);               /* -> banco 2 */
    EXPECT_CHR(0x0000, 16);

    /* 140: PRG bits 4-5, CHR bits 0-3, en $6000 */
    make(140, 128, 64, MIRROR_VERTICAL);
    W(0x6000, 0x12); EXPECT_PRG(0x8000, 4); EXPECT_CHR(0x0000, 16);

    /* 206 DxROM: R6 en $8000, R7 en $A000, últimos dos fijos; CHR R0 de 2K */
    make(206, 128, 64, MIRROR_VERTICAL);
    W(0x8000, 6); W(0x8001, 3); W(0x8000, 7); W(0x8001, 5); W(0x8000, 0); W(0x8001, 4);
    EXPECT_PRG(0x8000, 3); EXPECT_PRG(0xA000, 5); EXPECT_PRG(0xC000, 14); EXPECT_PRG(0xE000, 15);
    EXPECT_CHR(0x0000, 4); EXPECT_CHR(0x0400, 5);

    /* 9 MMC2: PRG 8K en $8000 y los tres últimos fijos; CHR con latches */
    make(9, 128, 128, MIRROR_VERTICAL);
    W(0xA000, 2); W(0xB000, 4); W(0xC000, 6); W(0xD000, 8); W(0xE000, 10);
    EXPECT_PRG(0x8000, 2); EXPECT_PRG(0xA000, 13); EXPECT_PRG(0xC000, 14); EXPECT_PRG(0xE000, 15);
    EXPECT_CHR(0x0000, 24);                       /* latch 0 en FE -> registro $C000 = 6 -> 4K 6 = 1K 24 */
    mapper_chr_latch(&n.cart, 0x0FD8);             /* latch FD -> $B000 = 4 */
    EXPECT_CHR(0x0000, 16);
    mapper_chr_latch(&n.cart, 0x0FDA);             /* MMC2: solo $0FD8 exacto en la mitad izquierda */
    mapper_chr_latch(&n.cart, 0x0FE8);             /* FE otra vez */
    EXPECT_CHR(0x0000, 24);
    EXPECT_CHR(0x1000, 40);                        /* derecho en FE -> $E000 = 10 -> 1K 40 */
    mapper_chr_latch(&n.cart, 0x1FDB);             /* derecha: rango */
    EXPECT_CHR(0x1000, 32);

    /* 10 MMC4: PRG 16K, rangos en las dos mitades */
    make(10, 128, 128, MIRROR_VERTICAL);
    W(0xA000, 3); W(0xB000, 1); W(0xC000, 2);
    EXPECT_PRG(0x8000, 6); EXPECT_PRG(0xC000, 14);
    mapper_chr_latch(&n.cart, 0x0FDC);
    EXPECT_CHR(0x0000, 4);

    /* 69 FME-7: comandos y IRQ por ciclos */
    make(69, 128, 64, MIRROR_VERTICAL);
    W(0x8000, 9); W(0xA000, 2); W(0x8000, 10); W(0xA000, 3); W(0x8000, 11); W(0xA000, 4);
    EXPECT_PRG(0x8000, 2); EXPECT_PRG(0xA000, 3); EXPECT_PRG(0xC000, 4); EXPECT_PRG(0xE000, 15);
    W(0x8000, 3); W(0xA000, 9); EXPECT_CHR(0x0C00, 9);
    n.cyc = 1000;
    W(0x8000, 14); W(0xA000, 10); W(0x8000, 15); W(0xA000, 0);   /* contador = 10 */
    W(0x8000, 13); W(0xA000, 0x81);                              /* IRQ activo y contador activo */
    if (n.cart.irq_cycle != 1000 + 10 + 1) { printf("FALLA mapper 69: IRQ en %llu, esperaba 1011\n", (unsigned long long)n.cart.irq_cycle); fails++; }

    /* 118 TxSROM: el bit 7 del banco de CHR elige la página del nametable */
    make(118, 128, 64, MIRROR_VERTICAL);
    W(0x8000, 0); W(0x8001, 0x80 | 4);   /* R0 con bit 7: nametables 0 y 1 -> página 1 */
    W(0x8000, 1); W(0x8001, 6);          /* R1 sin bit 7: nametables 2 y 3 -> página 0 */
    if (n.cart.nt_map[0] != 1 || n.cart.nt_map[1] != 1 || n.cart.nt_map[2] != 0 || n.cart.nt_map[3] != 0) {
        puts("FALLA mapper 118: nametables"); fails++;
    }

    /* 119 TQROM: como el MMC3, pero el bit 6 del banco de CHR elige RAM en lugar de ROM */
    make(119, 128, 64, MIRROR_VERTICAL);
    W(0x8000, 6); W(0x8001, 3); W(0x8000, 2); W(0x8001, 5);
    EXPECT_PRG(0x8000, 3); EXPECT_PRG(0xE000, 15); EXPECT_CHR(0x1000, 5);
    W(0x8000, 2); W(0x8001, 0x40 | 2);                 /* R2 (1 KB en $1000) -> RAM, página 2 */
    mapper_ppu_write(&n, 0x1000, 0xAB);
    if (n.cart.chr_ram[2 * 1024] != 0xAB) { puts("FALLA mapper 119: la RAM de CHR no recibió la escritura"); fails++; }
    W(0x8000, 2); W(0x8001, 7);                        /* de vuelta a ROM: ya no se escribe */
    mapper_ppu_write(&n, 0x1000, 0xCD);
    if (n.cart.chr[7 * 1024] == 0xCD) { puts("FALLA mapper 119: escribió en la ROM"); fails++; }

    /* 119 y 4: $A000 cambia el espejado (Pin Bot lo usa para cambiar entre dos imágenes) */
    W(0xA000, 0); if (n.cart.mirroring != MIRROR_VERTICAL) { puts("FALLA mapper 119: espejado vertical"); fails++; }
    W(0xA000, 1); if (n.cart.mirroring != MIRROR_HORIZONTAL) { puts("FALLA mapper 119: espejado horizontal"); fails++; }

    /* 1 SUROM (512 KB): el bit 4 del registro de CHR 0 elige el bloque de 256 KB */
    make(1, 512, 0, MIRROR_VERTICAL);
    EXPECT_PRG(0xE000, 31);                                      /* tras el reset: último banco del primer bloque */
    for (int i = 0; i < 5; i++) { n.cyc += 10; W(0xA000, i == 4 ? 1 : 0); }   /* CHR 0 = $10 (de menos a más significativo) */
    EXPECT_PRG(0xE000, 63);                                      /* segundo bloque */
    /* 5 MMC5: PCM en modo escritura y en modo lectura; ExGrafix se activa con $5104 = 1 */
    make(5, 256, 128, MIRROR_VERTICAL);
    n.apu.m5 = 1;
    W(0x5010, 0); W(0x5011, 0x40);
    if (n.apu.m5_pcm != 0x40) { puts("FALLA mapper 5: PCM en modo escritura"); fails++; }
    W(0x5010, 1); W(0x5011, 0x20);                         /* en modo lectura, escribir no cambia la salida */
    if (n.apu.m5_pcm != 0x40) { puts("FALLA mapper 5: PCM ignora $5011 en modo lectura"); fails++; }
    W(0x5100, 3); W(0x5114, 0x80 | 2);
    (void)mapper_cpu_read(&n, 0x8000);                     /* lectura de $8000-$BFFF: fija la salida con el byte leído */
    if (n.apu.m5_pcm != n.cart.prg[2 * 0x2000]) { puts("FALLA mapper 5: PCM en modo lectura"); fails++; }
    W(0x5104, 1); if (!n.cart.exgfx) { puts("FALLA mapper 5: ExGrafix"); fails++; }
    W(0x5104, 0); if (n.cart.exgfx) { puts("FALLA mapper 5: ExGrafix se queda"); fails++; }

    if (!fails) puts("mappertest: bien");
    return fails != 0;
}
