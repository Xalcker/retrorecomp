#!/bin/sh
# Envoltorio: el código vive en shared/sst.sh (SingleStepTests de una CPU); uso: sh tests/sst.sh m68k|z80 [...]
cd "$(dirname "$0")/.." || exit 2
exec sh ../shared/sst.sh "$@"
