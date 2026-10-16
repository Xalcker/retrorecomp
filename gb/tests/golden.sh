#!/bin/sh
# Línea base exacta de Game Boy: hash de todos los cuadros (pantalla del GB y del SGB) y de todo el audio
# (gbshot --hash) de cada juego de roms/ y de pruebas de la PPU y del APU de testroms/. El código común está en
# shared/golden.sh (opciones, formato de tests/golden.txt y qué cuenta como falla).
#   sh tests/golden.sh [--update] [ROM...]
# Los juegos corren 2389 cuadros (40 s) con tests/inputs/golden.txt; las pruebas, sin entradas, con el modelo de
# cada una (la variante del caso). Incluye las de Mealybug que todavía fallan: si un cambio las arregla, la
# diferencia aquí es la esperada.
cd "$(dirname "$0")/.." || exit 2
SIS=gb
GOLDEN_OPTS=
T=testroms
golden_casos() {
    for r in roms/*.gb roms/*.gbc; do
        caso "$r" -- --frames 2389 --input tests/inputs/golden.txt
    done
    caso "$T/dmg-acid2/dmg-acid2.gb" dmg -- --frames 179 --model dmg
    caso "$T/dmg-acid2/dmg-acid2.gb" cgb -- --frames 179 --model cgb
    caso "$T/cgb-acid2/cgb-acid2.gbc" -- --frames 179
    caso "$T/cgb-acid-hell/cgb-acid-hell.gbc" -- --frames 179
    for t in m3_bgp_change_sprites m3_lcdc_obj_size_change_scx m3_lcdc_win_en_change_multiple_wx \
             m3_scx_low_3_bits m3_scy_change m3_window_timing m2_win_en_toggle; do
        caso "$T/mealybug-tearoom-tests/ppu/$t.gb" dmg -- --frames 119 --model dmg
    done
    caso "$T/mealybug-tearoom-tests/dma/hdma_timing-C.gb" cgb -- --frames 119 --model cgb
    caso "$T/blargg/cgb_sound/cgb_sound.gb" cgb -- --frames 2389 --model cgb
}
. ../shared/golden.sh
