#!/bin/sh
# Línea base exacta: hash de la imagen final y de todo el audio de varios juegos y ROMs de prueba.
# Una optimización no debe cambiar ni un byte.
#   tests/golden.sh            compara con tests/golden.txt
#   tests/golden.sh --update   reescribe tests/golden.txt con el estado actual
cd "$(dirname "$0")/.." || exit 1
T=testroms/nes-test-roms
mkdir -p build/gold
OUT=build/gold/now.txt
: > $OUT
A1="start:120-125 right:400-3000 a:500-520 a:700-720 a:900-920 a:1100-1120"
one() {     # rom nombre cuadros [botones...]
    rom="$1"; name="$2"; fr="$3"; shift 3
    [ -f "$rom" ] || return
    ./nesshot "$rom" build/gold/$name.png "$fr" "$@" >/dev/null 2>&1
    ./nesaudio "$rom" build/gold/$name.wav "$fr" "$@" >/dev/null 2>&1
    printf "%s img %s aud %s\n" "$name" "$(md5 -q build/gold/$name.png)" "$(md5 -q build/gold/$name.wav)" >> $OUT
}
one "roms/Super Mario Bros. (Japan, USA).nes" smb 1500 $A1
one "roms/Super Mario Bros. 3 (U) (PRG0) [!].nes" smb3 1800 start:300-305 start:400-405 a:500-505 start:600-605 right:1000-1010 up:1100-1110 a:1300-1305 a:1400-1405 right:1500-1800
one "roms/Contra (U) [!].nes" contra 1500 start:300-305 start:400-405 right:600-1500 a:700-710 a:900-910
one "roms/Castlevania (U) (PRG0) [!].nes" cv 1500 start:300-305 start:400-405 right:600-1500 a:700-710
one "roms/Megaman 1(U) [!].nes" mm 1500 start:300-305 start:400-405 a:500-505 right:600-1500 a:700-710
one "roms/Legend of Zelda, The (U) (PRG0) [!].nes" zelda 1500 start:300-305 start:400-405 a:500-505 start:600-605 a:700-705 a:800-805
one "roms/Arkanoid (USA).nes" ark 900 start:200-205 a:300-305
one "roms/Donkey Kong (U) (PRG1) [!p].nes" dk 900 start:200-205 a:300-305 right:400-900
one $T/other/nestest.nes nestest 300
one $T/apu_test/rom_singles/1-len_ctr.nes apu1 200
one $T/apu_test/rom_singles/7-dmc_basics.nes apu7 400
one $T/ppu_vbl_nmi/rom_singles/05-nmi_timing.nes vbl5 200
one $T/sprite_hit_tests_2005.10.05/09.timing_basics.nes sh9 200
one $T/mmc3_test_2/rom_singles/3-A12_clocking.nes mmc3 200
one $T/other/PCM.demo.wgraphics.nes pcm 600
one $T/other/SimpleParallaxDemo.nes parallax 400
if [ "$1" = "--update" ]; then cp $OUT tests/golden.txt; echo "tests/golden.txt actualizado ($(wc -l < $OUT) casos)"; exit 0; fi
if diff -u tests/golden.txt $OUT >/dev/null; then echo "golden: igual ($(wc -l < $OUT) casos)"; else diff -u tests/golden.txt $OUT; echo "golden: DIFERENTE"; exit 1; fi
