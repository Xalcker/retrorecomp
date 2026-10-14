#!/usr/bin/env python3
"""Oráculos: emuladores de referencia (núcleos libretro de Geolith y FBNeo) para comparar con NEORecomp.
No van en el repositorio: se bajan la primera vez a build/oracles/ desde buildbot.libretro.com (la
compilación nocturna de cada núcleo; el SHA-256 de lo que se bajó queda en build/oracles/NUCLEO/SHA256 y
se imprime) y los maneja build/retro_run (tools/oracles/retro_run.c), sin ventana. Geolith es BSD-3;
FBNeo tiene una licencia no comercial: solo se usan como programas aparte. Es el de GENRecomp con el
Neo Geo.

Lo que cada uno necesita se arma en build/oracles/ con los archivos del usuario: Geolith carga el juego
como .neo (neorecomp --neo) y busca aes.zip (neo-epo.bin y 000-lo.lo) en build/oracles/sys/; FBNeo carga
el zip de MAME por el nombre del set desde build/oracles/fbneo_roms/ (enlaces a roms/) y busca ahí un
neogeo.zip con las BIOS del AES (el neogeo.zip de MAME más neo-epo.bin, neo-po.bin y 000-lo.lo de
aes.zip). Los dos en modo AES.

  tools/oracles/oracle.py fetch [geolith|fbneo]                    bajar (todos si no se dice)
  tools/oracles/oracle.py ramdump NUCLEO juego [opciones]           la misma salida que ./neoramdump
  tools/oracles/oracle.py shot NUCLEO juego salida.png [opciones]
  tools/oracles/oracle.py wav NUCLEO juego salida.wav [opciones]    el sonido y su nivel por segundo
  tools/oracles/oracle.py compare juego [opciones] [--png CARPETA]  NEORecomp contra los oráculos
  tools/oracles/oracle.py mix juego [opciones]                      niveles de la mezcla del YM2610

Opciones: las de las herramientas (--frames N, --input archivo, --seed S, boton:desde-hasta). compare
dice, para la RAM del 68000 y la imagen del último cuadro, qué parte coincide con cada oráculo y entre
ellos. mix graba el sonido de NEORecomp por partes (FM, SSG, ADPCM-A, ADPCM-B: neorun --mute) y el de
cada oráculo, mezclado; ajusta la energía del oráculo, por tramos de cuadros, como suma de las energías de
las partes (mínimos cuadrados sin negativos) y da cuánto más fuerte suena cada parte en el oráculo que
en NEORecomp, con la FM como referencia."""
import hashlib, io, math, os, platform, re, struct, subprocess, sys, tempfile, urllib.request, zipfile

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
DIR = os.path.join(RAIZ, "build", "oracles")
EXE = ".exe" if os.name == "nt" else ""
RETRO_RUN = os.path.join(RAIZ, "build", "retro_run" + EXE)

# nombre corto -> (núcleo en el buildbot, opciones de retro_run, cuadros por segundo)
NUCLEOS = {
    "geolith": ("geolith", ["--opt", "geolith_system_type=aes", "--opt", "geolith_region=us",
                            "--opt", "geolith_overscan_t=0", "--opt", "geolith_overscan_b=0",
                            "--opt", "geolith_overscan_l=0", "--opt", "geolith_overscan_r=0",
                            "--opt", "geolith_palette=raw"], 59.5995),
    "fbneo": ("fbneo", ["--opt", "fbneo-neogeo-mode=AES_EUR", "--opt", "fbneo-samplerate=48000",
                        "--opt", "fbneo-lowpass-filter=disabled"], 59.18),
}
NEO_FPS = 59.5995


def plataforma():
    s, m = platform.system(), platform.machine().lower()
    arq = "arm64" if m in ("arm64", "aarch64") else "x86_64"
    if s == "Darwin": return f"apple/osx/{arq}", ".dylib"
    if s == "Linux": return f"linux/{arq}", ".so"
    if s == "Windows" or os.name == "nt": return f"windows/{arq}", ".dll"
    sys.exit(f"sistema sin núcleos en el buildbot: {s} {m}")


def nucleo(corto):
    """Ruta del núcleo, bajándolo la primera vez."""
    if corto not in NUCLEOS: sys.exit(f"núcleo desconocido: {corto} (geolith o fbneo)")
    nombre = NUCLEOS[corto][0]
    camino, ext = plataforma()
    dest = os.path.join(DIR, corto)
    lib = os.path.join(dest, f"{nombre}_libretro{ext}")
    if os.path.isfile(lib): return lib
    url = f"https://buildbot.libretro.com/nightly/{camino}/latest/{nombre}_libretro{ext}.zip"
    print(f"descargando {url}", file=sys.stderr)
    datos = urllib.request.urlopen(url, timeout=300).read()
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
    return RETRO_RUN


def propio(herramienta):
    return os.path.join(RAIZ, herramienta + EXE)


def info(juego):
    """SHA1 de la imagen y nombre del set (el de la base de datos, o el del archivo)"""
    r = subprocess.run([propio("neorecomp"), juego, "--info"], capture_output=True, text=True)
    sha = re.search(r"^SHA1:\s+([0-9a-f]{40})", r.stdout, re.M)
    if not sha: sys.exit(f"{juego}: neorecomp no lo carga\n{r.stdout}{r.stderr}")
    s = re.search(r"^juego:.*\((\w+),", r.stdout, re.M)
    return sha.group(1), (s.group(1) if s else os.path.splitext(os.path.basename(juego))[0])


def bios_aes():
    """aes.zip del usuario (roms/ o NEO_BIOS)"""
    for c in (os.environ.get("NEO_BIOS", ""), os.path.join(RAIZ, "roms", "aes.zip")):
        if c and c.endswith(".zip") and os.path.isfile(c): return c
    sys.exit("falta la BIOS del AES: roms/aes.zip (con neo-epo.bin y 000-lo.lo) o NEO_BIOS")


def preparar(corto, juego):
    """Lo que el núcleo carga: Geolith un .neo (y aes.zip en sys/), FBNeo el zip por el nombre del set"""
    sha, setname = info(juego)
    sysdir = os.path.join(DIR, "sys")
    os.makedirs(sysdir, exist_ok=True)
    if corto == "geolith":
        aes = os.path.join(sysdir, "aes.zip")
        if not os.path.isfile(aes):
            with open(bios_aes(), "rb") as f, open(aes, "wb") as g: g.write(f.read())
        neo = os.path.join(DIR, "neo", sha + ".neo")
        if not os.path.isfile(neo):
            os.makedirs(os.path.dirname(neo), exist_ok=True)
            subprocess.run([propio("neorecomp"), juego, "--neo", neo], capture_output=True, check=True)
        return neo, sysdir
    rdir = os.path.join(DIR, "fbneo_roms")
    os.makedirs(rdir, exist_ok=True)
    bios = os.path.join(rdir, "neogeo.zip")
    if not os.path.isfile(bios):
        mvs = os.path.join(os.path.dirname(os.path.abspath(juego)), "neogeo.zip")
        with zipfile.ZipFile(bios, "w", zipfile.ZIP_DEFLATED) as z:
            fuentes = [(mvs, None)] if os.path.isfile(mvs) else []
            fuentes.append((bios_aes(), ("neo-epo.bin", "neo-po.bin", "000-lo.lo")))
            for src, keep in fuentes:
                with zipfile.ZipFile(src) as s:
                    for n in s.namelist():
                        if (keep and n not in keep) or n in z.namelist(): continue
                        z.writestr(n, s.read(n))
    if not juego.endswith(".zip"): sys.exit("FBNeo necesita el zip de MAME del juego")
    # el set con su nombre (un clon de un zip merged, como kof98h dentro de kof98.zip, apunta al mismo zip)
    for n in {setname, os.path.splitext(os.path.basename(juego))[0]}:
        enlace = os.path.join(rdir, n + ".zip")
        if not os.path.lexists(enlace): os.symlink(os.path.abspath(juego), enlace)
    return os.path.join(rdir, setname + ".zip"), sysdir


def correr(corto, juego, args, shot=None, ram=True, wav=None):
    ruta, sysdir = preparar(corto, juego)
    argv = [retro_run(), nucleo(corto), ruta, "--sys", sysdir, *NUCLEOS[corto][1], *args]
    if shot: argv += ["--shot", shot]
    if wav: argv += ["--wav", wav]
    if not ram: argv += ["--no-ram"]
    r = subprocess.run(argv, cwd=RAIZ, capture_output=True, text=True)
    if r.returncode != 0: sys.exit(f"{corto}: {r.stderr.strip()}")
    return r.stdout


def leer_ram(texto):
    """Los 64 KB de una salida como la de neoramdump (los núcleos pueden escribir otras cosas)"""
    v = [int(m.group(1), 16) for m in re.finditer(r"^10[0-9A-F]{4} ([0-9A-F]{2})$", texto, re.M)]
    if len(v) != 0x10000: sys.exit(f"la salida no trae los 64 KB de RAM ({len(v)} bytes)")
    return bytes(v)


def leer_png(path):
    """Pixeles de un PNG (8 bits RGB sin entrelazar, como los que escribe png.c), sin dependencias."""
    import zlib
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


def recorte(img, w, h):
    """El centro de w × h de una imagen (Geolith da 320 × 240 con los bordes; FBNeo 304 × 224)"""
    wi, hi, filas = img
    x0, y0 = (wi - w) // 2, (hi - h) // 2
    return [f[3 * x0:3 * (x0 + w)] for f in filas[y0:y0 + h]]


def parecido_img(a, b):
    """Parte de los pixeles iguales en el área común centrada (con tolerancia de 24 por canal: cada
    emulador convierte el color de 16 bits a 8 bits por canal a su manera)."""
    w, h = min(a[0], b[0]), min(a[1], b[1])
    fa, fb = recorte(a, w, h), recorte(b, w, h)
    iguales = 0
    for ra, rb in zip(fa, fb):
        for x in range(0, w * 3, 3):
            if abs(ra[x] - rb[x]) <= 24 and abs(ra[x + 1] - rb[x + 1]) <= 24 and abs(ra[x + 2] - rb[x + 2]) <= 24: iguales += 1
    return iguales / (w * h)


def parecido_ram(a, b):
    return sum(x == y for x, y in zip(a, b)) / len(a)


def compare(juego, args, png_dir=None):
    tmp = png_dir or tempfile.mkdtemp()
    os.makedirs(tmp, exist_ok=True)
    rams, imgs = {}, {}
    r = subprocess.run([propio("neoramdump"), juego, *args], capture_output=True, text=True, check=True)
    rams["neorecomp"] = leer_ram(r.stdout)
    shot = os.path.join(tmp, "neorecomp.png")
    subprocess.run([propio("neoshot"), juego, shot, *args], capture_output=True, check=True)
    imgs["neorecomp"] = leer_png(shot)
    for corto in NUCLEOS:
        shot = os.path.join(tmp, f"{corto}.png")
        rams[corto] = leer_ram(correr(corto, juego, args, shot=shot))
        imgs[corto] = leer_png(shot)
    nombres = list(rams)
    res = {}
    for i, a in enumerate(nombres):
        for b in nombres[i + 1:]:
            pi = parecido_img(imgs[a], imgs[b])
            res[f"{a}-{b}"] = {"ram": parecido_ram(rams[a], rams[b]), "img": pi}
            print(f"{a:>9} / {b:<8}  RAM {100 * res[f'{a}-{b}']['ram']:5.1f}%   imagen {100 * pi:5.1f}%")
    return res


# --- mix ---

PARTES = [("FM", "70"), ("SSG", "6F"), ("ADPCM-A", "5F"), ("ADPCM-B", "3F")]   # neorun --mute: lo demás callado


def leer_wav(path):
    """Muestras (izquierda + derecha) de un WAV estéreo de 16 bits y su frecuencia"""
    with open(path, "rb") as f: d = f.read()
    hz = struct.unpack("<I", d[24:28])[0]
    pos = d.find(b"data") + 8
    n = (len(d) - pos) // 4
    s = struct.unpack(f"<{2 * n}h", d[pos:pos + 4 * n])
    return [s[2 * i] + s[2 * i + 1] for i in range(n)], hz


def energia_por_tramo(muestras, hz, fps, cuadros, tramo):
    """Energía (suma de cuadrados) de cada tramo de `tramo` cuadros: así se alinean emuladores con otra
    frecuencia de cuadro o de sonido (la lógica del juego va por cuadros)"""
    por_cuadro = hz / fps
    out = []
    for t in range(0, cuadros - tramo + 1, tramo):
        a, b = int(t * por_cuadro), int((t + tramo) * por_cuadro)
        seg = muestras[a:b]
        out.append(sum(v * v for v in seg) / max(1, len(seg)))
    return out


def nnls(filas, y):
    """Mínimos cuadrados con coeficientes >= 0 (pocas columnas: se sacan las negativas y se repite)"""
    k = len(filas[0])
    activas = list(range(k))
    while True:
        m = len(activas)
        A = [[sum(f[activas[i]] * f[activas[j]] for f in filas) for j in range(m)] for i in range(m)]
        b = [sum(f[activas[i]] * yy for f, yy in zip(filas, y)) for i in range(m)]
        for c in range(m):                                  # Gauss con pivote
            p = max(range(c, m), key=lambda r: abs(A[r][c]))
            A[c], A[p], b[c], b[p] = A[p], A[c], b[p], b[c]
            if abs(A[c][c]) < 1e-30: A[c][c] = 1e-30
            for r in range(m):
                if r != c:
                    q = A[r][c] / A[c][c]
                    A[r] = [x - q * z for x, z in zip(A[r], A[c])]; b[r] -= q * b[c]
        x = [b[i] / A[i][i] for i in range(m)]
        if all(v >= 0 for v in x):
            out = [0.0] * k
            for i, j in enumerate(activas): out[j] = x[i]
            return out
        activas = [j for j, v in zip(activas, x) if v > 0]
        if not activas: return [0.0] * k


def mix(juego, args, tramo=6):
    cuadros = 600
    for i, a in enumerate(args):
        if a == "--frames" and i + 1 < len(args): cuadros = int(args[i + 1])
    tmp = tempfile.mkdtemp()
    partes = {}
    for nombre, mute in PARTES:
        w = os.path.join(tmp, f"neo_{mute}.wav")
        subprocess.run([propio("neorun"), juego, *args, "--wav", w, "--mute", mute], capture_output=True, check=True)
        s, hz = leer_wav(w)
        partes[nombre] = energia_por_tramo(s, hz, NEO_FPS, cuadros, tramo)
    nivel = {n: math.sqrt(sum(e) / len(e)) for n, e in partes.items()}
    print("NEORecomp por partes (nivel medio): " + ", ".join(f"{n} {nivel[n]:.0f}" for n, _ in PARTES))
    usadas = [n for n, _ in PARTES if nivel[n] > 1]
    for corto in NUCLEOS:
        w = os.path.join(tmp, f"{corto}.wav")
        correr(corto, juego, args, ram=False, wav=w)
        s, hz = leer_wav(w)
        y = energia_por_tramo(s, hz, NUCLEOS[corto][2], cuadros, tramo)
        n = min(len(y), *(len(partes[p]) for p in usadas))
        filas = [[partes[p][i] for p in usadas] for i in range(n)]
        coef = nnls(filas, y[:n])
        g = {p: math.sqrt(c) for p, c in zip(usadas, coef)}
        ajuste = [sum(c * f for c, f in zip(coef, fila)) for fila in filas]
        media = sum(y[:n]) / n
        r2 = 1 - sum((a - b) ** 2 for a, b in zip(y[:n], ajuste)) / max(1e-30, sum((a - media) ** 2 for a in y[:n]))
        ref = g.get("FM") or 1
        print(f"{corto:>8}: " + ", ".join(f"{p} ×{g[p]:.2f} ({20 * math.log10(g[p] / ref) if g[p] > 0 else -99:+.1f} dB)"
                                           for p in usadas) + f"   ajuste R² {r2:.3f}")
    print("(× = cuánto más fuerte suena cada parte en el oráculo que en NEORecomp; dB relativos a la FM)")


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"): print(__doc__); return 0
    cmd, a = sys.argv[1], sys.argv[2:]
    # Los núcleos usan la BIOS de SNK; las herramientas propias, la misma (si no, buscarían antes la propia)
    if cmd != "fetch" and not os.environ.get("NEO_BIOS"): os.environ["NEO_BIOS"] = bios_aes()
    if cmd == "fetch":
        for corto in (a or list(NUCLEOS)): print(nucleo(corto))
    elif cmd == "ramdump" and len(a) >= 2:
        ram = leer_ram(correr(a[0], a[1], a[2:]))
        sys.stdout.write("".join(f"{0x100000 | i:06X} {v:02X}\n" for i, v in enumerate(ram)))
    elif cmd == "shot" and len(a) >= 3:
        correr(a[0], a[1], a[3:], shot=os.path.abspath(a[2]), ram=False)
    elif cmd == "wav" and len(a) >= 3:
        sys.stdout.write(correr(a[0], a[1], a[3:], wav=os.path.abspath(a[2]), ram=False))
    elif cmd == "compare" and len(a) >= 1:
        png_dir = None
        if "--png" in a:
            i = a.index("--png"); png_dir = a[i + 1]; a = a[:i] + a[i + 2:]
        compare(a[0], a[1:], png_dir)
    elif cmd == "mix" and len(a) >= 1:
        mix(a[0], a[1:])
    else:
        print(__doc__); return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
