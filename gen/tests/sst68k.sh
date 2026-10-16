#!/bin/sh
# Envoltorio: el código vive en retrorecomp (shared/sst68k.sh), común a GENRecomp y
# NEORecomp; retrorecomp/tests/contract.py comprueba que este archivo sea el mismo en los dos.
cd "$(dirname "$0")/.." || exit 2
exec sh ../shared/sst68k.sh "$@"
