#!/bin/sh
# Línea base exacta de Genesis: hash de todos los cuadros y de todo el audio (genshot --hash) de cada ROM de roms/
# y de las pruebas libres, 1800 cuadros con las mismas entradas. El código común está en shared/golden.sh
# (opciones, formato de tests/golden.txt y qué cuenta como falla).
#   sh tests/golden.sh [--update] [ROM...]
cd "$(dirname "$0")/.." || exit 2
SIS=gen
GOLDEN_OPTS="--frames 1800 --seed 1 start:120-125 start:300-305 start:600-605"
# El SHA1 de la ROM normalizada (un .smd intercalado da el mismo que el .bin)
golden_sha1() { "$(exe genrecomp)" "$1" --info 2>/dev/null | awk '/^SHA1:/ {print $2}'; }
golden_casos() {
    for r in roms/*.bin roms/*.md roms/*.gen roms/*.smd testroms/demos/*.bin testroms/240p/*.bin; do
        caso "$r"
    done
}
. ../shared/golden.sh
