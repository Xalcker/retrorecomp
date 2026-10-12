#!/bin/sh
# Línea base exacta: hash de todos los cuadros y de todo el audio (nesshot --hash) de cada ROM de
# roms/ y de las pruebas libres. Una optimización no debe cambiar ni un bit.
#   sh tests/golden.sh            compara con tests/golden.txt (solo los casos que se pueden correr)
#   sh tests/golden.sh --update   reescribe tests/golden.txt con el estado actual
# Los casos se nombran por el SHA1 de la ROM sin la cabecera iNES (el de No-Intro: no cambia con el
# nombre del archivo ni con la cabecera). Con GOLDEN_SHOT=ruta se usa otro nesshot.
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
SHOT=${GOLDEN_SHOT:-$(exe nesshot)}
T=testroms/nes-test-roms
OUT=build/gold.txt
mkdir -p build
: > $OUT
sha1() {    # SHA1 de la ROM sin los 16 bytes de cabecera (macOS: shasum; Linux y MSYS2: sha1sum)
    if command -v sha1sum >/dev/null 2>&1; then tail -c +17 "$1" | sha1sum | cut -d' ' -f1
    else tail -c +17 "$1" | shasum | cut -d' ' -f1; fi
}
IN="--seed 1 start:120-125 start:300-305 start:600-605"
for r in roms/*.nes $T/other/nestest.nes $T/apu_test/rom_singles/1-len_ctr.nes \
         $T/apu_test/rom_singles/7-dmc_basics.nes $T/ppu_vbl_nmi/rom_singles/05-nmi_timing.nes \
         $T/sprite_hit_tests_2005.10.05/09.timing_basics.nes $T/mmc3_test_2/rom_singles/3-A12_clocking.nes \
         $T/other/PCM.demo.wgraphics.nes $T/other/SimpleParallaxDemo.nes; do
    [ -f "$r" ] || continue
    sha=$(sha1 "$r")
    h=$("$SHOT" "$r" /dev/null --frames 1800 $IN --hash 2>/dev/null | grep "^hash")
    [ -n "$h" ] || h="hash error"
    printf "%s %s  # %s\n" "$sha" "$h" "$(basename "$r")" >> $OUT
done
if [ "$1" = "--update" ]; then cp $OUT tests/golden.txt; echo "tests/golden.txt actualizado ($(wc -l < $OUT) casos)"; exit 0; fi
bad=0; n=0
while read -r sha rest; do
    n=$((n + 1))
    want=$(grep "^$sha " tests/golden.txt | sed 's/  #.*//')
    now=$(echo "$sha $rest" | sed 's/  #.*//')
    if [ -z "$want" ]; then echo "(nuevo: $rest)"; continue; fi
    [ "$want" = "$now" ] || { echo "DIFERENTE: ${rest#*# }"; echo "  esperado: $want"; echo "  ahora:    $now"; bad=1; }
done < $OUT
if [ $bad = 0 ]; then echo "golden: igual en los $n casos que se pudieron correr (de $(wc -l < tests/golden.txt))"
else echo "golden: DIFERENTE"; exit 1; fi
