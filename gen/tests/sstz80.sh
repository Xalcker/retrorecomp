#!/bin/sh
# SingleStepTests del Z80 (make testroms): corre los 1604 archivos con z80test.
#   sh tests/sstz80.sh            resumen (solo los que fallan, y el total)
#   sh tests/sstz80.sh "ed b0" -v 5   un archivo, con el detalle de las 5 primeras que fallan
# Sale con 1 si alguna prueba falla.
cd "$(dirname "$0")/.." || exit 2
D=testroms/z80/v1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
T=$(exe z80test)
if [ ! -f "$D/00.json.gz" ]; then echo "(sin $D: make testroms)"; exit 0; fi
if [ -n "$1" ]; then n=$1; shift; gunzip -c "$D/$n.json.gz" | "$T" - --name "$n" "$@"; exit $?; fi
pasan=0; total=0; fallan=0
for f in "$D"/*.json.gz; do
    n=$(basename "$f" .json.gz)
    r=$(gunzip -c "$f" | "$T" - --name "$n")
    set -- $(echo "$r" | grep -o '[0-9]*/[0-9]*' | tail -1 | tr '/' ' ')
    pasan=$((pasan + $1)); total=$((total + $2))
    if [ "$1" != "$2" ]; then fallan=$((fallan + 1)); echo "$r"; fi
done
echo "SingleStepTests Z80: $pasan/$total ($fallan archivos con fallas)"
[ $fallan -eq 0 ]
