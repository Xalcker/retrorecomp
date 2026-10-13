#!/bin/sh
# Línea base exacta: hash de todos los cuadros y de todo el audio (neoshot --hash) de cada juego de
# roms/ y del juego de prueba del contrato. Una optimización no debe cambiar ni un bit.
#   sh tests/golden.sh            compara con tests/golden.txt (solo los casos que se pueden correr)
#   sh tests/golden.sh --update   reescribe tests/golden.txt con el estado actual
# Los casos se nombran por el SHA1 de la imagen normalizada (no por el nombre del archivo, que cambia de
# una copia a otra). 3600 cuadros con las entradas de tests/coverage.sh: de la demo a la partida. Con
# GOLDEN_SHOT=ruta se usa otro neoshot. Necesita la BIOS del AES (roms/aes.zip o NEO_BIOS).
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
SHOT=${GOLDEN_SHOT:-$(exe neoshot)}
OUT=build/gold.txt
mkdir -p build
: > $OUT
IN="--seed 1 start:1200-1205 start:1500-1505 a:1700-1710 a:1800-1810 a:1900-1910 a:2000-2010 a:2100-2110"
IN="$IN a:2200-2210 start:2300-2305 a:2400-2410 a:2500-2510 a:2600-2610 a:2700-2710 right:2800-3400"
IN="$IN b:3100-3105 c:3200-3205 left:3400-3600"
for r in roms/*.zip roms/*.neo testroms/neo/*.neo; do
    [ -f "$r" ] || continue
    case "$(basename "$r")" in aes.zip|neogeo.zip) continue ;; esac
    sha=$("$(exe neorecomp)" "$r" --info 2>/dev/null | awk '/^SHA1:/ {print $2}')
    [ -n "$sha" ] || continue
    h=$("$SHOT" "$r" /dev/null --frames 3600 $IN --hash 2>/dev/null | grep "^hash")
    [ -n "$h" ] || continue
    printf "%s %s  # %s\n" "$sha" "$h" "$(basename "$r")" >> $OUT
done
if [ "$1" = "--update" ]; then cp $OUT tests/golden.txt; echo "tests/golden.txt actualizado ($(wc -l < $OUT | tr -d ' ') casos)"; exit 0; fi
bad=0; n=0
while read -r sha rest; do
    n=$((n + 1))
    want=$(grep "^$sha " tests/golden.txt | sed 's/  #.*//')
    now=$(echo "$sha $rest" | sed 's/  #.*//')
    if [ -z "$want" ]; then echo "(nuevo: $rest)"; continue; fi
    [ "$want" = "$now" ] || { echo "DIFERENTE: ${rest#*# }"; echo "  esperado: $want"; echo "  ahora:    $now"; bad=1; }
done < $OUT
if [ $bad = 0 ]; then echo "golden: igual en los $n casos que se pudieron correr (de $(wc -l < tests/golden.txt | tr -d ' '))"
else echo "golden: DIFERENTE"; exit 1; fi
