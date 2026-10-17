#!/usr/bin/env python3
"""difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.py). Lo llama
make test: cada ROM, trazada y recompilada con su traza, tiene que pasar --estricto.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import estricto  # noqa: E402
import rr  # noqa: E402

if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(estricto.main(["testroms/neo/contrato.neo"]))
