#!/usr/bin/env python3
"""difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.py). Lo llama
make test: cada ROM, trazada y recompilada con su traza, tiene que pasar --estricto (y dmg-acid2 sin traza tiene que fallar).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import estricto  # noqa: E402
import rr  # noqa: E402

T = "testroms"
if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(estricto.main(["-n", f"{T}/dmg-acid2/dmg-acid2.gb", f"{T}/dmg-acid2/dmg-acid2.gb", f"{T}/cgb-acid2/cgb-acid2.gbc",
                            f"{T}/blargg/cpu_instrs/cpu_instrs.gb", f"{T}/mealybug-tearoom-tests/ppu/m3_scy_change.gb"]))
