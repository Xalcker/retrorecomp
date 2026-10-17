#!/usr/bin/env python3
"""Convierte los marcadores del desensamblado instrumentado en un mapa por byte.

Uso: python3 make_truth.py markers.txt archivo.sym archivo.map TAMAÑO > truth.txt
Salida: un carácter por byte de la ROM, 64 por línea:
  C = código, D = datos, - = fuera de toda sección (relleno)
"""
import re
import sys
from collections import Counter


def linear(banco, addr):
    return banco * 0x4000 + (addr - 0x4000) if banco else addr


def construir(markers_path, sym_path, map_path, size):
    """El mapa (lista de un carácter por byte) y los tres contadores (código, datos, relleno)"""
    tipo = {}
    with open(markers_path, encoding="utf-8") as f:
        for linea in f:
            campos = linea.split()
            if len(campos) >= 2:
                tipo[campos[0]] = campos[1]

    marcas = []     # (dirección lineal, tipo)
    patron = re.compile(r"^([0-9a-fA-F]+):([0-9a-fA-F]+)\s+\S*__m(\d+)\s*$")
    with open(sym_path, encoding="utf-8") as f:
        for linea in f:
            m = patron.match(linea)
            if not m:
                continue
            addr = linear(int(m.group(1), 16), int(m.group(2), 16))
            if addr >= size:
                continue
            marcas.append((addr, tipo.get(m.group(3), "?")))

    secciones = []  # (inicio, fin exclusivo)
    banco = 0
    with open(map_path, encoding="utf-8") as f:
        for linea in f:
            m = re.match(r"ROM[0X] bank #(\d+)", linea)
            if m:
                banco = int(m.group(1))
                continue
            if re.match(r"(WRAM|HRAM|SRAM|VRAM|OAM)", linea):
                break
            m = re.search(r"SECTION: \$([0-9a-fA-F]+)-\$([0-9a-fA-F]+)", linea)
            if m:
                secciones.append((linear(banco, int(m.group(1), 16)), linear(banco, int(m.group(2), 16)) + 1))

    mapa = ["-"] * size
    marcas.sort(key=lambda x: x[0])
    for inicio, fin in secciones:
        dentro = [x for x in marcas if inicio <= x[0] < fin]
        for i, (desde, k) in enumerate(dentro):
            hasta = dentro[i + 1][0] if i + 1 < len(dentro) else fin
            for a in range(desde, hasta):
                while a >= len(mapa):
                    mapa.append("-")
                mapa[a] = k
    cuenta = Counter(mapa)
    return mapa, cuenta


def main(argv):
    if len(argv) < 4:
        sys.exit("uso: make_truth.py markers.txt sym map tamaño")
    size = int(argv[3])
    mapa, cuenta = construir(argv[0], argv[1], argv[2], size)
    texto = "".join(mapa[:size])
    sys.stdout.write("".join(texto[i:i + 64] + "\n" for i in range(0, len(texto), 64)))
    sys.stderr.write(f"código {cuenta['C']} bytes, datos {cuenta['D']}, relleno {cuenta['-']}\n")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    sys.stderr.reconfigure(encoding="utf-8", newline="\n")
    main(sys.argv[1:])
