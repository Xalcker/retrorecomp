#!/bin/sh
# SingleStepTests de una CPU (make testroms): corre cada archivo con m68ktest o z80test.
#   sh tests/sst.sh m68k                       resumen por familia (intérprete)
#   SST_OPTS=--recomp sh tests/sst.sh m68k     lo mismo, por el camino del código generado
#   sh tests/sst.sh m68k ADD.w -v 5            una familia, con el detalle de las 5 primeras que fallan
#   sh tests/sst.sh z80                        resumen (solo los que fallan, y el total)
#   sh tests/sst.sh z80 "ed b0" -v 5           un archivo, con el detalle de las 5 primeras que fallan
# Sale con 1 si alguna prueba falla. Común a todos: lo llama el tests/sst.sh de cada sistema desde su raíz.
case $1 in
    m68k) D=testroms/680x0/68000/v1; T=m68ktest; PRIMERO=NOP; NOM="68000"; PLURAL=familias ;;
    z80)  D=testroms/z80/v1;         T=z80test;  PRIMERO=00;  NOM="Z80";   PLURAL=archivos ;;
    *) echo "uso: sst.sh m68k|z80 [NOMBRE [opciones]]" >&2; exit 2 ;;
esac
shift
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
T=$(exe $T)
if [ ! -f "$D/$PRIMERO.json.gz" ]; then echo "(sin $D: make testroms)"; exit 0; fi
if [ -n "$1" ]; then n=$1; shift; gunzip -c "$D/$n.json.gz" | "$T" - --name "$n" "$@"; exit $?; fi
pasan=0; total=0; fallan=0
# Sin basename, grep, tail, awk ni tr dentro del bucle: en MSYS2 (Windows) cada proceso cuesta, y con 1604
# archivos del Z80 eso eran más de 4 minutos (GENRecomp#5). Por archivo solo corren gunzip y la prueba.
cr=$(printf '\r'); nl='
'
for f in "$D"/*.json.gz; do
    n=${f##*/}; n=${n%.json.gz}
    r=$(gunzip -c "$f" | "$T" - --name "$n" $SST_OPTS)
    l=${r##*"$nl"}; l=${l%"$cr"}            # última línea: "NOMBRE   pasan/total[  <--]"
    c=; set -f; for w in $l; do case $w in *[0-9]/[0-9]*) c=$w;; esac; done; set +f
    [ -n "$c" ] || c=0/1                 # sin resumen (¿se cayó?): cuenta como falla
    p=${c%/*}; t=${c#*/}
    pasan=$((pasan + p)); total=$((total + t))
    if [ "$p" != "$t" ]; then fallan=$((fallan + 1)); [ $PLURAL = archivos ] && echo "$r"; fi
    [ $PLURAL = familias ] && echo "$r"
done
echo "SingleStepTests $NOM${SST_OPTS:+ ($SST_OPTS)}: $pasan/$total ($fallan $PLURAL con fallas)"
[ $fallan -eq 0 ]
