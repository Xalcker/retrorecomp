#!/usr/bin/env python3
"""Une mapas de código (formato de gbrecomp --codemap): C si alguno dice C.

Uso: python3 merge_maps.py mapa1 mapa2 ... > unión
"""
import sys


def unir(rutas):
    m = []
    for ruta in rutas:
        with open(ruta, encoding="utf-8") as f:
            s = "".join(f.read().split())
        for i, c in enumerate(s):
            if i >= len(m):
                m.append(".")
            if c == "C":
                m[i] = "C"
    out = "".join(m)
    # Un salto de línea después de cada 64 caracteres completos, como el original
    llenos = len(out) // 64 * 64
    return "".join(out[i:i + 64] + "\n" for i in range(0, llenos, 64)) + out[llenos:]


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    sys.stdout.write(unir(sys.argv[1:]))
