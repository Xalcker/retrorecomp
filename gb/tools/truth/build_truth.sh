#!/bin/sh
# Construye el mapa "verdadero" de código y datos de Tetris a partir del
# desensamblado de kaspermeerts/tetris, para medir el descubrimiento de gbrecomp.
#
# Uso: sh tools/truth/build_truth.sh DESENSAMBLADO RGBDS ROM SALIDA
#   DESENSAMBLADO  clon de https://github.com/kaspermeerts/tetris
#   RGBDS          carpeta con rgbasm, rgblink, rgbfix y rgbgfx (v0.6.1)
#   ROM            Tetris (JUE) (V1.1), para comprobar que el resultado es idéntico
#   SALIDA         carpeta de trabajo; deja ahí truth.txt
set -e
DIS=$(cd "$1" && pwd)
R=$(cd "$2" && pwd)
ROM=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$OUT"
cp -r "$DIS"/* "$OUT"/
cd "$OUT"

# Gráficos: los PNG traen mosaicos vacíos al final que la ROM no incluye
"$R/rgbgfx" -x 11 -o gfx/configandgameplay.2bpp gfx/configandgameplay.png
"$R/rgbgfx" -x 9 -o gfx/copyrightandtitlescreen.2bpp gfx/copyrightandtitlescreen.png
"$R/rgbgfx" -x 1 -o gfx/multiplayerandburan.2bpp gfx/multiplayerandburan.png
"$R/rgbgfx" -d 1 -x 9 -o gfx/font.1bpp gfx/font.png

perl "$HERE/instrument.pl" "$DIS" . markers.txt tetris.asm audio.asm sprites.asm music.asm

for f in tetris audio wram hram music sprites; do
    "$R/rgbasm" -Wno-obsolete --export-all --halt-without-nop --preserve-ld -o $f.o $f.asm
done
"$R/rgblink" --dmg --tiny --sym truth.sym --map truth.map -o truth.gb \
    tetris.o audio.o wram.o hram.o music.o sprites.o
"$R/rgbfix" --validate --title TETRIS --old-licensee 1 --rom-version 1 truth.gb

if cmp -s truth.gb "$ROM"; then
    echo "ROM instrumentada idéntica a la original"
else
    echo "ERROR: la ROM instrumentada no es idéntica" >&2
    exit 1
fi

perl "$HERE/make_truth.pl" markers.txt truth.sym truth.map 32768 > truth.txt
echo "mapa en $OUT/truth.txt"
