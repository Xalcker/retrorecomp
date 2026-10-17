"""Lo que baja `make testroms` en cada sistema (no se versionan): descargas con SHA-256, tarballs y zips, las
SingleStepTests y el juego de prueba del contrato. Solo la biblioteca estándar (urllib, tarfile, zipfile, gzip).

Cada tests/testroms.py entra a la raíz de su sistema y llama a lo que necesita; las versiones fijas viven ahí (de las
URLs depende el caché de ROMs de prueba del CI).
"""
import gzip
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

GITHUB = "https://github.com"
SST_68000 = "https://codeload.github.com/SingleStepTests/680x0/tar.gz/e0d5ece9670205cc84a0101081837deb446f86a3"
SST_Z80 = "https://codeload.github.com/SingleStepTests/z80/tar.gz/ebe1875d48f374bcfd4b505d8eb8ee751568b5f7"


class Falla(Exception):
    pass


def sha256(ruta):
    h = hashlib.sha256()
    with open(ruta, "rb") as f:
        for bloque in iter(lambda: f.read(1 << 20), b""):
            h.update(bloque)
    return h.hexdigest()


def bajar(url, destino):
    """Baja url a destino (sigue las redirecciones); Falla si no se puede"""
    os.makedirs(os.path.dirname(destino) or ".", exist_ok=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "retrorecomp-testroms"})
        with urllib.request.urlopen(req, timeout=120) as r, open(destino, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
    except (OSError, ValueError) as e:
        if os.path.exists(destino):
            os.remove(destino)
        raise Falla(f"no se pudo bajar {url}: {e}")


def bajar_verificado(url, suma, destino):
    """Baja url a destino y comprueba su SHA-256 (si no coincide, lo borra)"""
    bajar(url, destino)
    if sha256(destino) != suma:
        os.remove(destino)
        raise Falla(f"{url}: el SHA-256 no coincide")


def extraer_tar(tarball, destino, quitar=1):
    """Extrae un .tar.gz en destino sin sus primeros `quitar` componentes (tar --strip-components)"""
    os.makedirs(destino, exist_ok=True)
    base = os.path.realpath(destino)
    with tarfile.open(tarball, "r:gz") as t:
        for m in t:
            partes = m.name.split("/")[quitar:]
            if not partes or not any(partes):
                continue
            m.name = "/".join(partes)
            if os.path.realpath(os.path.join(destino, m.name)) != base and not os.path.realpath(os.path.join(destino, m.name)).startswith(base + os.sep):
                raise Falla(f"{tarball}: ruta insegura {m.name}")
            if m.isfile() or m.isdir():
                t.extract(m, destino)


def sst_68000():
    if os.path.isfile("testroms/680x0/68000/v1/NOP.json.gz"):
        print("testroms/680x0 ya tiene las SingleStepTests del 68000")
        return
    bajar(SST_68000, "build/680x0.tar.gz")
    extraer_tar("build/680x0.tar.gz", "testroms/680x0")
    os.remove("build/680x0.tar.gz")
    print("SingleStepTests del 68000 en testroms/680x0")


def sst_z80():
    if os.path.isfile("testroms/z80/v1/00.json.gz"):
        print("testroms/z80 ya tiene las SingleStepTests del Z80")
        return
    shutil.rmtree("build/z80tests", ignore_errors=True)
    os.makedirs("build/z80tests")
    os.makedirs("testroms/z80/v1", exist_ok=True)
    bajar(SST_Z80, "build/z80.tar.gz")
    extraer_tar("build/z80.tar.gz", "build/z80tests")
    os.remove("build/z80.tar.gz")

    def comprimir(nombre):
        with open(f"build/z80tests/v1/{nombre}", "rb") as f, gzip.open(f"testroms/z80/v1/{nombre}.gz", "wb", compresslevel=6) as g:
            shutil.copyfileobj(f, g, 1 << 20)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(comprimir, sorted(n for n in os.listdir("build/z80tests/v1") if n.endswith(".json"))))
    shutil.copyfile("build/z80tests/LICENSE", "testroms/z80/LICENSE")
    shutil.rmtree("build/z80tests")
    print("SingleStepTests del Z80 en testroms/z80")


def sst(saltar, cpus):
    """Las SingleStepTests de `cpus` ("68000" y/o "z80"), salvo con SKIP_SST (el CI de macOS y Windows: son lo que más
    pesa; make test las omite si no están)"""
    nombres = " ni del ".join({"68000": "68000", "z80": "Z80"}[c] for c in cpus)
    if saltar:
        print(f"SKIP_SST: sin las SingleStepTests del {nombres}")
        return
    if "68000" in cpus:
        sst_68000()
    if "z80" in cpus:
        sst_z80()


def zip_get(url, suma, carpeta, *miembros):
    """Baja el zip una vez, comprueba su SHA-256 y saca cada miembro «origen=destino» a testroms/carpeta/destino"""
    dir_ = f"testroms/{carpeta}"
    pares = [m.split("=", 1) for m in miembros]
    if all(os.path.isfile(f"{dir_}/{d}") for _, d in pares):
        print(f"{dir_}: ya están " + "".join(d + " " for _, d in pares))
        return
    os.makedirs(dir_, exist_ok=True)
    z = f"build/{os.path.basename(url)}"
    bajar_verificado(url, suma, z)
    with zipfile.ZipFile(z) as zf:
        for origen, destino in pares:
            try:
                datos = zf.read(origen)
            except KeyError:
                datos = b""
            if not datos:
                if os.path.exists(f"{dir_}/{destino}"):
                    os.remove(f"{dir_}/{destino}")
                raise Falla(f"{z}: falta {origen}")
            with open(f"{dir_}/{destino}", "wb") as f:
                f.write(datos)
    os.remove(z)
    print(f"{dir_}: " + "".join(d + " " for _, d in pares))


def demo_scene(archivo, suma, miembro, destino):
    """Una demo de scene.org: baja el zip, comprueba su SHA-256 y saca el miembro a testroms/demos/destino"""
    if os.path.isfile(f"testroms/demos/{destino}"):
        print(f"testroms/demos/{destino} ya está")
        return
    os.makedirs("testroms/demos", exist_ok=True)
    z = f"build/{os.path.basename(archivo)}"
    bajar_verificado(f"https://files.scene.org/get/{archivo}", suma, z)
    with zipfile.ZipFile(z) as zf, open(f"testroms/demos/{destino}", "wb") as f:
        f.write(zf.read(miembro))
    os.remove(z)
    print(f"testroms/demos/{destino}")


def mkcontrato(salida):
    """El juego de prueba del contrato común, hecho aquí (src/tools/mkcontrato.c, MIT): no se baja de ningún lado. Siempre,
    aunque ya esté: el caché de testroms del CI depende solo de tests/testroms.py, no de mkcontrato.c"""
    os.makedirs(os.path.dirname(salida), exist_ok=True)
    x = ".exe" if os.name == "nt" or os.path.isfile("./gbrecomp.exe") or any(os.path.isfile(f"./{s}recomp.exe") for s in rr.registro()) else ""
    c, o = rr.correr(["make", "-s", f"build/mkcontrato{x}"])
    if c == 0:
        c, o = rr.correr([f"build/mkcontrato{x}", salida])
    if c != 0:
        sys.stdout.write(o)
        raise Falla("mkcontrato")


def ejecutar(principal):
    """Corre `principal()` desde la raíz de un tests/testroms.py: 0, o 1 si algo falló (con el motivo)"""
    rr.utf8()
    try:
        principal()
    except Falla as e:
        print(e)
        return 1
    return 0
