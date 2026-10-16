#!/bin/sh
# difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.sh). Lo llama
# tests/run_tests.sh; sale con 0 si todo pasa.
# Una ROM hecha aquí (1 KB, como tests/minima.sh) salta a $310 con una dirección calculada (move.l #$300,d0;
# addi.l #$10,d0; movea.l d0,a0; jmp (a0)), que el análisis estático no ve: sin traza tiene que fallar en $000310,
# y con la traza pasar. Las demos de testroms/ no sirven: tardan minutos en recompilarse y se descubren enteras.
cd "$(dirname "$0")/.." || exit 2
R=build/tests/salto.bin
mkdir -p build/tests
{ printf '\000\377\376\000\000\000\002\000'; head -c 248 /dev/zero; printf 'SEGA MEGA DRIVE '; head -c 240 /dev/zero
  printf '\040\074\000\000\003\000\006\200\000\000\000\020\040\100\116\320'; head -c 256 /dev/zero
  printf '\140\376'; head -c 494 /dev/zero; } > "$R"
exec sh ../shared/estricto.sh -n "$R" "$R"
