/*
 * Super Game Boy multijugador: con 4 jugadores (MLT_REQ), cada control
 * devuelve su número y sus botones (gb_set_pad_player). Se lee P1 como lo
 * hace un juego: cruceta, botones y una subida de P15 para pasar al
 * siguiente control. La ROM solo sirve para encender un Super Game Boy.
 *
 * Uso: sgb_controles rom_sgb.gb      Código de salida: 0 pasó, 1 falló.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../src/runtime/gb.h"
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: sgb_controles rom_sgb.gb\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "no se pudo abrir %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *rom = malloc((size_t)n);
    if (!rom || fread(rom, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    static GB g;
    const char *err = gb_init_model(&g, rom, (u32)n, GB_MODEL_SGB);
    if (err) {
        fprintf(stderr, "%s\n", err);
        return 2;
    }
    g.sgbs.players = 4;
    const u8 pads[4] = { PAD_A, PAD_B | PAD_START, PAD_LEFT, PAD_DOWN | PAD_SELECT };
    for (int p = 0; p < 4; p++) gb_set_pad_player(&g, p, pads[p]);
    int ok = 1;
    /* Como un juego: cada subida de P15 (escribir $30 tras $10) pasa al
     * siguiente control. Se empieza en el 4 para que la primera vuelva al 1. */
    g.sgbs.player = 3;
    gb_write_slow(&g, 0xFF00, 0x10);
    gb_write_slow(&g, 0xFF00, 0x30);
    for (int p = 0; p < 4; p++) {
        u8 id = 0xF - (gb_read_slow(&g, 0xFF00) & 0x0F);
        gb_write_slow(&g, 0xFF00, 0x20);    /* P14 abajo: cruceta */
        u8 dir = (u8)(~gb_read_slow(&g, 0xFF00) & 0x0F);
        gb_write_slow(&g, 0xFF00, 0x10);    /* P15 abajo: botones */
        u8 btn = (u8)(~gb_read_slow(&g, 0xFF00) & 0x0F);
        gb_write_slow(&g, 0xFF00, 0x30);    /* sube P15: siguiente control */
        u8 got = (u8)(dir << 4 | btn);
        printf("control %d: id=%X botones=%02X esperado=%02X %s\n", p + 1, id, got, pads[p],
               got == pads[p] && id == p ? "ok" : "MAL");
        if (got != pads[p] || id != p) ok = 0;
    }
    gb_free(&g);
    free(rom);
    return !ok;
}
