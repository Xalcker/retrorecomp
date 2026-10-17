#!/usr/bin/env python3
"""make testroms: baja las ROMs de prueba libres a testroms/ (no se versionan): c-sp/game-boy-test-roms v7.0."""
import os
import sys
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
import testroms as t  # noqa: E402

URL = "https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip"


def principal():
    if os.path.isdir("testroms/blargg"):
        print("testroms/ ya tiene las ROMs de prueba")
        return
    os.makedirs("testroms", exist_ok=True)
    t.bajar(URL, "build/testroms.zip")
    with zipfile.ZipFile("build/testroms.zip") as z:
        z.extractall("testroms")
    os.remove("build/testroms.zip")
    print("ROMs de prueba en testroms/")


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(t.ejecutar(principal))
