#!/bin/sh
# Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones del juego que se
# ejecutaron encuentra solo el análisis estático, con cada juego de roms/; y lo mismo para la BIOS
# (su análisis sin la caché, como para un usuario nuevo). Las trazas se hacen una vez con neotrace y
# se guardan en build/traces/: 6000 cuadros con las mismas entradas para todos (Start y A para pasar
# del título y de los menús, golpes y movimiento en la partida) más un mando aleatorio con semilla 1.
#   sh tests/coverage.sh [opciones de neorecomp, p. ej. --no-tables o --no-spec]
cd "$(dirname "$0")/.." || exit 1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
mkdir -p build/traces
IN="start:1200-1205 start:1500-1505 a:1700-1710 a:1800-1810 a:1900-1910 a:2000-2010 a:2100-2110 a:2200-2210"
IN="$IN start:2300-2305 a:2400-2410 a:2500-2510 a:2600-2610 a:2700-2710 right:2800-3400 b:3100-3105 c:3200-3205"
IN="$IN left:3400-3900 a:3600-3605 d:3700-3705 right:4000-4600 a:4200-4210 b:4400-4410 c:4500-4510"
total=0; found=0; n=0; btotal=0; bfound=0
for r in roms/*.zip roms/*.neo; do
    [ -f "$r" ] || continue
    case "$(basename "$r")" in aes.zip|neogeo.zip) continue ;; esac
    t="build/traces/$(basename "$r" | sed 's/\.[^.]*$//').txt"
    [ -s "$t" ] || "$(exe neotrace)" "$r" "$t" --frames 6000 --seed 1 $IN >/dev/null 2>&1 || continue
    out=$("$(exe neorecomp)" "$r" --coverage "$t" "$@" 2>/dev/null)
    line=$(echo "$out" | grep "^cobertura:")
    bline=$(echo "$out" | grep "^cobertura de la BIOS:")
    [ -n "$line" ] || continue
    printf "%-14s %s\n" "$(basename "$r" | sed 's/\.[^.]*$//')" "$(echo "$line" | sed 's/cobertura: //; s/ instrucciones ejecutadas descubiertas sin traza//')"
    f=$(echo "$line" | awk '{print $2}'); a=$(echo "$line" | awk '{print $4}')
    found=$((found + f)); total=$((total + a)); n=$((n + 1))
    if [ -n "$bline" ]; then
        bfound=$((bfound + $(echo "$bline" | awk '{print $5}'))); btotal=$((btotal + $(echo "$bline" | awk '{print $7}')))
    fi
done
[ $n -gt 0 ] && awk -v f=$found -v t=$total -v n=$n 'BEGIN { printf "en total: %d de %d instrucciones ejecutadas (%.1f%%) en %d juegos\n", f, t, 100 * f / t, n }'
[ $btotal -gt 0 ] && awk -v f=$bfound -v t=$btotal 'BEGIN { printf "BIOS (sin caché): %d de %d instrucciones ejecutadas (%.1f%%), sumando las de cada juego\n", f, t, 100 * f / t }'
