/*
 * Corre las SingleStepTests del 68000 (github.com/SingleStepTests/680x0, `make testroms`):
 * cada prueba da el estado del CPU, la cola de prefetch y la RAM antes y después de una
 * instrucción, y cuántos ciclos tarda. Se compara todo.
 *
 *   gunzip -c testroms/680x0/68000/v1/ADD.w.json.gz | ./m68ktest - --name ADD.w [-v N] [--bus]
 *   ./m68ktest archivo.json
 *
 * Imprime "NOMBRE: pasadas/total" y, con -v N, el detalle de las primeras N que fallan. --bus
 * también compara la secuencia de accesos al bus (dirección, tamaño, lectura/escritura, valor).
 * --recomp ejecuta cada instrucción como lo hará el código generado: la función k_* de
 * m68k_ops.h con las palabras de extensión como constantes, sin cola de prefetch (ir/irc se
 * rehacen de la memoria al terminar, como al salir de una función generada).
 * Sale con 1 si alguna falla. Las pruebas con datos malos conocidos se omiten.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "m68k_ops.h"
#include "console.h"

/* Datos malos conocidos: github.com/SingleStepTests/680x0/issues/4 */
static const char *const known_bad[] = { "e502 [ASL.b Q, D2] 1583", "e502 [ASL.b Q, D2] 1761" };

/* Las funciones k_* como las llamará el código generado (aquí con ext no constante) */
typedef u32 (*KindFn)(Neo *, u16, const u16 *, u32);
#define R(name) static u32 r_##name(Neo *g, u16 op, const u16 *e, u32 pc) { return k_##name(g, op, e, pc); }
R(ILLEGAL) R(LINEA) R(LINEF) R(IMM) R(ICCR) R(ISR) R(BIT) R(MOVEP) R(MOVE) R(MOVEA) R(UNARY) R(MOVEFSR)
R(MOVETCCR) R(MOVETSR) R(CHK) R(LEA) R(NBCD) R(SWAP) R(PEA) R(EXT) R(MOVEM) R(MOVEMR) R(TST) R(TAS) R(TRAP)
R(LINK) R(UNLK) R(MUSP) R(RESET) R(NOP) R(STOP) R(RTE) R(RTS) R(TRAPV) R(RTR) R(JSR) R(JMP) R(QUICK) R(SCC)
R(DBCC) R(BCC) R(MOVEQ) R(ALU) R(ALUA) R(MUL) R(DIV) R(BCD) R(ADDX) R(CMPM) R(EXG) R(SHIFTR) R(SHIFTM)
#undef R
static const KindFn recomp_fn[K_COUNT] = {
#define F(name) [K_##name] = r_##name
    F(ILLEGAL), F(LINEA), F(LINEF), F(IMM), F(ICCR), F(ISR), F(BIT), F(MOVEP), F(MOVE), F(MOVEA), F(UNARY),
    F(MOVEFSR), F(MOVETCCR), F(MOVETSR), F(CHK), F(LEA), F(NBCD), F(SWAP), F(PEA), F(EXT), F(MOVEM),
    F(MOVEMR), F(TST), F(TAS), F(TRAP), F(LINK), F(UNLK), F(MUSP), F(RESET), F(NOP), F(STOP), F(RTE),
    F(RTS), F(TRAPV), F(RTR), F(JSR), F(JMP), F(QUICK), F(SCC), F(DBCC), F(BCC), F(MOVEQ), F(ALU),
    F(ALUA), F(MUL), F(DIV), F(BCD), F(ADDX), F(CMPM), F(EXG), F(SHIFTR), F(SHIFTM),
#undef F
};

#include "json.h"

/* --- Bus de prueba: 16 MB planos, con registro de accesos --- */

static u8 mem[1 << 24];
static u32 touched[4096]; static int ntouched;
typedef struct { char rw; u32 addr; int word; u32 val; } Access;
static Access acc[512]; static int nacc;

static void log_acc(char rw, u32 a, int word, u32 v) { if (nacc < 512) acc[nacc++] = (Access){ rw, a, word, v }; }

uint16_t m68k_fetch(Neo *g, uint32_t a) { g->cyc += 4; u16 v = (u16)(mem[a] << 8 | mem[(a + 1) & 0xFFFFFF]); log_acc('r', a, 1, v); return v; }
uint16_t m68k_rd16(Neo *g, uint32_t a) { g->cyc += 4; u16 v = (u16)(mem[a] << 8 | mem[(a + 1) & 0xFFFFFF]); log_acc('r', a, 1, v); return v; }
uint8_t  m68k_rd8(Neo *g, uint32_t a)  { g->cyc += 4; log_acc('r', a, 0, mem[a]); return mem[a]; }
void m68k_wr16(Neo *g, uint32_t a, uint16_t v)
{
    g->cyc += 4; log_acc('w', a, 1, v);
    mem[a] = (u8)(v >> 8); mem[(a + 1) & 0xFFFFFF] = (u8)v;
    if (ntouched < 4094) { touched[ntouched++] = a; touched[ntouched++] = (a + 1) & 0xFFFFFF; }
}
void m68k_wr8(Neo *g, uint32_t a, uint8_t v)
{
    g->cyc += 4; log_acc(g->cpu.tas ? 't' : 'w', a, 0, v);
    mem[a] = v;
    if (ntouched < 4095) touched[ntouched++] = a;
}
uint16_t m68k_peek16(Neo *g, uint32_t a) { (void)g; return (u16)(mem[a] << 8 | mem[(a + 1) & 0xFFFFFF]); }
void m68k_reset_line(Neo *g) { (void)g; }
void m68k_int_ack(Neo *g, int level) { (void)g; (void)level; }

/* --- Una prueba --- */

static const char *rn[] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6" };

static void load_state(Neo *g, int st)
{
    M68k *c = &g->cpu;
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 8; i++) c->d[i] = (u32)num(st, rn[i]);
    for (int i = 0; i < 7; i++) c->a[i] = (u32)num(st, rn[8 + i]);
    u16 sr = (u16)num(st, "sr");
    c->s = sr >> 13 & 1;
    c->a[7] = (u32)num(st, c->s ? "ssp" : "usp");
    c->osp = (u32)num(st, c->s ? "usp" : "ssp");
    m68k_set_sr(c, sr);
    c->pc = (u32)num(st, "pc");
    int pf = get(st, "prefetch");
    c->ir = (u16)nodes[child(pf, 0)].num;
    c->irc = (u16)nodes[child(pf, 1)].num;
    int ram = get(st, "ram");
    for (int k = nodes[ram].first; k >= 0; k = nodes[k].next) {
        u32 a = (u32)nodes[child(k, 0)].num & 0xFFFFFF;
        mem[a] = (u8)nodes[child(k, 1)].num;
        if (ntouched < 4095) touched[ntouched++] = a;
    }
}

static int verbose, check_bus, recomp;
static int div_overflow_nz;     /* casos de DIVU/DIVS con desborde: N y Z discrepan a propósito */
static char why[2048];

#define DIFF(...) do { size_t l = strlen(why); snprintf(why + l, sizeof why - l, __VA_ARGS__); bad = 1; } while (0)

static int run_one(Neo *g, int t)
{
    int ini = get(t, "initial"), fi = get(t, "final");
    ntouched = 0; nacc = 0; why[0] = 0;
    load_state(g, ini);
    g->cyc = 0;
    M68k *c = &g->cpu;
    if (!recomp) m68k_step(g);
    else {
        /* Como el código generado: las palabras de extensión son constantes (la primera es irc; las
         * demás, lo que hay en memoria después) y al terminar ir/irc se leen de la memoria */
        u16 op = c->ir, e[8];
        /* En una ROM, el opcode y la palabra siguiente están en memoria (la prueba solo los da en
         * la cola de prefetch): se ponen ahí, salvo que la prueba diga otra cosa de esas direcciones */
        u32 p0 = c->pc & 0xFFFFFF;
        u16 w[2] = { c->ir, c->irc };
        for (int i = 0; i < 4; i++) {
            u32 a = (p0 + (u32)i) & 0xFFFFFF;
            int given = 0;
            for (int k = 0; k < ntouched; k++) if (touched[k] == a) given = 1;
            if (!given) { mem[a] = (u8)(w[i / 2] >> (i & 1 ? 0 : 8)); if (ntouched < 4095) touched[ntouched++] = a; }
        }
        e[0] = c->irc;
        for (int i = 1; i < 8; i++) e[i] = m68k_peek16(g, (c->pc + 2 + 2 * (u32)i) & 0xFFFFFF);
        jmp_buf jb;
        g->jb = &jb;
        if (setjmp(jb) == 0) {
            u32 npc = recomp_fn[m68k_decode(op)](g, op, e, c->pc);
            c->pc = npc;
            c->ir = m68k_peek16(g, npc & 0xFFFFFF);
            c->irc = m68k_peek16(g, (npc + 2) & 0xFFFFFF);
        }
        g->jb = NULL;
    }
    int bad = 0;
    for (int i = 0; i < 8; i++) if (c->d[i] != (u32)num(fi, rn[i])) DIFF(" %s=%08X(esperado %08X)", rn[i], c->d[i], (u32)num(fi, rn[i]));
    for (int i = 0; i < 7; i++) if (c->a[i] != (u32)num(fi, rn[8 + i])) DIFF(" %s=%08X(esperado %08X)", rn[8 + i], c->a[i], (u32)num(fi, rn[8 + i]));
    u32 usp = c->s ? c->osp : c->a[7], ssp = c->s ? c->a[7] : c->osp;
    if (usp != (u32)num(fi, "usp")) DIFF(" usp=%08X(esperado %08X)", usp, (u32)num(fi, "usp"));
    if (ssp != (u32)num(fi, "ssp")) DIFF(" ssp=%08X(esperado %08X)", ssp, (u32)num(fi, "ssp"));
    u16 sr = m68k_get_sr(c), esr = (u16)num(fi, "sr");
    /* DIVU/DIVS con desborde: aquí N = 1 y Z = 0 (lo que hace el hardware: ver k_DIV); las pruebas los
     * dejan sin cambiar. Se compara el resto del SR y se cuenta aparte. */
    u16 op0 = (u16)nodes[child(get(ini, "prefetch"), 0)].num;
    if (m68k_decode(op0) == K_DIV && (esr & 0x02) && !(esr & 0x01) && ((sr ^ esr) & ~0x0C) == 0 && sr != esr) {
        div_overflow_nz++;
        esr = sr;
    }
    if (sr != esr) DIFF(" sr=%04X(esperado %04X)", sr, esr);
    if (c->pc != (u32)num(fi, "pc")) DIFF(" pc=%06X(esperado %06X)", c->pc, (u32)num(fi, "pc"));
    int pf = get(fi, "prefetch");
    /* Código que se modifica a sí mismo: si la instrucción escribió sobre su propia cola de prefetch,
     * el CPU sigue con lo que ya había leído, pero el código generado (que solo sale de la ROM, donde
     * esto no pasa) relee la memoria. En --recomp no se compara el prefetch en ese caso. */
    int smc = 0;
    for (int i = 0; recomp && i < nacc; i++)
        if (acc[i].rw != 'r' && ((acc[i].addr - c->pc) & 0xFFFFFF) < 4) smc = 1;
    if (!smc && (c->ir != (u16)nodes[child(pf, 0)].num || c->irc != (u16)nodes[child(pf, 1)].num))
        DIFF(" prefetch=%04X,%04X(esperado %04X,%04X)", c->ir, c->irc, (u16)nodes[child(pf, 0)].num, (u16)nodes[child(pf, 1)].num);
    int ram = get(fi, "ram");
    for (int k = nodes[ram].first; k >= 0; k = nodes[k].next) {
        u32 a = (u32)nodes[child(k, 0)].num & 0xFFFFFF;
        u8 v = (u8)nodes[child(k, 1)].num;
        if (mem[a] != v) DIFF(" [%06X]=%02X(esperado %02X)", a, mem[a], v);
    }
    long long len = num(t, "length");
    if ((long long)g->cyc != len) DIFF(" ciclos=%llu(esperado %lld)", (unsigned long long)g->cyc, len);
    if (check_bus) {
        int tr = get(t, "transactions"), i = 0;
        for (int k = nodes[tr].first; k >= 0; k = nodes[k].next) {
            int kind = child(k, 0);
            if (nodes[kind].s[0] == 'n') continue;
            u32 a = (u32)nodes[child(k, 3)].num, v = (u32)nodes[child(k, 5)].num;
            int word = nodes[child(k, 4)].s[1] == 'w';
            char rw = nodes[kind].s[0];
            if (i >= nacc || acc[i].rw != rw || acc[i].addr != a || acc[i].word != word || acc[i].val != v) {
                DIFF(" acceso %d: %c%s %06X=%X (esperado %c%s %06X=%X)", i,
                     i < nacc ? acc[i].rw : '-', i < nacc && acc[i].word ? ".w" : ".b", i < nacc ? acc[i].addr : 0, i < nacc ? acc[i].val : 0,
                     rw, word ? ".w" : ".b", a, v);
                break;
            }
            i++;
        }
        if (!bad && i != nacc) DIFF(" %d accesos de más", nacc - i);
    }
    for (int i = 0; i < ntouched; i++) mem[touched[i]] = 0;
    return !bad;
}

int main(int argc, char **argv)
{
    console_utf8();
    const char *path = NULL, *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) verbose = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--bus")) check_bus = 1;
        else if (!strcmp(argv[i], "--recomp")) recomp = 1;
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "uso: m68ktest archivo.json | - [--name NOMBRE] [-v N] [--bus] [--recomp]\n"
                        "  (las pruebas vienen comprimidas: gunzip -c X.json.gz | m68ktest -)\n");
        return 2;
    }
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) { fprintf(stderr, "no se pudo abrir %s\n", path); return 2; }
    size_t len;
    char *text = read_all(f, &len);
    if (f != stdin) fclose(f);

    Neo *g = calloc(1, sizeof *g);
    int total = 0, pass = 0, shown = 0, skipped = 0;
    jp = text; ws();
    if (*jp++ != '[') { fprintf(stderr, "%s: no es una lista de pruebas\n", path); return 2; }
    for (;;) {
        ws();
        if (*jp == ']' || !*jp) break;
        nn = 0;
        int t = parse_value();
        if (t < 0) { fprintf(stderr, "%s: JSON inválido cerca de la prueba %d\n", path, total + 1); return 2; }
        int nm = get(t, "name");
        int skip = 0;
        for (size_t k = 0; nm >= 0 && k < sizeof known_bad / sizeof known_bad[0]; k++)
            if ((size_t)nodes[nm].slen == strlen(known_bad[k]) && !memcmp(nodes[nm].s, known_bad[k], strlen(known_bad[k]))) skip = 1;
        ws();
        if (skip) { skipped++; if (*jp == ',') jp++; continue; }
        total++;
        if (run_one(g, t)) pass++;
        else if (shown < verbose) {
            shown++;
            printf("  FALLA %.*s:%s\n", nm >= 0 ? nodes[nm].slen : 0, nm >= 0 ? nodes[nm].s : "", why);
        }
        ws();
        if (*jp == ',') jp++;
    }
    if (!name) {                    /* el archivo sin carpeta ni extensión */
        name = path;
        for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    }
    printf("%-14.*s %5d/%d%s", (int)(strstr(name, ".json") ? strstr(name, ".json") - name : (long)strlen(name)), name,
           pass, total, pass == total ? "" : "  <--");
    if (skipped) printf("  (%d omitidas: datos malos conocidos)", skipped);
    if (div_overflow_nz) printf("  (%d con desborde: N y Z como el hardware)", div_overflow_nz);
    printf("\n");
    free(text);
    free(g);
    return pass != total;
}
