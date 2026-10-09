#!/bin/sh
# Super Game Boy de 4 jugadores con Bomberman Collection (J): entra al modo
# BATTLE de Bomberman GB, los cuatro se inscriben cada uno con su control y,
# ya en la arena, cada uno camina hacia un lado (1 derecha, 2 izquierda,
# 3 abajo, 4 arriba). Deja la imagen en out/sgb_bomberman.png para revisarla:
# los cuatro Bomberman se alejaron de su esquina en esas direcciones. Llegar
# hasta ahí también requiere que el título de Bomberman GB se vea (antes se
# perdía su PAL_TRN y quedaba en blanco).
# Uso: sh tests/sgb_bomberman.sh "RUTA/Bomberman Collection (J) [S].gb"

cd "$(dirname "$0")/.." || exit 2
ROM=$1
[ -f "$ROM" ] || { echo "uso: sh tests/sgb_bomberman.sh RUTA/bomberman_collection.gb" >&2; exit 2; }
EXE=
[ -x ./gbtest.exe ] && EXE=.exe

make -s gbtest$EXE || exit 2
mkdir -p out
I=tests/inputs/bomberman_batalla
# gbtest termina con 2 ("sin resultado"): aquí no hay criterio automático
./gbtest$EXE "$ROM" --model sgb --seconds 63.7 --input ${I}_1.txt \
    --input2 ${I}_2.txt --input3 ${I}_3.txt --input4 ${I}_4.txt \
    --dump-sgb out/sgb_bomberman.png > /dev/null
[ $? -le 2 ] || exit 1
echo "Revisa out/sgb_bomberman.png: cuatro Bomberman fuera de sus esquinas"
