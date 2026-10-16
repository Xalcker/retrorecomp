#!/bin/sh
# Compila y prueba un sistema como lo hace el CI, en cualquier sistema operativo:
#   sh tests/ci_sistema.sh gen            todo
#   RAPIDAS=1 sh tests/ci_sistema.sh gen  sin lo más caro (Overdrive en GEN; ZEX y Dragon Blaster en SMS)
#   SKIP_SST=1 ...                        sin las SingleStepTests (no las baja ni las corre)
#   DIST=1 ...                            además arma el paquete (make dist)
# Sin RAPIDAS también corre la línea base exacta (tests/golden.sh --libres) en las ROMs libres de testroms/, con
# el intérprete y con el código recompilado: los hashes son los mismos en Linux, macOS y Windows.
s=$1
[ -n "$s" ] && [ -f "$s/Makefile" ] || { echo "uso: sh tests/ci_sistema.sh gb|nes|gen|neo|sms" >&2; exit 2; }
cd "$s" || exit 2
set -e
echo "::group::$s: compilar"
make -j4
[ "$s" = neo ] && make neobios
echo "::endgroup::"
echo "::group::$s: ROMs de prueba"
make testroms
echo "::endgroup::"
echo "::group::$s: contrato"
if [ "$s" = neo ]; then NEO_BIOS=build/neobios.bin sh tests/contract.sh; else sh tests/contract.sh; fi
echo "::endgroup::"
echo "::group::$s: pruebas"
make test
echo "::endgroup::"
if [ -z "$RAPIDAS" ]; then
    echo "::group::$s: golden (ROMs libres)"
    sh tests/golden.sh --libres
    sh tests/golden.sh --libres --recompilado
    echo "::endgroup::"
fi
if [ -n "$DIST" ]; then make dist; fi
