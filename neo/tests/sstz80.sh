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
# Sin basename, grep, tail ni tr dentro del bucle: en MSYS2 (Windows) cada proceso cuesta, y con 1604
# archivos eso eran más de 4 minutos (GENRecomp#5). Por archivo solo corren gunzip y z80test.
cr=$(printf '\r'); nl='
'
for f in "$D"/*.json.gz; do
    n=${f##*/}; n=${n%.json.gz}
    r=$(gunzip -c "$f" | "$T" - --name "$n")
    l=${r##*"$nl"}; l=${l%"$cr"}            # última línea: "NOMBRE   pasan/total[  <--]"
    c=; set -f; for w in $l; do case $w in *[0-9]/[0-9]*) c=$w;; esac; done; set +f
    [ -n "$c" ] || c=0/1                 # sin resumen (¿se cayó?): cuenta como falla
    p=${c%/*}; t=${c#*/}
    pasan=$((pasan + p)); total=$((total + t))
    if [ "$p" != "$t" ]; then fallan=$((fallan + 1)); echo "$r"; fi
done
echo "SingleStepTests Z80: $pasan/$total ($fallan archivos con fallas)"
[ $fallan -eq 0 ]
