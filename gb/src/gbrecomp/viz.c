/*
 * Registro de eventos del descubrimiento y su animación en la terminal.
 *
 * El descubrimiento tarda milisegundos, así que no se dibuja mientras ocurre:
 * se anota cada paso y al final se reproduce a la velocidad pedida. La ROM
 * se dibuja como una cuadrícula por banco; cada celda cubre unos bytes y se
 * enciende cuando aparece código en ella. El color dice de dónde vino ese
 * código (arranque, interrupción, tabla de punteros o traza) y el relleno
 * qué tanto de la celda es código.
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE     /* isatty, nanosleep, ioctl (glibc) */
#define _DARWIN_C_SOURCE
#endif
#include "viz.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long std_handle);
__declspec(dllimport) int __stdcall GetConsoleMode(void *h, unsigned long *mode);
__declspec(dllimport) int __stdcall SetConsoleMode(void *h, unsigned long mode);
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
#define IS_TTY() _isatty(_fileno(stdout))
#else
#include <sys/ioctl.h>
#include <unistd.h>
#define IS_TTY() isatty(STDOUT_FILENO)
#endif

#define BANK_SIZE 0x4000

int ev_on;
static Event *evs;
static size_t nev, capev;

void ev_add(int type, int origin, unsigned bank, unsigned addr, unsigned bank2, unsigned addr2,
            int len, int aux)
{
    if (!ev_on) return;
    if (nev == capev) {
        capev = capev ? capev * 2 : 4096;
        evs = realloc(evs, capev * sizeof *evs);
        if (!evs) {
            fprintf(stderr, "gbrecomp: sin memoria para los eventos\n");
            exit(1);
        }
    }
    Event *e = &evs[nev++];
    e->type = (uint8_t)type;
    e->origin = (uint8_t)origin;
    e->len = (uint8_t)len;
    e->aux = (uint8_t)aux;
    e->bank = (uint16_t)bank;
    e->addr = (uint16_t)addr;
    e->bank2 = (uint16_t)bank2;
    e->addr2 = (uint16_t)addr2;
}

/* ------------------------------------------------------------------ */
/* Archivo de eventos (--events)                                       */
/* ------------------------------------------------------------------ */

static const char *const org_tag[ORG_COUNT] = { "arranque", "irq", "tabla", "traza" };
static const char *const phase_tag[] = { "vectores", "tablas", "traza", "traza-tablas" };

void ev_write_file(FILE *f)
{
    fprintf(f, "# gbrecomp eventos 1\n"
               "# fase NOMBRE                       vectores | tablas | traza | traza-tablas\n"
               "# recorrido BB:AAAA ORIGEN [desde|desde-entrada BB:AAAA]\n"
               "#     ORIGEN: arranque | irq | tabla | traza; desde: la instrucción que\n"
               "#     saltó aquí; desde-entrada: la entrada de tabla que apunta aquí\n"
               "# insn BB:AAAA LARGO\n"
               "# tabla BB:AAAA en-linea|fuera-de-linea [desde BB:AAAA]\n"
               "# entrada BB:AAAA -> BB:AAAA       (?? si no se sabe el banco)\n"
               "# sin-banco BB:AAAA -> AAAA        salto al área de bancos sin banco conocido\n"
               "# jp-hl BB:AAAA                    salto indirecto sin tabla reconocida\n"
               "# traza BB:AAAA [conocida]\n"
               "# args BB:AAAA LARGO                argumentos en línea después de una llamada\n");
    for (size_t i = 0; i < nev; i++) {
        const Event *e = &evs[i];
        switch (e->type) {
        case EV_PHASE:
            fprintf(f, "fase %s\n", phase_tag[e->aux]);
            break;
        case EV_WALK:
            fprintf(f, "recorrido %02X:%04X %s", e->bank, e->addr, org_tag[e->origin]);
            if (e->bank2 != EV_NONE)
                fprintf(f, " %s %02X:%04X", e->aux ? "desde-entrada" : "desde", e->bank2, e->addr2);
            fputc('\n', f);
            break;
        case EV_INSN:
            fprintf(f, "insn %02X:%04X %u\n", e->bank, e->addr, e->len);
            break;
        case EV_TABLE:
            fprintf(f, "tabla %02X:%04X %s", e->bank, e->addr, e->aux ? "en-linea" : "fuera-de-linea");
            if (e->bank2 != EV_NONE) fprintf(f, " desde %02X:%04X", e->bank2, e->addr2);
            fputc('\n', f);
            break;
        case EV_TENTRY:
            if (e->bank2 != EV_NONE)
                fprintf(f, "entrada %02X:%04X -> %02X:%04X\n", e->bank, e->addr, e->bank2, e->addr2);
            else
                fprintf(f, "entrada %02X:%04X -> ??:%04X\n", e->bank, e->addr, e->addr2);
            break;
        case EV_UNRESOLVED:
            fprintf(f, "sin-banco %02X:%04X -> %04X\n", e->bank, e->addr, e->addr2);
            break;
        case EV_INDIRECT:
            fprintf(f, "jp-hl %02X:%04X\n", e->bank, e->addr);
            break;
        case EV_TRACE:
            fprintf(f, "traza %02X:%04X%s\n", e->bank, e->addr, e->aux ? " conocida" : "");
            break;
        case EV_ARGS:
            fprintf(f, "args %02X:%04X %u\n", e->bank, e->addr, e->len);
            break;
        }
    }
}

int ev_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    ev_write_file(f);
    return fclose(f) == 0;
}

const Event *ev_list(size_t *n)
{
    *n = nev;
    return evs;
}

/* ------------------------------------------------------------------ */
/* Animación (--viz)                                                   */
/* ------------------------------------------------------------------ */

#define CAT_DATA ORG_COUNT          /* bytes de una tabla de punteros */
#define NCAT (ORG_COUNT + 1)

typedef struct {
    uint16_t n[NCAT];   /* bytes de la celda por categoría */
    uint8_t bad;        /* hay un salto sin resolver o un jp hl sin tabla */
    uint16_t want;      /* direcciones que ejecutó el juego y aún no se descubren */
    int lit;            /* cuadro en que cambió por última vez */
} Cell;

static unsigned v_nbanks, v_bpc, v_cpl, v_lpb, v_cols, v_lw;
static Cell *cells;
static uint8_t *painted;    /* un byte por byte de ROM: ya se contó */
static uint8_t *wanted;     /* un byte por byte de ROM: lo ejecutó el juego y falta */

/* Direcciones de --coverage, hasta que empiece la animación */
static struct { uint16_t bank, addr; } *wants;
static size_t nwants, capwants;

void viz_want(unsigned bank, unsigned addr)
{
    if (nwants == capwants) {
        capwants = capwants ? capwants * 2 : 1024;
        wants = realloc(wants, capwants * sizeof *wants);
        if (!wants) exit(1);
    }
    wants[nwants].bank = (uint16_t)bank;
    wants[nwants].addr = (uint16_t)addr;
    nwants++;
}
static int frame, color;
int viz_force_color;    /* --color: con color aunque la salida no sea una terminal */
static const char *v_title;

/* Estado que se muestra abajo del mapa */
static int cur_phase;
static const Event *last_walk;
static unsigned n_insns, n_code_bytes, n_tables, n_bad, n_want;

static char *buf;
static size_t blen, bcap;

static void put(const char *s)
{
    size_t n = strlen(s);
    if (blen + n + 1 > bcap) {
        bcap = (blen + n + 1) * 2;
        buf = realloc(buf, bcap);
        if (!buf) exit(1);
    }
    memcpy(buf + blen, s, n + 1);
    blen += n;
}

static void putf(const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    put(tmp);
}

static void sgr(const char *code)
{
    if (color) putf("\x1b[%sm", code);
}

static void restore_cursor(int sig)
{
    static const char s[] = "\x1b[0m\x1b[?25h\n";
#ifdef _WIN32
    fputs(s, stdout);
    fflush(stdout);
#else
    if (write(STDOUT_FILENO, s, sizeof s - 1) < 0) { /* nada que hacer */ }
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static void term_size(unsigned *w, unsigned *h)
{
    *w = 100;
    *h = 30;
#ifndef _WIN32
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        *w = ws.ws_col;
        *h = ws.ws_row;
        return;
    }
#endif
    const char *c = getenv("COLUMNS"), *l = getenv("LINES");
    if (c && atoi(c) > 0) *w = (unsigned)atoi(c);
    if (l && atoi(l) > 0) *h = (unsigned)atoi(l);
}

#define HEADER_LINES 1
#define FOOTER_LINES 4

static unsigned map_lines(void)
{
    unsigned rows = (v_nbanks + v_cols - 1) / v_cols;
    return rows * v_lpb + (v_lpb > 1 ? rows - 1 : 0);
}

/* Elige el tamaño de celda más chico con el que el mapa cabe en la terminal;
 * 0 si ni con el más grande cabe (entonces solo se dibuja el resultado) */
static int choose_layout(unsigned w, unsigned h)
{
    static const unsigned sizes[] = { 16, 32, 64, 128, 256, 512, 1024 };
    v_lw = v_nbanks > 0x100 ? 9 : 8;   /* "BB:AAAA " */
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        v_bpc = sizes[i];
        unsigned cpb = BANK_SIZE / v_bpc;
        v_cpl = cpb < 64 ? cpb : 64;
        v_lpb = cpb / v_cpl;
        unsigned block = v_lw + v_cpl;
        v_cols = w >= block ? (w + 3) / (block + 3) : 1;
        if (v_cols > v_nbanks) v_cols = v_nbanks;
        if (HEADER_LINES + map_lines() + FOOTER_LINES < h) return 1;
    }
    return 0;
}

static const char *const cat_color[NCAT] = { "32", "36", "33", "35", "34" };
static const char *const cat_bright[NCAT] = { "92", "96", "93", "95", "94" };
static const char *const org_name[ORG_COUNT] = { "arranque", "interrupción", "tabla", "traza" };
static const char *const phase_name[] = {
    "vectores de arranque e interrupciones", "tablas de punteros",
    "traza: puntos de entrada", "traza: tablas de punteros", "listo"
};
#define PH_DONE 4

static void draw_cell(const Cell *c)
{
    unsigned total = 0, best = 0;
    for (unsigned k = 0; k < NCAT; k++) {
        total += c->n[k];
        if (c->n[k] > c->n[best]) best = k;
    }
    int age = frame - c->lit;
    /* Fondo rojo: el juego ejecutó algo aquí que todavía no se descubre */
    const char *bg = c->want ? ";41" : "";
    char code[16];
    if (c->bad) {
        sgr(age == 0 ? "1;97;41" : c->want ? "1;97;41" : "1;91");
        put("!");
    } else if (total == 0) {
        snprintf(code, sizeof code, "%s%s", c->want ? "97" : "90", bg);
        sgr(code);
        put(c->want ? "×" : "·");
    } else {
        snprintf(code, sizeof code, "%s%s", age == 0 ? "1;97" : age < 4 ? cat_bright[best] : cat_color[best], bg);
        sgr(code);
        if (best == CAT_DATA) put("≡");
        else if (total * 4 <= v_bpc) put("░");
        else if (total * 2 <= v_bpc) put("▒");
        else if (total * 4 <= v_bpc * 3) put("▓");
        else put("█");
    }
    sgr("0");
}

static void render(void)
{
    blen = 0;
    if (buf) buf[0] = 0;

    putf("gbrecomp · %s · %u bancos · 1 celda = %u bytes", v_title[0] ? v_title : "(sin título)",
         v_nbanks, v_bpc);
    sgr("90");
    putf("   fase: %s", phase_name[cur_phase]);
    sgr("0");
    put("\x1b[K\n");

    unsigned rows = (v_nbanks + v_cols - 1) / v_cols;
    for (unsigned r = 0; r < rows; r++) {
        if (r > 0 && v_lpb > 1) put("\x1b[K\n");
        for (unsigned l = 0; l < v_lpb; l++) {
            for (unsigned col = 0; col < v_cols; col++) {
                unsigned bank = r * v_cols + col;
                if (bank >= v_nbanks) break;
                if (col > 0) put("   ");
                unsigned addr = (bank == 0 ? 0 : 0x4000) + l * v_cpl * v_bpc;
                sgr("90");
                putf(v_lw == 9 ? "%03X:%04X " : "%02X:%04X ", bank, addr);
                sgr("0");
                const Cell *c = &cells[(size_t)bank * (BANK_SIZE / v_bpc) + l * v_cpl];
                for (unsigned k = 0; k < v_cpl; k++) draw_cell(&c[k]);
            }
            put("\x1b[K\n");
        }
    }

    put("\x1b[K\n");
    for (unsigned k = 0; k < ORG_COUNT; k++) {
        sgr(cat_color[k]);
        put("█");
        sgr("0");
        putf(" %s  ", org_name[k]);
    }
    sgr(cat_color[CAT_DATA]);
    put("≡");
    sgr("0");
    put(" tabla o argumentos (datos)  ");
    sgr("91");
    put("!");
    sgr("0");
    put(" sin resolver  ");
    if (nwants) {
        sgr("97;41");
        put("×");
        sgr("0");
        put(" ejecutado sin descubrir  ");
    }
    put(" ░▒▓█ densidad\x1b[K\n");

    if (last_walk) {
        putf("▶ recorrido %02X:%04X", last_walk->bank, last_walk->addr);
        if (last_walk->bank2 != EV_NONE)
            putf(" ← %02X:%04X", last_walk->bank2, last_walk->addr2);
        putf(" (%s)", org_name[last_walk->origin]);
    }
    put("\x1b[K\n");
    putf("instrucciones %u · código %.2f %% de la ROM · tablas %u · sin resolver %u",
         n_insns, 100.0 * n_code_bytes / ((double)v_nbanks * BANK_SIZE), n_tables, n_bad);
    if (nwants) putf(" · ejecutado sin descubrir %u", n_want);
    put("\x1b[K\n");

    if (!color) {
        /* Sin terminal: quitar los "borrar hasta el fin de línea" */
        char *w = buf;
        for (const char *p = buf; *p; ) {
            if (!strncmp(p, "\x1b[K", 3)) p += 3;
            else *w++ = *p++;
        }
        *w = 0;
        blen = (size_t)(w - buf);
    }
}

/* Celda de una dirección de ROM (NULL si no está en el banco); idx: el byte */
static Cell *cell_at(unsigned bank, unsigned addr, size_t *idx)
{
    unsigned base = bank == 0 ? 0 : 0x4000;
    if (bank >= v_nbanks || addr < base || addr >= base + BANK_SIZE) return NULL;
    *idx = (size_t)bank * BANK_SIZE + (addr - base);
    return &cells[*idx / v_bpc];
}

static void paint(unsigned bank, unsigned addr, unsigned len, int cat)
{
    if (bank >= v_nbanks) return;
    unsigned base = bank == 0 ? 0 : 0x4000;
    for (unsigned i = 0; i < len; i++) {
        unsigned a = addr + i;
        if (a < base || a >= base + BANK_SIZE) return;
        size_t idx = (size_t)bank * BANK_SIZE + (a - base);
        if (painted[idx]) continue;
        painted[idx] = 1;
        Cell *c = &cells[idx / v_bpc];
        c->n[cat]++;
        c->lit = frame;
        if (cat != CAT_DATA) n_code_bytes++;
    }
}

static void mark_bad(unsigned bank, unsigned addr)
{
    size_t idx;
    Cell *c = cell_at(bank, addr, &idx);
    if (!c) return;
    c->bad = 1;
    c->lit = frame;
    n_bad++;
}

/* Una instrucción empieza en una dirección que ejecutó el juego */
static void unwant(unsigned bank, unsigned addr)
{
    size_t idx;
    Cell *c = cell_at(bank, addr, &idx);
    if (!c || !wanted[idx]) return;
    wanted[idx] = 0;
    c->want--;
    c->lit = frame;
    n_want--;
}

/* Aplica un evento; 1 si es un cambio de fase (para hacer una pausa) */
static int apply(const Event *e, int *walk_origin)
{
    switch (e->type) {
    case EV_PHASE:
        cur_phase = e->aux;
        return 1;
    case EV_WALK:
        last_walk = e;
        *walk_origin = e->origin;
        break;
    case EV_INSN:
        n_insns++;
        paint(e->bank, e->addr, e->len, *walk_origin);
        unwant(e->bank, e->addr);
        break;
    case EV_TABLE:
        n_tables++;
        break;
    case EV_TENTRY:
        paint(e->bank, e->addr, 2, CAT_DATA);
        break;
    case EV_ARGS:
        paint(e->bank, e->addr, e->len, CAT_DATA);
        break;
    case EV_UNRESOLVED:
    case EV_INDIRECT:
        mark_bad(e->bank, e->addr);
        break;
    }
    return 0;
}

void viz_play(unsigned nbanks, const char *title, double seconds)
{
    v_nbanks = nbanks;
    v_title = title;
    int tty = IS_TTY();
    color = viz_force_color || (tty && !getenv("NO_COLOR"));     /* --color gana a NO_COLOR */
#ifdef _WIN32
    if (tty && color) {
        void *h = GetStdHandle((unsigned long)-11);   /* STD_OUTPUT_HANDLE */
        unsigned long mode;
        if (!GetConsoleMode(h, &mode) || !SetConsoleMode(h, mode | 0x0004))  /* VT */
            color = 0;
    }
#endif
    unsigned w, h;
    term_size(&w, &h);
    if (!tty) h = 60;       /* a un archivo: sin límite real de alto */
    int fits = choose_layout(w, h);
    int animate = fits && color && tty && seconds > 0;
    if (!fits) {
        /* No cabe: celdas de 1 KB, una columna (habrá que desplazarse) */
        v_bpc = 1024;
        v_cpl = BANK_SIZE / v_bpc;
        v_lpb = 1;
        v_cols = 1;
    }

    cells = calloc((size_t)nbanks * (BANK_SIZE / v_bpc), sizeof *cells);
    painted = calloc((size_t)nbanks * BANK_SIZE, 1);
    wanted = calloc((size_t)nbanks * BANK_SIZE, 1);
    if (!cells || !painted || !wanted) return;
    for (size_t i = 0; i < (size_t)nbanks * (BANK_SIZE / v_bpc); i++) cells[i].lit = -1000;
    for (size_t i = 0; i < nwants; i++) {
        size_t idx;
        Cell *c = cell_at(wants[i].bank, wants[i].addr, &idx);
        if (!c || wanted[idx]) continue;
        wanted[idx] = 1;
        c->want++;
        n_want++;
    }

    int walk_origin = ORG_BOOT;
    if (animate) {
        const unsigned fps = 30;
        size_t nframes = (size_t)(seconds * fps);
        size_t per_frame = nframes ? (nev + nframes - 1) / nframes : nev;
        if (per_frame == 0) per_frame = 1;
        unsigned lines = HEADER_LINES + map_lines() + FOOTER_LINES;

        signal(SIGINT, restore_cursor);
        fputs("\x1b[?25l", stdout);
        render();
        fwrite(buf, 1, blen, stdout);
        fflush(stdout);
        size_t since = 0;
        for (size_t i = 0; i < nev; i++) {
            int phase_change = apply(&evs[i], &walk_origin);
            if (++since < per_frame && !phase_change && i + 1 < nev) continue;
            since = 0;
            render();
            printf("\x1b[%uA", lines);
            fwrite(buf, 1, blen, stdout);
            fflush(stdout);
            frame++;
            sleep_ms(phase_change ? 400 : 1000 / fps);
        }
        frame += 1000;      /* el cuadro final, sin celdas encendidas */
        cur_phase = PH_DONE;
        render();
        printf("\x1b[%uA", lines);
        fwrite(buf, 1, blen, stdout);
        fputs("\x1b[?25h", stdout);
        signal(SIGINT, SIG_DFL);
    } else {
        for (size_t i = 0; i < nev; i++) apply(&evs[i], &walk_origin);
        frame = 1000;
        cur_phase = PH_DONE;
        render();
        fwrite(buf, 1, blen, stdout);
    }
    putchar('\n');
    fflush(stdout);
    free(cells);
    free(painted);
    free(wanted);
}
