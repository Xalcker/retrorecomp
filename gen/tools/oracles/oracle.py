#!/usr/bin/env python3
"""Oráculos: emuladores de referencia (núcleos libretro de Genesis Plus GX y BlastEm) para comparar con
GENRecomp. No van en el repositorio: se bajan la primera vez a build/oracles/ desde buildbot.libretro.com
(la compilación nocturna de cada núcleo; el SHA-256 de lo que se bajó queda en build/oracles/NUCLEO/SHA256
y se imprime) y los maneja build/retro_run (tools/oracles/retro_run.c), sin ventana. Genesis Plus GX
tiene una licencia no comercial y BlastEm es GPL-3: solo se usan como programas aparte.

  tools/oracles/oracle.py fetch [gpgx|blastem]                    bajar (todos si no se dice)
  tools/oracles/oracle.py ramdump NUCLEO rom.bin [opciones]        la misma salida que ./genramdump
  tools/oracles/oracle.py shot NUCLEO rom.bin salida.png [opciones]
  tools/oracles/oracle.py compare rom.bin [opciones]              GENRecomp contra los oráculos

Opciones: las de las herramientas (--frames N, --input archivo, --seed S, boton:desde-hasta). compare
dice, para la RAM del 68000 y la imagen del último cuadro, qué parte coincide con cada oráculo y entre
ellos; con --png CARPETA guarda las tres capturas."""
import hashlib, io, os, platform, subprocess, sys, tempfile, urllib.request, zipfile

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
DIR = os.path.join(RAIZ, "build", "oracles")
EXE = ".exe" if os.name == "nt" else ""
RETRO_RUN = os.path.join(RAIZ, "build", "retro_run" + EXE)

# nombre corto -> (núcleo en el buildbot, opciones de retro_run)
NUCLEOS = {
    "gpgx": ("genesis_plus_gx", ["--swap16", "--opt", "genesis_plus_gx_region_detect=ntsc-u",
                                 "--opt", "genesis_plus_gx_bios=disabled"]),
    "blastem": ("blastem", ["--swap16"]),
}


def plataforma():
    s, m = platform.system(), platform.machine().lower()
    arq = "arm64" if m in ("arm64", "aarch64") else "x86_64"
    if s == "Darwin": return f"apple/osx/{arq}", ".dylib"
    if s == "Linux": return f"linux/{arq}", ".so"
    if s == "Windows" or os.name == "nt": return f"windows/{arq}", ".dll"
    sys.exit(f"sistema sin núcleos en el buildbot: {s} {m}")


def nucleo(corto):
    """Ruta del núcleo, bajándolo la primera vez."""
    if corto not in NUCLEOS: sys.exit(f"núcleo desconocido: {corto} (gpgx o blastem)")
    nombre = NUCLEOS[corto][0]
    camino, ext = plataforma()
    dest = os.path.join(DIR, corto)
    lib = os.path.join(dest, f"{nombre}_libretro{ext}")
    if os.path.isfile(lib): return lib
    url = f"https://buildbot.libretro.com/nightly/{camino}/latest/{nombre}_libretro{ext}.zip"
    print(f"descargando {url}", file=sys.stderr)
    datos = urllib.request.urlopen(url, timeout=120).read()
    sha = hashlib.sha256(datos).hexdigest()
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(datos)) as z:
        miembro = next((n for n in z.namelist() if n.endswith(f"_libretro{ext}")), None)
        if not miembro: sys.exit(f"{url}: el zip no trae el núcleo")
        with open(lib, "wb") as f: f.write(z.read(miembro))
    with open(os.path.join(dest, "SHA256"), "w") as f: f.write(f"{sha}  {url}\n")
    print(f"{corto}: {len(datos)} bytes, SHA-256 {sha}", file=sys.stderr)
    return lib


def retro_run():
    if not os.path.isfile(RETRO_RUN):
        subprocess.run(["make", "-C", RAIZ, "-s", os.path.relpath(RETRO_RUN, RAIZ)], check=True)
    os.makedirs(os.path.join(DIR, "sys"), exist_ok=True)
    return RETRO_RUN


def correr(corto, rom, args, shot=None, ram=True):
    argv = [retro_run(), nucleo(corto), rom, *NUCLEOS[corto][1], *args]
    if shot: argv += ["--shot", shot]
    if not ram: argv += ["--no-ram"]
    r = subprocess.run(argv, cwd=RAIZ, capture_output=True, text=True)
    if r.returncode != 0: sys.exit(f"{corto}: {r.stderr.strip()}")
    return r.stdout


def propio(herramienta):
    return os.path.join(RAIZ, herramienta + EXE)


def leer_ram(texto):
    """Los 64 KB de una salida como la de genramdump (los núcleos pueden escribir otras cosas)"""
    import re
    v = [int(m.group(1), 16) for m in re.finditer(r"^FF[0-9A-F]{4} ([0-9A-F]{2})$", texto, re.M)]
    if len(v) != 0x10000: sys.exit(f"la salida no trae los 64 KB de RAM ({len(v)} bytes)")
    return bytes(v)


def leer_png(path):
    """Pixeles de un PNG (8 bits RGB sin entrelazar, como los que escribe png.c), sin dependencias."""
    import struct, zlib
    with open(path, "rb") as f: d = f.read()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(d):
        n, tipo = struct.unpack(">I4s", d[pos:pos + 8])
        cuerpo = d[pos + 8:pos + 8 + n]
        if tipo == b"IHDR": w, h = struct.unpack(">II", cuerpo[:8])
        elif tipo == b"IDAT": idat += cuerpo
        pos += 12 + n
    raw, filas, prev, i = zlib.decompress(idat), [], bytearray(w * 3), 0
    for _ in range(h):
        filtro, fila = raw[i], bytearray(raw[i + 1:i + 1 + w * 3]); i += 1 + w * 3
        for x in range(len(fila)):
            a = fila[x - 3] if x >= 3 else 0
            b = prev[x]; c = prev[x - 3] if x >= 3 else 0
            if filtro == 1: fila[x] = (fila[x] + a) & 255
            elif filtro == 2: fila[x] = (fila[x] + b) & 255
            elif filtro == 3: fila[x] = (fila[x] + (a + b) // 2) & 255
            elif filtro == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                fila[x] = (fila[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        filas.append(bytes(fila)); prev = fila
    return w, h, filas


def parecido_img(a, b):
    """Parte de los pixeles iguales (con tolerancia de 24 por canal: las paletas de cada emulador
    convierten el color de 9 bits a 8 bits por canal de forma distinta). None si el tamaño difiere."""
    (wa, ha, fa), (wb, hb, fb) = a, b
    if (wa, ha) != (wb, hb): return None
    iguales = 0
    for ra, rb in zip(fa, fb):
        for x in range(0, wa * 3, 3):
            if abs(ra[x] - rb[x]) <= 24 and abs(ra[x + 1] - rb[x + 1]) <= 24 and abs(ra[x + 2] - rb[x + 2]) <= 24: iguales += 1
    return iguales / (wa * ha)


def parecido_ram(a, b):
    return sum(x == y for x, y in zip(a, b)) / len(a)


def compare(rom, args, png_dir=None):
    tmp = png_dir or tempfile.mkdtemp()
    os.makedirs(tmp, exist_ok=True)
    rams, imgs = {}, {}
    r = subprocess.run([propio("genramdump"), rom, *args], capture_output=True, text=True, check=True)
    rams["genrecomp"] = leer_ram(r.stdout)
    shot = os.path.join(tmp, "genrecomp.png")
    subprocess.run([propio("genshot"), rom, shot, *args], capture_output=True, check=True)
    imgs["genrecomp"] = leer_png(shot)
    for corto in NUCLEOS:
        shot = os.path.join(tmp, f"{corto}.png")
        rams[corto] = leer_ram(correr(corto, rom, args, shot=shot))
        imgs[corto] = leer_png(shot)
    nombres = list(rams)
    res = {}
    for i, a in enumerate(nombres):
        for b in nombres[i + 1:]:
            pi = parecido_img(imgs[a], imgs[b])
            res[f"{a}-{b}"] = {"ram": parecido_ram(rams[a], rams[b]), "img": pi}
            print(f"{a:>9} / {b:<8}  RAM {100 * res[f'{a}-{b}']['ram']:5.1f}%   imagen "
                  + (f"{100 * pi:5.1f}%" if pi is not None else "de otro tamaño"))
    return res


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"): print(__doc__); return 0
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == "fetch":
        for corto in (a or list(NUCLEOS)): print(nucleo(corto))
    elif cmd == "ramdump" and len(a) >= 2:
        ram = leer_ram(correr(a[0], a[1], a[2:]))
        sys.stdout.write("".join(f"{0xFF0000 | i:06X} {v:02X}\n" for i, v in enumerate(ram)))
    elif cmd == "shot" and len(a) >= 3:
        correr(a[0], a[1], a[3:], shot=os.path.abspath(a[2]), ram=False)
    elif cmd == "compare" and len(a) >= 1:
        png_dir = None
        if "--png" in a:
            i = a.index("--png"); png_dir = a[i + 1]; a = a[:i] + a[i + 2:]
        compare(a[0], a[1:], png_dir)
    else:
        print(__doc__); return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
