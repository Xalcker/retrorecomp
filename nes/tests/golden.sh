#!/bin/sh
# Línea base exacta de NES: hash de todos los cuadros y de todo el audio (nesshot --hash) de cada ROM de roms/
# y de las pruebas libres, 1800 cuadros con las mismas entradas. El código común está en shared/golden.sh
# (opciones, formato de tests/golden.txt y qué cuenta como falla).
#   sh tests/golden.sh [--update] [ROM...]
cd "$(dirname "$0")/.." || exit 2
SIS=nes
GOLDEN_OPTS="--frames 1800 --seed 1 start:120-125 start:300-305 start:600-605"
# El SHA1 de la ROM sin la cabecera iNES (el de No-Intro: no cambia si se arregla la cabecera)
golden_sha1() { tail -c +17 "$1" | sha1_file; }
T=testroms/nes-test-roms
golden_casos() {
    for r in roms/*.nes $T/other/nestest.nes $T/apu_test/rom_singles/1-len_ctr.nes \
             $T/apu_test/rom_singles/7-dmc_basics.nes $T/ppu_vbl_nmi/rom_singles/05-nmi_timing.nes \
             $T/sprite_hit_tests_2005.10.05/09.timing_basics.nes $T/mmc3_test_2/rom_singles/3-A12_clocking.nes \
             $T/other/PCM.demo.wgraphics.nes $T/other/SimpleParallaxDemo.nes; do
        caso "$r"
    done
}
. ../shared/golden.sh
