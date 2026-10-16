#!/bin/sh
# Línea base exacta de Master System y Game Gear: hash de todos los cuadros y de todo el audio (smsshot --hash) de
# cada ROM de roms/ (y de sus subcarpetas) y de las pruebas libres, 1800 cuadros con las mismas entradas. El código
# común está en shared/golden.sh (opciones, formato de tests/golden.txt y qué cuenta como falla).
#   sh tests/golden.sh [--update] [ROM...]
cd "$(dirname "$0")/.." || exit 2
SIS=sms
GOLDEN_OPTS="--frames 1800 --seed 1 start:120-125 start:300-305 start:600-605"
# El SHA1 que da smsrecomp --info (sin la cabecera de copiador de 512 bytes, si la tiene)
golden_sha1() { "$(exe smsrecomp)" "$1" --info 2>/dev/null | awk '/^SHA1:/ {print $2}'; }
golden_casos() {
    for r in roms/*.sms roms/*.gg roms/*/*.sms roms/*/*.gg testroms/homebrew/*.sms testroms/homebrew/*.gg \
             testroms/sms/*.sms; do
        caso "$r"
    done
}
. ../shared/golden.sh
