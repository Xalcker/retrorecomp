/*
 * Prueba sintética del cartucho (make test): arma un juego en memoria (sus chips como en un zip de
 * MAME, con el P1 de 2 MB de mitades cambiadas), lo carga desde los chips, como .neo, como zip
 * (guardado) y como carpeta, y comprueba que los cuatro dan la misma imagen normalizada, el mismo
 * SHA1 y la misma cabecera. Además: el inflate con códigos fijos y dinámicos, los sufijos de los
 * archivos (P2, buses de V separados), los errores, los juegos cifrados, la tabla L0, la BIOS y la
 * identidad del código (neo_code_offset) con su ida y vuelta por el formato de traza.
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "neo.h"
#include "neodb.h"
#include "sha1.h"
#include "console.h"
#include "trace_line.h"
#include "inflate_vectors.h"

static int fallas;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FALLA: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)

static unsigned seed = 12345;
static void fill(u8 *p, size_t n) { for (size_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; p[i] = (u8)(seed >> 16); } }

/* El programa como lo ve el 68000 (big-endian, lineal) */
static void wr16(u8 *p, u32 a, u16 v) { p[a] = (u8)(v >> 8); p[a + 1] = (u8)v; }
static void wr32(u8 *p, u32 a, u32 v) { wr16(p, a, (u16)(v >> 16)); wr16(p, a + 2, (u16)v); }

static void make_program(u8 *cpu, size_t size, u16 ngh)
{
    fill(cpu, size);
    for (u32 n = 0; n < VEC_COUNT; n++) wr32(cpu, n * 4, 0xC00426);
    wr32(cpu, 0, 0x0010F300);                   /* SSP */
    wr32(cpu, 4, 0x00C00402);                   /* reset: la BIOS */
    wr32(cpu, 0x64, 0x00000500);                /* VBLANK */
    wr32(cpu, 0x68, 0x00000600);                /* temporizador */
    memcpy(cpu + 0x100, "NEO-GEO", 7);
    cpu[0x107] = 0;
    wr16(cpu, 0x108, ngh);
    wr32(cpu, 0x10A, (u32)size);
    wr32(cpu, 0x10E, 0x0010F000);
    wr16(cpu, 0x112, 0x100);
    cpu[0x114] = 2; cpu[0x115] = 0;
    wr32(cpu, 0x116, 0x1000); wr32(cpu, 0x11A, 0x1010); wr32(cpu, 0x11E, 0x1020);
    for (u32 i = 0; i < 4; i++) { wr16(cpu, 0x122 + 6 * i, 0x4EF9); wr32(cpu, 0x124 + 6 * i, 0x400 + 0x10 * i); }
    wr32(cpu, 0x182, 0x2000);
}

/* Lo que se vuelca de un chip P: las palabras con los bytes invertidos */
static void to_chip(u8 *dst, const u8 *cpu, size_t n) { for (size_t i = 0; i < n; i += 2) { dst[i] = cpu[i + 1]; dst[i + 1] = cpu[i]; } }

static RomPart part(const char *name, size_t size)
{
    RomPart r;
    memset(&r, 0, sizeof r);
    snprintf(r.name, sizeof r.name, "%s", name);
    r.size = size;
    r.data = malloc(size);
    return r;
}

static void crcs(RomPart *p, int n) { for (int i = 0; i < n; i++) p[i].crc = crc32_buf(0, p[i].data, p[i].size); }

static int write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

/* Un zip con los archivos guardados tal cual (método 0), dentro de una carpeta "juego/" */
static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static u8 *make_zip(const RomPart *p, int n, size_t *len)
{
    size_t cap = 1024;
    for (int i = 0; i < n; i++) cap += p[i].size + 2 * (46 + 140);
    u8 *z = calloc(1, cap), *cd = calloc(1, 46 * (size_t)(n + 1) + 140 * (size_t)(n + 1));
    size_t o = 0, c = 0;
    u32 *local = calloc((size_t)n, sizeof *local);
    for (int i = 0; i < n; i++) {
        char name[160];
        snprintf(name, sizeof name, "juego/%s", p[i].name);
        u32 nl = (u32)strlen(name);
        local[i] = (u32)o;
        put32(z + o, 0x04034B50); put16(z + o + 4, 20); put16(z + o + 8, 0);
        put32(z + o + 14, p[i].crc); put32(z + o + 18, (u32)p[i].size); put32(z + o + 22, (u32)p[i].size);
        put16(z + o + 26, nl); put16(z + o + 28, 0);
        memcpy(z + o + 30, name, nl);
        memcpy(z + o + 30 + nl, p[i].data, p[i].size);
        o += 30 + nl + p[i].size;
        put32(cd + c, 0x02014B50); put16(cd + c + 4, 20); put16(cd + c + 6, 20); put16(cd + c + 10, 0);
        put32(cd + c + 16, p[i].crc); put32(cd + c + 20, (u32)p[i].size); put32(cd + c + 24, (u32)p[i].size);
        put16(cd + c + 28, nl); put32(cd + c + 42, local[i]);
        memcpy(cd + c + 46, name, nl);
        c += 46 + nl;
    }
    /* Y la entrada de la carpeta, que se ignora */
    put32(cd + c, 0x02014B50); put16(cd + c + 28, 6); put32(cd + c + 42, 0); memcpy(cd + c + 46, "juego/", 6);
    c += 52;
    u32 cdoff = (u32)o;
    memcpy(z + o, cd, c);
    o += c;
    put32(z + o, 0x06054B50); put16(z + o + 8, (u32)n + 1); put16(z + o + 10, (u32)n + 1);
    put32(z + o + 12, (u32)c); put32(z + o + 16, cdoff);
    o += 22;
    free(cd); free(local);
    *len = o;
    return z;
}

static void same(const char *what, const Cart *a, const Cart *b)
{
    CHECK(a->size == b->size && !memcmp(a->data, b->data, a->size), "%s: imagen distinta", what);
    CHECK(!strcmp(a->sha1, b->sha1), "%s: SHA1 distinto", what);
    for (int r = 0; r < REG_COUNT; r++)
        CHECK(a->off[r] == b->off[r] && a->len[r] == b->len[r], "%s: región %s distinta", what, neo_region_name[r]);
    CHECK(!memcmp(&a->hdr, &b->hdr, sizeof a->hdr), "%s: cabecera distinta", what);
    CHECK(!memcmp(a->vec, b->vec, sizeof a->vec), "%s: vectores distintos", what);
}

int main(void)
{
    console_utf8();
    const char *err = NULL;
    MKDIR("build"); MKDIR("build/tests");

    /* --- Inflate --- */
    {
        u8 out[4096];
        CHECK(!inflate_raw(deflate_fixed, sizeof deflate_fixed, out, DEFLATE_FIXED_LEN)
              && crc32_buf(0, out, DEFLATE_FIXED_LEN) == DEFLATE_FIXED_CRC, "inflate con códigos fijos");
        CHECK(!inflate_raw(deflate_dynamic, sizeof deflate_dynamic, out, DEFLATE_DYNAMIC_LEN)
              && crc32_buf(0, out, DEFLATE_DYNAMIC_LEN) == DEFLATE_DYNAMIC_CRC, "inflate con códigos dinámicos");
        CHECK(inflate_raw(deflate_dynamic, sizeof deflate_dynamic - 40, out, DEFLATE_DYNAMIC_LEN), "inflate cortado debe fallar");
        CHECK(inflate_raw(deflate_dynamic, sizeof deflate_dynamic, out, DEFLATE_DYNAMIC_LEN - 1), "inflate con salida chica debe fallar");
    }

    /* --- Juego 1: P1 de 2 MB con las mitades cambiadas, V en un bus, C en dos pares --- */
    const size_t PSZ = 0x200000;
    u8 *cpu = malloc(PSZ);
    make_program(cpu, PSZ, 0x0999);
    RomPart g[9];
    g[0] = part("999-p1.p1", PSZ);
    to_chip(g[0].data, cpu + 0x100000, 0x100000);           /* el segundo MB va primero en el chip */
    to_chip(g[0].data + 0x100000, cpu, 0x100000);
    g[1] = part("999-s1.s1", 0x20000);
    g[2] = part("999-m1.m1", 0x10000);
    g[3] = part("999-v1.v1", 0x40000);
    g[4] = part("999-v2.v2", 0x40000);
    g[5] = part("999-c1.c1", 0x20000);
    g[6] = part("999-c2.c2", 0x20000);
    g[7] = part("999-c3.c3", 0x20000);
    g[8] = part("999-c4.c4", 0x20000);
    for (int i = 1; i < 9; i++) fill(g[i].data, g[i].size);
    crcs(g, 9);

    static Cart a, b, d, z;
    CHECK(!cart_load_parts(&a, g, 9, ROMFMT_ZIP, &err), "chips: %s", err);
    const NeoHeader *h = &a.hdr;
    CHECK(a.p1_swapped, "no acomodó las mitades del P1");
    CHECK(h->neo, "no encontró NEO-GEO");
    CHECK(h->ngh == 0x0999 && h->p_size == PSZ, "NGH $%04X, tamaño %u", h->ngh, h->p_size);
    CHECK(h->backup_ptr == 0x10F000 && h->backup_size == 0x100 && h->eyecatch == 2, "respaldo o logo");
    CHECK(h->dip[0] == 0x1000 && h->dip[1] == 0x1010 && h->dip[2] == 0x1020 && h->security == 0x2000, "DIP o seguridad");
    for (int i = 0; i < ENTRY_COUNT; i++) CHECK(h->entry[i] == 0x400 + 0x10 * (u32)i, "entrada %s: $%06X", neo_entry_name[i], h->entry[i]);
    CHECK(a.vec[VEC_RESET] == 0xC00402 && a.vec[VEC_AUTO1] == 0x500 && a.vec[VEC_AUTO1 + 1] == 0x600, "vectores");
    CHECK(a.len[REG_P] == PSZ && a.len[REG_S] == 0x20000 && a.len[REG_M] == 0x10000 && a.len[REG_VA] == 0x80000
          && a.len[REG_VB] == 0 && a.len[REG_C] == 0x80000, "tamaños de las regiones");
    CHECK(a.p_banks == 1, "un P de 2 MB tiene un banco P2 fijo (%d)", a.p_banks);
    /* El 68000 lee el programa lineal */
    int lineal = 1;
    for (u32 i = 0; i < PSZ; i += 2) if (cart_p16(&a, i) != (u16)(cpu[i] << 8 | cpu[i + 1])) { lineal = 0; break; }
    CHECK(lineal, "el programa no queda lineal para el 68000");
    /* C intercalada por pares; V seguidas */
    const u8 *C = a.data + a.off[REG_C];
    CHECK(C[0] == g[5].data[0] && C[1] == g[6].data[0] && C[2] == g[5].data[1] && C[0x40000] == g[7].data[0]
          && C[0x40001] == g[8].data[0], "C mal intercalada");
    CHECK(!memcmp(a.data + a.off[REG_VA] + 0x40000, g[4].data, 0x40000), "V2 fuera de lugar");
    CHECK(!strcmp(a.files[REG_C], "999-c1.c1+999-c2.c2 999-c3.c3+999-c4.c4"), "archivos de C: %s", a.files[REG_C]);
    int fatal = 0;
    CHECK(cart_unsupported(&a, &fatal) == NULL, "el juego de prueba no debería tener avisos");
    char s[41];
    sha1_hex(a.data, a.size, s);
    CHECK(!strcmp(s, a.sha1), "SHA1 de la imagen");

    /* .neo: la misma imagen; su SHA1 es el de lo que sigue a la cabecera */
    snprintf(a.title, sizeof a.title, "Prueba");
    CHECK(cart_write_neo(&a, "build/tests/prueba.neo"), "no se pudo escribir el .neo");
    CHECK(!cart_load_path(&b, "build/tests/prueba.neo", &err), ".neo: %s", err);
    same(".neo", &a, &b);
    CHECK(b.format == ROMFMT_NEO && !strcmp(b.title, "Prueba"), ".neo: formato o nombre");

    /* zip (guardado, en una carpeta interna) */
    size_t zl;
    u8 *zip = make_zip(g, 9, &zl);
    CHECK(write_file("build/tests/prueba.zip", zip, zl), "no se pudo escribir el zip");
    CHECK(!cart_load_path(&z, "build/tests/prueba.zip", &err), "zip: %s", err);
    same("zip", &a, &z);
    CHECK(z.format == ROMFMT_ZIP, "zip: formato");
    zip[zl - 22 - 1] ^= 0xFF;                                       /* daña el directorio */
    RomPart *zp;
    int zn = zip_read_all(zip, zl, &zp, &err);
    if (zn >= 0) parts_free(zp, zn);
    zip[zl - 22 - 1] ^= 0xFF;
    zip[100] ^= 0xFF;                                               /* daña los datos: el CRC no coincide */
    zn = zip_read_all(zip, zl, &zp, &err);
    CHECK(zn < 0, "un zip con datos dañados debe fallar");
    if (zn >= 0) parts_free(zp, zn);
    free(zip);

    /* Carpeta */
    MKDIR("build/tests/prueba");
    for (int i = 0; i < 9; i++) {
        char path[256];
        snprintf(path, sizeof path, "build/tests/prueba/%s", g[i].name);
        write_file(path, g[i].data, g[i].size);
    }
    CHECK(!cart_load_path(&d, "build/tests/prueba", &err), "carpeta: %s", err);
    same("carpeta", &a, &d);
    CHECK(d.format == ROMFMT_DIR, "carpeta: formato");
    cart_free(&b); cart_free(&d); cart_free(&z);

    /* Errores */
    static Cart e;
    CHECK(cart_load_parts(&e, g + 1, 8, ROMFMT_ZIP, &err) && strstr(err, "P1"), "sin P1 debe fallar");
    RomPart sin_m[8];
    memcpy(sin_m, g, sizeof sin_m[0] * 2);
    memcpy(sin_m + 2, g + 3, sizeof sin_m[0] * 6);
    CHECK(cart_load_parts(&e, sin_m, 8, ROMFMT_ZIP, &err) && strstr(err, "M1"), "sin M1 debe fallar");
    CHECK(cart_load_parts(&e, g, 8, ROMFMT_ZIP, &err) && strstr(err, "pares"), "C impares debe fallar");
    RomPart dup[10];
    memcpy(dup, g, sizeof dup[0] * 9);
    dup[9] = g[5];
    snprintf(dup[9].name, sizeof dup[9].name, "otro.c1");
    CHECK(cart_load_parts(&e, dup, 10, ROMFMT_ZIP, &err), "dos .c1 debe fallar");
    CHECK(cart_load_path(&e, "build/tests/no-existe.zip", &err), "un archivo que no existe debe fallar");
    CHECK(write_file("build/tests/basura.bin", "hola", 4) && cart_load_path(&e, "build/tests/basura.bin", &err), "basura debe fallar");

    /* Juego cifrado (NGH de Garou): se carga, pero no se puede correr */
    make_program(cpu, PSZ, 0x0253);
    to_chip(g[0].data, cpu + 0x100000, 0x100000);
    to_chip(g[0].data + 0x100000, cpu, 0x100000);
    crcs(g, 1);
    CHECK(!cart_load_parts(&e, g, 9, ROMFMT_ZIP, &err), "cifrado: %s", err);
    CHECK(cart_unsupported(&e, &fatal) && fatal, "un juego cifrado no se puede correr");
    cart_free(&e);
    /* Sin NEO-GEO en $100 (en ningún orden de las mitades) */
    memset(g[0].data, 0, PSZ);
    crcs(g, 1);
    CHECK(!cart_load_parts(&e, g, 9, ROMFMT_ZIP, &err) && !e.p1_swapped, "sin cabecera: %s", err);
    CHECK(cart_unsupported(&e, &fatal) && fatal, "sin NEO-GEO no se puede correr");
    cart_free(&e);

    /* --- Juego 2: P1 de 1 MB + SP2 de 512 KB, V en dos buses (v11/v21), nombres con '_' --- */
    {
        const size_t P1 = 0x100000, P2 = 0x80000;
        u8 *c2 = malloc(P1 + P2);
        make_program(c2, P1 + P2, 0x0998);
        RomPart q[8];
        q[0] = part("juego_p1.rom", P1); to_chip(q[0].data, c2, P1);
        q[1] = part("998-sp2.sp2", P2); to_chip(q[1].data, c2 + P1, P2);
        q[2] = part("998-s1.s1", 0x20000);
        q[3] = part("998-m1.m1", 0x20000);
        q[4] = part("998-v11.v11", 0x40000);
        q[5] = part("998-v21.v21", 0x20000);
        q[6] = part("juego_c1.rom", 0x20000);
        q[7] = part("juego_c2.rom", 0x20000);
        for (int i = 2; i < 8; i++) fill(q[i].data, q[i].size);
        crcs(q, 8);
        static Cart f;
        CHECK(!cart_load_parts(&f, q, 8, ROMFMT_DIR, &err), "juego 2: %s", err);
        CHECK(!f.p1_swapped && f.len[REG_P] == P1 + P2 && f.p_banks == 1, "juego 2: P de %zu KB", f.len[REG_P] / 1024);
        CHECK(cart_p16(&f, 0x100000) == (u16)(c2[0x100000] << 8 | c2[0x100001]), "juego 2: P2 en $200000");
        CHECK(f.len[REG_VA] == 0x40000 && f.len[REG_VB] == 0x20000, "juego 2: buses de V");
        CHECK(!memcmp(f.data + f.off[REG_VB], q[5].data, 0x20000), "juego 2: V-B");
        cart_free(&f);
        for (int i = 0; i < 8; i++) free(q[i].data);
        free(c2);
    }

    /* --- Identidad del código: P1, la ventana P2 con sus bancos, la BIOS con sus espejos --- */
    {
        static Neo n;
        static u8 big[0x500000];                        /* P de 5 MB: P1 de 1 MB y cuatro bancos */
        make_program(big, sizeof big, 0x0997);
        RomPart q[4];
        q[0] = part("997-p1.p1", 0x100000); to_chip(q[0].data, big, 0x100000);
        q[1] = part("997-p2.sp2", 0x400000); to_chip(q[1].data, big + 0x100000, 0x400000);
        q[2] = part("997-m1.m1", 0x10000); fill(q[2].data, q[2].size);
        q[3] = part("997-s1.s1", 0x20000); fill(q[3].data, q[3].size);
        crcs(q, 4);
        CHECK(!cart_load_parts(&n.cart, q, 4, ROMFMT_ZIP, &err), "P de 5 MB: %s", err);
        CHECK(n.cart.p_banks == 4, "P de 5 MB: %d bancos", n.cart.p_banks);
        CHECK(neo_code_offset(&n, 0x000100) == 0x100 && neo_code_offset(&n, 0x0FFFFE) == 0xFFFFE, "P1");
        CHECK(neo_code_offset(&n, 0x200000) == 0x100000, "banco 0");
        n.p2_bank = 2;
        CHECK(neo_code_offset(&n, 0x212344) == 0x312344, "banco 2: $%lX", neo_code_offset(&n, 0x212344));
        CHECK(neo_code_offset(&n, 0xC00402) == (long)BIOS_SPACE + 0x402 && neo_code_offset(&n, 0xC20402) == (long)BIOS_SPACE + 0x402
              && neo_code_offset(&n, 0xCFFFFE) == (long)BIOS_SPACE + 0x1FFFE, "BIOS y sus espejos");
        CHECK(neo_code_offset(&n, 0x100000) < 0 && neo_code_offset(&n, 0x3C0000) < 0 && neo_code_offset(&n, 0x400000) < 0, "RAM y registros no son código");
        CHECK(neo_trace_index(&n, 0x312344) == 0x312344 / 2 && neo_trace_index(&n, (long)BIOS_SPACE + 0x402) == (0x500000 + 0x402) / 2
              && neo_trace_words(&n) == (0x500000 + BIOS_SIZE) / 2, "posiciones en la traza");
        /* Ida y vuelta por una línea de traza (BANCO:DIRECCION, bancos de 1 MB; la BIOS en el FF) */
        static const u32 pcs[] = { 0x000100, 0x212344, 0xC00402, 0xC20402 };
        for (int i = 0; i < 4; i++) {
            long off = neo_code_offset(&n, pcs[i]);
            char line[64];
            FILE *f = fopen("build/tests/traza.txt", "w+");
            trace_line_write(f, TRACE_SHIFT, 6, (unsigned long)off, pcs[i]);
            rewind(f);
            if (!fgets(line, sizeof line, f)) line[0] = 0;
            fclose(f);
            unsigned long o2, a2;
            CHECK(trace_line_read(line, TRACE_SHIFT, &o2, &a2) && o2 == (unsigned long)off && a2 == pcs[i], "traza de $%06X: %s", pcs[i], line);
            if (i == 2) CHECK(!strcmp(line, "FF:C00402\n"), "la BIOS va en el banco FF: %s", line);
            if (i == 1) CHECK(!strcmp(line, "03:212344\n"), "el banco 2 de P2 es el MB 3: %s", line);
        }
        cart_free(&n.cart);
        for (int i = 0; i < 4; i++) free(q[i].data);
    }

    /* --- Tabla L0 y BIOS --- */
    static u8 l0[L0_SIZE];
    l0_generate(l0);
    sha1_hex(l0, L0_SIZE, s);
    CHECK(!strcmp(s, L0_SHA1), "la tabla L0 calculada no es la del chip (SHA1 %s)", s);
    CHECK(l0[0] == 0x88 && l0[1] == 0xFF && l0[255 * 256 + 255] == 0xFF && l0[254 * 256] == 0x00, "tabla L0: valores");
    static Bios bios;
    static u8 falsa[BIOS_SIZE];
    fill(falsa, sizeof falsa);
    write_file("build/tests/falsa.bin", falsa, sizeof falsa);
    CHECK(bios_load(&bios, "build/tests/falsa.bin", l0, &err) && strstr(err, "neo-epo.bin"), "una BIOS desconocida debe fallar y decir cuáles se aceptan");
    write_file("build/tests/falsa.bin", falsa, 1000);
    CHECK(bios_load(&bios, "build/tests/falsa.bin", l0, &err) && strstr(err, "128 KB"), "una BIOS de otro tamaño debe fallar");

    /* --- Base de datos: coherente --- */
    for (int i = 0; i < neodb_count; i++) {
        const NeoDbGame *db = &neodb_games[i];
        int np = 0;
        for (const NeoDbChip *c = db->chips; c->size; c++) np += c->role == PART_P && c->idx == 1;
        CHECK(np == 1 && db->set && db->title, "base de datos: %s", db->set ? db->set : "?");
    }

    cart_free(&a);
    for (int i = 0; i < 9; i++) free(g[i].data);
    free(cpu);
    if (fallas) { printf("carttest: %d fallas\n", fallas); return 1; }
    printf("carttest: todo bien\n");
    return 0;
}
