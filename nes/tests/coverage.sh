#!/bin/sh
# Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones que el juego
# ejecutó (sus trazas en build/) encuentra solo el análisis estático.
#   tests/coverage.sh [opciones de nesrecomp, p. ej. --no-tables]
cd "$(dirname "$0")/.." || exit 1
check() {   # rom traza
    [ -f "$1" ] && [ -f "$2" ] || return
    printf "%-45s " "$(basename "$1" | cut -c1-44)"
    ./nesrecomp "$1" --coverage "$2" "${@:3}" 2>/dev/null | grep cobertura | sed 's/cobertura: //'
}
OPT="$*"
check "roms/Super Mario Bros. (Japan, USA).nes" build/smb.trace $OPT
check "roms/Donkey Kong (U) (PRG1) [!p].nes" build/Donkey.trace $OPT
check "roms/Arkanoid (USA).nes" build/Arkanoid.trace $OPT
check "roms/Contra (U) [!].nes" build/Contra.trace $OPT
check "roms/Castlevania (U) (PRG0) [!].nes" build/Castlevania.trace $OPT
check "roms/Megaman 1(U) [!].nes" build/Megaman.trace $OPT
check "roms/Super Mario Bros. 3 (U) (PRG0) [!].nes" build/Super_Ma.trace $OPT
check "roms/Legend of Zelda, The (U) (PRG0) [!].nes" build/Legend.trace $OPT
