#!/bin/sh
# Ciclo completo para un juego: traza guionada, recompila, compila, corre difftest con
# varias semillas, junta las direcciones que cayeron al intérprete, recompila con esa
# traza y vuelve a probar.
#
# Uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]
#   CUADROS: los de cada difftest (6000 en Game Boy, 3000 en Neo Geo, 1500 en NES y Genesis si no se dice).
#   Deja el proyecto en build/out/NOMBRE y la traza acumulada en build/NOMBRE.trace.
#   TRACE_FRAMES=N: cuadros de la traza guionada inicial (2400 si no se dice; 3600 en Neo Geo).
#   NOTRACE=1: sin la traza guionada inicial (solo análisis estático y lo que descubra difftest).
#   INPUT_TRACE=ARCHIVO, INPUT_DIFFTEST=ARCHIVO: otras entradas (una partida grabada con --record)
#     en lugar de las guionadas de abajo; sirven cuando estas no pasan del título de un juego.
#
# Es el mismo archivo en NESRecomp, GBRecomp, GENRecomp y NEORecomp (tests/contract.py de retrorecomp lo
# comprueba): la consola sale de qué recompilador hay en la carpeta. Las entradas guionadas
# (Start, A, derecha...) son las de tests/inputs/ciclo_traza.txt y ciclo_difftest.txt, las
# mismas para todos los juegos (las del Neo Geo van desplazadas 1200 cuadros): sirven para pasar del título al juego y moverse un poco. Las
# semillas agregan un mando aleatorio.
cd "$(dirname "$0")/.." || exit 2
CON=
for c in nes gb gen neo; do
    [ -f "./${c}recomp" ] || [ -f "./${c}recomp.exe" ] && CON=$c
done
[ -n "$CON" ] || { echo "no encuentro nesrecomp, gbrecomp, genrecomp ni neorecomp en esta carpeta" >&2; exit 2; }
ROM=$1; NAME=$2; TF=${TRACE_FRAMES:-2400}
IN_T=${INPUT_TRACE:-tests/inputs/ciclo_traza.txt}; IN_D=${INPUT_DIFFTEST:-tests/inputs/ciclo_difftest.txt}
case $CON in
    gb) DF=${3:-6000} ;;
    neo) DF=${3:-3000}; TF=${TRACE_FRAMES:-3600} ;;    # la BIOS del AES tarda ~1200 cuadros en dejar el juego
    *) DF=${3:-1500} ;;
esac
[ -f "$ROM" ] && [ -n "$NAME" ] || { echo "uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]  (TRACE_FRAMES=N, NOTRACE=1, INPUT_TRACE=F, INPUT_DIFFTEST=F)" >&2; exit 2; }
shift 3 2>/dev/null || shift $#
SEEDS=${*:-"1 2 3"}
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
EXE=
[ -x "./${CON}recomp.exe" ] && EXE=.exe
OUT=build/out/$NAME
TRACE=build/$NAME.trace
mkdir -p build/out
touch "$TRACE"

if [ -z "$NOTRACE" ]; then
    "./${CON}trace$EXE" "$ROM" "build/$NAME.guiada" --frames "$TF" --input "$IN_T" >/dev/null || exit 2
    cat "build/$NAME.guiada" >> "$TRACE"
    sort -u "$TRACE" -o "$TRACE"
fi

falla=0
ronda() {
    "./${CON}recomp$EXE" "$ROM" -o "$OUT" --name "$NAME" --trace "$TRACE" | grep -E "código|funciones" || exit 2
    make -s -C "$OUT" ${CC:+CC="$CC"} -j8 2>&1 | grep -E "error" && exit 2
    # Entradas distintas a las de la traza guionada, más el mando aleatorio de cada semilla
    for s in $SEEDS; do
        printf '  semilla %-4s ' "$s"
        if ! "$OUT/difftest$EXE" "$ROM" --frames "$DF" --input "$IN_D" \
                --seed "$s" --uncovered "$OUT/unc_$s.txt" > "$OUT/difftest.log" 2>&1; then
            echo "DIFERENCIA"
            cat "$OUT/difftest.log"
            falla=1
            return
        fi
        grep -oE '[0-9.]+% (de los ciclos|recompilado)' "$OUT/difftest.log" | tr '\n' ' '
        grep -oE '(Instrucciones|Direcciones) de (PRG|ROM) interpretadas: [0-9]+' "$OUT/difftest.log" || echo
        cat "$OUT/unc_$s.txt" >> "$TRACE"
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
