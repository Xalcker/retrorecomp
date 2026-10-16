/*
 * dis68kbin: desensambla las instrucciones de un binario del 68000 en las direcciones que se le dan (una por
 * línea en la entrada estándar, en hexadecimal) con el desensamblador de neorecomp. Sirve para probar asm68k:
 *   dis68kbin imagen.bin base < direcciones.txt
 */
#include <stdio.h>
#include <stdlib.h>

#include "dis68k.h"

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "uso: dis68kbin imagen.bin base < direcciones\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static unsigned char img[0x100000];
    size_t n = fread(img, 1, sizeof img, f);
    fclose(f);
    unsigned long base = strtoul(argv[2], NULL, 16), a;
    while (scanf("%lx", &a) == 1) {
        uint16_t w[8];
        for (int i = 0; i < 8; i++) {
            size_t o = a - base + 2 * (size_t)i;
            w[i] = o + 1 < n ? (uint16_t)(img[o] << 8 | img[o + 1]) : 0;
        }
        char out[128];
        dis68k(w, (uint32_t)a, out, sizeof out);
        printf("%06lX %s\n", a, out);
    }
    return 0;
}
