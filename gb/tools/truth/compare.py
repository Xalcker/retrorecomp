#!/usr/bin/env python3
"""Compara un mapa de código (gbrecomp --codemap o difftest --executed) contra
el mapa verdadero del desensamblado (build_truth.py).

Uso: python3 compare.py truth.txt mapa.txt [simbolos.sym] [máximo de tramos]
  Con el .sym original del desensamblado, los tramos se nombran con la
  rutina que los contiene.
"""
import re
import sys


def cargar(ruta):
    with open(ruta, encoding="utf-8") as f:
        return "".join(f.read().split())


def lineal(banco, addr):
    return banco * 0x4000 + (addr - 0x4000) if banco else addr


def simbolos(ruta):
    """(dirección lineal, nombre) ordenados, sin los marcadores ni las etiquetas locales"""
    res = []
    with open(ruta, encoding="utf-8") as f:
        for linea in f:
            m = re.match(r"([0-9a-fA-F]+):([0-9a-fA-F]+)\s+(\S+)", linea)
            if not m:
                continue
            nombre = m.group(3)
            if re.search(r"__m\d", nombre) or "." in nombre:
                continue
            res.append((lineal(int(m.group(1), 16), int(m.group(2), 16)), nombre))
    res.sort(key=lambda x: x[0])
    return res


def donde(syms, lin):
    mejor = ""
    for addr, nombre in syms:
        if addr > lin:
            break
        mejor = nombre if addr == lin else "%s+$%X" % (nombre, lin - addr)
    return mejor


def texto_dir(lin):
    banco = lin // 0x4000
    addr = 0x4000 + lin % 0x4000 if banco else lin
    return "%02X:%04X" % (banco, addr)


def tramos(n, cond):
    """Tramos contiguos (inicio, largo) donde cond(i) es cierto, los más largos primero"""
    r = []
    inicio = -1
    for i in range(n + 1):
        c = i < n and cond(i)
        if c and inicio < 0:
            inicio = i
        elif not c and inicio >= 0:
            r.append((inicio, i - inicio))
            inicio = -1
    r.sort(key=lambda x: -x[1])
    return r


def main(argv):
    if len(argv) < 2:
        sys.exit("uso: compare.py truth.txt mapa.txt [sym] [máximo]")
    truth = cargar(argv[0])
    mapa = cargar(argv[1])
    syms = simbolos(argv[2]) if len(argv) > 2 and argv[2] else []
    maximo = int(argv[3]) if len(argv) > 3 else 12
    if len(truth) != len(mapa):
        sys.exit("tamaños distintos")
    n = len(truth)

    codigo = encontrado = marcado = falso = 0
    for t, m in zip(truth, mapa):
        es = m == "C"
        codigo += t == "C"
        marcado += es
        encontrado += es and t == "C"
        falso += es and t != "C"

    print("Código según el desensamblado: %d bytes" % codigo)
    print("Marcado como código:           %d bytes" % marcado)
    print("  Exhaustividad (código encontrado): %d de %d = %.1f%%" % (encontrado, codigo, 100 * encontrado / codigo if codigo else 0))
    print("  Precisión (lo marcado que sí es código): %d de %d = %.1f%%" % (encontrado, marcado, 100 * encontrado / marcado if marcado else 0))
    print("  Datos decodificados como código: %d bytes" % falso)

    faltan = tramos(n, lambda i: truth[i] == "C" and mapa[i] != "C")
    print("\nCódigo no encontrado: %d tramos" % len(faltan))
    for inicio, largo in faltan[:maximo]:
        print("  %s  %5d bytes  %s" % (texto_dir(inicio), largo, donde(syms, inicio)))

    sobran = tramos(n, lambda i: truth[i] != "C" and mapa[i] == "C")
    print("\nDatos tomados por código: %d tramos" % len(sobran))
    for inicio, largo in sobran[:maximo]:
        print("  %s  %5d bytes  %s" % (texto_dir(inicio), largo, donde(syms, inicio)))


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n", encoding="utf-8")
    main(sys.argv[1:])
