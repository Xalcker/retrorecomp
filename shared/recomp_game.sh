#!/bin/sh
# Ciclo completo para un juego: traza guionada, recompila, compila, corre difftest con
# varias semillas, junta las direcciones que cayeron al intérprete, recompila con esa
# traza y vuelve a probar.
#
# Uso: sh tests/recomp_game.sh ROM NOMBRE [CUADROS] [SEMILLAS...]
#   CUADROS: los de cada difftest («cuadros» del sistema en shared/sistemas.json si no se dice).
#   Deja el proyecto en build/out/NOMBRE y la traza acumulada en build/NOMBRE.trace.
#   TRACE_FRAMES=N: cuadros de la traza guionada inicial («cuadros_traza» del registro si no se dice).
#   NOTRACE=1: sin la traza guionada inicial (solo análisis estático y lo que descubra difftest).
#   INPUT_TRACE=ARCHIVO, INPUT_DIFFTEST=ARCHIVO: otras entradas (una partida grabada con --record)
#     en lugar de las guionadas de abajo; sirven cuando estas no pasan del título de un juego.
#
# Vive en shared/; cada sistema lo llama con un envoltorio idéntico, tests/recomp_game.sh, que entra a su raíz: la
# consola sale de qué recompilador del registro (shared/sistemas.json) hay en la carpeta. Las entradas guionadas
# (Start, A, derecha...) son las de tests/inputs/ciclo_traza.txt y ciclo_difftest.txt, las
# mismas para todos los juegos (las del Neo Geo van desplazadas 1200 cuadros): sirven para pasar del título al juego y moverse un poco. Las
# semillas agregan un mando aleatorio.
#
# Además de las líneas para leer, deja líneas "RESUMEN {json}" para la GUI y los scripts: una al empezar cada ronda
# ({"evento":"ronda","ronda":N,"direcciones":D}, D las de la traza en la segunda), una por semilla
# ({"evento":"semilla","ronda":N,"semilla":"S","difftest":{el RESUMEN de difftest, o null}}) y la última
# ({"evento":"fin","ok":true|false}); todas con "herramienta":"recomp_game".
REG=$(dirname "$0")/sistemas.json
# Un número del registro: campo $2 del sistema $1. El registro lleva una clave por línea (tests/contract.py lo comprueba).
reg() {
    awk -v id="$1" -v k="$2" '$0 ~ "^  \"" id "\": [{]" { d = 1; next } /^  [}]/ { d = 0 }
        d && $0 ~ "^    \"" k "\": " { gsub(/[^0-9]/, ""); print; exit }' "$REG"
}
CON=
for c in $(sed -n 's/^  "\([a-z0-9]*\)": {$/\1/p' "$REG"); do
    [ -f "./${c}recomp" ] || [ -f "./${c}recomp.exe" ] && CON=$c
done
[ -n "$CON" ] || { echo "no encuentro el recompilador de ningún sistema de $REG en esta carpeta" >&2; exit 2; }
ROM=$1; NAME=$2; TF=${TRACE_FRAMES:-$(reg "$CON" cuadros_traza)}; DF=${3:-$(reg "$CON" cuadros)}
IN_T=${INPUT_TRACE:-tests/inputs/ciclo_traza.txt}; IN_D=${INPUT_DIFFTEST:-tests/inputs/ciclo_difftest.txt}
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
resumen() { printf 'RESUMEN {"herramienta":"recomp_game",%s}\n' "$1"; }
ronda() {
    "./${CON}recomp$EXE" "$ROM" -o "$OUT" --name "$NAME" --trace "$TRACE" | grep -E "código|funciones" || exit 2
    make -s -C "$OUT" ${CC:+CC="$CC"} -j8 2>&1 | grep -E "error" && exit 2
    # Entradas distintas a las de la traza guionada, más el mando aleatorio de cada semilla
    for s in $SEEDS; do
        printf '  semilla %-4s ' "$s"
        "$OUT/difftest$EXE" "$ROM" --frames "$DF" --input "$IN_D" \
            --seed "$s" --uncovered "$OUT/unc_$s.txt" > "$OUT/difftest.log" 2>&1
        rc=$?
        dt=$(grep '^RESUMEN ' "$OUT/difftest.log" | tail -n 1 | sed 's/^RESUMEN //')
        if [ $rc -ne 0 ]; then
            echo "DIFERENCIA"
            grep -v '^RESUMEN ' "$OUT/difftest.log"
            resumen "\"evento\":\"semilla\",\"ronda\":$R,\"semilla\":\"$s\",\"difftest\":${dt:-null}"
            falla=1
            return
        fi
        grep -oE '[0-9.]+% de las instrucciones nativas' "$OUT/difftest.log" | tr '\n' ' '
        grep -oE 'Instrucciones de (PRG|ROM) interpretadas: [0-9]+' "$OUT/difftest.log" || echo
        resumen "\"evento\":\"semilla\",\"ronda\":$R,\"semilla\":\"$s\",\"difftest\":${dt:-null}"
        cat "$OUT/unc_$s.txt" >> "$TRACE"
    done
    sort -u "$TRACE" -o "$TRACE"
}

echo "$NAME: primera ronda"
R=1
resumen '"evento":"ronda","ronda":1'
ronda
if [ $falla -eq 0 ]; then
    D=$(wc -l < "$TRACE" | tr -d ' ')
    echo "$NAME: con la traza ($D direcciones)"
    R=2
    resumen "\"evento\":\"ronda\",\"ronda\":2,\"direcciones\":$D"
    ronda
fi
[ $falla -eq 0 ] && echo "Sin diferencias." || echo "HAY DIFERENCIAS."
[ $falla -eq 0 ] && resumen '"evento":"fin","ok":true' || resumen '"evento":"fin","ok":false'
exit $falla
