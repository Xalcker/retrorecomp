#!/bin/sh
# make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
#   - testroms/680x0: SingleStepTests del 68000 (github.com/SingleStepTests/680x0, un commit fijo;
#     ~200 MB que se quedan comprimidos: m68ktest los lee con gunzip -c)
#   - testroms/z80: SingleStepTests del Z80 (github.com/SingleStepTests/z80, un commit fijo; se
#     guardan comprimidas, z80test las lee con gunzip -c)
#   - testroms/demos: las demos de Titan de scene.org (gratuitas; se comprueba el SHA-256 del zip):
#     overdrive.bin (Overdrive, Evoke 2013, la versión para hardware) y overdrive2.bin (Overdrive 2,
#     Revision 2017: 8 MB con el mapper de SSF2, solo PAL; la meta más exigente que hay)
#   - testroms/240p (a mano, opcional): el 240p Test Suite de Artemio Urbina (GPL). Su ROM compilada
#     está en itch.io (artemiourbina.itch.io/240p-test-suite), que no se puede bajar desde un script;
#     se pone el .bin en testroms/240p/.
cd "$(dirname "$0")/.." || exit 2
mkdir -p testroms build

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

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

# demo archivo-en-scene.org sha256-del-zip miembro-del-zip destino
demo() {
    if [ -f "testroms/demos/$4" ]; then echo "testroms/demos/$4 ya está"; return 0; fi
    mkdir -p testroms/demos
    z=build/$(basename "$1")
    curl -fsSL -o "$z" "https://files.scene.org/get/$1" || { echo "no se pudo bajar $1"; return 1; }
    if [ "$(sha256 "$z")" != "$2" ]; then echo "$1: el SHA-256 no coincide"; rm -f "$z"; return 1; fi
    unzip -p "$z" "$3" > "testroms/demos/$4" && rm -f "$z" && echo "testroms/demos/$4"
}
demo parties/2013/evoke13/wild/titan-overdrivemegademo-v1.1-final.zip \
     e8cdcc8c828362ed6c4c6d223bc5052a062126d9dc3db6bf85686dfa5856d6ff \
     "TiTAN - Overdrive (Rev1.1-106-Final) (Hardware).bin" overdrive.bin || exit 1
demo parties/2017/revision17/oldskool_demo/titan-overdrive2.zip \
     1fa9ebdfc4129ec8abf17846634427b4b8dca53c060efa27a3391f76ad0742e1 \
     titan-overdrive2.bin overdrive2.bin || exit 1
if ls testroms/240p/*.bin >/dev/null 2>&1; then echo "testroms/240p: $(ls testroms/240p/*.bin | head -1)"
else echo "(opcional: el 240p Test Suite se baja a mano de artemiourbina.itch.io/240p-test-suite y va en testroms/240p/)"; fi
