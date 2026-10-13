#!/bin/sh
# make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
#   - testroms/680x0: SingleStepTests del 68000 (github.com/SingleStepTests/680x0, un commit fijo, el
#     mismo que GENRecomp; ~200 MB que se quedan comprimidos: m68ktest los lee con gunzip -c)
#   - testroms/z80: SingleStepTests del Z80 (github.com/SingleStepTests/z80, un commit fijo, el mismo
#     que GENRecomp; se guardan comprimidas, z80test las lee con gunzip -c)
#   - testroms/neo/contrato.neo: el juego de prueba del contrato común, hecho aquí (src/tools/mkcontrato.c,
#     MIT): no se baja de ningún lado. Necesita la BIOS del AES (la busca junto al juego, en roms/ o en
#     NEO_BIOS), como cualquier juego
# Las otras pruebas de Neo Geo (ngdevkit, 240p Test Suite) llegan en las fases siguientes (docs/PLAN.md).
cd "$(dirname "$0")/.." || exit 2
mkdir -p testroms build

# SKIP_SST=1 (el CI de macOS y Windows): sin las SingleStepTests, que son lo que más pesa; make test las omite
# si no están. En Linux y en tu máquina se bajan completas.
if [ -n "$SKIP_SST" ]; then echo "SKIP_SST: sin las SingleStepTests del 68000 ni del Z80"
else
if [ -f testroms/680x0/68000/v1/NOP.json.gz ]; then echo "testroms/680x0 ya tiene las SingleStepTests del 68000"
else
    mkdir -p testroms/680x0 && \
    curl -fsSL -o build/680x0.tar.gz https://codeload.github.com/SingleStepTests/680x0/tar.gz/e0d5ece9670205cc84a0101081837deb446f86a3 && \
    tar -xzf build/680x0.tar.gz -C testroms/680x0 --strip-components=1 && rm -f build/680x0.tar.gz && \
    echo "SingleStepTests del 68000 en testroms/680x0" || exit 1
fi

if [ -f "testroms/z80/v1/00.json.gz" ]; then echo "testroms/z80 ya tiene las SingleStepTests del Z80"
else
    rm -rf build/z80tests && mkdir -p build/z80tests testroms/z80/v1 && \
    curl -fsSL -o build/z80.tar.gz https://codeload.github.com/SingleStepTests/z80/tar.gz/ebe1875d48f374bcfd4b505d8eb8ee751568b5f7 && \
    tar -xzf build/z80.tar.gz -C build/z80tests --strip-components=1 && rm -f build/z80.tar.gz && \
    for f in build/z80tests/v1/*.json; do gzip -c "$f" > "testroms/z80/v1/$(basename "$f").gz" || exit 1; done && \
    cp build/z80tests/LICENSE testroms/z80/ && rm -rf build/z80tests && \
    echo "SingleStepTests del Z80 en testroms/z80" || exit 1
fi
fi

mkdir -p testroms/neo
# En Windows (MSYS2) los programas llevan .exe y el objetivo del Makefile también
x=; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) x=.exe;; esac
make -s "build/mkcontrato$x" && "build/mkcontrato$x" testroms/neo/contrato.neo || exit 1
