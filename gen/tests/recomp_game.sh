#!/bin/sh
# Ciclo completo para un juego: traza guionada, recompila, compila, corre difftest con
# varias semillas, junta las instrucciones que cayeron al intérprete, recompila con esa
# traza y vuelve a probar.
#
# Uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]
#   Los mismos argumentos que en NESRecomp y GBRecomp. CUADROS: los de cada difftest (1500 si no se dice).
#   Deja el proyecto en build/out/NOMBRE y la traza acumulada en build/NOMBRE.trace.
#   TRACE_FRAMES=N: cuadros de la traza guionada inicial (2400 si no se dice).
#   NOTRACE=1: sin la traza guionada inicial (solo análisis estático y lo que descubra difftest).
#
# Las entradas guionadas (Start, A, derecha...) son las mismas para todos los juegos: sirven
# para pasar del título al juego y moverse un poco. Las semillas agregan un mando aleatorio.
cd "$(dirname "$0")/.." || exit 2
ROM=$1; NAME=$2; DF=${3:-1500}; TF=${TRACE_FRAMES:-2400}
[ -f "$ROM" ] && [ -n "$NAME" ] || { echo "uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]  (TRACE_FRAMES=N, NOTRACE=1)" >&2; exit 2; }
shift 3 2>/dev/null || shift $#
SEEDS=${*:-"1 2 3"}
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
EXE=
[ -x ./genrecomp.exe ] && EXE=.exe
OUT=build/out/$NAME
TRACE=build/$NAME.trace
mkdir -p build/out
touch "$TRACE"

if [ -z "$NOTRACE" ]; then
    A1="start:200-205 start:300-305 start:400-405 a:500-505 start:600-605 a:700-705 a:800-805"
    for t in 900 960 1020 1080 1140 1200 1260 1320 1380 1440 1500 1560 1620 1680 1740 1800 1860 1920 1980 2040 2100; do
        A1="$A1 a:$t-$((t+25)) b:$((t+30))-$((t+50))"
    done
    ./gentrace$EXE "$ROM" "build/$NAME.guiada" "$TF" $A1 right:900-1400 left:1400-1700 right:1700-2300 >/dev/null || exit 2
    cat "build/$NAME.guiada" >> "$TRACE"
    sort -u "$TRACE" -o "$TRACE"
fi

falla=0
ronda() {
    ./genrecomp$EXE "$ROM" -o "$OUT" --name "$NAME" --trace "$TRACE" | grep -E "ROM|código|funciones" || exit 2
    make -s -C "$OUT" ${CC:+CC="$CC"} -j8 2>&1 | grep -E "error" && exit 2
    # Entradas distintas a las de la traza guionada, más el mando aleatorio de cada semilla
    for s in $SEEDS; do
        printf '  semilla %-4s ' "$s"
        if ! "$OUT/difftest$EXE" "$ROM" "$DF" start:150-155 start:350-355 a:450-455 start:650-655 \
                right:700-1500 a:760-800 a:1000-1040 b:900-1000 a:1200-1220 \
                --seed "$s" --uncovered "$OUT/unc_$s.txt" > "$OUT/difftest.log" 2>&1; then
            echo "DIFERENCIA"
            cat "$OUT/difftest.log"
            falla=1
            return
        fi
        grep -o '[0-9.]*% de los ciclos' "$OUT/difftest.log" | tr '\n' ' '
        grep -o 'Instrucciones de ROM interpretadas: [0-9]*' "$OUT/difftest.log" || echo
        cat "$OUT/unc_$s.txt" >> "$TRACE"
    done
    sort -u "$TRACE" -o "$TRACE"
}

echo "$NAME: primera ronda"
ronda
if [ $falla -eq 0 ]; then
    echo "$NAME: con la traza ($(wc -l < "$TRACE") instrucciones)"
    ronda
fi
[ $falla -eq 0 ] && echo "Sin diferencias." || echo "HAY DIFERENCIAS."
exit $falla
