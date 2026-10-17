#!/usr/bin/env python3
"""make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
  - testroms/680x0: SingleStepTests del 68000 (github.com/SingleStepTests/680x0, un commit fijo, el mismo que GENRecomp;
    ~200 MB que se quedan comprimidos)
  - testroms/z80: SingleStepTests del Z80 (github.com/SingleStepTests/z80, un commit fijo, el mismo que GENRecomp; se
    guardan comprimidas)
  - testroms/neo/contrato.neo: el juego de prueba del contrato común, hecho aquí (src/tools/mkcontrato.c, MIT): no se baja
    de ningún lado. Necesita la BIOS del AES (la busca junto al juego, en roms/ o en NEO_BIOS), como cualquier juego
Las otras pruebas de Neo Geo (ngdevkit, 240p Test Suite) llegan en las fases siguientes (docs/PLAN.md).
"""
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
    t.mkcontrato("testroms/neo/contrato.neo")


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(t.ejecutar(principal))
