#!/usr/bin/env python3
"""Ciclo completo para un juego: traza guionada, recompila, compila, corre difftest con
varias semillas, junta las direcciones que cayeron al intérprete, recompila con esa
traza y vuelve a probar.

Uso (desde la raíz de un sistema): python3 ../shared/recomp_game.py ROM NOMBRE [CUADROS] [SEMILLAS...]
  CUADROS: los de cada difftest («cuadros» del sistema en shared/sistemas.json si no se dice).
  Deja el proyecto en build/out/NOMBRE y la traza acumulada en build/NOMBRE.trace.
  TRACE_FRAMES=N: cuadros de la traza guionada inicial («cuadros_traza» del registro si no se dice).
  NOTRACE=1: sin la traza guionada inicial (solo análisis estático y lo que descubra difftest).
  INPUT_TRACE=ARCHIVO, INPUT_DIFFTEST=ARCHIVO: otras entradas (una partida grabada con --record)
    en lugar de las guionadas de abajo; sirven cuando estas no pasan del título de un juego.

La consola sale de qué recompilador del registro (shared/sistemas.json) hay en la carpeta. Las entradas guionadas
(Start, A, derecha...) son las de tests/inputs/ciclo_traza.txt y ciclo_difftest.txt, las mismas para todos los
juegos (las del Neo Geo van desplazadas 1200 cuadros): sirven para pasar del título al juego y moverse un poco. Las
semillas agregan un mando aleatorio.

Además de las líneas para leer, deja líneas "RESUMEN {json}" para la GUI y los scripts: una al empezar cada ronda
({"evento":"ronda","ronda":N,"direcciones":D}, D las de la traza en la segunda), una por semilla
({"evento":"semilla","ronda":N,"semilla":"S","difftest":{el RESUMEN de difftest, o null}}) y la última
({"evento":"fin","ok":true|false}); todas con "herramienta":"recomp_game".
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

USO = ("uso: python3 ../shared/recomp_game.py ROM NOMBRE [CUADROS] [SEMILLAS...]  "
       "(TRACE_FRAMES=N, NOTRACE=1, INPUT_TRACE=F, INPUT_DIFFTEST=F)")


def resumen(campos):
    print('RESUMEN {"herramienta":"recomp_game",' + campos + "}")


def ordenar_unico(ruta):
    with open(ruta, encoding="utf-8") as f:
        lineas = sorted({l.rstrip("\n") for l in f})
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(l + "\n" for l in lineas))


def main(argv):
    rr.utf8()
    reg = rr.registro()
    con = rr.sistema_aqui()
    if not con:
        print("no encuentro el recompilador de ningún sistema de shared/sistemas.json en esta carpeta", file=sys.stderr)
        return 2
    env = os.environ
    rom = argv[0] if argv else ""
    nombre = argv[1] if len(argv) > 1 else ""
    tf = env.get("TRACE_FRAMES") or reg[con]["cuadros_traza"]
    df = argv[2] if len(argv) > 2 else reg[con]["cuadros"]
    in_t = env.get("INPUT_TRACE") or "tests/inputs/ciclo_traza.txt"
    in_d = env.get("INPUT_DIFFTEST") or "tests/inputs/ciclo_difftest.txt"
    if not (os.path.isfile(rom) and nombre):
        print(USO, file=sys.stderr)
        return 2
    semillas = argv[3:] or ["1", "2", "3"]
    rom = os.path.abspath(rom)
    x = ".exe" if os.path.isfile(f"./{con}recomp.exe") else ""
    out = f"build/out/{nombre}"
    traza = f"build/{nombre}.trace"
    os.makedirs("build/out", exist_ok=True)
    open(traza, "a").close()

    if not env.get("NOTRACE"):
        c, _ = rr.correr([f"./{con}trace{x}", rom, f"build/{nombre}.guiada", "--frames", str(tf), "--input", in_t])
        if c != 0:
            return 2
        with open(f"build/{nombre}.guiada", encoding="utf-8") as g, open(traza, "a", encoding="utf-8", newline="\n") as t:
            t.write(g.read())
        ordenar_unico(traza)

    estado = {"falla": False, "ronda": 1}

    def ronda():
        c, salida = rr.correr([f"./{con}recomp{x}", rom, "-o", out, "--name", nombre, "--trace", traza])
        util = [l for l in salida.splitlines() if re.search("código|funciones", l)]
        if not util:
            return 2
        print("\n".join(util))
        cmd = ["make", "-s", "-C", out, "-j8"]
        if env.get("CC"):
            cmd.insert(1, "CC=" + env["CC"])
        _, salida = rr.correr(cmd)
        errores = [l for l in salida.splitlines() if "error" in l]
        if errores:
            print("\n".join(errores))
            return 2
        # Entradas distintas a las de la traza guionada, más el mando aleatorio de cada semilla
        for s in semillas:
            print(f"  semilla {s:<4} ", end="", flush=True)
            rc, log = rr.correr([f"{out}/difftest{x}", rom, "--frames", str(df), "--input", in_d, "--seed", s,
                                 "--uncovered", f"{out}/unc_{s}.txt"])
            with open(f"{out}/difftest.log", "w", encoding="utf-8", newline="\n") as f:
                f.write(log)
            res = [l[len("RESUMEN "):] for l in log.splitlines() if l.startswith("RESUMEN ")]
            dt = res[-1] if res else "null"
            ev = f'"evento":"semilla","ronda":{estado["ronda"]},"semilla":"{s}","difftest":{dt}'
            if rc != 0:
                print("DIFERENCIA")
                print("\n".join(l for l in log.splitlines() if not l.startswith("RESUMEN ")))
                resumen(ev)
                estado["falla"] = True
                return 0
            pct = re.findall(r"[0-9.]+% de las instrucciones nativas", log)
            print("".join(p + " " for p in pct), end="")
            interp = re.findall(r"Instrucciones de (?:PRG|ROM) interpretadas: [0-9]+", log)
            if interp:
                print("\n".join(interp))
            else:
                print()
            resumen(ev)
            with open(f"{out}/unc_{s}.txt", encoding="utf-8") as u, open(traza, "a", encoding="utf-8", newline="\n") as t:
                t.write(u.read())
        ordenar_unico(traza)
        return 0

    print(f"{nombre}: primera ronda")
    resumen('"evento":"ronda","ronda":1')
    if ronda():
        return 2
    if not estado["falla"]:
        with open(traza, encoding="utf-8") as f:
            d = sum(1 for _ in f)
        print(f"{nombre}: con la traza ({d} direcciones)")
        estado["ronda"] = 2
        resumen(f'"evento":"ronda","ronda":2,"direcciones":{d}')
        if ronda():
            return 2
    print("HAY DIFERENCIAS." if estado["falla"] else "Sin diferencias.")
    resumen('"evento":"fin","ok":' + ("false" if estado["falla"] else "true"))
    return 1 if estado["falla"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
