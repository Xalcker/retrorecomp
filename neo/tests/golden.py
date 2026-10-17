#!/usr/bin/env python3
"""Línea base exacta de Neo Geo: hash de todos los cuadros y de todo el audio (neoshot --hash) de cada juego de
roms/ y del juego de prueba del contrato, 3600 cuadros con las entradas de tests/coverage.sh (de la demo a la
partida). El código común está en shared/golden.py (opciones, formato de tests/golden.txt y qué cuenta como falla).
    python3 tests/golden.py [--update] [ROM...]
La BIOS cambia lo que se ve: con la de SNK (roms/aes.zip, o NEO_BIOS) los casos llevan solo el SHA1; con la
propia (build/neobios.bin, la de make neobios y la del CI) llevan la variante /neobios.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import golden  # noqa: E402
import rr  # noqa: E402

OPCIONES = ["--frames", "3600", "--seed", "1", "start:1200-1205", "start:1500-1505", "a:1700-1710", "a:1800-1810",
            "a:1900-1910", "a:2000-2010", "a:2100-2110", "a:2200-2210", "start:2300-2305", "a:2400-2410",
            "a:2500-2510", "a:2600-2610", "a:2700-2710", "right:2800-3400", "b:3100-3105", "c:3200-3205",
            "left:3400-3600"]


def bios():
    """NEO_BIOS, o la de SNK de roms/ o la propia de build/ (y la deja en el entorno para los programas)"""
    b = os.environ.get("NEO_BIOS", "")
    if not b and os.path.isfile("roms/aes.zip"):
        b = os.path.abspath("roms/aes.zip")
    if not b and os.path.isfile("build/neobios.bin"):
        b = os.path.abspath("build/neobios.bin")
    if b:
        os.environ["NEO_BIOS"] = b
    return b


def sha1(rom):
    return rr.sha1_de_info("neorecomp", rom)


def skip():
    return "" if bios() else "falta la BIOS: roms/aes.zip, NEO_BIOS o make neobios"


def casos(caso):
    variante = "neobios" if bios().endswith("neobios.bin") else None
    for patron in ("roms/*.zip", "roms/*.neo", "testroms/neo/*.neo"):
        for r in sorted(glob.glob(patron)):
            if os.path.basename(r) in ("aes.zip", "neogeo.zip"):
                continue
            caso(r, variante)


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(golden.main("neo", OPCIONES, casos, sha1=sha1, skip=skip))
