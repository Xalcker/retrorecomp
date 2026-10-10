#!/bin/sh
# Contrato de línea de comandos común con NESRecomp/GBRecomp: las herramientas sin ventana
# aceptan las mismas opciones (--frames, --input, --seed, --repeat). Ejecuta cada una sin
# argumentos y comprueba que su ayuda las menciona. Es la misma prueba en los dos repositorios
# (y en retrorecomp/tests/contract.py, que mira los dos a la vez). Necesita `make` hecho.
#   sh tests/contract.sh
cd "$(dirname "$0")/.." || exit 2
if [ -f src/gbrecomp/gbrecomp.c ]; then
    P=gb
    CHECKS="gbtrace:--frames,--input,--seed gbshot:--frames,--input,--seed gbaudio:--frames,--input,--seed gbbench:--frames,--input,--repeat"
elif [ -f src/nesrecomp/nesrecomp.c ]; then
    P=nes
    CHECKS="nestrace:--frames,--input,--seed nesshot:--frames,--input,--seed nesaudio:--frames,--input,--seed nesbench:--frames,--input,--repeat"
else
    echo "no es la raíz de NESRecomp ni de GBRecomp" >&2; exit 2
fi
fallas=0
for c in $CHECKS; do
    t=${c%%:*}; opts=${c#*:}
    exe=./$t
    [ -x ./$t.exe ] && exe=./$t.exe
    if [ ! -x "$exe" ]; then echo "FALLA $t: no está compilado"; fallas=$((fallas + 1)); continue; fi
    uso=$("$exe" 2>&1 </dev/null)
    faltan=
    for o in $(echo "$opts" | tr ',' ' '); do
        echo "$uso" | grep -q -- "$o" || faltan="$faltan $o"
    done
    if [ -n "$faltan" ]; then echo "FALLA $t: la ayuda no menciona$faltan"; fallas=$((fallas + 1)); else echo "ok    $t"; fi
done
[ $fallas -eq 0 ] && echo "Contrato común: sin fallas." || echo "Contrato común: $fallas fallas."
[ $fallas -eq 0 ]
