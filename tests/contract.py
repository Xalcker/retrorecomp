#!/usr/bin/env python3
"""Prueba de contrato: las herramientas sin ventana de NESRecomp, GBRecomp, GENRecomp y NEORecomp aceptan
las mismas opciones comunes (--frames, --input, --seed). Sin ROMs: ejecuta cada herramienta sin argumentos y
comprueba que su ayuda las menciona. También comprueba que el código compartido (SHARED, p. ej. el
mando aleatorio random_pad.h) sea idéntico en todos los que encuentre. Corre con los recompiladores ya compilados (make).

    python3 tests/contract.py [--nes DIR] [--gb DIR] [--gen DIR] [--neo DIR]
"""
import argparse, os, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import retrorecomp_gui as rr  # noqa: E402

COMMON = {  # herramienta -> opciones que debe mencionar su ayuda
    "nes": {"nestrace": ("--frames", "--input", "--seed"), "nesshot": ("--frames", "--input", "--seed"),
            "nesaudio": ("--frames", "--input", "--seed"), "nesbench": ("--frames", "--input", "--repeat", "--seed")},
    "gb": {"gbtrace": ("--frames", "--input", "--seed"), "gbshot": ("--frames", "--input", "--seed"),
           "gbaudio": ("--frames", "--input", "--seed"), "gbbench": ("--frames", "--input", "--repeat", "--seed")},
    "gen": {"gentrace": ("--frames", "--input", "--seed"), "genshot": ("--frames", "--input", "--seed"),
            "genaudio": ("--frames", "--input", "--seed"), "genbench": ("--frames", "--input", "--repeat", "--seed")},
    "neo": {"neotrace": ("--frames", "--input", "--seed"), "neoshot": ("--frames", "--input", "--seed"),
            "neoaudio": ("--frames", "--input", "--seed"), "neobench": ("--frames", "--input", "--repeat", "--seed")},
}

# Archivos que todos tienen tal cual (hasta que se muevan aquí): el mando aleatorio de --seed, el
# formato de las trazas y la propia prueba de contrato de cada uno
SHARED = (os.path.join("src", "platform", "random_pad.h"), os.path.join("src", "runtime", "trace_line.h"),
          os.path.join("tests", "contract.sh"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nes")
    ap.add_argument("--gb")
    ap.add_argument("--gen")
    ap.add_argument("--neo")
    a = ap.parse_args()
    roots = {"nes": rr.find_repo("NESRecomp", "NESRECOMP_DIR", a.nes, os.path.join("src", "nesrecomp", "nesrecomp.c")),
             "gb": rr.find_repo("GBRecomp", "GBRECOMP_DIR", a.gb, os.path.join("src", "gbrecomp", "gbrecomp.c")),
             "gen": rr.find_repo("GENRecomp", "GENRECOMP_DIR", a.gen, os.path.join("src", "genrecomp", "genrecomp.c")),
             "neo": rr.find_repo("NEORecomp", "NEORECOMP_DIR", a.neo, os.path.join("src", "neorecomp", "neorecomp.c"))}
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
            p = subprocess.run([exe], cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
            usage = p.stdout + p.stderr
            missing = [o for o in opts if o not in usage]
            checked += 1
            if missing:
                print(f"FALLA {sid}/{tool}: la ayuda no menciona {', '.join(missing)}")
                fails += 1
            else:
                print(f"ok    {sid}/{tool}")
    # Archivos que deben ser idénticos en todos los repositorios (sin contar el fin de línea)
    for shared in SHARED:
        found = [s for s in roots if roots[s]]
        paths = [os.path.join(roots[s], shared) for s in found]
        if len(paths) < 2:
            continue
        checked += 1
        try:
            texts = [open(p, "rb").read().replace(b"\r\n", b"\n") for p in paths]
        except OSError as e:
            print(f"FALLA {shared}: {e}")
            fails += 1
            continue
        distintos = [s for s, t in zip(found, texts) if t != texts[0]]
        if not distintos:
            print(f"ok    {shared} (idéntico en {', '.join(found)})")
        else:
            print(f"FALLA {shared}: {', '.join(distintos)} no es igual que en {found[0]}")
            fails += 1
    print(f"{checked} herramientas y archivos revisados, {fails} fallas")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
