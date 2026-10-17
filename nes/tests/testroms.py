#!/usr/bin/env python3
"""make testroms: baja las ROMs de prueba libres a testroms/ (no se versionan): christopherpow/nes-test-roms."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
import testroms as t  # noqa: E402

URL = "https://github.com/christopherpow/nes-test-roms/archive/95d8f621ae55cee0d09b91519a8989ae0e64753b.tar.gz"


def principal():
    if os.path.isdir("testroms/nes-test-roms/other"):
        print("testroms/ ya tiene las ROMs de prueba")
        return
    t.bajar(URL, "build/testroms.tar.gz")
    t.extraer_tar("build/testroms.tar.gz", "testroms/nes-test-roms")
    os.remove("build/testroms.tar.gz")
    print("ROMs de prueba en testroms/")


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(t.ejecutar(principal))
