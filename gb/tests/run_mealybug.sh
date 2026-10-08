#!/bin/sh
# Pruebas de Mealybug Tearoom (cambios de registros a mitad de línea) con
# referencia DMG. Uso: sh tests/run_mealybug.sh [filtro]
cd "$(dirname "$0")/.." || exit 2
GBTEST=./gbtest
[ -x ./gbtest.exe ] && GBTEST=./gbtest.exe
D=testroms/mealybug-tearoom-tests/ppu
mkdir -p build/mealybug
pasa=0; total=0
for t in "$D"/*.gb; do
    b=$(basename "$t" .gb)
    case "$b" in *"$1"*) ;; *) continue ;; esac
    e=$D/${b}_dmg_blob.png
    [ -f "$e" ] || e=$D/${b}_dmg_b.png
    [ -f "$e" ] || continue
    total=$((total + 1))
    if out=$("$GBTEST" "$t" --until-ldbb --seconds 5 --expect "$e" --dump "build/mealybug/$b.png" 2>&1); then
        pasa=$((pasa + 1))
    else
        echo "$out" | tr '\n' ' '; echo
    fi
done
echo "Mealybug: pasan $pasa de $total"
[ $pasa -eq $total ]
