#!/usr/bin/env python3
"""Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones que el juego ejecutó (sus trazas en build/)
encuentra solo el análisis estático.
  python3 tests/coverage.py [opciones de nesrecomp, p. ej. --no-tables]
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402

JUEGOS = (("roms/Super Mario Bros. (Japan, USA).nes", "build/smb.trace"),
          ("roms/Donkey Kong (U) (PRG1) [!p].nes", "build/Donkey.trace"),
          ("roms/Arkanoid (USA).nes", "build/Arkanoid.trace"),
          ("roms/Contra (U) [!].nes", "build/Contra.trace"),
          ("roms/Castlevania (U) (PRG0) [!].nes", "build/Castlevania.trace"),
          ("roms/Megaman 1(U) [!].nes", "build/Megaman.trace"),
          ("roms/Super Mario Bros. 3 (U) (PRG0) [!].nes", "build/Super_Ma.trace"),
          ("roms/Legend of Zelda, The (U) (PRG0) [!].nes", "build/Legend.trace"))


def main(argv):
    rr.utf8()
    rr.entrar_a_la_raiz(__file__)
    for rom, traza in JUEGOS:
        if not (os.path.isfile(rom) and os.path.isfile(traza)):
            continue
        _, salida = rr.correr([rr.exe("nesrecomp"), rom, "--coverage", traza, *argv], stderr=rr.subprocess.DEVNULL)
        linea = next((l for l in salida.splitlines() if "cobertura" in l), "")
        print(f"{os.path.basename(rom)[:44]:<45} " + linea.replace("cobertura: ", "", 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
