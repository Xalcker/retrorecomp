#!/bin/sh
# Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones del juego que se ejecutaron encuentra
# solo el análisis estático, con cada juego de roms/ (y sus subcarpetas) y el homebrew. Las trazas se hacen una vez
# con smstrace y se guardan en build/traces/: 3000 cuadros con las mismas entradas para todos (Start y el botón 1
# para pasar del título y de los menús, movimiento y botones en la partida) más un mando aleatorio con semilla 1.
#   sh tests/coverage.sh [opciones de smsrecomp, p. ej. --verbose]
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
mkdir -p build/traces
IN="start:120-125 start:300-305 a:400-410 a:500-510 start:600-605 right:700-1300 a:800-805 b:900-905"
IN="$IN left:1300-1700 a:1400-1405 right:1700-2300 b:1800-1810 up:2300-2400 down:2400-2500 a:2600-2610"
total=0; found=0; n=0
for r in roms/*.sms roms/*.gg roms/*/*.sms roms/*/*.gg testroms/homebrew/*.sms testroms/homebrew/*.gg; do
    [ -f "$r" ] || continue
    b=$(basename "$r" | sed 's/\.[^.]*$//')
    t="build/traces/$b.$(echo "$r" | sed 's/.*\.//').txt"
    [ -s "$t" ] || "$(exe smstrace)" "$r" "$t" --frames 3000 --seed 1 $IN >/dev/null 2>&1 || continue
    line=$("$(exe smsrecomp)" "$r" --coverage "$t" "$@" 2>/dev/null | grep "^cobertura:")
    [ -n "$line" ] || continue
    printf "%-40.40s %s\n" "$b" "$(echo "$line" | sed 's/cobertura: //; s/ instrucciones ejecutadas descubiertas sin traza//')"
    f=$(echo "$line" | awk '{print $2}'); a=$(echo "$line" | awk '{print $4}')
    found=$((found + f)); total=$((total + a)); n=$((n + 1))
done
[ $n -gt 0 ] && awk -v f=$found -v t=$total -v n=$n 'BEGIN { printf "en total: %d de %d instrucciones ejecutadas (%.1f%%) en %d juegos\n", f, t, 100 * f / t, n }'
