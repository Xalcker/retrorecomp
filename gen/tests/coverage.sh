#!/bin/sh
# Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones que el juego ejecutó
# encuentra solo el análisis estático, con cada ROM de roms/. Las trazas se hacen una vez con
# gentrace (1800 cuadros, mando aleatorio con semilla 1 y Start a los 2 y a los 5 s) y se guardan en
# build/traces/.
#   sh tests/coverage.sh [opciones de genrecomp, p. ej. --no-tables o --no-spec]
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
mkdir -p build/traces
total=0; found=0; n=0
for r in roms/*.bin roms/*.md roms/*.gen roms/*.smd; do
    [ -f "$r" ] || continue
    t="build/traces/$(basename "$r" | sed 's/\.[^.]*$//' | tr -c 'A-Za-z0-9\n' '_').txt"
    [ -s "$t" ] || "$(exe gentrace)" "$r" "$t" --frames 1800 --seed 1 start:120-125 start:300-305 >/dev/null 2>&1
    line=$("$(exe genrecomp)" "$r" --coverage "$t" "$@" 2>/dev/null | grep "^cobertura")
    [ -n "$line" ] || continue
    printf "%-48s %s\n" "$(basename "$r" | cut -c1-47)" "$(echo "$line" | sed 's/cobertura: //; s/ instrucciones ejecutadas descubiertas sin traza//')"
    f=$(echo "$line" | awk '{print $2}'); a=$(echo "$line" | awk '{print $4}')
    found=$((found + f)); total=$((total + a)); n=$((n + 1))
done
[ $n -gt 0 ] && awk -v f=$found -v t=$total -v n=$n 'BEGIN { printf "en total: %d de %d instrucciones ejecutadas (%.1f%%) en %d ROMs\n", f, t, 100 * f / t, n }'
