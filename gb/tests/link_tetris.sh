#!/bin/sh
# Cable link con Tetris: dos Game Boy conectados juegan una partida de dos
# jugadores (A inicia, B deja caer todo hasta perder) y llegan a las pantallas
# de victoria y derrota. Deja la captura final en build/link_tetris.png
# (izquierda A: Mario celebra; derecha B).
# Uso: sh tests/link_tetris.sh RUTA/tetris.gb
#   (Tetris (JUE) (V1.1), SHA1 74591cc9501af93873f9a5d3eb12da12c0723bbc)

cd "$(dirname "$0")/.." || exit 2
ROM=$1
[ -f "$ROM" ] || { echo "uso: sh tests/link_tetris.sh RUTA/tetris.gb" >&2; exit 2; }
EXE=
[ -x ./linktest.exe ] && EXE=.exe

make -s linktest$EXE || exit 2
mkdir -p build
# El intercambio después de la derrota lleva el total a más de 2440 bytes
./linktest$EXE "$ROM" --frames 5400 \
    --input-a tests/inputs/tetris_2p_a.txt --input-b tests/inputs/tetris_2p_b.txt \
    --dump build/link_tetris.png --min-bytes 2440
