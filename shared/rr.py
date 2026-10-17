"""Lo que repiten los scripts de prueba de shared/ (golden, estricto, sst, recomp_game, contrato, ui_golden): el
registro de sistemas, buscar el ejecutable de una herramienta, qué consola hay en la carpeta, correr un programa
y los hashes. Solo la biblioteca estándar.

Todos los scripts se corren desde la raíz de un sistema (gb/, nes/, gen/, neo/ o sms/):
    python3 ../shared/golden.py   (o los tests/golden.py de cada sistema, que entran a su raíz)
"""
import hashlib
import json
import os
import platform
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))


def utf8():
    """La salida en UTF-8 y con \\n también en Windows (los scripts imprimen acentos y los lee la GUI)"""
    for f in (sys.stdout, sys.stderr):
        try:
            f.reconfigure(encoding="utf-8", newline="\n")
        except (AttributeError, ValueError):
            pass


def registro():
    """shared/sistemas.json: un dict por id de sistema"""
    with open(os.path.join(AQUI, "sistemas.json"), encoding="utf-8") as f:
        return json.load(f)


def exe(nombre):
    """./nombre.exe si existe (Windows), si no ./nombre"""
    if os.path.isfile(f"./{nombre}.exe"):
        return f"./{nombre}.exe"
    return f"./{nombre}"


def sufijo_exe():
    """'.exe' si el recompilador de esta carpeta es de Windows, '' si no (el objetivo del Makefile generado es
    difftest.exe en Windows)"""
    return ".exe" if any(os.path.isfile(f"./{sid}recomp.exe") for sid in registro()) else ""


def sistema_aqui():
    """El id de la consola cuyo recompilador hay en la carpeta actual, o None"""
    aqui = None
    for sid in registro():
        if os.path.isfile(f"./{sid}recomp") or os.path.isfile(f"./{sid}recomp.exe") \
                or os.path.isfile(os.path.join("src", f"{sid}recomp", f"{sid}recomp.c")):
            aqui = sid
    return aqui


def entrar_a_la_raiz(script):
    """Cambia a la raíz del sistema de un tests/<script>.py (la carpeta que contiene tests/)"""
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(script)), ".."))


def correr(cmd, entrada=None, **kw):
    """Corre cmd y devuelve (código, salida con stdout y stderr juntos, como texto UTF-8). Un programa que no existe
    da el código 127 (como sh) en lugar de una excepción"""
    cmd = list(cmd)
    if os.sep in cmd[0] or "/" in cmd[0]:
        cmd[0] = os.path.abspath(cmd[0])        # en Windows, una ruta relativa con «/» no la encuentra CreateProcess
    stderr = kw.pop("stderr", subprocess.STDOUT)    # DEVNULL para quedarse solo con stdout
    try:
        p = subprocess.run(cmd, input=entrada, stdout=subprocess.PIPE, stderr=stderr, **kw)
    except OSError as e:
        return 127, f"{cmd[0]}: {e.strerror}\n"
    return p.returncode, p.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")


def sha1_archivo(ruta, saltar=0):
    """SHA1 hexadecimal del archivo (sin sus primeros `saltar` bytes)"""
    with open(ruta, "rb") as f:
        f.seek(saltar)
        return hashlib.sha1(f.read()).hexdigest()


def sha1_de_info(recompilador, rom):
    """El SHA1 que da `<sistema>recomp ROM --info` (sin la cabecera de copiador, si la ROM la tiene), o ''"""
    _, salida = correr([exe(recompilador), rom, "--info"])
    for linea in salida.splitlines():
        if linea.startswith("SHA1:"):
            return linea.split()[1]
    return ""


def uname():
    """Como `uname -s` (Darwin, Linux, Windows...)"""
    return platform.system()


def _tabla_cksum():
    t = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
        t.append(c)
    return t


_CK = _tabla_cksum()


def cksum(datos):
    """El «CRC tamaño» que imprime cksum(1) de POSIX, como el texto «CRC tamaño»"""
    crc = 0
    for b in datos:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CK[((crc >> 24) ^ b) & 0xFF]
    n = len(datos)
    while n:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CK[((crc >> 24) ^ n) & 0xFF]
        n >>= 8
    return f"{~crc & 0xFFFFFFFF} {len(datos)}"


def leer(ruta):
    with open(ruta, "rb") as f:
        return f.read()
