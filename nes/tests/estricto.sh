#!/bin/sh
# difftest --estricto con las ROMs de prueba (el piloto de --estricto, en NES): el intérprete no puede ejecutar una
# instrucción de PRG que no esté recompilada.
# - nestest recompilado sin traza tiene que fallar con "ESTRICTO: código que falta" (lo encuentra);
# - cada ROM, trazada con nestrace y las mismas entradas que difftest, recompilada con esa traza, tiene que pasar:
#   todo lo que se ejecutó quedó recompilado (el código en RAM sí puede ir al intérprete).
# Sale con 0 si todo pasa. Lo llama tests/run_tests.sh.
cd "$(dirname "$0")/.." || exit 2
x=; [ -x ./nesrecomp.exe ] && x=.exe      # en Windows el objetivo del Makefile generado es difftest.exe
T=testroms/nes-test-roms
fail=0
proyecto() {    # proyecto ROM NOMBRE [--trace traza]: build/out/NOMBRE con su difftest
    rm -rf "build/out/$2"
    ./nesrecomp$x "$1" -o "build/out/$2" --name "$2" $3 $4 >/dev/null 2>&1 && make -C "build/out/$2" -j4 -s "difftest$x" >/dev/null 2>&1
}
mkdir -p build/out
r=$T/other/nestest.nes
if proyecto $r estricto_sin && ! build/out/estricto_sin/difftest$x $r --frames 600 --seed 1 --estricto > build/estricto.out 2>&1 \
        && grep -q "^ESTRICTO: código que falta en \$[0-9A-F]\{4\}" build/estricto.out; then
    echo "ok   --estricto: nestest sin traza falla en el código que falta ($(sed -n 's/^ESTRICTO: código que falta en \(\$[0-9A-F]*\).*/\1/p' build/estricto.out))"
else cat build/estricto.out 2>/dev/null; echo "FALLA --estricto no detecta el código que falta"; fail=1; fi
for r in $T/other/nestest.nes $T/instr_test-v5/rom_singles/01-basics.nes $T/mmc3_test_2/rom_singles/3-A12_clocking.nes \
         $T/cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes $T/apu_test/rom_singles/7-dmc_basics.nes \
         $T/other/PCM.demo.wgraphics.nes $T/other/SimpleParallaxDemo.nes; do
    n=$(basename "$r" .nes)
    [ -f "$r" ] || { echo "FALLA --estricto: falta $r (make testroms)"; fail=1; continue; }
    if ./nestrace$x "$r" "build/estricto_$n.trace" --frames 600 --seed 1 >/dev/null 2>&1 \
            && proyecto "$r" estricto --trace "build/estricto_$n.trace" \
            && build/out/estricto/difftest$x "$r" --frames 600 --seed 1 --estricto > build/estricto.out 2>&1; then
        echo "ok   --estricto $n: $(grep '^Métrica:' build/estricto.out | sed 's/;.*//')"
    else cat build/estricto.out 2>/dev/null; echo "FALLA --estricto $n"; fail=1; fi
done
exit $fail
