#!/bin/sh
# Regresión del recompilador con Tetris: regenera el proyecto, lo compila y
# corre la prueba diferencial con las partidas grabadas y con semillas
# aleatorias.
# Uso: sh tests/difftest_tetris.sh RUTA/tetris.gb
#   (Tetris (JUE) (V1.1), SHA1 74591cc9501af93873f9a5d3eb12da12c0723bbc)

cd "$(dirname "$0")/.." || exit 2
ROM=$1
[ -f "$ROM" ] || { echo "uso: sh tests/difftest_tetris.sh RUTA/tetris.gb" >&2; exit 2; }
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
EXE=
[ -x ./gbrecomp.exe ] && EXE=.exe

make -s || exit 2
./gbrecomp$EXE "$ROM" -o build/out/tetris > /dev/null || exit 2
make -s -C build/out/tetris || exit 2

falla=0
corre() {
    printf '%-40s ' "$1"
    shift
    if build/out/tetris/difftest$EXE "$ROM" "$@" > build/out/tetris/difftest.log 2>&1; then
        grep -oE '[0-9.]+% de los ciclos' build/out/tetris/difftest.log
    else
        echo "DIFERENCIA"
        cat build/out/tetris/difftest.log
        falla=1
    fi
}

for f in tests/inputs/tetris_*.txt tests/inputs/partida*.txt; do
    [ -f "$f" ] && corre "$(basename "$f")" --input "$f"
done
for s in 1 2 3; do
    corre "aleatorio, semilla $s" --frames 10000 --seed $s
done

[ $falla -eq 0 ] && echo "Sin diferencias." || echo "HAY DIFERENCIAS."
exit $falla
