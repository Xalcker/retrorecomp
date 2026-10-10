#!/usr/bin/env python3
"""Prueba de contrato: las herramientas sin ventana de NESRecomp y GBRecomp aceptan las mismas
opciones comunes (--frames, --input, --seed). Sin ROMs: ejecuta cada herramienta sin argumentos y
comprueba que su ayuda las menciona. Corre solo con los recompiladores ya compilados (make).

    python3 tests/contract.py [--nes DIR] [--gb DIR]
"""
import argparse, os, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import retrorecomp_gui as rr  # noqa: E402

COMMON = {  # herramienta -> opciones que debe mencionar su ayuda
    "nes": {"nestrace": ("--frames", "--input", "--seed"), "nesshot": ("--frames", "--input", "--seed"),
            "nesaudio": ("--frames", "--input", "--seed"), "nesbench": ("--frames", "--input", "--repeat")},
    "gb": {"gbtrace": ("--frames", "--input", "--seed"), "gbshot": ("--frames", "--input", "--seed"),
           "gbaudio": ("--frames", "--input", "--seed"), "gbbench": ("--frames", "--input", "--repeat")},
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nes")
    ap.add_argument("--gb")
    a = ap.parse_args()
    roots = {"nes": rr.find_repo("NESRecomp", "NESRECOMP_DIR", a.nes, os.path.join("src", "nesrecomp", "nesrecomp.c")),
             "gb": rr.find_repo("GBRecomp", "GBRECOMP_DIR", a.gb, os.path.join("src", "gbrecomp", "gbrecomp.c"))}
    fails = checked = 0
    for sid, tools in COMMON.items():
        root = roots[sid]
        if not root:
            print(f"{sid}: repositorio no encontrado (omitido)")
            continue
        for tool, opts in tools.items():
            exe = os.path.join(root, tool + rr.EXE)
            if not os.path.isfile(exe):
                print(f"FALLA {sid}/{tool}: no está compilado")
                fails += 1
                continue
            p = subprocess.run([exe], cwd=root, capture_output=True, text=True, errors="replace")
            usage = p.stdout + p.stderr
            missing = [o for o in opts if o not in usage]
            checked += 1
            if missing:
                print(f"FALLA {sid}/{tool}: la ayuda no menciona {', '.join(missing)}")
                fails += 1
            else:
                print(f"ok    {sid}/{tool}")
    print(f"{checked} herramientas revisadas, {fails} fallas")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
