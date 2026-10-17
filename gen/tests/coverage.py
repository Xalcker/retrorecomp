#!/usr/bin/env python3
"""Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones que el juego ejecutó encuentra solo el análisis
estático, con cada ROM de roms/. Las trazas se hacen una vez con gentrace (1800 cuadros, mando aleatorio con semilla 1 y Start a
los 2 y a los 5 s) y se guardan en build/traces/.
  python3 tests/coverage.py [opciones de genrecomp, p. ej. --no-tables o --no-spec]
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import cobertura  # noqa: E402
import rr  # noqa: E402


def main(argv):
    rr.entrar_a_la_raiz(__file__)
    roms = []
    for patron in ("roms/*.bin", "roms/*.md", "roms/*.gen", "roms/*.smd"):
        roms += sorted(glob.glob(patron))
    base = lambda r: os.path.splitext(os.path.basename(r))[0]
    return cobertura.cobertura("gen", roms, 1800, ["start:120-125", "start:300-305"],
                               lambda r: re.sub(r"[^A-Za-z0-9]", "_", base(r)) + ".txt", lambda r: os.path.basename(r),
                               48, 47, "ROMs", opciones=argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
