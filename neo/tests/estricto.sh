#!/bin/sh
# difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.sh). Lo llama
# tests/run_tests.sh; sale con 0 si todo pasa.
# El juego del contrato, trazado y recompilado con esa traza, pasa. No hay una ROM libre en la que falte código sin
# traza (el análisis estático encuentra todo contrato.neo): la detección se prueba en GEN, con el mismo despachador del
# 68000. Necesita NEO_BIOS (run_tests.sh la fija).
cd "$(dirname "$0")/.." || exit 2
exec sh ../shared/estricto.sh testroms/neo/contrato.neo
