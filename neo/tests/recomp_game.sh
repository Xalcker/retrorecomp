#!/bin/sh
# Envoltorio: el código vive en retrorecomp (shared/). Es el mismo
# archivo en NESRecomp, GBRecomp, GENRecomp y NEORecomp; retrorecomp/tests/contract.py lo comprueba.
cd "$(dirname "$0")/.." || exit 2
exec sh ../shared/recomp_game.sh "$@"
