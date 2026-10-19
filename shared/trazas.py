#!/usr/bin/env python3
"""El almacén de trazas por SHA1 (F4.4 de docs/PLAN-ORDEN.md; shared/trazas.h es la parte de C). Se llama desde la raíz de un
sistema, como recomp_game.py: la consola sale de qué recompilador del registro hay en la carpeta.

    python3 ../shared/trazas.py agregar [--dir DIR] ROM TRAZA...   suma esas trazas a las del juego (DIR/<sistema>/<SHA1>/)
    python3 ../shared/trazas.py sondear [--dir DIR] [--cuadros N] ROM
                                                                   el sondeo: corre el juego en el intérprete (*trace) con
                                                                   las entradas de tests/inputs/ciclo_traza.txt y deja
                                                                   sondeo.trace en la carpeta del juego
    python3 ../shared/trazas.py resumen [--dir DIR]                qué juegos hay y cuántas instrucciones tiene cada uno
    python3 ../shared/trazas.py compartir --dir DESTINO [--alias A] [--entradas ARCHIVO] ROM TRAZA...
                                                                   una aportación para el repositorio de trazas compartidas
                                                                   (#16): DESTINO/<sistema>/<SHA1>/AAAA-MM-DD-<alias o
                                                                   huella>.trace, y las entradas grabadas al lado (.txt)
    python3 ../shared/trazas.py validar CARPETA...                 lo que comprueba el CI del repositorio de trazas: cada
                                                                   carpeta es <sistema>/<SHA1>/, solo hay .trace y .txt,
                                                                   cada línea es de traza y nada pasa del límite de tamaño

DIR sale de --dir o de la variable de entorno RETROCOMP_TRAZAS (varias carpetas separadas como en PATH: se escribe en la
primera; los recompiladores leen todas, así que el almacén propio y un clon del repositorio compartido van juntos). Las
trazas de un juego se juntan en acumulada.trace: una línea por instrucción ("BANCO:DIRECCION") y las líneas "ram", sin
repetir y en orden; si una traza trae un conteo al final de la línea (" xN", de --uncovered), el conteo se descarta: lo que
importa del almacén es qué instrucciones, no cuántas veces. Una traza no lleva nada de la ROM, solo desplazamientos y
direcciones. Una aportación sin --alias es anónima: el nombre lleva una huella del contenido en lugar de quién la hizo.
"""
import datetime
import hashlib
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

ENV = "RETROCOMP_TRAZAS"
ACUMULADA = "acumulada.trace"
SONDEO = "sondeo.trace"
RE_CONTEO = re.compile(r"\s+x\d+\s*$")
# Lo que puede tener una traza (trace_line.h y ram_code.h): "BANCO:DIRECCION" (con " xN" de --counts, o el formato viejo
# "DESPLAZAMIENTO:DIRECCION") y "ram DIRECCION B0 B1 ..."
RE_LINEA = re.compile(r"[0-9A-Fa-f]{1,8}:[0-9A-Fa-f]{1,8}( x\d+)?|ram [0-9A-Fa-f]{1,8}( [0-9A-Fa-f]{2}){1,16}")
RE_SHA1 = re.compile(r"[0-9a-f]{40}")
RE_ALIAS = re.compile(r"[A-Za-z0-9_-]{1,32}")
LIMITE = 8 * 1024 * 1024        # bytes por archivo en el repositorio compartido


def carpeta(dado):
    """La primera carpeta del almacén (donde se escribe): la de --dir o la de RETROCOMP_TRAZAS, que pueden ser varias
    separadas como en PATH (os.pathsep); los recompiladores leen todas"""
    return (dado or os.environ.get(ENV) or "").split(os.pathsep)[0]


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


def juntar(destino, trazas):
    """Suma las trazas (rutas que existen) a destino/acumulada.trace; devuelve (ruta, líneas, nuevas)"""
    os.makedirs(destino, exist_ok=True)
    ruta = os.path.join(destino, ACUMULADA)
    juntas = set(lineas_de(ruta)) if os.path.isfile(ruta) else set()
    antes = len(juntas)
    for t in trazas:
        juntas.update(lineas_de(t))
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(l + "\n" for l in sorted(juntas)))
    return ruta, len(juntas), len(juntas) - antes


def agregar(dir_, rom, trazas):
    for t in trazas:
        if not os.path.isfile(t):
            print(f"no encuentro la traza {t}", file=sys.stderr)
            return 2
    d = destino_de(dir_, rom)
    if not d:
        return 2
    sis, sha1, destino = d
    ruta, total, nuevas = juntar(destino, trazas)
    print(f"{sis}/{sha1}: {total} líneas ({nuevas} nuevas) en {ruta}")
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


def validar(carpetas):
    """Los errores de un repositorio (o almacén) de trazas compartidas, como lista de textos: lo que comprueba su CI"""
    sistemas = set(rr.registro())
    errores = []
    for raiz in carpetas:
        if not os.path.isdir(raiz):
            errores.append(f"{raiz}: no es una carpeta")
            continue
        for actual, dirs, archivos in os.walk(raiz):
            dirs[:] = [d for d in dirs if not (actual == raiz and d.startswith("."))]     # .git, .github
            rel = os.path.relpath(actual, raiz).replace("\\", "/")
            partes = [] if rel == "." else rel.split("/")
            if len(partes) == 1 and partes[0] not in sistemas:
                errores.append(f"{rel}: no es un sistema del registro ({', '.join(sorted(sistemas))})")
            if len(partes) == 2 and not RE_SHA1.fullmatch(partes[1]):
                errores.append(f"{rel}: la carpeta de un juego es su SHA1, 40 dígitos hexadecimales en minúsculas")
            if len(partes) > 2:
                errores.append(f"{rel}: demasiado profundo (va <sistema>/<SHA1>/archivo)")
            for a in archivos:
                ruta = os.path.join(actual, a)
                donde = f"{rel}/{a}" if partes else a
                if len(partes) < 2:
                    if a.lower() not in ("readme.md", "license", ".gitattributes", ".gitignore"):
                        errores.append(f"{donde}: fuera de <sistema>/<SHA1>/")
                    continue
                if not (a.endswith(".trace") or a.endswith(".txt")):
                    errores.append(f"{donde}: solo valen .trace (trazas) y .txt (entradas grabadas)")
                    continue
                if os.path.getsize(ruta) > LIMITE:
                    errores.append(f"{donde}: pasa de {LIMITE // (1024 * 1024)} MB")
                    continue
                if a.endswith(".trace"):
                    with open(ruta, "rb") as f:
                        for n, crudo in enumerate(f, 1):
                            l = crudo.decode("utf-8", "replace").strip()
                            if l and not RE_LINEA.fullmatch(l):
                                errores.append(f"{donde}:{n}: no es una línea de traza: {l[:60]!r}")
                                break
    return errores


def compartir(destino, alias, entradas, rom, trazas_):
    """Una aportación: las trazas juntas (sin conteos ni repetidas) en DESTINO/<sistema>/<SHA1>/AAAA-MM-DD-<alias o huella>.trace,
    y las entradas grabadas con el mismo nombre y .txt"""
    if alias and not RE_ALIAS.fullmatch(alias):
        print("--alias: letras, números, - y _ (32 como mucho)", file=sys.stderr)
        return 2
    for t in trazas_ + ([entradas] if entradas else []):
        if not os.path.isfile(t):
            print(f"no encuentro {t}", file=sys.stderr)
            return 2
    d = destino_de(destino, rom)
    if not d:
        return 2
    sis, sha1, carpeta_juego = d
    lineas = set()
    for t in trazas_:
        lineas.update(lineas_de(t))
    malas = [l for l in lineas if not RE_LINEA.fullmatch(l)]
    if malas:
        print(f"no son líneas de traza: {malas[0][:60]!r} (y {len(malas) - 1} más)", file=sys.stderr)
        return 2
    texto = "".join(l + "\n" for l in sorted(lineas))
    nombre = f"{datetime.date.today().isoformat()}-{alias or hashlib.sha1(texto.encode()).hexdigest()[:8]}"
    ruta = os.path.join(carpeta_juego, nombre + ".trace")
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)
    if entradas:
        shutil.copyfile(entradas, os.path.join(carpeta_juego, nombre + ".txt"))
    errores = validar([destino])
    for e in errores:
        print(f"aviso: {e}", file=sys.stderr)
    print(f"{sis}/{sha1}: {len(lineas)} líneas en {ruta}" + (" (con las entradas)" if entradas else ""))
    return 1 if errores else 0


def main(argv):
    rr.utf8()
    dir_ = None
    cuadros = None
    alias = None
    entradas = None
    resto = []
    i = 0
    while i < len(argv):
        if argv[i] == "--dir" and i + 1 < len(argv):
            dir_ = argv[i + 1]
            i += 2
        elif argv[i] == "--cuadros" and i + 1 < len(argv) and argv[i + 1].isdigit():
            cuadros = int(argv[i + 1])
            i += 2
        elif argv[i] == "--alias" and i + 1 < len(argv):
            alias = argv[i + 1]
            i += 2
        elif argv[i] == "--entradas" and i + 1 < len(argv):
            entradas = argv[i + 1]
            i += 2
        else:
            resto.append(argv[i])
            i += 1
    if resto[:1] == ["compartir"] and len(resto) >= 3:
        if not dir_:
            print("compartir: falta --dir DESTINO (la carpeta de la aportación o un clon del repositorio de trazas)", file=sys.stderr)
            return 2
        return compartir(dir_, alias, entradas, resto[1], resto[2:])
    if resto[:1] == ["validar"] and len(resto) >= 2:
        errores = validar(resto[1:])
        for e in errores:
            print(e)
        print(f"{len(errores)} errores" if errores else "todo en orden")
        return 1 if errores else 0
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
