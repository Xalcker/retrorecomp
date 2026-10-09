#!/bin/sh
# Cable link con Warlocked (GBC, doble velocidad y reloj rápido del serial):
# dos Game Boy Color entran al modo VS, pasan por "Head 2 Head" y el briefing
# del mapa GOLD RUSH y juegan; durante la partida intercambian unos 70 bytes
# por cuadro. Deja la captura final en out/link_warlocked.png (izquierda A
# con los humanos, derecha B con las bestias).
# Uso: sh tests/link_warlocked.sh RUTA/warlocked.gbc

cd "$(dirname "$0")/.." || exit 2
ROM=$1
[ -f "$ROM" ] || { echo "uso: sh tests/link_warlocked.sh RUTA/warlocked.gbc" >&2; exit 2; }
EXE=
[ -x ./linktest.exe ] && EXE=.exe

make -s linktest$EXE || exit 2
mkdir -p out
# En 3600 cuadros pasan unos 62,000 bytes (la partida empieza en el ~2700)
./linktest$EXE "$ROM" --frames 3600 \
    --input-a tests/inputs/warlocked_vs_a.txt --input-b tests/inputs/warlocked_vs_b.txt \
    --dump out/link_warlocked.png --min-bytes 60000
