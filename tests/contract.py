#!/usr/bin/env python3
"""Prueba de contrato: las herramientas sin ventana de NESRecomp y GBRecomp aceptan las mismas
opciones comunes (--frames, --input, --seed). Sin ROMs: ejecuta cada herramienta sin argumentos y
comprueba que su ayuda las menciona. También comprueba que el código compartido (SHARED, p. ej. el
mando aleatorio random_pad.h) sea idéntico en los dos. Corre con los recompiladores ya compilados (make).

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

# Código que comparten los dos tal cual (hasta que se mueva aquí): el mando aleatorio de --seed
SHARED = (os.path.join("src", "platform", "random_pad.h"),)


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
    # Archivos que deben ser idénticos en los dos repositorios (sin contar el fin de línea)
    for shared in SHARED:
        paths = [os.path.join(roots[s], shared) for s in ("nes", "gb") if roots[s]]
        if len(paths) < 2:
            continue
        checked += 1
        try:
            texts = [open(p, "rb").read().replace(b"\r\n", b"\n") for p in paths]
        except OSError as e:
            print(f"FALLA {shared}: {e}")
            fails += 1
            continue
        if texts[0] == texts[1]:
            print(f"ok    {shared} (idéntico en los dos)")
        else:
            print(f"FALLA {shared}: no es igual en NESRecomp y GBRecomp")
            fails += 1
    print(f"{checked} herramientas y archivos revisados, {fails} fallas")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
