#!/usr/bin/env python3
"""Cable link: dos Game Boy conectados (linktest) con juegos de dos jugadores.
Uso: python3 tests/link.py tetris RUTA/tetris.gb
       Tetris (JUE) (V1.1), SHA1 74591cc9501af93873f9a5d3eb12da12c0723bbc: A inicia, B deja caer todo hasta perder y los dos
       llegan a las pantallas de victoria y derrota. Deja la captura final en build/link_tetris.png (izquierda A: Mario
       celebra; derecha B). El intercambio después de la derrota lleva el total a más de 2440 bytes.
     python3 tests/link.py warlocked RUTA/warlocked.gbc
       Warlocked (GBC, doble velocidad y reloj rápido del serial): dos Game Boy Color entran al modo VS, pasan por «Head 2
       Head» y el briefing del mapa GOLD RUSH y juegan; durante la partida intercambian unos 70 bytes por cuadro (en 3600
       cuadros pasan unos 62 000 bytes; la partida empieza en el ~2700). Deja la captura final en build/link_warlocked.png
       (izquierda A con los humanos, derecha B con las bestias).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402

JUEGOS = {
    "tetris": dict(frames="5400", a="tests/inputs/tetris_2p_a.txt", b="tests/inputs/tetris_2p_b.txt",
                   shot="build/link_tetris.png", minimo="2440"),
    "warlocked": dict(frames="3600", a="tests/inputs/warlocked_vs_a.txt", b="tests/inputs/warlocked_vs_b.txt",
                      shot="build/link_warlocked.png", minimo="60000"),
}


def main(argv):
    rr.utf8()
    if len(argv) != 2 or argv[0] not in JUEGOS or not os.path.isfile(argv[1]):
        print("uso: python3 tests/link.py tetris|warlocked RUTA/rom.gb", file=sys.stderr)
        return 2
    j = JUEGOS[argv[0]]
    rom = os.path.abspath(argv[1])
    rr.entrar_a_la_raiz(__file__)
    x = ".exe" if os.path.isfile("./linktest.exe") else ""
    if rr.correr(["make", "-s", f"linktest{x}"])[0] != 0:
        return 2
    os.makedirs("build", exist_ok=True)
    codigo, salida = rr.correr([f"./linktest{x}", rom, "--frames", j["frames"], "--input-a", j["a"], "--input-b", j["b"],
                                "--shot", j["shot"], "--min-bytes", j["minimo"]])
    sys.stdout.write(salida)
    return codigo


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
