#!/bin/sh
# Contrato de línea de comandos común con NESRecomp/GBRecomp/GENRecomp. Es el mismo archivo en los
# tres repositorios (y retrorecomp/tests/contract.py los mira todos a la vez y comprueba que este
# archivo sea idéntico en cada uno). Necesita `make` hecho.
#   sh tests/contract.sh
#
# 1. Opciones: las herramientas sin ventana aceptan las mismas (--frames, --input, --seed,
#    --repeat; --seed también en bench); se ejecuta cada una sin argumentos y su ayuda debe mencionarlas.
# 2. Comportamiento (con las ROMs de prueba de `make testroms`; sin ellas se omite): la traza, la
#    captura y el audio salen iguales con la misma entrada (--seed, y --input con --seed, que se
#    suman), con un mismo guion para todas las consolas; en NES y Genesis, --input da lo mismo que
#    la sintaxis de antes (boton:desde-hasta).
cd "$(dirname "$0")/.." || exit 2
if [ -f src/gbrecomp/gbrecomp.c ]; then
    P=gb
    CHECKS="gbtrace:--frames,--input,--seed gbshot:--frames,--input,--seed gbaudio:--frames,--input,--seed gbbench:--frames,--input,--repeat,--seed"
    ROM=testroms/blargg/cpu_instrs/cpu_instrs.gb
    TOOLS="gbtrace:txt gbshot:png gbaudio:wav"
elif [ -f src/nesrecomp/nesrecomp.c ]; then
    P=nes
    CHECKS="nestrace:--frames,--input,--seed nesshot:--frames,--input,--seed nesaudio:--frames,--input,--seed nesbench:--frames,--input,--repeat,--seed"
    ROM=testroms/nes-test-roms/other/nestest.nes
    TOOLS="nestrace:txt nesshot:png nesaudio:wav"
elif [ -f src/genrecomp/genrecomp.c ]; then
    P=gen
    CHECKS="gentrace:--frames,--input,--seed genshot:--frames,--input,--seed genaudio:--frames,--input,--seed genbench:--frames,--input,--repeat,--seed"
    ROM=testroms/demos/overdrive.bin
    TOOLS="gentrace:txt genshot:png genaudio:wav"
else
    echo "no es la raíz de NESRecomp, GBRecomp ni GENRecomp" >&2; exit 2
fi
fallas=0
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }

# --- 1. Opciones ---
for c in $CHECKS; do
    t=${c%%:*}; opts=${c#*:}
    e=$(exe "$t")
    if [ ! -x "$e" ]; then echo "FALLA $t: no está compilado"; fallas=$((fallas + 1)); continue; fi
    uso=$("$e" 2>&1 </dev/null)
    faltan=
    for o in $(echo "$opts" | tr ',' ' '); do
        echo "$uso" | grep -q -- "$o" || faltan="$faltan $o"
    done
    if [ -n "$faltan" ]; then echo "FALLA $t: la ayuda no menciona$faltan"; fallas=$((fallas + 1)); else echo "ok    $t"; fi
done

# --- 2. Comportamiento ---
D=build/contract
suma() { cksum < "$1" | cut -d' ' -f1-2; }
igual() {   # descripción archivo1 archivo2: deben existir y ser idénticos
    if [ -s "$2" ] && [ -s "$3" ] && [ "$(suma "$2")" = "$(suma "$3")" ]; then echo "ok    $1"
    else echo "FALLA $1"; fallas=$((fallas + 1)); fi
}
if [ -f "$ROM" ]; then
    rm -rf "$D"; mkdir -p "$D"
    # El mismo guion en las dos consolas (inputs.h): Start en 30-35, abajo+A en 60-69
    printf '# contrato\n0 -\n30 START\n36 -\n60 DOWN+A\n70 -\n' > "$D/guion.txt"
    for t in $TOOLS; do
        n=${t%%:*}; x=${t#*:}; e=$(exe "$n")
        "$e" "$ROM" "$D/a.$x" --frames 120 --seed 7 >/dev/null 2>&1
        "$e" "$ROM" "$D/b.$x" --frames 120 --seed 7 >/dev/null 2>&1
        igual "$n: --seed da lo mismo dos veces" "$D/a.$x" "$D/b.$x"
        "$e" "$ROM" "$D/c.$x" --frames 120 --input "$D/guion.txt" --seed 7 >/dev/null 2>&1
        "$e" "$ROM" "$D/d.$x" --frames 120 --input "$D/guion.txt" --seed 7 >/dev/null 2>&1
        igual "$n: --input con --seed da lo mismo dos veces" "$D/c.$x" "$D/d.$x"
    done
    if [ $P = nes ] || [ $P = gen ]; then
        # Hash de todos los cuadros y de todo el audio (nesshot/genshot --hash): distingue un cuadro de diferencia
        e=$(exe ${P}shot)
        "$e" "$ROM" "$D/e.png" --frames 120 --input "$D/guion.txt" --hash 2>/dev/null | grep '^hash' > "$D/guion.hash"
        "$e" "$ROM" "$D/e.png" 120 start:30-35 down:60-69 a:60-69 --hash 2>/dev/null | grep '^hash' > "$D/antes.hash"
        igual "${P}shot: --input da lo mismo que boton:desde-hasta" "$D/guion.hash" "$D/antes.hash"
    fi
else
    echo "(sin $ROM se omiten las pruebas de comportamiento: make testroms)"
fi

[ $fallas -eq 0 ] && echo "Contrato común: sin fallas." || echo "Contrato común: $fallas fallas."
[ $fallas -eq 0 ]
