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
# Sin basename, awk ni tr dentro del bucle: en MSYS2 (Windows) cada proceso cuesta (GENRecomp#5).
# Por familia solo corren gunzip y m68ktest.
cr=$(printf '\r')
for f in "$D"/*.json.gz; do
    n=${f##*/}; n=${n%.json.gz}
    r=$(gunzip -c "$f" | "$T" - --name "$n" $SST_OPTS)
    echo "$r"
    l=${r%"$cr"}                    # "FAMILIA   pasan/total[  <--][  (… omitidas …)]"
    c=; set -f; for w in $l; do case $w in *[0-9]/[0-9]*) c=$w;; esac; done; set +f
    [ -n "$c" ] || c=0/1                 # sin resumen (¿se cayó?): cuenta como falla
    p=${c%/*}; t=${c#*/}
    pasan=$((pasan + p)); total=$((total + t))
    [ "$p" = "$t" ] || fallan=$((fallan + 1))
done
echo "SingleStepTests 68000${SST_OPTS:+ ($SST_OPTS)}: $pasan/$total ($fallan familias con fallas)"
[ $fallan -eq 0 ]
