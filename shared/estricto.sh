#!/bin/sh
# difftest --estricto con ROMs de prueba, común a los cinco (cada tests/estricto.sh lo llama desde su raíz):
#   sh ../shared/estricto.sh [-n ROM_SIN_TRAZA] ROM...
# - -n: esa ROM, recompilada sin traza, tiene que fallar con "ESTRICTO: código que falta" (lo detecta);
# - cada ROM, trazada con el *trace del sistema y las mismas entradas que difftest (600 cuadros, semilla 1) y
#   recompilada con esa traza, tiene que pasar: todo lo que se ejecutó de la ROM quedó recompilado (el código en RAM
#   sí puede ir al intérprete).
# La consola sale de qué recompilador hay en la carpeta (como recomp_game.sh). Sale con 0 si todo pasa.
CON=
for c in nes gb gen neo sms; do [ -f "./${c}recomp" ] || [ -f "./${c}recomp.exe" ] && CON=$c; done
[ -n "$CON" ] || { echo "no encuentro el recompilador en esta carpeta" >&2; exit 2; }
x=; [ -x "./${CON}recomp.exe" ] && x=.exe     # en Windows el objetivo del Makefile generado es difftest.exe
NEG=
[ "$1" = "-n" ] && { NEG=$2; shift 2; }
IN="--frames 600 --seed 1"
fail=0
mkdir -p build/out
proyecto() {    # proyecto ROM [--trace traza]: build/out/estricto con su difftest
    rm -rf build/out/estricto
    "./${CON}recomp$x" "$1" -o build/out/estricto --name estricto $2 $3 >/dev/null 2>&1 \
        && make -C build/out/estricto -j4 -s "difftest$x" >/dev/null 2>&1
}
if [ -n "$NEG" ]; then
    n=$(basename "$NEG")
    if [ ! -f "$NEG" ]; then echo "FALLA --estricto: falta $NEG (make testroms)"; fail=1
    elif proyecto "$NEG" && ! build/out/estricto/difftest$x "$NEG" $IN --estricto > build/estricto.out 2>&1 \
            && grep -q "^ESTRICTO: código que falta en \$[0-9A-F]" build/estricto.out; then
        echo "ok   --estricto: $n sin traza falla en el código que falta ($(sed -n 's/^ESTRICTO: código que falta en \(\$[0-9A-F]*\).*/\1/p' build/estricto.out))"
    else cat build/estricto.out 2>/dev/null; echo "FALLA --estricto no detecta el código que falta en $n"; fail=1; fi
fi
for r in "$@"; do
    n=$(basename "$r")
    [ -f "$r" ] || { echo "FALLA --estricto: falta $r (make testroms)"; fail=1; continue; }
    if "./${CON}trace$x" "$r" build/estricto.trace $IN >/dev/null 2>&1 && proyecto "$r" --trace build/estricto.trace \
            && build/out/estricto/difftest$x "$r" $IN --estricto > build/estricto.out 2>&1; then
        echo "ok   --estricto $n: $(grep '^Métrica:' build/estricto.out | sed 's/;.*//')"
    else cat build/estricto.out 2>/dev/null; echo "FALLA --estricto $n"; fail=1; fi
done
exit $fail
