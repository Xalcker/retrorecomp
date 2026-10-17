"""Línea base exacta (golden), común a los cinco sistemas: hash de todos los cuadros y de todo el audio de
cada caso (una ROM corrida con entradas fijas). Una optimización no debe cambiar ni un bit.
    python3 tests/golden.py                 compara con tests/golden.txt
    python3 tests/golden.py --update        escribe en tests/golden.txt los casos que corrieron; los que no se
                                            pudieron correr aquí (falta la ROM) se quedan como estaban
    python3 tests/golden.py --recompilado   lo mismo con el código recompilado: recompila cada ROM (sin traza, en
                                            build/golden_rec/SHA1), compila el bench del proyecto y compara su --hash
                                            con la misma línea base (si hace lo mismo que el intérprete, da lo mismo)
    python3 tests/golden.py --libres        solo los casos de testroms/ (las ROMs libres que baja make testroms: lo
                                            que corre el CI completo)
    python3 tests/golden.py ROM...          solo los casos de esas ROMs
    GOLDEN_SHOT=ruta                        otro *shot
    GOLDEN_JOBS=N                           casos a la vez (por omisión, uno por procesador)

Los casos se nombran por el SHA1 de la ROM, con /VARIANTE si una ROM corre de más de una forma (otro
modelo de consola, otra BIOS): no dependen del nombre del archivo. Cada línea de tests/golden.txt:
    SHA1[/VARIANTE] hash imagen XXXXXXXXXXXXXXXX audio YYYYYYYYYYYYYYYY  # archivo
La comparación salta los casos cuya ROM falta, cuenta aparte los nuevos (sin línea base: no cuentan
como iguales) y falla si un caso difiere, si un caso no dio hash o si no se comparó ninguno. Los hashes
no dependen de la plataforma (-ffp-contract=off en el Makefile).

Vive en shared/; cada sistema lo usa desde su tests/golden.py, que entra a su raíz y llama a
    golden.main(sis, opciones, casos, sha1=None, skip=None)
con:
    sis        gb, nes, gen, neo o sms (para <sis>shot)
    opciones   las opciones de todos los casos (cuadros y entradas), en una lista
    casos      función que recibe `caso` y la llama como caso(rom, variante=None, extra=[]) por cada caso; extra va
               después de las opciones, así que puede cambiar --frames
    sha1       (opcional) función ROM -> SHA1 que nombra el caso; por omisión, el del archivo
    skip       (opcional) función que da un motivo si el sistema no puede correr nada (y sale sin comparar, con 0)
"""
import os
import shutil
import sys
import threading
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402


def hash_de(salida):
    """La línea «hash imagen X audio Y» de la salida, o ''"""
    for linea in salida.splitlines():
        if linea.startswith("hash imagen"):
            return linea.strip()
    return ""


def main(sis, opciones, casos, sha1=None, skip=None, argv=None):
    rr.utf8()
    argv = sys.argv[1:] if argv is None else argv
    update = rec = libres = False
    solo = []
    for a in argv:
        if a == "--update":
            update = True
        elif a == "--recompilado":
            rec = True
        elif a == "--libres":
            libres = True
        elif a.startswith("-"):
            print(f"opción desconocida: {a}", file=sys.stderr)
            return 2
        else:
            solo.append(a.replace("\\", "/"))

    shot = os.environ.get("GOLDEN_SHOT") or rr.exe(f"{sis}shot")
    recomp = rr.exe(f"{sis}recomp")
    if not os.path.isfile(shot):
        print(f"falta {shot}: corre make", file=sys.stderr)
        return 2
    if rec and not os.path.isfile(recomp):
        print(f"falta {recomp}: corre make", file=sys.stderr)
        return 2
    if rec and update:
        print("--update es con el intérprete: la línea base no sale del código recompilado", file=sys.stderr)
        return 2
    exe_ext = ".exe" if shot.endswith(".exe") else ""
    if skip:
        porque = skip()
        if porque:
            print(f"golden: nada que comparar ({porque})")
            return 0
    jobs = int(os.environ.get("GOLDEN_JOBS") or os.cpu_count() or 4)
    out = "build/golden"
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    sha1 = sha1 or rr.sha1_archivo

    # Con --recompilado, el proyecto generado de la ROM (uno por SHA1, aunque la ROM tenga varias variantes). Sin
    # traza: lo que el análisis estático no encuentra lo corre el intérprete, y el hash debe salir igual
    cerrojo = threading.Lock()

    def proyecto(sha, rom):
        d = f"build/golden_rec/{sha}"
        with cerrojo:
            if os.path.isfile(f"{d}/bench{exe_ext}"):
                return True
            shutil.rmtree(d, ignore_errors=True)
            c1, s1 = rr.correr([recomp, rom, "-o", d])
            ok = c1 == 0
            s2 = ""
            if ok:
                cmd = ["make", "-s", "-C", d, f"-j{jobs}", f"bench{exe_ext}"]
                if os.environ.get("CC"):
                    cmd.insert(1, "CC=" + os.environ["CC"])
                c2, s2 = rr.correr(cmd)
                ok = c2 == 0
            with open(d + ".log", "w", encoding="utf-8") as f:
                f.write(s1 + s2)
            return ok

    pendientes = []     # (índice, clave, rom, extra)

    def caso(rom, variante=None, extra=()):
        if not os.path.isfile(rom):
            return
        rom = rom.replace("\\", "/")
        if not rom.startswith("testroms/") and libres:
            return
        if solo and rom not in solo:
            return
        sha = sha1(rom) or "sin-sha1"
        if rec:
            os.makedirs("build/golden_rec", exist_ok=True)
            if not proyecto(sha, rom):
                print(f"  no se pudo recompilar o compilar {rom} (build/golden_rec/{sha}.log)", file=sys.stderr)
        clave = sha + (f"/{variante}" if variante else "")
        pendientes.append((clave, rom, list(extra)))

    casos(caso)

    def correr_caso(c):
        clave, rom, extra = c
        if rec:
            cmd = [f"build/golden_rec/{clave.split('/')[0]}/bench{exe_ext}", rom, *opciones, *extra, "--hash"]
        else:
            cmd = [shot, rom, os.devnull, *opciones, *extra, "--hash"]
        _, salida = rr.correr(cmd)
        return f"{clave} {hash_de(salida) or 'hash error'}  # {os.path.basename(rom)}"

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        lineas = list(pool.map(correr_caso, pendientes))
    with open(out + ".txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(l + "\n" for l in lineas))

    base = "tests/golden.txt"
    if not os.path.isfile(base):
        open(base, "w").close()
    with open(base, encoding="utf-8") as f:
        existentes = [l.rstrip("\n") for l in f if l.strip()]

    if update:
        # Los casos que corrieron reemplazan su línea (o se agregan al final); los demás se quedan. Un caso que no dio
        # hash (una ROM que el emulador no carga) no entra
        nuevo = {}
        orden = []
        for l in lineas:
            if " hash error  #" in l:
                continue
            k = l.split(" ", 1)[0]
            nuevo[k] = l
            orden.append(k)
        hechos = set()
        salida = []
        for l in existentes:
            k = l.split(" ", 1)[0]
            if k in nuevo:
                salida.append(nuevo[k])
                hechos.add(k)
            else:
                salida.append(l)
        salida += [nuevo[k] for k in orden if k not in hechos]
        with open(base, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join(l + "\n" for l in salida))
        print(f"tests/golden.txt: {sum(1 for l in lineas if ' hash error ' not in l)} casos escritos ({len(salida)} en total)")
        for l in lineas:
            if " hash error " in l:
                print(f"no corre: {l}")
        return 0

    esperado = {}
    for l in existentes:
        k, _, resto = l.partition(" ")
        esperado.setdefault(k, resto.split("  #")[0])
    igual = nuevos = malo = nocorre = 0
    for l in lineas:
        k, _, resto = l.partition(" ")
        ahora, _, nombre = resto.partition("  # ")
        quiero = esperado.get(k)
        if ahora == "hash error" and quiero is None:
            print(f"(no corre: {nombre} ({k}), sin línea base)")
            nocorre += 1
        elif ahora == "hash error":
            print(f"ERROR: {nombre} ({k}): el caso no dio hash")
            malo += 1
        elif quiero is None:
            print(f"(nuevo: {k} {ahora}  # {nombre})")
            nuevos += 1
        elif quiero == ahora:
            igual += 1
        else:
            print(f"DIFERENTE: {nombre} ({k})")
            print(f"  esperado: {quiero}")
            print(f"  ahora:    {ahora}")
            malo += 1
    corrieron = {l.split(" ", 1)[0] for l in lineas}
    sin = sum(1 for l in existentes if l.split(" ", 1)[0] not in corrieron)
    extra = f", {nocorre} que no corren" if nocorre else ""
    resumen = f"{igual} iguales, {malo} diferentes, {nuevos} nuevos{extra}; {sin} de los {len(existentes)} de tests/golden.txt sin correr"
    que = "golden" + (" (recompilado)" if rec else "")
    if malo:
        print(f"{que}: DIFERENTE ({resumen})")
        return 1
    if igual == 0:
        print(f"{que}: no se comparó nada ({resumen})")
        return 1
    print(f"{que}: igual ({resumen})")
    return 0
