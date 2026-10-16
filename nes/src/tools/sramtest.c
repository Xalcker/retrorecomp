/*
 * sramtest: prueba la RAM con batería. Carga una ROM, escribe en $6000-$7FFF por el bus,
 * guarda, crea otra consola, carga y compara.   sramtest rom.nes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *rom = malloc(sz); if (fread(rom, 1, sz, f) != (size_t)sz) return 2; fclose(f);
    rom[6] |= 2;                                   /* declara batería */
    static NES a, b; const char *err;
    if (nes_load(&a, rom, sz, &err) || nes_load(&b, rom, sz, &err)) { fprintf(stderr, "%s\n", err); return 2; }
    const char *path = "build/sramtest.sav";
    remove(path);
    int fail = 0;
    if (nes_sram_load(&b, path)) { puts("FALLA: cargó un archivo que no existe"); fail = 1; }
    if (nes_sram_dirty(&a)) { puts("FALLA: sucia sin cambios"); fail = 1; }
    for (int i = 0; i < 0x2000; i++) nes_write(&a, 0x6000 + i, (u8)(i * 7 + 3));
    if (!nes_sram_dirty(&a)) { puts("FALLA: no detecta cambios"); fail = 1; }
    if (!nes_sram_save(&a, path)) { puts("FALLA: no guardó"); fail = 1; }
    if (nes_sram_dirty(&a)) { puts("FALLA: sigue sucia tras guardar"); fail = 1; }
    if (!nes_sram_load(&b, path)) { puts("FALLA: no cargó"); fail = 1; }
    if (memcmp(a.cart.prg_ram, b.cart.prg_ram, 0x2000)) { puts("FALLA: contenido distinto"); fail = 1; }
    if (nes_read(&b, 0x7FFF) != (u8)(0x1FFF * 7 + 3)) { puts("FALLA: lectura por el bus"); fail = 1; }
    if (!fail) puts("sramtest: bien");
    return fail;
}
