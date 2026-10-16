#!/bin/sh
# difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.sh). Lo llama
# tests/run_tests.sh; sale con 0 si todo pasa.
# dmg-acid2 sin traza tiene que fallar (lo detecta); las demás, trazadas y recompiladas con esa traza, pasan
# (cpu_instrs corre código en RAM: va al intérprete con el motivo «RAM» y no falla).
cd "$(dirname "$0")/.." || exit 2
T=testroms
exec sh ../shared/estricto.sh -n $T/dmg-acid2/dmg-acid2.gb $T/dmg-acid2/dmg-acid2.gb $T/cgb-acid2/cgb-acid2.gbc \
    $T/blargg/cpu_instrs/cpu_instrs.gb $T/mealybug-tearoom-tests/ppu/m3_scy_change.gb
