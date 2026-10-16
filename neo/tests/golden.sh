#!/bin/sh
# Línea base exacta de Neo Geo: hash de todos los cuadros y de todo el audio (neoshot --hash) de cada juego de
# roms/ y del juego de prueba del contrato, 3600 cuadros con las entradas de tests/coverage.sh (de la demo a la
# partida). El código común está en shared/golden.sh (opciones, formato de tests/golden.txt y qué cuenta como falla).
#   sh tests/golden.sh [--update] [ROM...]
# La BIOS cambia lo que se ve: con la de SNK (roms/aes.zip, o NEO_BIOS) los casos llevan solo el SHA1; con la
# propia (build/neobios.bin, la de make neobios y la del CI) llevan la variante /neobios.
cd "$(dirname "$0")/.." || exit 2
SIS=neo
if [ -z "$NEO_BIOS" ] && [ -f roms/aes.zip ]; then NEO_BIOS=$PWD/roms/aes.zip; fi
if [ -z "$NEO_BIOS" ] && [ -f build/neobios.bin ]; then NEO_BIOS=$PWD/build/neobios.bin; fi
export NEO_BIOS
BIOS_VAR=
case "$NEO_BIOS" in *neobios.bin) BIOS_VAR=neobios ;; esac
golden_skip() { [ -n "$NEO_BIOS" ] || echo "falta la BIOS: roms/aes.zip, NEO_BIOS o make neobios"; }
GOLDEN_OPTS="--frames 3600 --seed 1 start:1200-1205 start:1500-1505 a:1700-1710 a:1800-1810 a:1900-1910"
GOLDEN_OPTS="$GOLDEN_OPTS a:2000-2010 a:2100-2110 a:2200-2210 start:2300-2305 a:2400-2410 a:2500-2510 a:2600-2610"
GOLDEN_OPTS="$GOLDEN_OPTS a:2700-2710 right:2800-3400 b:3100-3105 c:3200-3205 left:3400-3600"
# El SHA1 de la imagen normalizada (el mismo para el .zip de MAME y el .neo)
golden_sha1() { "$(exe neorecomp)" "$1" --info 2>/dev/null | awk '/^SHA1:/ {print $2}'; }
golden_casos() {
    for r in roms/*.zip roms/*.neo testroms/neo/*.neo; do
        case "$(basename "$r")" in aes.zip|neogeo.zip) continue ;; esac
        caso "$r" $BIOS_VAR
    done
}
. ../shared/golden.sh
