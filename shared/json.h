/*
 * JSON mínimo para las SingleStepTests (m68ktest, z80test): un árbol por prueba, con nodos en un
 * arreglo que se reusa. Solo lo que usan esas pruebas: objetos, listas, enteros, textos y null.
 */
#ifndef TOOLS_JSON_H
#define TOOLS_JSON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int type; long long num; const char *s; int slen; const char *key; int klen; int first, next; } Node;
static Node *nodes; static int nn, ncap;
static const char *jp;

static void ws(void) { while (*jp == ' ' || *jp == '\n' || *jp == '\r' || *jp == '\t') jp++; }

static int newnode(int type)
{
    if (nn == ncap) { ncap = ncap ? ncap * 2 : 4096; nodes = realloc(nodes, (size_t)ncap * sizeof *nodes); }
    memset(&nodes[nn], 0, sizeof nodes[nn]);
    nodes[nn].type = type; nodes[nn].first = nodes[nn].next = -1;
    return nn++;
}

static int parse_string(const char **s, int *len)
{
    if (*jp != '"') return 0;
    *s = ++jp;
    while (*jp && *jp != '"') { if (*jp == '\\' && jp[1]) jp++; jp++; }
    if (*jp != '"') return 0;
    *len = (int)(jp - *s);
    jp++;
    return 1;
}

static int parse_value(void)
{
    ws();
    if (*jp == '{' || *jp == '[') {
        int obj = *jp == '{', n = newnode(obj ? 'o' : 'a'), last = -1;
        jp++; ws();
        if (*jp == (obj ? '}' : ']')) { jp++; return n; }
        for (;;) {
            const char *k = NULL; int kl = 0;
            if (obj) { ws(); if (!parse_string(&k, &kl)) return -1; ws(); if (*jp++ != ':') return -1; }
            int v = parse_value();
            if (v < 0) return -1;
            nodes[v].key = k; nodes[v].klen = kl;
            if (last < 0) nodes[n].first = v; else nodes[last].next = v;
            last = v;
            ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == (obj ? '}' : ']')) { jp++; return n; }
            return -1;
        }
    }
    if (*jp == '"') { int n = newnode('s'); if (!parse_string(&nodes[n].s, &nodes[n].slen)) return -1; return n; }
    if (!strncmp(jp, "null", 4)) { jp += 4; return newnode('z'); }
    if (!strncmp(jp, "true", 4)) { jp += 4; int n = newnode('n'); nodes[n].num = 1; return n; }
    if (!strncmp(jp, "false", 5)) { jp += 5; return newnode('n'); }
    char *end;
    long long v = strtoll(jp, &end, 10);
    if (end == jp) return -1;
    jp = end;
    int n = newnode('n'); nodes[n].num = v;
    return n;
}

static int get(int n, const char *key)
{
    size_t kl = strlen(key);
    for (int k = nodes[n].first; k >= 0; k = nodes[k].next)
        if (nodes[k].key && (size_t)nodes[k].klen == kl && !memcmp(nodes[k].key, key, kl)) return k;
    return -1;
}
static long long num(int n, const char *key) { int k = get(n, key); return k >= 0 ? nodes[k].num : 0; }
static int child(int n, int i) { int k = nodes[n].first; while (k >= 0 && i--) k = nodes[k].next; return k; }

static char *read_all(FILE *f, size_t *len)
{
    size_t cap = 1 << 24, n = 0;
    char *buf = malloc(cap + 1);
    for (;;) {
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (n < cap) break;
        cap *= 2; buf = realloc(buf, cap + 1);
    }
    buf[n] = 0; *len = n;
    return buf;
}

#endif
