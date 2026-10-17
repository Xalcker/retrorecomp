#!/usr/bin/env python3
"""Prueba de la tarjeta de memoria de la BIOS propia: corre tarjeta.guion con tools/neocard en el juego de prueba del contrato
(testroms/neo/contrato.neo) y compara las respuestas y lo cargado con tarjeta.esperado, y lo que queda en la tarjeta
(tools/neobios/tarjeta.py) con tarjeta.estructura. Los dos salieron de la BIOS original (las dos BIOS dan lo mismo):

  python3 tests/neobios/prueba_tarjeta.py                    la BIOS propia (build/neobios.bin)
  python3 tests/neobios/prueba_tarjeta.py --esperado BIOS    rehace lo esperado con otra BIOS (la de SNK)
"""
import difflib
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "shared"))
import rr  # noqa: E402


def sin_cr(ruta):
    with open(ruta, encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r", "")


def escribir(ruta, texto):
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)


def main(argv):
    rr.utf8()
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    bios = "build/neobios.bin"
    rehacer = False
    if argv[:1] == ["--esperado"]:
        rehacer, bios = True, argv[1]
    rom = "testroms/neo/contrato.neo"
    if not os.path.isfile(rom):
        rr.correr([sys.executable, "tests/testroms.py"])
    os.makedirs("build/tests", exist_ok=True)
    if os.path.exists("build/tests/tarjeta.bin"):
        os.remove("build/tests/tarjeta.bin")
    with open("tests/neobios/tarjeta.guion", encoding="utf-8") as f:
        ordenes = " ".join(l for l in f.read().splitlines() if not l.startswith("#")).split()
    _, salida = rr.correr([rr.exe("neocard"), rom, "--frames", "300", "--bios", bios, "--card", "build/tests/tarjeta.bin", *ordenes])
    # tail -n +2 (sin la primera línea) y sin los ciclos de cada respuesta
    lineas = salida.replace("\r", "").split("\n")[1:]
    txt = "\n".join(re.sub(r"  \([0-9]* ciclos\)", "", l) for l in lineas)
    escribir("build/tests/tarjeta.txt", txt)
    # En Windows, python3 escribe CRLF y en la página de códigos local (la ñ de «tamaño») al redirigir su salida: lo esperado
    # está en UTF-8 (y el checkout puede dejarlo con CRLF: se comparan sin CR los dos lados)
    _, est = rr.correr([sys.executable, "tools/neobios/tarjeta.py", "build/tests/tarjeta.bin"], env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    escribir("build/tests/tarjeta.est", est.replace("\r", ""))
    if rehacer:
        shutil.copyfile("build/tests/tarjeta.txt", "tests/neobios/tarjeta.esperado")
        shutil.copyfile("build/tests/tarjeta.est", "tests/neobios/tarjeta.estructura")
        print(f"escrito tests/neobios/tarjeta.esperado y tarjeta.estructura con {bios}")
        return 0
    esperado, estructura = sin_cr("tests/neobios/tarjeta.esperado"), sin_cr("tests/neobios/tarjeta.estructura")
    n = sum(1 for l in esperado.splitlines() if ": respuesta" in l)
    if esperado.rstrip("\n") == txt.rstrip("\n") and estructura.rstrip("\n") == est.replace("\r", "").rstrip("\n"):
        print(f"ok    tarjeta de memoria con la BIOS propia: {n} llamadas a CARD como con la original")
        return 0
    print("FALLA tarjeta de memoria con la BIOS propia (contra tests/neobios/tarjeta.esperado y .estructura)")
    for a, b in ((esperado, txt), (estructura, est.replace("\r", ""))):
        print("\n".join(list(difflib.unified_diff(a.splitlines(), b.splitlines(), lineterm=""))[:10]))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
