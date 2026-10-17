#!/usr/bin/env python3
"""Super Game Boy de 4 jugadores con Bomberman Collection (J): entra al modo BATTLE de Bomberman GB, los cuatro se inscriben
cada uno con su control y, ya en la arena, cada uno camina hacia un lado (1 derecha, 2 izquierda, 3 abajo, 4 arriba). Deja
la imagen en build/sgb_bomberman.png para revisarla: los cuatro Bomberman se alejaron de su esquina en esas direcciones.
Llegar hasta ahí también requiere que el título de Bomberman GB se vea (antes se perdía su PAL_TRN y quedaba en blanco).
Uso: python3 tests/sgb_bomberman.py "RUTA/Bomberman Collection (J) [S].gb"
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402


def main(argv):
    rr.utf8()
    if not argv or not os.path.isfile(argv[0]):
        print("uso: python3 tests/sgb_bomberman.py RUTA/bomberman_collection.gb", file=sys.stderr)
        return 2
    rom = os.path.abspath(argv[0])
    rr.entrar_a_la_raiz(__file__)
    x = ".exe" if os.path.isfile("./gbtest.exe") else ""
    if rr.correr(["make", "-s", f"gbtest{x}"])[0] != 0:
        return 2
    os.makedirs("build", exist_ok=True)
    i = "tests/inputs/bomberman_batalla"
    # gbtest termina con 2 («sin resultado»): aquí no hay criterio automático
    codigo, _ = rr.correr([f"./gbtest{x}", rom, "--model", "sgb", "--seconds", "63.7", "--input", f"{i}_1.txt",
                           "--input2", f"{i}_2.txt", "--input3", f"{i}_3.txt", "--input4", f"{i}_4.txt",
                           "--shot-sgb", "build/sgb_bomberman.png"])
    if codigo > 2:
        return 1
    print("Revisa build/sgb_bomberman.png: cuatro Bomberman fuera de sus esquinas")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
