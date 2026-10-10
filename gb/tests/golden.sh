#!/bin/sh
# Línea base exacta: hash de todos los cuadros (pantalla del GB y del SGB) y de
# todo el audio de varios juegos y ROMs de prueba, corridos en el intérprete
# con entradas fijas. Una optimización del runtime no debe cambiar ni un byte.
#   sh tests/golden.sh            compara con tests/golden.txt
#   sh tests/golden.sh --update   reescribe tests/golden.txt con el estado actual
#
# Los juegos se buscan en roms/ y las pruebas en testroms/ (no se versionan); si
# falta alguno, su línea sale como "falta" y la comparación lo marca. Los hashes
# dependen del compilador y la plataforma (el APU usa punto flotante): la línea
# base es de una máquina; tras un cambio que sí debe cambiar la salida (un
# arreglo de la PPU, por ejemplo), se revisa a ojo y se corre con --update.
cd "$(dirname "$0")/.." || exit 2
EXE=
[ -x ./gbtest.exe ] && EXE=.exe
[ -x ./gbtest$EXE ] || { echo "falta gbtest: corre make" >&2; exit 2; }
R=roms
T=testroms
G=tests/inputs/golden.txt
OUT=build/golden
rm -rf "$OUT"
mkdir -p "$OUT"

# Cada caso corre en segundo plano y deja su línea en build/golden/NOMBRE.
# Uso: caso NOMBRE ROM SEGUNDOS [opciones de gbtest...]
caso() {
    name=$1; rom=$2; secs=$3; shift 3
    (
        if [ ! -f "$rom" ]; then
            echo "$name falta" > "$OUT/$name"
            exit
        fi
        h=$(./gbtest$EXE "$rom" --seconds "$secs" --hash "$@" 2>/dev/null | sed -n 's/^hash //p')
        echo "$name ${h:-error}" > "$OUT/$name"
    ) &
}

# Juegos: un minuto de juego con las mismas entradas (40 s) en cada consola
caso tetris      "$R/Tetris (World) (Rev 1).gb" 40 --input tests/inputs/partida1.txt
caso tetris_gbc  "$R/Tetris (World) (Rev 1).gb" 40 --input "$G" --model cgb
caso sml         "$R/Super Mario Land (World).gb" 40 --input "$G"
caso pokered     "$R/Pokemon - Red Version (USA, Europe) (SGB Enhanced).gb" 40 --input "$G"
caso wario2      "$R/Wario Land II (USA, Europe) (SGB Enhanced).gb" 40 --input "$G"
caso dkl2        "$R/Donkey Kong Land 2 (USA, Europe) (SGB Enhanced).gb" 40 --input "$G"
caso tetrisdx    "$R/Tetris DX (World) (SGB Enhanced) (GB Compatible).gbc" 40 --input "$G"
caso smbdx       "$R/Super Mario Bros. Deluxe (USA, Europe).gbc" 40 --input "$G"
caso pokegold    "$R/Pokemon - Gold Version (USA, Europe) (SGB Enhanced) (GB Compatible).gbc" 40 --input "$G"
caso pokeyellow  "$R/Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced).gb" 40 --input "$G"
caso harrypotter "$R/Harry Potter and the Chamber of Secrets (USA, Europe) (En,Fr,De,Es,It,Nl,Pt,Sv,Da).gbc" 40 --input "$G"
wait

# Pruebas de la PPU y del APU (incluye las de Mealybug que todavía fallan:
# si un cambio las arregla, la diferencia aquí es la esperada)
caso dmg_acid2      "$T/dmg-acid2/dmg-acid2.gb" 3 --model dmg
caso dmg_acid2_cgb  "$T/dmg-acid2/dmg-acid2.gb" 3 --model cgb
caso cgb_acid2      "$T/cgb-acid2/cgb-acid2.gbc" 3
caso cgb_acid_hell  "$T/cgb-acid-hell/cgb-acid-hell.gbc" 3
for t in m3_bgp_change_sprites m3_lcdc_obj_size_change_scx m3_lcdc_win_en_change_multiple_wx \
         m3_scx_low_3_bits m3_scy_change m3_window_timing m2_win_en_toggle; do
    caso "mealybug_$t" "$T/mealybug-tearoom-tests/ppu/$t.gb" 2 --model dmg
done
caso hdma_timing    "$T/mealybug-tearoom-tests/dma/hdma_timing-C.gb" 2 --model cgb
caso cgb_sound      "$T/blargg/cgb_sound/cgb_sound.gb" 40 --model cgb
wait

ls "$OUT" | sort | while read -r f; do cat "$OUT/$f"; done > "$OUT.txt"
n=$(wc -l < "$OUT.txt" | tr -d ' ')
if [ "$1" = "--update" ]; then
    cp "$OUT.txt" tests/golden.txt
    echo "tests/golden.txt actualizado ($n casos)"
    grep -E " (falta|error)$" "$OUT.txt"
    exit 0
fi
[ -f tests/golden.txt ] || { echo "no hay tests/golden.txt: corre sh tests/golden.sh --update" >&2; exit 2; }
if diff -u tests/golden.txt "$OUT.txt"; then
    echo "golden: igual ($n casos)"
else
    echo "golden: DIFERENTE"
    exit 1
fi
