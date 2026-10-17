#!/usr/bin/env python3
"""Prueba en lote: recompila varios juegos, compila cada proyecto y corre la prueba diferencial con entradas aleatorias,
guardando capturas.

Uso: python3 tests/batch_roms.py LISTA CARPETA [CUADROS] [SEMILLA]
  LISTA: un archivo con una ruta por línea (.gb, .gbc o .zip con la ROM).
  CARPETA: donde quedan las ROMs extraídas, los proyectos, las capturas y resultados.txt (una línea por juego, separada por
  tabuladores).

Resultado por juego: OK (sin diferencias), DIFERENCIA (difftest encontró una), FALLA_GEN (gbrecomp falló), FALLA_COMPILA (el
proyecto no compiló), FALLA_CORRE (difftest terminó mal sin reportar diferencia).
"""
import os
import re
import shutil
import sys
import time
import zipfile
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402

USO = "uso: python3 tests/batch_roms.py LISTA CARPETA [CUADROS] [SEMILLA]"


def escribir(ruta, texto):
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)


def main(argv):
    rr.utf8()
    rr.entrar_a_la_raiz(__file__)
    if len(argv) < 2 or not os.path.isfile(argv[0]):
        print(USO, file=sys.stderr)
        return 2
    lista = os.path.abspath(argv[0])
    dir_ = argv[1]
    cuadros = argv[2] if len(argv) > 2 else "3600"
    semilla = argv[3] if len(argv) > 3 else "1"
    x = ".exe" if os.path.isfile("./gbrecomp.exe") else ""
    for sub in ("roms", "proyectos", "capturas"):
        os.makedirs(f"{dir_}/{sub}", exist_ok=True)
    res = f"{dir_}/resultados.txt"
    escribir(res, "juego\tresultado\trecompilado\tsegundos\tdetalle\n")

    def anotar(*campos):
        with open(res, "a", encoding="utf-8", newline="\n") as f:
            f.write("\t".join(str(c) for c in campos) + "\n")

    def cola(ruta):
        lineas = open(ruta, encoding="utf-8", errors="replace").read().rstrip("\n").split("\n")
        return lineas[-1]

    for src in open(lista, encoding="utf-8").read().splitlines():
        if not src:
            continue
        nombre = os.path.splitext(os.path.basename(src))[0]
        slug = re.sub(r"[^A-Za-z0-9]", "_", nombre)[:40]
        inicio = time.time()

        # ROM: tal cual o extraída del zip
        rom = src
        if src.endswith(".zip"):
            carpeta = f"{dir_}/roms/{slug}"
            os.makedirs(carpeta, exist_ok=True)
            try:
                with zipfile.ZipFile(src) as z:
                    z.extractall(carpeta)
            except (OSError, zipfile.BadZipFile):
                anotar(nombre, "FALLA_ZIP", "", "", "")
                continue
            hallados = sorted(os.path.join(d, f) for d, _, fs in os.walk(carpeta) for f in fs if f.lower().endswith((".gb", ".gbc")))
            rom = ""
            if hallados:
                # Nombre sin comas ni apóstrofos: MSYS2 no convierte bien esas rutas al pasarlas a un ejecutable de Windows
                rom = f"{carpeta}/{slug}{os.path.splitext(hallados[0])[1]}"
                if os.path.abspath(hallados[0]) != os.path.abspath(rom):
                    shutil.move(hallados[0], rom)
        if not (rom and os.path.isfile(rom)):
            anotar(nombre, "FALLA_ZIP", "", "", "")
            continue

        proj = f"{dir_}/proyectos/{slug}"
        print(f"{nombre:<60} ", end="", flush=True)
        c, salida = rr.correr([f"./gbrecomp{x}", rom, "-o", proj, "--name", slug])
        escribir(f"{dir_}/proyectos/{slug}.gen.log", salida)
        if c != 0:
            print("FALLA_GEN")
            anotar(nombre, "FALLA_GEN", "", int(time.time() - inicio), cola(f"{dir_}/proyectos/{slug}.gen.log"))
            continue
        c, salida = rr.correr(["make", "-s", "-C", proj, "-j8", f"difftest{x}"])
        escribir(f"{dir_}/proyectos/{slug}.make.log", salida)
        if c != 0:
            print("FALLA_COMPILA")
            err = next((l for l in salida.splitlines() if "error" in l), "")
            anotar(nombre, "FALLA_COMPILA", "", int(time.time() - inicio), err)
            continue
        os.makedirs(f"{dir_}/capturas/{slug}", exist_ok=True)
        rc, log = rr.correr([f"{proj}/difftest{x}", rom, "--frames", cuadros, "--seed", semilla, "--every", "1200",
                             "--screens", f"{dir_}/capturas/{slug}"])
        escribir(f"{dir_}/proyectos/{slug}.diff.log", log)
        m = re.search(r"([0-9.]+)% de las instrucciones nativas", log)
        pct = m.group(1) if m else ""
        segundos = int(time.time() - inicio)
        if rc == 0 and re.search(r"^difftest: .* cuadros iguales", log, re.M):
            r, detalle = "OK", ""
        elif re.search("diferencia", log, re.I):
            r, detalle = "DIFERENCIA", next(l for l in log.splitlines() if re.search("diferencia", l, re.I))
        else:
            r, detalle = "FALLA_CORRE", f"salida {rc}: {cola(f'{dir_}/proyectos/{slug}.diff.log')}"
        print(f"{r} {pct}")
        anotar(nombre, r, pct, segundos, detalle)
        # Los proyectos pesan: se conserva solo el registro (salvo que fallen)
        if r == "OK":
            shutil.rmtree(proj, ignore_errors=True)

    print()
    print(f"Resultados en {res}")
    cuenta = Counter(l.split("\t")[1] for l in open(res, encoding="utf-8").read().splitlines()[1:] if "\t" in l)
    for k, n in cuenta.items():
        print(f"  {k}: {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
