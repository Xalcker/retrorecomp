#!/bin/sh
# La ROM mínima de las pruebas (1 KB): vectores (SSP $00FFFE00, PC $200), "SEGA MEGA DRIVE " en $100 y un bra.s $200
# (60 FE) en $200. La usa make test (tests/run_tests.sh).
#   sh tests/minima.sh build/tests/minima.bin
mkdir -p "$(dirname "$1")"
{ printf '\000\377\376\000\000\000\002\000'; head -c 248 /dev/zero; printf 'SEGA MEGA DRIVE '
  head -c 240 /dev/zero; printf '\140\376'; head -c 510 /dev/zero; } > "$1"
