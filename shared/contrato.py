#!/usr/bin/env python3
"""Contrato de línea de comandos común a los cinco recompiladores (NESRecomp, GBRecomp, GENRecomp, NEORecomp y
SMSRecomp). Se corre desde la raíz de un sistema, con `make` hecho:
    python3 ../shared/contrato.py

1. Opciones: las herramientas sin ventana aceptan las mismas (--frames, --input, --seed,
   --repeat; --seed también en bench); se ejecuta cada una sin argumentos y su ayuda debe mencionarlas.
2. Comportamiento (con las ROMs de prueba de `make testroms`; sin ellas se omite): la traza, la
   captura y el audio salen iguales con la misma entrada (--seed, y --input con --seed, que se
   suman), con un mismo guion para todas las consolas; en NES, Genesis, Neo Geo y Master System, --input
   da lo mismo que la sintaxis de antes (boton:desde-hasta).
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

ENTRADAS = ("--frames", "--input", "--seed")
ROM = {
    "gb": "testroms/blargg/cpu_instrs/cpu_instrs.gb",
    "nes": "testroms/nes-test-roms/other/nestest.nes",
    "gen": "testroms/demos/overdrive.bin",
    "neo": "testroms/neo/contrato.neo",
    "sms": "testroms/sms/contrato.sms",
}
HERRAMIENTAS = (("trace", "txt"), ("shot", "png"), ("audio", "wav"))
GUION = "# contrato\n0 -\n30 START\n36 -\n60 DOWN+A\n70 -\n"


def main(argv):
    rr.utf8()
    sid = rr.sistema_aqui()
    if not sid:
        print("no es la raíz de NESRecomp, GBRecomp, GENRecomp, NEORecomp ni SMSRecomp", file=sys.stderr)
        return 2
    fallas = 0

    # --- 1. Opciones ---
    checks = [(f"{sid}{rol}", opts) for rol, opts in (("trace", ENTRADAS), ("shot", ENTRADAS), ("audio", ENTRADAS),
                                                        ("bench", (*ENTRADAS, "--repeat", "--profile", "--hash")))]
    for tool, opts in checks:
        e = rr.exe(tool)
        if not os.path.isfile(e):
            print(f"FALLA {tool}: no está compilado")
            fallas += 1
            continue
        _, uso = rr.correr([e], stdin=open(os.devnull))
        faltan = [o for o in opts if o not in uso]
        if faltan:
            print(f"FALLA {tool}: la ayuda no menciona {' '.join(faltan)}")
            fallas += 1
        else:
            print(f"ok    {tool}")

    # --- 2. Comportamiento ---
    d = "build/contract"
    rom = ROM[sid]

    def igual(descripcion, a, b):
        """Los dos archivos deben existir, no estar vacíos y ser idénticos"""
        nonlocal fallas
        if os.path.isfile(a) and os.path.isfile(b) and os.path.getsize(a) > 0 and rr.leer(a) == rr.leer(b):
            print(f"ok    {descripcion}")
        else:
            print(f"FALLA {descripcion}")
            fallas += 1

    if os.path.isfile(rom):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        # El mismo guion en todas las consolas (inputs.h): Start en 30-35, abajo+A en 60-69
        with open(f"{d}/guion.txt", "w", newline="\n") as f:
            f.write(GUION)
        for rol, ext in HERRAMIENTAS:
            n = f"{sid}{rol}"
            e = rr.exe(n)
            base = [e, rom]
            rr.correr([*base, f"{d}/a.{ext}", "--frames", "120", "--seed", "7"])
            rr.correr([*base, f"{d}/b.{ext}", "--frames", "120", "--seed", "7"])
            igual(f"{n}: --seed da lo mismo dos veces", f"{d}/a.{ext}", f"{d}/b.{ext}")
            rr.correr([*base, f"{d}/c.{ext}", "--frames", "120", "--input", f"{d}/guion.txt", "--seed", "7"])
            rr.correr([*base, f"{d}/d.{ext}", "--frames", "120", "--input", f"{d}/guion.txt", "--seed", "7"])
            igual(f"{n}: --input con --seed da lo mismo dos veces", f"{d}/c.{ext}", f"{d}/d.{ext}")
        if sid != "gb":
            # Hash de todos los cuadros y de todo el audio (nesshot/genshot --hash): distingue un cuadro de diferencia
            e = rr.exe(f"{sid}shot")

            def hashes(extra, destino):
                _, s = rr.correr([e, rom, f"{d}/e.png", *extra, "--hash"])
                with open(destino, "w", encoding="utf-8", newline="\n") as f:
                    f.write("".join(l + "\n" for l in s.splitlines() if l.startswith("hash")))

            hashes(["--frames", "120", "--input", f"{d}/guion.txt"], f"{d}/guion.hash")
            hashes(["120", "start:30-35", "down:60-69", "a:60-69"], f"{d}/antes.hash")
            igual(f"{sid}shot: --input da lo mismo que boton:desde-hasta", f"{d}/guion.hash", f"{d}/antes.hash")
    else:
        print(f"(sin {rom} se omiten las pruebas de comportamiento: make testroms)")

    print("Contrato común: sin fallas." if not fallas else f"Contrato común: {fallas} fallas.")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
