/*
 * Los mensajes de las herramientas llevan acentos y están en UTF-8. La consola
 * de Windows usa otra página de códigos por omisión ("intérprete" sale como
 * "int├®rprete"), así que hay que pedirle UTF-8 al arrancar.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef _WIN32
/* Prototipo de kernel32, para no incluir windows.h */
__declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int code_page);
static inline void console_utf8(void) { SetConsoleOutputCP(65001); }
#else
static inline void console_utf8(void) {}
#endif

#endif
