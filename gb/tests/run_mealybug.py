#!/usr/bin/env python3
"""Pruebas de Mealybug Tearoom (cambios de registros a mitad de línea) con referencia DMG.
Uso: python3 tests/run_mealybug.py [filtro]
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402


def main(argv):
    rr.utf8()
    rr.entrar_a_la_raiz(__file__)
    filtro = argv[0] if argv else ""
    D = "testroms/mealybug-tearoom-tests/ppu"
    os.makedirs("build/mealybug", exist_ok=True)
    pasa = total = 0
    for t in sorted(glob.glob(f"{D}/*.gb")):
        b = os.path.basename(t)[:-3]
        if filtro not in b:
            continue
        e = f"{D}/{b}_dmg_blob.png"
        if not os.path.isfile(e):
            e = f"{D}/{b}_dmg_b.png"
        if not os.path.isfile(e):
            continue
        total += 1
        codigo, salida = rr.correr([rr.exe("gbtest"), t, "--until-ldbb", "--seconds", "5", "--expect", e, "--shot", f"build/mealybug/{b}.png"])
        if codigo == 0:
            pasa += 1
        else:
            print(salida.replace("\n", " "))
    print(f"Mealybug: pasan {pasa} de {total}")
    return 0 if pasa == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
