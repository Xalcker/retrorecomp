#!/usr/bin/env python3
"""difftest --estricto con ROMs de prueba, común a los cinco (cada tests/estricto.py lo llama desde su raíz):
    python3 ../shared/estricto.py [-n ROM_SIN_TRAZA] ROM...
- -n: esa ROM, recompilada sin traza, tiene que fallar con «ESTRICTO: código que falta» (lo detecta), y con --uncovered
  dejar la traza del hueco: recompilada con ella, ya no falla en el mismo lugar;
- cada ROM, trazada con el *trace del sistema y las mismas entradas que difftest (600 cuadros, semilla 1) y
  recompilada con esa traza, tiene que pasar: todo lo que se ejecutó de la ROM quedó recompilado (el código en RAM
  sí puede ir al intérprete).
La consola sale de qué recompilador hay en la carpeta (como recomp_game.py). Sale con 0 si todo pasa.
"""
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

ENTRADAS = ["--frames", "600", "--seed", "1"]


def main(argv):
    rr.utf8()
    con = rr.sistema_aqui()
    if not con:
        print("no encuentro el recompilador en esta carpeta", file=sys.stderr)
        return 2
    x = rr.sufijo_exe()     # en Windows el objetivo del Makefile generado es difftest.exe
    neg = None
    if argv[:1] == ["-n"]:
        neg, argv = argv[1], argv[2:]
    os.makedirs("build/out", exist_ok=True)
    fallo = 0

    def proyecto(rom, traza=None):
        """build/out/estricto con su difftest"""
        shutil.rmtree("build/out/estricto", ignore_errors=True)
        cmd = [f"./{con}recomp{x}", rom, "-o", "build/out/estricto", "--name", "estricto", *rr.opciones_pruebas(con)]
        if traza:
            cmd += ["--trace", traza]
        if rr.correr(cmd)[0] != 0:
            return False
        return rr.correr(["make", "-C", "build/out/estricto", "-j4", "-s", f"difftest{x}"])[0] == 0

    def guardar(salida):
        with open("build/estricto.out", "w", encoding="utf-8", newline="\n") as f:
            f.write(salida)

    if neg:
        n = os.path.basename(neg)
        if not os.path.isfile(neg):
            print(f"FALLA --estricto: falta {neg} (make testroms)")
            fallo = 1
        else:
            salida = ""
            detecta = False
            hueco = "build/estricto_hueco.trace"
            if os.path.exists(hueco):
                os.remove(hueco)
            if proyecto(neg):
                codigo, salida = rr.correr([f"build/out/estricto/difftest{x}", neg, *ENTRADAS, "--estricto", "--uncovered", hueco])
                guardar(salida)
                detecta = codigo != 0 and re.search(r"^ESTRICTO: código que falta en \$[0-9A-F]", salida, re.M)
            if detecta:
                donde = re.search(r"^ESTRICTO: código que falta en (\$[0-9A-F]*)", salida, re.M).group(1)
                print(f"ok   --estricto: {n} sin traza falla en el código que falta ({donde})")
                # --uncovered deja la traza del hueco; recompilado con ella, ese hueco ya no está (pasa o falla más adelante)
                otra = ""
                if re.search(r"^ESTRICTO: la traza del hueco", salida, re.M) and os.path.getsize(hueco) > 0 \
                        and proyecto(neg, hueco):
                    _, otra = rr.correr([f"build/out/estricto/difftest{x}", neg, *ENTRADAS, "--estricto"])
                m = re.search(r"^ESTRICTO: código que falta en (\$[0-9A-F]*)", otra, re.M)
                if otra and (not m or m.group(1) != donde):
                    print(f"ok   --estricto --uncovered: con la traza del hueco, {n} ya no falla en {donde}"
                          + (f" (ahora en {m.group(1)})" if m else ""))
                else:
                    sys.stdout.write(otra or salida)
                    print(f"FALLA --estricto --uncovered: la traza del hueco de {n} no lo cubre")
                    fallo = 1
            else:
                sys.stdout.write(salida)
                print(f"FALLA --estricto no detecta el código que falta en {n}")
                fallo = 1
    for r in argv:
        n = os.path.basename(r)
        if not os.path.isfile(r):
            print(f"FALLA --estricto: falta {r} (make testroms)")
            fallo = 1
            continue
        salida = ""
        ok = False
        if rr.correr([f"./{con}trace{x}", r, "build/estricto.trace", *ENTRADAS])[0] == 0 \
                and proyecto(r, "build/estricto.trace"):
            codigo, salida = rr.correr([f"build/out/estricto/difftest{x}", r, *ENTRADAS, "--estricto"])
            guardar(salida)
            ok = codigo == 0
        if ok:
            m = re.search(r"^(Métrica:[^;\n]*)", salida, re.M)
            print(f"ok   --estricto {n}: {m.group(1) if m else ''}")
        else:
            sys.stdout.write(salida)
            print(f"FALLA --estricto {n}")
            fallo = 1
    return fallo


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
