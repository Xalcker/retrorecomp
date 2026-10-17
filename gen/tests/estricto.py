#!/usr/bin/env python3
"""difftest --estricto con las ROMs de prueba (el código es el de los cinco: ../shared/estricto.py). Lo llama
make test: cada ROM, trazada y recompilada con su traza, tiene que pasar --estricto (y salto.bin sin traza tiene que fallar).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import estricto  # noqa: E402
import rr  # noqa: E402

R = "build/tests/salto.bin"


def salto():
    """Una ROM mínima que salta a una dirección que ningún análisis estático encuentra (JMP (A0))"""
    os.makedirs("build/tests", exist_ok=True)
    rom = (bytes([0, 0xFF, 0xFE, 0, 0, 0, 2, 0]) + bytes(248) + b"SEGA MEGA DRIVE " + bytes(240)
           + bytes([0x20, 0x3C, 0, 0, 3, 0, 6, 0x80, 0, 0, 0, 0x10, 0x20, 0x40, 0x4E, 0xD0]) + bytes(256)
           + bytes([0x60, 0xFE]) + bytes(494))
    with open(R, "wb") as f:
        f.write(rom)


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    salto()
    sys.exit(estricto.main(["-n", R, R]))
