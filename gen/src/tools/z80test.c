/*
 * Corre las SingleStepTests del Z80 (github.com/SingleStepTests/z80, `make testroms`): cada prueba
 * da los registros (con los internos WZ, Q y P), la RAM y los puertos antes y después de una
 * instrucción, y sus ciclos T. Se compara todo.
 *
 *   gunzip -c testroms/z80/v1/ed b0.json.gz | ./z80test - --name "ed b0" [-v N]
 *
 * Imprime "NOMBRE: pasadas/total" y, con -v N, el detalle de las primeras N que fallan.
 *
 *   ./z80test --int
 *
 * Prueba la interrupción desde un HALT (las SingleStepTests no tienen interrupciones): en modo 1
 * y en modo 2 se apila la instrucción que sigue al HALT, y RETI vuelve a ella (GENRecomp#15).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80.h"
#include "console.h"
#include "json.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* --- Bus de prueba: 64 KB y los puertos de la prueba --- */

static u8 mem[65536];
static int touched[4096], ntouched;
static int port_test = -1;          /* nodo "ports" de la prueba actual */
static int out_port = -1, out_val = -1;

uint8_t z80_read(struct Gen *g, uint16_t a) { (void)g; return mem[a]; }
void z80_write(struct Gen *g, uint16_t a, uint8_t v) { (void)g; mem[a] = v; if (ntouched < 4096) touched[ntouched++] = a; }
uint8_t z80_in(struct Gen *g, uint16_t port)
{
    (void)g;
    if (port_test >= 0)
        for (int k = nodes[port_test].first; k >= 0; k = nodes[k].next)
            if ((u16)nodes[child(k, 0)].num == port && nodes[child(k, 2)].s[0] == 'r') return (u8)nodes[child(k, 1)].num;
    return 0xFF;
}
void z80_out(struct Gen *g, uint16_t port, uint8_t v) { (void)g; out_port = port; out_val = v; }

static int verbose;
static char why[2048];
#define DIFF(...) do { size_t l = strlen(why); snprintf(why + l, sizeof why - l, __VA_ARGS__); bad = 1; } while (0)

static void load(Z80 *z, int st)
{
    memset(z, 0, sizeof *z);
    z->a = (u8)num(st, "a"); z->f = (u8)num(st, "f");
    z->b = (u8)num(st, "b"); z->c = (u8)num(st, "c");
    z->d = (u8)num(st, "d"); z->e = (u8)num(st, "e");
    z->h = (u8)num(st, "h"); z->l = (u8)num(st, "l");
    z->af_ = (u16)num(st, "af_"); z->bc_ = (u16)num(st, "bc_");
    z->de_ = (u16)num(st, "de_"); z->hl_ = (u16)num(st, "hl_");
    z->ix = (u16)num(st, "ix"); z->iy = (u16)num(st, "iy");
    z->sp = (u16)num(st, "sp"); z->pc = (u16)num(st, "pc");
    z->wz = (u16)num(st, "wz");
    z->i = (u8)num(st, "i"); z->r = (u8)num(st, "r");
    z->iff1 = (u8)num(st, "iff1"); z->iff2 = (u8)num(st, "iff2");
    z->im = (u8)num(st, "im"); z->ei = (u8)num(st, "ei");
    z->q = (u8)num(st, "q"); z->p = (u8)num(st, "p");
    int ram = get(st, "ram");
    for (int k = nodes[ram].first; k >= 0; k = nodes[k].next) {
        u16 a = (u16)nodes[child(k, 0)].num;
        mem[a] = (u8)nodes[child(k, 1)].num;
        if (ntouched < 4096) touched[ntouched++] = a;
    }
}

static int run_one(int t)
{
    static Z80 z;
    int ini = get(t, "initial"), fi = get(t, "final");
    ntouched = 0; why[0] = 0;
    port_test = get(t, "ports");
    out_port = out_val = -1;
    load(&z, ini);
    int cyc = z80_step(NULL, &z);
    int bad = 0;
#define R8(f, k) if (z.f != (u8)num(fi, k)) DIFF(" %s=%02X(esperado %02X)", k, z.f, (u8)num(fi, k))
#define R16(f, k) if (z.f != (u16)num(fi, k)) DIFF(" %s=%04X(esperado %04X)", k, z.f, (u16)num(fi, k))
    R8(a, "a"); R8(f, "f"); R8(b, "b"); R8(c, "c"); R8(d, "d"); R8(e, "e"); R8(h, "h"); R8(l, "l");
    R16(af_, "af_"); R16(bc_, "bc_"); R16(de_, "de_"); R16(hl_, "hl_");
    R16(ix, "ix"); R16(iy, "iy"); R16(sp, "sp"); R16(pc, "pc"); R16(wz, "wz");
    R8(i, "i"); R8(r, "r"); R8(iff1, "iff1"); R8(iff2, "iff2"); R8(im, "im"); R8(ei, "ei");
    R8(q, "q"); R8(p, "p");
    int ram = get(fi, "ram");
    for (int k = nodes[ram].first; k >= 0; k = nodes[k].next) {
        u16 a = (u16)nodes[child(k, 0)].num;
        u8 v = (u8)nodes[child(k, 1)].num;
        if (mem[a] != v) DIFF(" [%04X]=%02X(esperado %02X)", a, mem[a], v);
    }
    int ncyc = 0, cy = get(t, "cycles");
    for (int k = nodes[cy].first; k >= 0; k = nodes[k].next) ncyc++;
    if (cyc != ncyc) DIFF(" ciclos=%d(esperado %d)", cyc, ncyc);
    if (port_test >= 0)
        for (int k = nodes[port_test].first; k >= 0; k = nodes[k].next)
            if (nodes[child(k, 2)].s[0] == 'w' && (out_port != (int)nodes[child(k, 0)].num || out_val != (int)nodes[child(k, 1)].num))
                DIFF(" OUT %04X=%02X(esperado %04X=%02X)", out_port & 0xFFFF, out_val & 0xFF,
                     (int)nodes[child(k, 0)].num, (int)nodes[child(k, 1)].num);
    for (int i = 0; i < ntouched; i++) mem[touched[i]] = 0;
    return !bad;
}

/* --- Interrupción desde un HALT --- */

static int int_test(void)
{
    static const u8 prog[] = { 0xFB, 0xED, 0x56, 0x76, 0x00, 0x00 };     /* EI; IM 1; HALT; NOP; NOP */
    memset(mem, 0, sizeof mem);
    memcpy(mem, prog, sizeof prog);
    mem[0x38] = 0xFB; mem[0x39] = 0xED; mem[0x3A] = 0x4D;               /* EI; RETI */
    mem[0x12FF] = 0x38; mem[0x1300] = 0x00;                             /* vector del modo 2: $0038 */
    int fallas = 0;
    #define T(c, ...) do { if (!(c)) { printf("FALLA INT: " __VA_ARGS__); printf("\n"); fallas++; } } while (0)
    for (int im = 1; im <= 2; im++) {
        Z80 z;
        z80_reset(&z);
        if (im == 2) { mem[2] = 0x5E; z.i = 0x12; }                     /* IM 2 en vez de IM 1 */
        z80_step(NULL, &z); z80_step(NULL, &z); z80_step(NULL, &z);   /* EI, IM, HALT */
        T(z.halted && z.iff1 && z.im == im, "modo %d: no quedó en HALT con EI", im);
        z80_step(NULL, &z);
        T(z.halted && z.pc == 0x0004, "modo %d: HALT terminó solo o el PC no quedó en $0004", im);
        z.irq = 1;
        int t = z80_step(NULL, &z);
        T(t == (im == 2 ? 19 : 13) && z.pc == 0x38 && !z.halted && z.iff1 == 0,
          "modo %d: %d ciclos, PC $%04X", im, t, z.pc);
        T(z.sp == 0xFFFD && mem[0xFFFD] == 0x04 && mem[0xFFFE] == 0x00,
          "modo %d: apiló $%02X%02X (esperado $0004, la instrucción que sigue al HALT)", im, mem[0xFFFE], mem[0xFFFD]);
        z.irq = 0;
        z80_step(NULL, &z); z80_step(NULL, &z);                         /* EI; RETI */
        T(z.pc == 0x0004 && z.iff1 == 1 && z.sp == 0xFFFF, "modo %d: RETI volvió a $%04X", im, z.pc);
    }
    mem[2] = 0x56;
    #undef T
    printf("INT desde HALT: %s\n", fallas ? "con fallas" : "bien");
    return fallas != 0;
}

int main(int argc, char **argv)
{
    console_utf8();
    if (argc == 2 && !strcmp(argv[1], "--int")) return int_test();
    const char *path = NULL, *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) verbose = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "uso: z80test archivo.json | - [--name NOMBRE] [-v N]\n"
                        "  (las pruebas vienen comprimidas: gunzip -c X.json.gz | z80test -)\n"
                        "     z80test --int   (la interrupción desde un HALT, modos 1 y 2)\n");
        return 2;
    }
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) { fprintf(stderr, "no se pudo abrir %s\n", path); return 2; }
    size_t len;
    char *text = read_all(f, &len);
    if (f != stdin) fclose(f);

    int total = 0, pass = 0, shown = 0;
    jp = text; ws();
    if (*jp++ != '[') { fprintf(stderr, "%s: no es una lista de pruebas\n", path); return 2; }
    for (;;) {
        ws();
        if (*jp == ']' || !*jp) break;
        nn = 0;
        int t = parse_value();
        if (t < 0) { fprintf(stderr, "%s: JSON inválido cerca de la prueba %d\n", path, total + 1); return 2; }
        int nm = get(t, "name");
        total++;
        if (run_one(t)) pass++;
        else if (shown < verbose) {
            shown++;
            printf("  FALLA %.*s:%s\n", nm >= 0 ? nodes[nm].slen : 0, nm >= 0 ? nodes[nm].s : "", why);
        }
        ws();
        if (*jp == ',') jp++;
    }
    if (!name) {
        name = path;
        for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    }
    printf("%-14.*s %5d/%d%s\n", (int)(strstr(name, ".json") ? strstr(name, ".json") - name : (long)strlen(name)), name,
           pass, total, pass == total ? "" : "  <--");
    free(text);
    return pass != total;
}
