#!/bin/sh
# Ciclo completo para un juego: recompila, compila, corre la prueba
# diferencial con varias semillas, junta las direcciones que cayeron al
# intérprete, recompila con esa traza y vuelve a probar.
#
# Uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]   (los mismos argumentos que en NESRecomp)
#   Deja el proyecto en build/out/NOMBRE (como en NESRecomp y GENRecomp) y la traza acumulada en build/NOMBRE.trace.

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
    ./gbrecomp$EXE "$ROM" -o "build/out/$NAME" --name "$NAME" --trace "$TRACE" | grep -E "^código" || exit 2
    make -s -C "build/out/$NAME" 2>&1 | grep -E "error" && exit 2
    for s in $SEEDS; do
        printf '  semilla %-4s ' "$s"
        if ! "build/out/$NAME/difftest$EXE" "$ROM" --frames "$FRAMES" --seed "$s" \
                --uncovered "build/out/$NAME/unc_$s.txt" > "build/out/$NAME/difftest.log" 2>&1; then
            echo "DIFERENCIA"
            cat "build/out/$NAME/difftest.log"
            falla=1
            return
        fi
        grep -o '[0-9.]*% recompilado' "build/out/$NAME/difftest.log" | tr '\n' ' '
        grep -o 'Direcciones de ROM interpretadas: [0-9]*' "build/out/$NAME/difftest.log" || echo
        cat "build/out/$NAME/unc_$s.txt" >> "$TRACE"
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
