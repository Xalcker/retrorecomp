#!/bin/sh
# Ciclo completo para un juego: recompila, compila, corre la prueba
# diferencial con varias semillas, junta las direcciones que cayeron al
# intérprete, recompila con esa traza y vuelve a probar.
#
# Uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]
#   Deja el proyecto en out/NOMBRE y la traza acumulada en build/NOMBRE.trace.

cd "$(dirname "$0")/.." || exit 2
ROM=$1
NAME=$2
FRAMES=${3:-6000}
[ -f "$ROM" ] && [ -n "$NAME" ] || { echo "uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]" >&2; exit 2; }
shift 3 2>/dev/null || shift $#
SEEDS=${*:-"1 2 3"}
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
EXE=
[ -x ./gbrecomp.exe ] && EXE=.exe
TRACE=build/$NAME.trace
mkdir -p build
touch "$TRACE"

falla=0
ronda() {
    ./gbrecomp$EXE "$ROM" -o "out/$NAME" --name "$NAME" --trace "$TRACE" | grep -E "^código" || exit 2
    make -s -C "out/$NAME" 2>&1 | grep -E "error" && exit 2
    for s in $SEEDS; do
        printf '  semilla %-4s ' "$s"
        if ! "out/$NAME/difftest$EXE" "$ROM" --frames "$FRAMES" --seed "$s" \
                --uncovered "out/$NAME/unc_$s.txt" > "out/$NAME/difftest.log" 2>&1; then
            echo "DIFERENCIA"
            cat "out/$NAME/difftest.log"
            falla=1
            return
        fi
        grep -o '[0-9.]*% recompilado' "out/$NAME/difftest.log" | tr '\n' ' '
        grep -o 'Direcciones de ROM interpretadas: [0-9]*' "out/$NAME/difftest.log" || echo
        cat "out/$NAME/unc_$s.txt" >> "$TRACE"
    done
    sort -u "$TRACE" -o "$TRACE"
}

echo "$NAME: primera ronda"
ronda
if [ $falla -eq 0 ]; then
    echo "$NAME: con la traza ($(wc -l < "$TRACE") direcciones)"
    ronda
fi
[ $falla -eq 0 ] && echo "Sin diferencias." || echo "HAY DIFERENCIAS."
exit $falla
