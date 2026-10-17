#!/usr/bin/env python3
"""Resultado de las 10 pruebas ppu_vbl_nmi de Blargg (una letra cada una: P pasa, F falla)"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402


def main():
    rr.utf8()
    rr.entrar_a_la_raiz(__file__)
    os.makedirs("build/shots", exist_ok=True)
    letras = ""
    for f in sorted(glob.glob("testroms/nes-test-roms/ppu_vbl_nmi/rom_singles/*.nes")):
        letras += "P" if rr.correr([rr.exe("nesshot"), f, "build/shots/x.png", "1200"])[0] == 0 else "F"
    print(letras)
    return 0


if __name__ == "__main__":
    sys.exit(main())
