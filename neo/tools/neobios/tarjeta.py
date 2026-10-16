#!/usr/bin/env python3
"""Muestra lo que importa de una tarjeta de memoria del Neo Geo (un archivo de 2 KB o más, un byte por byte
de la tarjeta): la cabecera, las partidas del directorio con su cadena de bloques y las dos FAT. Deja afuera lo
que no se usa (los bytes libres, los de las entradas borradas y el resto del último bloque de cada partida), que
cada BIOS deja a su manera: sirve para comparar lo que escriben la original y la propia.

    python3 tools/neobios/tarjeta.py tarjeta.bin [otra.bin]

Con dos archivos, dice si son iguales en eso y, si no, en qué difieren. El formato está en tools/neobios/neobios.s.
"""
import sys

MAGIA = b"NEO-GEO\x80"


def leer(path):
    d = open(path, "rb").read()
    out = []
    if bytes(d[0x20:0x30:2]) != MAGIA:
        return ["sin formato"]
    h = d[0x0A] << 8 | d[0x0B]
    n = h // 128
    if h not in (0x1000, 0x2000, 0x4000, 0x8000) or len(d) < h // 2:
        return ["tamaño en la cabecera: $%04X" % h]
    fatb = (n + 63) // 64
    fat1 = (1 + n // 16) * 64
    fat2 = fat1 + fatb * 64
    data0 = 1 + n // 16 + 2 * fatb
    f1 = d[fat1:fat1 + fatb * 64]
    f2 = d[fat2:fat2 + fatb * 64]
    s1, s2 = sum(f1) & 255, sum(f2) & 255
    out.append("tamaño $%04X (%d bloques, datos desde el %d)" % (h, n, data0))
    out.append("FAT 1: suma $%02X (cabecera $%02X)%s" % (s1, d[0x0D], "" if s1 == d[0x0D] else " MAL"))
    out.append("FAT 2: suma $%02X (cabecera $%02X)%s" % (s2, d[0x0E], "" if s2 == d[0x0E] else " MAL"))
    out.append("FAT 1: " + f1[:n].hex(" "))
    if f2 != f1:
        out.append("FAT 2: " + f2[:n].hex(" "))
    out.append("nombre: " + (repr(bytes(d[0x10:0x20]).decode("latin-1")) if d[0x0F] else "(no)"))
    out.append("región: %d" % d[0x30])
    fat = f1 if s1 == d[0x0D] else f2
    for i in range(n):
        e = d[64 + 4 * i:68 + 4 * i]
        if e[0] == 0xFF:
            continue
        cadena, b = [], e[3]
        while data0 <= b < n and len(cadena) <= n:
            cadena.append(b)
            b = fat[b]
        datos = b"".join(d[k * 64:k * 64 + 64] for k in cadena)
        out.append("entrada %2d: NGH $%04X sub %d, bloques %s, título %r" % (
            i, e[1] << 8 | e[2], e[0], cadena, datos[:20].decode("latin-1")))
        # Los datos menos el último bloque (su resto es basura que cada BIOS deja a su manera)
        out.append("            datos: %s" % datos[:max(0, len(datos) - 64)].hex())
    return out


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__.strip().splitlines()[-3].strip())
        return 2
    a = leer(sys.argv[1])
    if len(sys.argv) == 2:
        print("\n".join(a))
        return 0
    b = leer(sys.argv[2])
    if a == b:
        print("iguales: %s" % a[0])
        return 0
    for x, y in zip(a + [""] * len(b), b + [""] * len(a)):
        if x != y:
            print("- " + x)
            print("+ " + y)
    return 1


if __name__ == "__main__":
    sys.exit(main())
