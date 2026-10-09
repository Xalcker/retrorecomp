#!/bin/sh
# Recompila un juego con una traza y compara código recompilado contra el intérprete.
#   tests/recomp_game.sh rom.nes nombre [cuadros_traza] [cuadros_difftest]
# NOTRACE=1: sin traza, solo el análisis estático.
# Las entradas (Start, A, derecha...) son las mismas para todos los juegos: sirven para
# pasar del título al juego y moverse un poco.
cd "$(dirname "$0")/.." || exit 1
ROM="$1"; NAME="$2"; TF=${3:-2400}; DF=${4:-1500}
OUT=build/out/$NAME
mkdir -p build/out
A1="start:200-205 start:300-305 start:400-405 a:500-505 start:600-605 a:700-705 a:800-805"
for t in 900 960 1020 1080 1140 1200 1260 1320 1380 1440 1500 1560 1620 1680 1740 1800 1860 1920 1980 2040 2100; do
    A1="$A1 a:$t-$((t+25)) b:$((t+30))-$((t+50))"
done
if [ -n "$NOTRACE" ]; then
    # solo con el análisis estático (vectores, tablas de saltos, bancos)
    ./nesrecomp "$ROM" -o "$OUT" --name "$NAME" | grep -E "mapper|código|funciones" || exit 1
else
    ./nestrace "$ROM" build/$NAME.trace "$TF" $A1 right:900-1400 left:1400-1700 right:1700-2300 >/dev/null || exit 1
    ./nesrecomp "$ROM" -o "$OUT" --name "$NAME" --trace build/$NAME.trace | grep -E "mapper|código|funciones" || exit 1
fi
(cd "$OUT" && make -j8 2>&1 | grep -E "warning|error" ; true)
# difftest con otras entradas distintas a las de la traza
(cd "$OUT" && ./difftest "../../../$ROM" "$DF" start:150-155 start:350-355 a:450-455 start:650-655 right:700-1500 a:760-800 a:1000-1040 b:900-1000 a:1200-1220)
