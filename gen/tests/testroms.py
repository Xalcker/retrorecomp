#!/usr/bin/env python3
"""make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
  - testroms/680x0: SingleStepTests del 68000 (github.com/SingleStepTests/680x0, un commit fijo; ~200 MB que se quedan
    comprimidos: m68ktest los lee ya descomprimidos en memoria)
  - testroms/z80: SingleStepTests del Z80 (github.com/SingleStepTests/z80, un commit fijo; se guardan comprimidas)
  - testroms/demos: las demos de Titan de scene.org (gratuitas; se comprueba el SHA-256 del zip):
    overdrive.bin (Overdrive, Evoke 2013, la versión para hardware) y overdrive2.bin (Overdrive 2, Revision 2017: 8 MB con el
    mapper de SSF2, solo PAL; la meta más exigente que hay)
  - testroms/240p (a mano, opcional): el 240p Test Suite de Artemio Urbina (GPL). Su ROM compilada está en itch.io
    (artemiourbina.itch.io/240p-test-suite), que no se puede bajar desde un script; se pone el .bin en testroms/240p/.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
import testroms as t  # noqa: E402


def principal():
    os.makedirs("testroms", exist_ok=True)
    os.makedirs("build", exist_ok=True)
    # SKIP_SST=1 (el CI de macOS y Windows): sin las SingleStepTests, que son lo que más pesa; make test las omite si no
    # están. En Linux y en tu máquina se bajan completas.
    t.sst(os.environ.get("SKIP_SST"), ("68000", "z80"))
    t.demo_scene("parties/2013/evoke13/wild/titan-overdrivemegademo-v1.1-final.zip",
                 "e8cdcc8c828362ed6c4c6d223bc5052a062126d9dc3db6bf85686dfa5856d6ff",
                 "TiTAN - Overdrive (Rev1.1-106-Final) (Hardware).bin", "overdrive.bin")
    t.demo_scene("parties/2017/revision17/oldskool_demo/titan-overdrive2.zip",
                 "1fa9ebdfc4129ec8abf17846634427b4b8dca53c060efa27a3391f76ad0742e1",
                 "titan-overdrive2.bin", "overdrive2.bin")
    bins = sorted(glob.glob("testroms/240p/*.bin"))
    if bins:
        print(f"testroms/240p: {bins[0]}")
    else:
        print("(opcional: el 240p Test Suite se baja a mano de artemiourbina.itch.io/240p-test-suite y va en testroms/240p/)")


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(t.ejecutar(principal))
