#!/bin/sh
# Prueba de la tarjeta de memoria de la BIOS propia: corre tarjeta.guion con tools/neocard en el juego de prueba
# del contrato (testroms/neo/contrato.neo) y compara las respuestas y lo cargado con tarjeta.esperado, y lo que
# queda en la tarjeta (tools/neobios/tarjeta.py) con tarjeta.estructura. Los dos salieron de la BIOS original
# (las dos BIOS dan lo mismo):
#
#   sh tests/neobios/prueba_tarjeta.sh                    la BIOS propia (build/neobios.bin)
#   sh tests/neobios/prueba_tarjeta.sh --esperado BIOS    rehace lo esperado con otra BIOS (la de SNK)
cd "$(dirname "$0")/../.." || exit 2
x=; [ -x ./neocard.exe ] && x=.exe
bios=build/neobios.bin; rehacer=
[ "$1" = "--esperado" ] && { rehacer=1; bios=$2; }
rom=testroms/neo/contrato.neo
[ -f "$rom" ] || sh tests/testroms.sh >/dev/null 2>&1
mkdir -p build/tests
rm -f build/tests/tarjeta.bin
ordenes=$(grep -v '^#' tests/neobios/tarjeta.guion)
# shellcheck disable=SC2086
"./neocard$x" "$rom" --frames 300 --bios "$bios" --card build/tests/tarjeta.bin $ordenes 2>&1 | tail -n +2 \
    | sed -e 's/  ([0-9]* ciclos)//' > build/tests/tarjeta.txt
python3 tools/neobios/tarjeta.py build/tests/tarjeta.bin > build/tests/tarjeta.est
if [ -n "$rehacer" ]; then
    cp build/tests/tarjeta.txt tests/neobios/tarjeta.esperado
    cp build/tests/tarjeta.est tests/neobios/tarjeta.estructura
    echo "escrito tests/neobios/tarjeta.esperado y tarjeta.estructura con $bios"
    exit 0
fi
n=$(grep -c ': respuesta' tests/neobios/tarjeta.esperado)
if cmp -s build/tests/tarjeta.txt tests/neobios/tarjeta.esperado && cmp -s build/tests/tarjeta.est tests/neobios/tarjeta.estructura; then
    echo "ok    tarjeta de memoria con la BIOS propia: $n llamadas a CARD como con la original"
else
    echo "FALLA tarjeta de memoria con la BIOS propia (contra tests/neobios/tarjeta.esperado y .estructura)"
    diff tests/neobios/tarjeta.esperado build/tests/tarjeta.txt | head -10
    diff tests/neobios/tarjeta.estructura build/tests/tarjeta.est | head -10
    exit 1
fi
