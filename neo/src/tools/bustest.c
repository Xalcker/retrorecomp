/*
 * Prueba del bus y de la consola (make test), sin la BIOS de SNK: una BIOS de prueba hecha aquí (pasa
 * a los vectores del juego y salta a $000400) y juegos sintéticos.
 *   - Mapa de memoria: vectores de la BIOS y del juego, P1, RAM y espejos, BIOS y espejos, bus abierto.
 *   - Bancos de P2 (P de 5 MB), mandos, paleta (bancos y bytes), tarjeta de memoria.
 *   - LSPC: VRAM con su incremento, contador de líneas.
 *   - Interrupciones con un programa de 68000: la de arranque, VBLANK y temporizador (con IRQACK) y
 *     un VBLANK sin reconocer que vuelve a entrar.
 *   - Z80 con un programa propio: banco de la M1, NMI con el comando del 68000 y su respuesta, INT
 *     del temporizador A del YM2610.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neo.h"
#include "console.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

/* --- Programas: se arman como los ve el 68000 y se guardan como el chip (palabras invertidas) --- */

typedef struct { u8 *m; u32 pc; } Asm;
static void w16(Asm *a, u16 v) { a->m[a->pc] = (u8)(v >> 8); a->m[a->pc + 1] = (u8)v; a->pc += 2; }
static void w32(Asm *a, u32 v) { w16(a, (u16)(v >> 16)); w16(a, (u16)v); }
static void at(Asm *a, u32 pc) { a->pc = pc; }
static void put32(u8 *m, u32 o, u32 v) { m[o] = (u8)(v >> 24); m[o + 1] = (u8)(v >> 16); m[o + 2] = (u8)(v >> 8); m[o + 3] = (u8)v; }
static void to_chip(u8 *dst, const u8 *cpu, size_t n) { for (size_t i = 0; i < n; i += 2) { dst[i] = cpu[i + 1]; dst[i + 1] = cpu[i]; } }

/* Instrucciones que usan las pruebas */
static void move_w_imm_abs(Asm *a, u16 v, u32 adr) { w16(a, 0x33FC); w16(a, v); w32(a, adr); }
static void move_b_imm_abs(Asm *a, u8 v, u32 adr) { w16(a, 0x13FC); w16(a, v); w32(a, adr); }
static void addq_w_1_abs(Asm *a, u32 adr) { w16(a, 0x5279); w32(a, adr); }
static void move_sr(Asm *a, u16 v) { w16(a, 0x46FC); w16(a, v); }
static void rte(Asm *a) { w16(a, 0x4E73); }
static void bra_self(Asm *a) { w16(a, 0x60FE); }
static void jmp_abs(Asm *a, u32 adr) { w16(a, 0x4EF9); w32(a, adr); }

/* La BIOS de prueba: vectores (SSP $10F300, PC $C00402) y en $402: SWPROM y salta a $000400 */
static void make_bios(Neo *g)
{
    static u8 cpu[BIOS_SIZE];
    memset(cpu, 0, sizeof cpu);
    put32(cpu, 0, 0x0010F300);
    put32(cpu, 4, 0x00C00402);
    Asm a = { cpu, 0x402 };
    move_b_imm_abs(&a, 0, 0x3A0013);
    jmp_abs(&a, 0x000400);
    to_chip(g->bios.rom, cpu, BIOS_SIZE);
}

/*
 * El juego de prueba. ack = 0: el VBLANK no se reconoce. P de p_size bytes; en cada MB a partir del
 * segundo, la palabra $B000 + número de MB en su primera dirección (para ver los bancos).
 *   $400  SR = $2000; temporizador: recarga = timer_reload, LSPCMODE = $B0 (repetir, recargar al
 *         escribir TIMERLOW, interrupción); y espera
 *   $500  VBLANK: suma 1 a $100000 (y reconoce)    $600 temporizador: suma 1 a $100004 y reconoce
 *   $700  arranque (nivel 3): suma 1 a $100002 y reconoce
 */
static u8 *make_program(size_t p_size, int ack, u32 timer_reload)
{
    u8 *cpu = calloc(1, p_size);
    for (u32 n = 0; n < 64; n++) put32(cpu, n * 4, 0x000800);
    put32(cpu, 0x64, 0x500); put32(cpu, 0x68, 0x600); put32(cpu, 0x6C, 0x700);
    memcpy(cpu + 0x100, "NEO-GEO", 7);
    Asm a = { cpu, 0x400 };
    move_sr(&a, 0x2000);
    move_w_imm_abs(&a, (u16)(timer_reload >> 16), 0x3C0008);
    move_w_imm_abs(&a, 0x00B0, 0x3C0006);
    move_w_imm_abs(&a, (u16)timer_reload, 0x3C000A);
    bra_self(&a);
    at(&a, 0x500); addq_w_1_abs(&a, 0x100000); if (ack) move_w_imm_abs(&a, 4, 0x3C000C); rte(&a);
    at(&a, 0x600); addq_w_1_abs(&a, 0x100004); move_w_imm_abs(&a, 2, 0x3C000C); rte(&a);
    at(&a, 0x700); addq_w_1_abs(&a, 0x100002); move_w_imm_abs(&a, 1, 0x3C000C); rte(&a);
    at(&a, 0x800); bra_self(&a);
    for (size_t mb = 1; mb < p_size >> 20; mb++) { cpu[mb << 20] = 0xB0; cpu[(mb << 20) + 1] = (u8)mb; }
    return cpu;
}

/*
 * El programa del Z80 (M1 de 128 KB):
 *   $0000  DI; SP = $FFFF; IM 1; banco 5 en la ventana de 16 KB y copia $8000 a $F801; temporizador
 *          A con N = 1000 (24 muestras del FM) y su bandera; habilita la NMI; EI; espera
 *   $0038  INT: suma 1 a $F800 y borra la bandera del temporizador A
 *   $0066  NMI: lee el comando y contesta su complemento
 * En $14000 (banco 5 de 16 KB) está $5A.
 */
static void make_m1(u8 *m)
{
    static const u8 main_[] = {
        0xF3, 0x31, 0xFF, 0xFF, 0xED, 0x56,                     /* di; ld sp,$FFFF; im 1 */
        0x06, 0x05, 0x0E, 0x0B, 0xED, 0x78,                     /* ld b,5; ld c,$0B; in a,(c) */
        0x3A, 0x00, 0x80, 0x32, 0x01, 0xF8,                     /* ld a,($8000); ld ($F801),a */
        0x3E, 0x24, 0xD3, 0x04, 0x3E, 0xFA, 0xD3, 0x05,         /* $24 = $FA */
        0x3E, 0x25, 0xD3, 0x04, 0x3E, 0x00, 0xD3, 0x05,         /* $25 = 0 */
        0x3E, 0x27, 0xD3, 0x04, 0x3E, 0x05, 0xD3, 0x05,         /* $27 = 5: A anda y pone su bandera */
        0xD3, 0x08, 0xFB, 0x18, 0xFE,                           /* out ($08),a; ei; jr $ */
    };
    static const u8 isr[] = {
        0xF5, 0x3A, 0x00, 0xF8, 0x3C, 0x32, 0x00, 0xF8,         /* push af; ($F800)++ */
        0x3E, 0x27, 0xD3, 0x04, 0x3E, 0x15, 0xD3, 0x05,         /* $27 = $15: borra la bandera A */
        0xF1, 0xFB, 0xED, 0x4D,                                 /* pop af; ei; reti */
    };
    static const u8 nmi[] = { 0xF5, 0xDB, 0x00, 0x2F, 0xD3, 0x0C, 0xF1, 0xED, 0x45 };  /* push af; in a,(0); cpl; out ($0C),a; pop af; retn */
    memset(m, 0, 0x20000);
    memcpy(m, main_, sizeof main_);
    memcpy(m + 0x38, isr, sizeof isr);
    memcpy(m + 0x66, nmi, sizeof nmi);
    m[0x14000] = 0x5A;
}

/* Arma la consola con el juego de prueba */
static void build(Neo *g, size_t p_size, int ack, u32 timer_reload)
{
    memset(g, 0, sizeof *g);
    u8 *cpu = make_program(p_size, ack, timer_reload);
    RomPart q[4];
    memset(q, 0, sizeof q);
    size_t p1 = p_size < 0x100000 ? p_size : 0x100000;
    snprintf(q[0].name, sizeof q[0].name, "999-p1.p1"); q[0].size = p1; q[0].data = malloc(p1); to_chip(q[0].data, cpu, p1);
    int n = 1;
    if (p_size > p1) {
        snprintf(q[1].name, sizeof q[1].name, "999-p2.sp2"); q[1].size = p_size - p1; q[1].data = malloc(q[1].size);
        to_chip(q[1].data, cpu + p1, q[1].size);
        n = 2;
    }
    snprintf(q[n].name, sizeof q[n].name, "999-m1.m1"); q[n].size = 0x20000; q[n].data = malloc(0x20000); make_m1(q[n].data); n++;
    snprintf(q[n].name, sizeof q[n].name, "999-s1.s1"); q[n].size = 0x20000; q[n].data = calloc(1, 0x20000); n++;
    for (int i = 0; i < n; i++) q[i].crc = crc32_buf(0, q[i].data, q[i].size);
    const char *err = NULL;
    if (cart_load_parts(&g->cart, q, n, ROMFMT_ZIP, &err)) { printf("FALLA: juego de prueba: %s\n", err); exit(1); }
    for (int i = 0; i < n; i++) free(q[i].data);
    free(cpu);
    make_bios(g);
    l0_generate(g->l0);
    neo_power_on(g);
}

static u16 ram16(const Neo *g, u32 a) { return (u16)(g->ram[a & 0xFFFF] << 8 | g->ram[(a + 1) & 0xFFFF]); }

int main(void)
{
    console_utf8();
    static Neo n;
    Neo *g = &n;

    /* --- Mapa de memoria (antes de correr nada) --- */
    build(g, 0x100000, 1, 3839);
    CHECK(g->cpu.pc == 0xC00402 && g->cpu.a[7] == 0x10F300, "reset: PC $%06X SP $%08X (vectores de la BIOS)", g->cpu.pc, g->cpu.a[7]);
    CHECK(m68k_rd16(g, 0x000006) == 0x0402 && m68k_rd16(g, 0x000100) == 0x4E45, "vectores de la BIOS y P1 en $100");
    m68k_wr8(g, 0x3A0013, 0);
    CHECK(m68k_rd16(g, 0x000066) == 0x0500 && (g->latch & LATCH_SWPROM), "REG_SWPROM: vectores del juego ($%04X)", m68k_rd16(g, 0x000066));
    m68k_wr8(g, 0x3A0003, 0);
    CHECK(m68k_rd16(g, 0x000006) == 0x0402, "REG_SWPBIOS: otra vez los de la BIOS");
    CHECK(m68k_rd16(g, 0xC20406) == m68k_rd16(g, 0xC00406) && m68k_rd16(g, 0xCFFFF0) == m68k_rd16(g, 0xC1FFF0), "espejos de la BIOS");
    m68k_wr16(g, 0x100010, 0x1234);
    CHECK(m68k_rd16(g, 0x1F0010) == 0x1234 && m68k_rd8(g, 0x180011) == 0x34, "RAM y sus espejos");
    m68k_rd16(g, 0x100010);
    CHECK(m68k_rd16(g, 0xD00000) == 0x1234, "bus abierto en $D00000");
    u64 c0 = g->cyc;
    m68k_rd8(g, 0x800001);
    CHECK(g->cyc - c0 == 6, "la tarjeta tiene 2 ciclos de espera (%llu)", (unsigned long long)(g->cyc - c0));

    /* Mandos */
    g->pad[0] = NEO_UP | NEO_A | NEO_START;
    g->pad[1] = NEO_D | NEO_SELECT;
    CHECK(m68k_rd8(g, 0x300000) == 0xEE && m68k_rd8(g, 0x340000) == 0x7F, "REG_P1CNT $%02X / REG_P2CNT", m68k_rd8(g, 0x300000));
    CHECK(m68k_rd8(g, 0x380000) == 0x36, "REG_STATUS_B $%02X (Start 1, Select 2, sin tarjeta, AES)", m68k_rd8(g, 0x380000));
    g->pad[0] = g->pad[1] = 0;

    /* Paleta: dos bancos; un byte llega a las dos mitades */
    m68k_wr16(g, 0x400002, 0x7FFF);
    m68k_wr8(g, 0x3A001F, 0);
    CHECK(m68k_rd16(g, 0x400002) == 0, "banco 1 de la paleta vacío");
    m68k_wr8(g, 0x400005, 0x12);
    CHECK(m68k_rd16(g, 0x400004) == 0x1212 && g->pal[1][2] == 0x1212, "byte en la paleta");
    m68k_wr8(g, 0x3A000F, 0);
    CHECK(m68k_rd16(g, 0x7FE002) == 0x7FFF, "banco 0 y espejo de la paleta");

    /* Tarjeta: sin tarjeta, con tarjeta bloqueada y desbloqueada */
    neo_card_insert(g, NULL);
    CHECK((m68k_rd8(g, 0x380000) & 0x70) == 0x00, "tarjeta puesta: bits 6-4 de REG_STATUS_B");
    m68k_wr8(g, 0x800003, 0x42);
    m68k_wr8(g, 0x3A0007, 0);                       /* CRDLOCK2: sigue bloqueada */
    m68k_wr8(g, 0x800003, 0x42);
    CHECK(g->card.data[1] == 0xFF && !g->card.dirty, "la tarjeta bloqueada no se escribe");
    m68k_wr8(g, 0x3A0017, 0);                       /* CRDUNLOCK2 (CRDUNLOCK1 ya está) */
    m68k_wr8(g, 0x800003, 0x42);
    CHECK(g->card.data[1] == 0x42 && m68k_rd8(g, 0x800003) == 0x42 && m68k_rd8(g, 0x800002) == 0xFF && g->card.dirty,
          "tarjeta: escribir y leer los bytes impares");
    CHECK(m68k_rd8(g, 0x801003) == 0x42, "tarjeta de 2 KB con espejos");

    /* LSPC: VRAM con incremento; el incremento no cambia de mitad */
    m68k_wr16(g, 0x3C0000, 0x7FFE);
    m68k_wr16(g, 0x3C0004, 1);
    m68k_wr16(g, 0x3C0002, 0xAAAA);
    m68k_wr16(g, 0x3C0002, 0xBBBB);
    m68k_wr16(g, 0x3C0002, 0xCCCC);
    CHECK(g->lspc.vram[0x7FFE] == 0xAAAA && g->lspc.vram[0x7FFF] == 0xBBBB && g->lspc.vram[0x0000] == 0xCCCC, "VRAM lenta con incremento");
    m68k_wr16(g, 0x3C0000, 0x87FF);
    m68k_wr16(g, 0x3C0002, 0x1111);
    m68k_wr16(g, 0x3C0002, 0x2222);
    CHECK(g->lspc.vram[0x87FF] == 0x1111 && g->lspc.vram[0x8000] == 0x2222, "VRAM rápida con incremento");
    m68k_wr16(g, 0x3C0000, 0x8000);
    CHECK(m68k_rd16(g, 0x3C0002) == 0x2222 && m68k_rd16(g, 0x3C0000) == 0x2222 && m68k_rd16(g, 0x3C0004) == 1, "leer la VRAM y VRAMMOD");
    m68k_wr8(g, 0x3C0005, 0x02);
    CHECK(g->lspc.vram_mod == 0x0202, "un byte en un registro del LSPC llega a las dos mitades");
    /* Contador de líneas: la línea 0 es la del VBLANK ($1F0); la 40, la primera visible ($110) */
    {
        static const struct { int line; u16 counter; } cases[] = { { 0, 0x1F0 }, { 15, 0x1FF }, { 16, 0x0F8 }, { 24, 0x100 }, { 40, 0x110 }, { 263, 0x1EF } };
        u64 base = g->lspc.line_start - (u64)g->lspc.line * M68K_PER_LINE;
        for (int i = 0; i < 6; i++) {
            g->cyc = base + (u64)cases[i].line * M68K_PER_LINE + 100;
            u16 v = m68k_rd16(g, 0x3C0006);
            CHECK(v >> 7 == cases[i].counter, "línea %d: contador $%03X (esperado $%03X)", cases[i].line, v >> 7, cases[i].counter);
        }
    }

    /* --- Interrupciones con el programa --- */
    build(g, 0x100000, 1, 3839);                    /* temporizador cada 3840 pixeles: 10 líneas */
    for (int f = 0; f < 10; f++) neo_run_frame(g);
    unsigned vbl = ram16(g, 0x100000), cold = ram16(g, 0x100002), tim = ram16(g, 0x100004);
    CHECK(cold == 1, "interrupción de arranque: %u veces", cold);
    CHECK(vbl >= 9 && vbl <= 10, "VBLANK: %u veces en 10 cuadros", vbl);
    CHECK(tim >= 262 && tim <= 265, "temporizador cada 10 líneas: %u veces en 10 cuadros (264)", tim);
    CHECK(g->exc_count[VEC_AUTO1] == vbl && g->exc_count[VEC_AUTO1 + 1] == tim && g->exc_count[VEC_AUTO1 + 2] == 1, "excepciones contadas");
    CHECK(g->lspc.frame == 10 && g->cyc >= 10ull * M68K_PER_FRAME && g->cyc < 10ull * M68K_PER_FRAME + 200, "10 cuadros: %llu ciclos",
          (unsigned long long)g->cyc);
    /* Sin reconocer el VBLANK, vuelve a entrar en cuanto termina RTE */
    build(g, 0x100000, 0, 0x7FFFFFFF);
    neo_run_frame(g);
    neo_run_frame(g);
    CHECK(ram16(g, 0x100000) > 1000, "VBLANK sin IRQACK: %u entradas", ram16(g, 0x100000));

    /* --- Bancos de P2 --- */
    build(g, 0x500000, 1, 0x7FFFFFFF);
    CHECK(g->cart.p_banks == 4 && m68k_rd16(g, 0x200000) == 0xB001, "banco 0 de P2: $%04X", m68k_rd16(g, 0x200000));
    u32 v0 = g->map_ver;
    m68k_wr16(g, 0x2FFFF0, 2);
    CHECK(m68k_rd16(g, 0x200000) == 0xB003 && g->map_ver == v0 + 1 && neo_code_offset(g, 0x200000) == 0x300000, "banco 2 de P2");
    m68k_wr8(g, 0x2FFFF1, 3);
    CHECK(m68k_rd16(g, 0x200000) == 0xB004, "banco 3 con un byte");
    m68k_wr16(g, 0x212340, 0);
    CHECK(m68k_rd16(g, 0x200000) == 0xB004, "una escritura fuera de $2FFFF0 no cambia el banco");

    /* --- Z80: banco de la M1, temporizador A, NMI y respuesta --- */
    build(g, 0x100000, 1, 0x7FFFFFFF);
    neo_run_frame(g);
    CHECK(g->zram[1] == 0x5A, "Z80: banco 5 de la ventana de 16 KB ($%02X)", g->zram[1]);
    for (int f = 0; f < 9; f++) neo_run_frame(g);
    /* 10 cuadros = 4 055 040 ciclos maestros; el temporizador vence cada 24 × 432 = 10 368 */
    unsigned ints = g->zram[0];
    CHECK(ints >= 389 % 256 - 2 && ints <= 389 % 256 + 2, "Z80: INT del temporizador A: %u (esperado 389 mod 256)", ints);
    m68k_wr8(g, 0x320000, 0x12);
    CHECK(g->sound_cmd == 0x12 && g->sound_cmds == 1, "comando al Z80");
    g->cyc += 2000;                                 /* el Z80 atiende la NMI */
    CHECK(m68k_rd8(g, 0x320000) == 0xED, "respuesta del Z80: $%02X", g->sound_reply);
    m68k_wr8(g, 0x320000, 0x40);
    g->cyc += 2000;
    CHECK(m68k_rd16(g, 0x320000) >> 8 == 0xBF, "segundo comando y respuesta");

    neo_free(g);
    if (fallas) { printf("bustest: %d fallas\n", fallas); return 1; }
    printf("bustest: todo bien\n");
    return 0;
}
