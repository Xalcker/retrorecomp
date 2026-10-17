#!/usr/bin/env python3
"""Línea base exacta de Genesis: hash de todos los cuadros y de todo el audio (genshot --hash) de cada ROM de roms/
y de las pruebas libres, 1800 cuadros con las mismas entradas. El código común está en shared/golden.py
(opciones, formato de tests/golden.txt y qué cuenta como falla).
    python3 tests/golden.py [--update] [ROM...]
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import golden  # noqa: E402
import rr  # noqa: E402

OPCIONES = ["--frames", "1800", "--seed", "1", "start:120-125", "start:300-305", "start:600-605"]


def sha1(rom):
    """El SHA1 de la ROM normalizada (un .smd intercalado da el mismo que el .bin)"""
    return rr.sha1_de_info("genrecomp", rom)


def casos(caso):
    for patron in ("roms/*.bin", "roms/*.md", "roms/*.gen", "roms/*.smd", "testroms/demos/*.bin", "testroms/240p/*.bin"):
        for r in sorted(glob.glob(patron)):
            caso(r)


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(golden.main("gen", OPCIONES, casos, sha1=sha1))
