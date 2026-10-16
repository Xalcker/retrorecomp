#!/bin/sh
# Línea base exacta: hash de todos los cuadros y de todo el audio (genshot --hash) de cada ROM de
# roms/ y de las pruebas libres. Una optimización no debe cambiar ni un bit.
#   sh tests/golden.sh            compara con tests/golden.txt (solo los casos que se pueden correr)
#   sh tests/golden.sh --update   reescribe tests/golden.txt con el estado actual
# Los casos se nombran por el SHA1 de la ROM (no por el nombre del archivo, que cambia de una copia a
# otra). Con GOLDEN_SHOT=ruta se usa otro genshot (p. ej. el de un proyecto generado).
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
SHOT=${GOLDEN_SHOT:-$(exe genshot)}
OUT=build/gold.txt
mkdir -p build
: > $OUT
IN="--seed 1 start:120-125 start:300-305 start:600-605"
for r in roms/*.bin roms/*.md roms/*.gen roms/*.smd testroms/demos/*.bin testroms/240p/*.bin; do
    [ -f "$r" ] || continue
    sha=$("$(exe genrecomp)" "$r" --info 2>/dev/null | awk '/^SHA1:/ {print $2}')
    [ -n "$sha" ] || continue
    h=$("$SHOT" "$r" /dev/null --frames 1800 $IN --hash 2>/dev/null | grep "^hash")
    printf "%s %s  # %s\n" "$sha" "$h" "$(basename "$r")" >> $OUT
done
if [ "$1" = "--update" ]; then cp $OUT tests/golden.txt; echo "tests/golden.txt actualizado ($(wc -l < $OUT) casos)"; exit 0; fi
bad=0; n=0
while read -r sha rest; do
    n=$((n + 1))
    want=$(grep "^$sha " tests/golden.txt | sed 's/  #.*//')
    now=$(echo "$sha $rest" | sed 's/  #.*//')
    if [ -z "$want" ]; then echo "(nuevo: $rest)"; continue; fi
    [ "$want" = "$now" ] || { echo "DIFERENTE: ${rest#*# }"; echo "  esperado: $want"; echo "  ahora:    $now"; bad=1; }
done < $OUT
if [ $bad = 0 ]; then echo "golden: igual en los $n casos que se pudieron correr (de $(wc -l < tests/golden.txt))"
else echo "golden: DIFERENTE"; exit 1; fi
