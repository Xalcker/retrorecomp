#!/bin/sh
# Envoltorio: el código vive en retrorecomp (submódulo tools/retrorecomp, shared/). Es el mismo
# archivo en NESRecomp, GBRecomp, GENRecomp y NEORecomp; retrorecomp/tests/contract.py lo comprueba.
cd "$(dirname "$0")/.." || exit 2
[ -f tools/retrorecomp/shared/contract.sh ] || { echo "falta el submódulo: git submodule update --init" >&2; exit 2; }
exec sh tools/retrorecomp/shared/contract.sh "$@"
