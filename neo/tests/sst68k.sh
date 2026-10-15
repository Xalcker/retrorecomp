#!/bin/sh
# Envoltorio: el código vive en retrorecomp (submódulo tools/retrorecomp, shared/sst68k.sh), común a GENRecomp y
# NEORecomp; retrorecomp/tests/contract.py comprueba que este archivo sea el mismo en los dos.
cd "$(dirname "$0")/.." || exit 2
[ -f tools/retrorecomp/shared/sst68k.sh ] || { echo "falta el submódulo: git submodule update --init" >&2; exit 2; }
exec sh tools/retrorecomp/shared/sst68k.sh "$@"
