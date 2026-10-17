#!/usr/bin/env python3
"""Compila y prueba un sistema como lo hace el CI, en cualquier sistema operativo:
  python3 tests/ci_sistema.py gen            todo
  RAPIDAS=1 python3 tests/ci_sistema.py gen  sin lo más caro (Overdrive en GEN; ZEX y Dragon Blaster en SMS)
  SKIP_SST=1 ...                             sin las SingleStepTests (no las baja ni las corre)
  DIST=1 ...                                 además arma el paquete (make dist)
Sin RAPIDAS también corre la línea base exacta (tests/golden.py --libres) en las ROMs libres de testroms/, con el intérprete y
con el código recompilado: los hashes son los mismos en Linux, macOS y Windows.
"""
import os
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(AQUI, "..", "shared"))
import rr  # noqa: E402


def main(argv):
    rr.utf8()
    s = argv[0] if argv else ""
    raiz = os.path.join(AQUI, "..")
    if not (s and os.path.isfile(os.path.join(raiz, s, "Makefile"))):
        print("uso: python3 tests/ci_sistema.py gb|nes|gen|neo|sms", file=sys.stderr)
        return 2
    os.chdir(os.path.join(raiz, s))

    def paso(*cmd, env=None):
        sys.stdout.flush()
        codigo = subprocess.run(list(cmd), env=env).returncode
        if codigo != 0:
            sys.exit(codigo)

    def grupo(titulo, *pasos):
        print(f"::group::{s}: {titulo}", flush=True)
        for p in pasos:
            paso(*p[0], env=p[1] if len(p) > 1 else None)
        print("::endgroup::", flush=True)

    py = sys.executable
    grupo("compilar", (["make", "-j4"],), *([(["make", "neobios"],)] if s == "neo" else []))
    grupo("ROMs de prueba", (["make", "testroms"],))
    entorno = dict(os.environ, NEO_BIOS="build/neobios.bin") if s == "neo" else None
    grupo("contrato", ([py, "../shared/contrato.py"], entorno))
    grupo("pruebas", (["make", "test"],))
    if not os.environ.get("RAPIDAS"):
        grupo("golden (ROMs libres)", ([py, "tests/golden.py", "--libres"],), ([py, "tests/golden.py", "--libres", "--recompilado"],))
    if os.environ.get("DIST"):
        paso("make", "dist")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
