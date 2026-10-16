#!/bin/sh
# difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.sh). Lo llama
# tests/run_tests.sh; sale con 0 si todo pasa.
# Waternet sin traza tiene que fallar (lo detecta); las demás, trazadas y recompiladas con esa traza, pasan.
cd "$(dirname "$0")/.." || exit 2
T=testroms
exec sh ../shared/estricto.sh -n $T/homebrew/waternet.sms $T/homebrew/waternet.sms $T/homebrew/waternet.gg $T/sms/contrato.sms
