#!/usr/bin/env python3
"""Regresión del recompilador con Tetris: regenera el proyecto, lo compila y corre la prueba diferencial con las partidas
grabadas y con semillas aleatorias.
Uso: python3 tests/difftest_tetris.py RUTA/tetris.gb
  (Tetris (JUE) (V1.1), SHA1 74591cc9501af93873f9a5d3eb12da12c0723bbc)
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402


def main(argv):
    rr.utf8()
    if not argv or not os.path.isfile(argv[0]):
        print("uso: python3 tests/difftest_tetris.py RUTA/tetris.gb", file=sys.stderr)
        return 2
    rom = os.path.abspath(argv[0])
    rr.entrar_a_la_raiz(__file__)
    x = ".exe" if os.path.isfile("./gbrecomp.exe") else ""

    if rr.correr(["make", "-s"])[0] != 0:
        return 2
    if rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/tetris"])[0] != 0:
        return 2
    if rr.correr(["make", "-s", "-C", "build/out/tetris"])[0] != 0:
        return 2

    falla = 0

    def corre(etiqueta, *args):
        nonlocal falla
        print(f"{etiqueta:<40} ", end="", flush=True)
        codigo, salida = rr.correr([f"build/out/tetris/difftest{x}", rom, *args])
        with open("build/out/tetris/difftest.log", "w", encoding="utf-8", newline="\n") as f:
            f.write(salida)
        if codigo == 0:
            m = re.search(r"[0-9.]+% de las instrucciones nativas", salida)
            print(m.group(0) if m else "")
        else:
            print("DIFERENCIA")
            sys.stdout.write(salida)
            falla = 1

    for f in sorted(glob.glob("tests/inputs/tetris_*.txt")) + sorted(glob.glob("tests/inputs/partida*.txt")):
        corre(os.path.basename(f), "--input", f)
    for s in (1, 2, 3):
        corre(f"aleatorio, semilla {s}", "--frames", "10000", "--seed", str(s))

    print("HAY DIFERENCIAS." if falla else "Sin diferencias.")
    return falla


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
