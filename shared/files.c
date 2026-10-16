/*
 * Archivos con rutas en UTF-8 y el diálogo para elegir la ROM (files.h). Común a los cuatro
 * recompiladores (retrorecomp/shared); lo propio de cada consola, el filtro del diálogo, llega como
 * argumento de ask_rom_path.
 */
#include "files.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

/* UTF-8 → UTF-16 (malloc) */
static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

FILE *u8_fopen(const char *path, const char *mode)
{
    wchar_t *wp = widen(path), *wm = widen(mode);
    FILE *f = wp && wm ? _wfopen(wp, wm) : NULL;
    free(wp);
    free(wm);
    return f;
}

/* Crea una carpeta (si ya existe no es error). Devuelve 1 si todo bien. */
int u8_mkdir(const char *path)
{
    wchar_t *wp = widen(path);
    int ok = wp && (CreateDirectoryW(wp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    free(wp);
    return ok;
}

int u8_remove(const char *path)
{
    wchar_t *wp = widen(path);
    int r = wp ? _wremove(wp) : -1;
    free(wp);
    return r;
}

static int u8_replace(const char *from, const char *to)
{
    wchar_t *wf = widen(from), *wt = widen(to);
    int ok = wf && wt && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
    free(wf);
    free(wt);
    return ok;
}

int ask_rom_path(const char *title, const char *types, char *out, size_t size)
{
    wchar_t file[1024] = L"";
    wchar_t *wtitle = widen(title);
    /* "Descripción|*.a;*.b" -> "Descripción\0*.a;*.b\0Todos los archivos\0*.*\0\0" */
    static const wchar_t todos[] = L"Todos los archivos\0*.*\0";
    wchar_t *wtypes = widen(types ? types : "");
    size_t n = wtypes ? wcslen(wtypes) : 0;
    wchar_t *filter = malloc((n + 1) * sizeof *filter + sizeof todos);
    if (filter) {
        for (size_t i = 0; i < n; i++) filter[i] = wtypes[i] == L'|' ? L'\0' : wtypes[i];
        filter[n] = 0;
        memcpy(filter + (n ? n + 1 : 0), todos, sizeof todos);
    }
    free(wtypes);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file / sizeof file[0];
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    int ok = GetOpenFileNameW(&ofn);
    free(wtitle);
    free(filter);
    if (!ok) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)size, NULL, NULL) > 0;
}

#else

FILE *u8_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int u8_remove(const char *path) { return remove(path); }
int u8_mkdir(const char *path) { return mkdir(path, 0755) == 0 || errno == EEXIST; }
static int u8_replace(const char *from, const char *to) { return rename(from, to) == 0; }

int ask_rom_path(const char *title, const char *types, char *out, size_t size)
{
    (void)title;
    (void)types;
    (void)out;
    (void)size;
    return 0;
}

#endif

unsigned char *u8_read_file(const char *path, size_t *size)
{
    FILE *f = u8_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n >= 0 ? malloc(n ? (size_t)n : 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) *size = (size_t)n;
    return buf;
}

int u8_write_file(const char *path, const void *data, size_t size)
{
    size_t len = strlen(path);
    char *tmp = malloc(len + 5);
    if (!tmp) return 0;
    memcpy(tmp, path, len);
    memcpy(tmp + len, ".tmp", 5);
    FILE *f = u8_fopen(tmp, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = 0;
    if (ok) ok = u8_replace(tmp, path);
    if (!ok) u8_remove(tmp);
    free(tmp);
    return ok;
}
