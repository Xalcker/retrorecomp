#!/usr/bin/env python3
"""El almacén de trazas por SHA1 (F4.4 de docs/PLAN-ORDEN.md; shared/trazas.h es la parte de C). Se llama desde la raíz de un
sistema, como recomp_game.py: la consola sale de qué recompilador del registro hay en la carpeta.

    python3 ../shared/trazas.py agregar [--dir DIR] ROM TRAZA...   suma esas trazas a las del juego (DIR/<sistema>/<SHA1>/)
    python3 ../shared/trazas.py sondear [--dir DIR] [--cuadros N] ROM
                                                                   el sondeo: corre el juego en el intérprete (*trace) con
                                                                   las entradas de tests/inputs/ciclo_traza.txt y deja
                                                                   sondeo.trace en la carpeta del juego
    python3 ../shared/trazas.py resumen [--dir DIR]                qué juegos hay y cuántas instrucciones tiene cada uno

DIR sale de --dir o de la variable de entorno RETROCOMP_TRAZAS. Las trazas de un juego se juntan en acumulada.trace: una
línea por instrucción ("BANCO:DIRECCION") y las líneas "ram", sin repetir y en orden; si una traza trae un conteo al final de
la línea (" xN", de --uncovered), el conteo se descarta: lo que importa del almacén es qué instrucciones, no cuántas veces.
Una traza no lleva nada de la ROM, solo desplazamientos y direcciones.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

ENV = "RETROCOMP_TRAZAS"
ACUMULADA = "acumulada.trace"
SONDEO = "sondeo.trace"
RE_CONTEO = re.compile(r"\s+x\d+\s*$")


def carpeta(dado):
    return dado or os.environ.get(ENV) or ""


def lineas_de(ruta):
    """Las líneas útiles de una traza: sin conteo al final, sin espacios sobrantes y sin las vacías"""
    with open(ruta, encoding="utf-8", errors="replace") as f:
        for l in f:
            l = RE_CONTEO.sub("", l.strip())
            if l:
                yield l


def destino_de(dir_, rom):
    """(sistema, SHA1, DIR/<sistema>/<SHA1>) de esa ROM, con la carpeta ya creada; None si falta algo (ya avisado)"""
    sis = rr.sistema_aqui()
    if not sis:
        print("no encuentro el recompilador de ningún sistema de shared/sistemas.json en esta carpeta", file=sys.stderr)
        return None
    if not dir_:
        print(f"falta la carpeta del almacén: --dir DIR o la variable {ENV}", file=sys.stderr)
        return None
    if not os.path.isfile(rom):
        print(f"no encuentro la ROM {rom}", file=sys.stderr)
        return None
    sha1 = rr.sha1_de_info(f"{sis}recomp", rom)
    if not re.fullmatch(r"[0-9a-fA-F]{40}", sha1 or ""):
        print(f"no pude leer el SHA1 de {rom} con {sis}recomp --info", file=sys.stderr)
        return None
    destino = os.path.join(dir_, sis, sha1.lower())
    os.makedirs(destino, exist_ok=True)
    return sis, sha1.lower(), destino


def agregar(dir_, rom, trazas):
    d = destino_de(dir_, rom)
    if not d:
        return 2
    sis, sha1, destino = d
    ruta = os.path.join(destino, ACUMULADA)
    juntas = set(lineas_de(ruta)) if os.path.isfile(ruta) else set()
    antes = len(juntas)
    for t in trazas:
        if not os.path.isfile(t):
            print(f"no encuentro la traza {t}", file=sys.stderr)
            return 2
        juntas.update(lineas_de(t))
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(l + "\n" for l in sorted(juntas)))
    print(f"{sis}/{sha1}: {len(juntas)} líneas ({len(juntas) - antes} nuevas) en {ruta}")
    return 0


def sondear(dir_, rom, cuadros=None):
    """El sondeo: corre el juego en el intérprete con el *trace del sistema y las entradas guionadas de
    tests/inputs/ciclo_traza.txt (las de la traza inicial de recomp_game.py) y deja lo que ejecutó en
    DIR/<sistema>/<SHA1>/sondeo.trace, que el recompilador suma solo con --traces-dir"""
    d = destino_de(dir_, rom)
    if not d:
        return 2
    sis, sha1, destino = d
    cuadros = cuadros or rr.registro()[sis]["cuadros_traza"]
    ruta = os.path.join(destino, SONDEO)
    entradas = os.path.join("tests", "inputs", "ciclo_traza.txt")
    cmd = [rr.exe(f"{sis}trace"), rom, ruta, "--frames", str(cuadros), *(["--input", entradas] if os.path.isfile(entradas) else [])]
    codigo, salida = rr.correr(cmd)
    if codigo != 0 or not os.path.isfile(ruta):
        sys.stdout.write(salida)
        print(f"el sondeo de {rom} falló ({sis}trace salió con {codigo})", file=sys.stderr)
        return 2
    print(f"{sis}/{sha1}: sondeo de {cuadros} cuadros, {sum(1 for _ in lineas_de(ruta))} líneas en {ruta}")
    return 0


def resumen(dir_):
    if not dir_ or not os.path.isdir(dir_):
        print(f"no hay almacén en {dir_ or '(sin carpeta)'}", file=sys.stderr)
        return 2
    total = 0
    for sis in sorted(os.listdir(dir_)):
        base = os.path.join(dir_, sis)
        if not os.path.isdir(base):
            continue
        for sha1 in sorted(os.listdir(base)):
            d = os.path.join(base, sha1)
            if not os.path.isdir(d):
                continue
            archivos = [a for a in os.listdir(d) if a.endswith(".trace")]
            lineas = set()
            for a in archivos:
                lineas.update(lineas_de(os.path.join(d, a)))
            print(f"{sis:4} {sha1} {len(archivos):3} archivos {len(lineas):8} líneas")
            total += 1
    print(f"{total} juegos")
    return 0


def main(argv):
    rr.utf8()
    dir_ = None
    cuadros = None
    resto = []
    i = 0
    while i < len(argv):
        if argv[i] == "--dir" and i + 1 < len(argv):
            dir_ = argv[i + 1]
            i += 2
        elif argv[i] == "--cuadros" and i + 1 < len(argv) and argv[i + 1].isdigit():
            cuadros = int(argv[i + 1])
            i += 2
        else:
            resto.append(argv[i])
            i += 1
    dir_ = carpeta(dir_)
    if resto[:1] == ["agregar"] and len(resto) >= 3:
        return agregar(dir_, resto[1], resto[2:])
    if resto[:1] == ["sondear"] and len(resto) == 2:
        return sondear(dir_, resto[1], cuadros)
    if resto[:1] == ["resumen"] and len(resto) == 1:
        return resumen(dir_)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
