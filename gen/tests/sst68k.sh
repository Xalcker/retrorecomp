#!/bin/sh
# SingleStepTests del 68000 (make testroms): corre las 125 familias con m68ktest.
#   sh tests/sst68k.sh                 resumen por familia (intérprete)
#   SST_OPTS=--recomp sh tests/sst68k.sh   lo mismo, por el camino del código generado
#   sh tests/sst68k.sh ADD.w -v 5      una familia, con el detalle de las 5 primeras que fallan
# Sale con 1 si alguna prueba falla.
cd "$(dirname "$0")/.." || exit 2
D=testroms/680x0/68000/v1
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
T=$(exe m68ktest)
if [ ! -f "$D/NOP.json.gz" ]; then echo "(sin $D: make testroms)"; exit 0; fi
if [ -n "$1" ]; then n=$1; shift; gunzip -c "$D/$n.json.gz" | "$T" - --name "$n" "$@"; exit $?; fi
pasan=0; total=0; fallan=0
for f in "$D"/*.json.gz; do
    n=$(basename "$f" .json.gz)
    r=$(gunzip -c "$f" | "$T" - --name "$n" $SST_OPTS)
    echo "$r"
    set -- $(echo "$r" | awk '{print $2}' | tr '/' ' ')
    pasan=$((pasan + $1)); total=$((total + $2))
    [ "$1" = "$2" ] || fallan=$((fallan + 1))
done
echo "SingleStepTests 68000${SST_OPTS:+ ($SST_OPTS)}: $pasan/$total ($fallan familias con fallas)"
[ $fallan -eq 0 ]
