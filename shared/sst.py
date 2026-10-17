#!/usr/bin/env python3
"""SingleStepTests de una CPU (make testroms): corre cada archivo con m68ktest o z80test.

    python3 ../shared/sst.py m68k                       resumen por familia (intérprete)
    SST_OPTS=--recomp python3 ../shared/sst.py m68k     lo mismo, por el camino del código generado
    python3 ../shared/sst.py m68k ADD.w -v 5            una familia, con el detalle de las 5 primeras que fallan
    python3 ../shared/sst.py z80                        resumen (solo los que fallan, y el total)
    python3 ../shared/sst.py z80 "ed b0" -v 5           un archivo, con el detalle de las 5 primeras que fallan

Se corre desde la raíz de un sistema. Sale con 1 si alguna prueba falla.
"""
import glob
import gzip
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

CPUS = {
    "m68k": dict(dir="testroms/680x0/68000/v1", prueba="m68ktest", primero="NOP", nombre="68000", plural="familias"),
    "z80": dict(dir="testroms/z80/v1", prueba="z80test", primero="00", nombre="Z80", plural="archivos"),
}


def cuenta(texto):
    """La última palabra «pasan/total» del texto (0/1 si no hay: se cayó, cuenta como falla)"""
    c = "0/1"
    for w in texto.split():
        if re.search(r"[0-9]/[0-9]", w):
            c = w
    p, t = c.split("/", 1)
    return int(re.sub(r"\D", "", p) or 0), int(re.sub(r"\D", "", t) or 0)


def main(argv):
    rr.utf8()
    if not argv or argv[0] not in CPUS:
        print("uso: sst.py m68k|z80 [NOMBRE [opciones]]", file=sys.stderr)
        return 2
    cpu = CPUS[argv[0]]
    resto = argv[1:]
    d = cpu["dir"]
    prueba = rr.exe(cpu["prueba"])
    if not os.path.isfile(f"{d}/{cpu['primero']}.json.gz"):
        print(f"(sin {d}: make testroms)")
        return 0
    if resto:
        nombre = resto[0]
        with gzip.open(f"{d}/{nombre}.json.gz", "rb") as f:
            datos = f.read()
        codigo, salida = rr.correr([prueba, "-", "--name", nombre, *resto[1:]], entrada=datos)
        sys.stdout.write(salida)
        return codigo
    opts = os.environ.get("SST_OPTS", "").split()

    def uno(ruta):
        nombre = os.path.basename(ruta)[:-len(".json.gz")]
        with gzip.open(ruta, "rb") as f:
            datos = f.read()
        return nombre, rr.correr([prueba, "-", "--name", nombre, *opts], entrada=datos)[1]

    pasan = total = fallan = 0
    archivos = sorted(glob.glob(f"{d}/*.json.gz"))
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for nombre, salida in pool.map(uno, archivos):
            r = salida.rstrip("\n")
            # m68k: se ve la línea de cada familia; z80: solo las que fallan (son 1604 archivos). La cuenta sale
            # de todas las palabras (m68k) o de la última línea (z80)
            p, t = cuenta(r if cpu["plural"] == "familias" else r.split("\n")[-1])
            pasan += p
            total += t
            if cpu["plural"] == "familias":
                print(r)
            if p != t:
                fallan += 1
                if cpu["plural"] == "archivos":
                    print(r)
    extra = f" ({' '.join(opts)})" if opts else ""
    print(f"SingleStepTests {cpu['nombre']}{extra}: {pasan}/{total} ({fallan} {cpu['plural']} con fallas)")
    return 1 if fallan else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
