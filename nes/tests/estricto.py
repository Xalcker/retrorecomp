#!/usr/bin/env python3
"""difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.py). Lo llama
make test: cada ROM, trazada y recompilada con su traza, tiene que pasar --estricto (y nestest sin traza tiene que fallar).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import estricto  # noqa: E402
import rr  # noqa: E402

T = "testroms/nes-test-roms"
if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(estricto.main(["-n", f"{T}/other/nestest.nes", f"{T}/other/nestest.nes", f"{T}/instr_test-v5/rom_singles/01-basics.nes",
                            f"{T}/mmc3_test_2/rom_singles/3-A12_clocking.nes", f"{T}/cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes",
                            f"{T}/apu_test/rom_singles/7-dmc_basics.nes", f"{T}/other/PCM.demo.wgraphics.nes",
                            f"{T}/other/SimpleParallaxDemo.nes"]))
