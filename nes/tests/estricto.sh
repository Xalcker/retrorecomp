#!/bin/sh
# difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.sh). Lo llama
# tests/run_tests.sh; sale con 0 si todo pasa.
# nestest sin traza tiene que fallar (lo detecta); las demás, trazadas y recompiladas con esa traza, pasan.
cd "$(dirname "$0")/.." || exit 2
T=testroms/nes-test-roms
exec sh ../shared/estricto.sh -n $T/other/nestest.nes $T/other/nestest.nes $T/instr_test-v5/rom_singles/01-basics.nes \
    $T/mmc3_test_2/rom_singles/3-A12_clocking.nes $T/cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes \
    $T/apu_test/rom_singles/7-dmc_basics.nes $T/other/PCM.demo.wgraphics.nes $T/other/SimpleParallaxDemo.nes
