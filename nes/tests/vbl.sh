#!/bin/sh
# Resultado de las 10 pruebas ppu_vbl_nmi de Blargg (una letra cada una: P pasa, F falla)
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/shots
T=testroms/nes-test-roms
for f in $T/ppu_vbl_nmi/rom_singles/*.nes; do
    if ./nesshot "$f" build/shots/x.png 1200 >/dev/null; then printf P; else printf F; fi
done; echo
