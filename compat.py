#!/usr/bin/env python3
"""Prueba de compatibilidad común de NESRecomp, GBRecomp, GENRecomp y NEORecomp con una colección de ROMs (solo
la biblioteca estándar de Python).

Cada recompilador la llama desde su tools/compat/run.py, que define lo propio de su consola (una subclase de
Sistema: leer la cabecera, la categoría de cada ROM, qué se soporta, los botones); el resto es igual en los tres
y usa solo la línea de comandos común (el contrato del README): *shot, *recomp y difftest.

Etapa 1 (todas las ROMs): el intérprete (*shot) con entradas guionadas, capturas a los cuadros 450, 900 y 1500.
  Detecta errores de carga, CPU detenido y pantallas en blanco; la captura más colorida va al informe.
Etapa 2 (una selección): el ciclo del recompilador sin traza: *recomp, compilar difftest, difftest con las semillas
  1 y 2 (entradas fijas más un mando aleatorio), una segunda ronda recompilando con lo que difftest --uncovered vio
  caer al intérprete y, al final y de una ROM a la vez, la velocidad (difftest --bench).

Uso, desde la raíz del recompilador y con "make" ya hecho:
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO]
                                [--juego TEXTO] [--categoria A,B] [--hilos N] [--salida DIR] [--rehacer] [--oraculos]
    python3 tools/compat/report.py [--salida DIR]

Las carpetas (por omisión roms/) pueden tener zips de No-Intro o ROMs sueltas. Los resultados se guardan en
SALIDA/results.json (por omisión docs/compat) después de cada ROM: si la corrida se interrumpe, la misma orden
sigue donde iba. Una ROM se salta si ya tiene resultado de la misma versión del recompilador (el commit y si
src/ tiene cambios); --rehacer la repite igual.
"""
import argparse, concurrent.futures as cf, datetime, html, json, os, pathlib, re, shlex, shutil, struct
import subprocess, sys, threading, time, zipfile, zlib

EXE = ".exe" if os.name == "nt" else ""
CUADROS1 = (450, 900, 1500)
CUADROS_BENCH = 1800
SEMILLAS = (1, 2)
ESTADOS = ["recompilado", "intérprete", "diferencia", "falla", "pantalla en blanco", "se cuelga", "no carga",
           "sin soporte"]


# --------------------------------------------------------------------------- lo que cambia entre consolas

class Sistema:
    """Lo propio de cada consola. Las subclases están en tools/compat/run.py de cada recompilador."""
    id = ""                          # "nes" / "gb" / "gen" / "neo": prefijo de las herramientas (nesshot, gbrecomp...)
    nombre = ""                      # "NESRecomp"
    consola = ""                     # para los textos del informe
    exts = ()                        # extensiones de ROM (dentro de los zips o sueltas)
    categoria_nombre = "Categoría"   # "Mapper", "MBC"...
    botones = ("A", "B")             # los dos botones de acción del guion
    cuadros_diff = 1800              # cuadros de difftest por semilla
    metrica = "ciclos"               # qué mide recompilado_pct de difftest: "ciclos" o "instrucciones"
    re_metrica = r"([\d.]+)% de los ciclos"
    compara = ""                     # qué compara difftest, para el informe
    escala_mini = 2                  # la miniatura es la captura dividida entre esto
    cupo = {}                        # categoría -> cuántas ROMs entran a la etapa 2 por omisión
    cupo_def = 3
    juego_zip = False                # el zip entero es el juego (Neo Geo: zips de MAME, muchos chips): no se extrae
    cuadros1 = CUADROS1              # capturas de la etapa 1

    def es_juego(self, p):
        """¿Este archivo de la carpeta es un juego? (no, p. ej., un zip de BIOS)"""
        return True

    def guiones(self):
        """(guion de la etapa 1, entradas fijas de difftest): por omisión los comunes"""
        return guiones(self)

    def __init__(self, root):
        self.root = pathlib.Path(root)

    def herramienta(self, n):
        return self.root / f"{self.id}{n}{EXE}"

    def coleccion(self, carpeta):
        return pathlib.Path(carpeta).name

    def leer(self, rom):
        """Datos de la cabecera de una ROM ya extraída; si no sirve, {"error": ...}."""
        raise NotImplementedError

    def soportado(self, r):
        return True

    def categoria(self, r):
        return ""

    def orden_categoria(self, c):
        return (0, c)

    def nombre_categoria(self, c):
        return c

    def otros_resumenes(self):
        """[(título, función fila -> grupo, orden, nombre)] además del de categoría."""
        return []

    def combinacion(self, r):
        """Para elegir ROMs variadas dentro de una categoría."""
        return (r.get("rom_kb"),)

    def tamano(self, r):
        return f'{r.get("rom_kb", "")}'

    def tamano_titulo(self):
        return "KB"

    def caracteristicas(self, r):
        return []

    def subtitulo(self, r):
        return ""

    def migrar(self, r):
        """Pasa una fila de un results.json viejo al formato común."""
        return r

    oraculos = None                  # función (fila, guion) -> dict, si la consola tiene emuladores de referencia

    def columnas_oraculos(self, r):
        return []


# --------------------------------------------------------------------------- utilidades

def correr(argv, cwd=None, timeout=900, env=None):
    """(código, salida, segundos), sin shell"""
    t0 = time.time()
    try:
        p = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, timeout=timeout, text=True,
                           encoding="utf-8", errors="replace")
        return p.returncode, p.stdout + p.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        out = e.stdout if isinstance(e.stdout, str) else (e.stdout or b"").decode("utf-8", "replace")
        return 124, out + "\n(tiempo agotado)", time.time() - t0
    except OSError as e:
        return 127, str(e), time.time() - t0


def msys():
    for c in (os.environ.get("MSYS2_ROOT"), r"C:\msys64"):
        if c and os.path.isfile(os.path.join(c, "usr", "bin", "bash.exe")):
            return c
    return None


def posix(p):
    return str(p).replace("\\", "/")


def make(carpeta, objetivo, hilos):
    """make del proyecto generado. En Windows, dentro de MSYS2 UCRT64 (como se compila a mano); su shell de login
    pierde OS=Windows_NT y sin eso el Makefile generado no sabe que está en Windows."""
    if os.name == "nt":
        raiz = msys()
        if not raiz:
            return 127, "no encontré MSYS2 (C:\\msys64 o MSYS2_ROOT)", 0
        env = dict(os.environ, MSYSTEM="UCRT64", CHERE_INVOKING="1")
        cmd = f"export OS=Windows_NT; exec make -s -j{hilos} -C {shlex.quote(posix(carpeta))} {shlex.quote(objetivo)}"
        return correr([os.path.join(raiz, "usr", "bin", "bash.exe"), "-lc", cmd], timeout=2400, env=env)
    return correr(["make", "-s", f"-j{hilos}", "-C", carpeta, objetivo], timeout=2400)


def utf8():
    """La consola de Windows no es UTF-8 por omisión y los nombres de juegos y categorías traen de todo"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def num(rx, texto, tipo=float):
    m = re.search(rx, texto)
    return tipo(m.group(1)) if m else None


def slug(fila):
    """Nombre corto y seguro para archivos; el CRC del nombre (con la colección) separa variantes y juegos con el
    mismo nombre en dos colecciones."""
    s = re.sub(r"[^a-z0-9]+", "", re.sub(r"\([^)]*\)", "", fila["juego"]).lower())[:18] or "rom"
    return "g" + s + format(zlib.crc32((fila.get("coleccion", "") + "/" + fila["archivo"]).encode()) & 0xFFFFFF, "06x")


def version(root):
    """El commit del recompilador y si src/ tiene cambios sin commitear (no cuenta docs/, que esta misma prueba
    escribe)."""
    rc, o, _ = correr(["git", "rev-parse", "--short", "HEAD"], cwd=root, timeout=30)
    v = o.strip() if rc == 0 else "?"
    rc, _, _ = correr(["git", "diff", "--quiet", "HEAD", "--", "src", "Makefile"], cwd=root, timeout=30)
    return v + ("+cambios" if rc == 1 else "")


# --------------------------------------------------------------------------- PNG (RGB de 8 bits, lo que escriben los *shot)

def leer_png(path):
    """(ancho, alto, filas de bytes RGB)"""
    d = pathlib.Path(path).read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("no es PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        c = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, prof, tipo = struct.unpack(">IIBB", c[:10])
            if prof != 8 or tipo not in (2, 6):
                raise ValueError("PNG que no es RGB de 8 bits")
            bpp = 3 if tipo == 2 else 4
        elif t == b"IDAT":
            idat += c
        pos += 12 + n
    raw, filas, prev, st = zlib.decompress(idat), [], bytearray(w * bpp), w * bpp
    for y in range(h):
        f, cur = raw[y * (st + 1)], bytearray(raw[y * (st + 1) + 1:(y + 1) * (st + 1)])
        for i in range(st):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                cur[i] = (cur[i] + a) & 255
            elif f == 2:
                cur[i] = (cur[i] + b) & 255
            elif f == 3:
                cur[i] = (cur[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = cur
        filas.append(bytes(cur) if bpp == 3 else bytes(b for k in range(w) for b in cur[k * 4:k * 4 + 3]))
    return w, h, filas


def escribir_png(path, w, h, filas):
    """Con paleta si caben en 256 colores (las miniaturas pesan mucho menos), si no RGB."""
    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    colores = {}
    for f in filas:
        for x in range(0, w * 3, 3):
            colores.setdefault(f[x:x + 3], len(colores))
    if len(colores) <= 256:
        cab = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
        raw = b"".join(b"\0" + bytes(colores[f[x:x + 3]] for x in range(0, w * 3, 3)) for f in filas)
        extra = ch(b"PLTE", b"".join(colores))
    else:
        cab = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
        raw = b"".join(b"\0" + f for f in filas)
        extra = b""
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", cab) + extra + ch(b"IDAT", zlib.compress(raw, 9)) +
                                   ch(b"IEND", b""))


def analizar(png):
    """(colores distintos, fracción del color dominante) de una captura"""
    w, h, filas = leer_png(png)
    cuenta = {}
    for f in filas:
        for x in range(0, w * 3, 3):
            k = f[x:x + 3]
            cuenta[k] = cuenta.get(k, 0) + 1
    return len(cuenta), round(max(cuenta.values()) / (w * h), 3)


def miniatura(png, destino, escala):
    w, h, filas = leer_png(png)
    nf = [bytes(b for x in range(0, w, escala) for b in f[x * 3:x * 3 + 3]) for f in filas[::escala]]
    destino.parent.mkdir(parents=True, exist_ok=True)
    escribir_png(destino, (w + escala - 1) // escala, len(nf), nf)


# --------------------------------------------------------------------------- entradas

def guion_texto(tramos, ultimo):
    """Tramos (botón, desde, hasta) -> archivo de entradas (cuadro BOTONES por cada cambio, el formato común)"""
    lineas, prev = [], None
    for f in range(ultimo + 1):
        pad = sorted({b for b, i, j in tramos if i <= f < j})
        txt = "+".join(pad) or "-"
        if txt != prev:
            lineas.append(f"{f} {txt}")
            prev = txt
    return "# generado por tools/retrorecomp/compat.py\n" + "\n".join(lineas) + "\n"


def guiones(S):
    """(guion de la etapa 1, entradas fijas de difftest). Pasan del logo y del título al juego y se mueven un poco;
    en difftest se suma el mando aleatorio de cada semilla."""
    p, s = S.botones
    t = [("START", 200, 206), ("START", 300, 306), ("START", 400, 406), (p, 500, 506), ("START", 600, 606),
         (s, 700, 706), ("START", 800, 806)]
    for k in range(900, 2101, 60):
        t += [(p, k, k + 26), (s, k + 30, k + 51)]
    t += [("RIGHT", 900, 1400), ("LEFT", 1400, 1700), ("RIGHT", 1700, 2301), ("UP", 1750, 1760), ("DOWN", 1950, 1960)]
    d = [("START", 150, 156), ("START", 350, 356), (p, 450, 456), ("START", 650, 656), ("RIGHT", 700, 1501),
         (p, 760, 801), (p, 1000, 1041), (s, 900, 1001), (p, 1200, 1221)]
    return guion_texto(t, 2400), guion_texto(d, 1500)


# --------------------------------------------------------------------------- colección

class Prueba:
    def __init__(self, S, salida):
        self.S = S
        self.work = S.root / "build" / "compat"
        self.out = pathlib.Path(salida)
        self.res_path = self.out / "results.json"
        self.lock = threading.Lock()
        self.version = version(S.root)
        self.res = {}
        if self.res_path.exists():
            datos = json.loads(self.res_path.read_text(encoding="utf-8"))
            for r in (datos["roms"] if isinstance(datos, dict) else datos):
                r = S.migrar(r)
                self.res[self.clave(r)] = r
        self.guion = self.work / "guion.txt"
        self.entradas_diff = self.work / "entradas_diff.txt"

    @staticmethod
    def clave(r):
        return (r.get("coleccion") or "") + "/" + r["archivo"]

    def fila(self, f):
        """La fila de resultados de una ROM (la crea, o adopta la de un results.json viejo sin colección)"""
        k = self.clave(f)
        if k not in self.res and "/" + f["archivo"] in self.res:
            self.res[k] = self.res.pop("/" + f["archivo"])
        r = self.res.setdefault(k, {})
        r.update({c: v for c, v in f.items() if c not in ("rom", "ruta", "entrada")})
        return r

    def guardar(self):
        with self.lock:
            self.out.mkdir(parents=True, exist_ok=True)
            datos = {"sistema": self.S.id, "fecha": datetime.date.today().isoformat(), "version": self.version,
                     "roms": sorted(self.res.values(), key=lambda r: (r["juego"].lower(), r.get("coleccion") or ""))}
            tmp = self.res_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(datos, ensure_ascii=False, indent=1), encoding="utf-8")
            os.replace(tmp, self.res_path)

    # ------------------------------------------------------------- escaneo

    def escanear(self, carpetas):
        """Extrae las ROMs (a build/compat/roms) y lee sus cabeceras; guarda lo leído en build/compat/scan.json"""
        cache_path = self.work / "scan.json"
        try:
            cache = json.loads(cache_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cache = {}
        trabajos = []
        for c in carpetas:
            col = self.S.coleccion(c)
            for p in sorted(pathlib.Path(c).resolve().iterdir()):     # absolutas: difftest corre en su carpeta
                if (p.suffix.lower() == ".zip" or p.suffix.lower() in self.S.exts) and self.S.es_juego(p):
                    trabajos.append((col, p))
        if not trabajos:
            sys.exit(f"no hay ROMs en {', '.join(map(str, carpetas))}")

        def uno(cp):
            col, p = cp
            st = p.stat()
            k = f"{p}|{st.st_size}|{int(st.st_mtime)}"
            f = cache.get(k)
            if f and (f.get("error") or pathlib.Path(f["rom"]).exists()):
                return k, f
            f = {"archivo": p.name, "juego": p.stem, "coleccion": col}
            f["slug"] = slug(f)
            try:
                if p.suffix.lower() == ".zip" and not self.S.juego_zip:
                    with zipfile.ZipFile(p) as z:
                        ent = [n for n in z.namelist() if n.lower().endswith(self.S.exts)]
                        if not ent:
                            return k, None
                        rom = self.work / "roms" / (f["slug"] + pathlib.Path(ent[0]).suffix.lower())
                        rom.parent.mkdir(parents=True, exist_ok=True)
                        rom.write_bytes(z.read(ent[0]))
                else:
                    rom = p
                f["rom"] = str(rom)
                f.update(self.S.leer(rom))
            except Exception as e:  # noqa: BLE001
                f["error"] = str(e)[:200]
            return k, f

        filas, nuevo = [], {}
        with cf.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            for k, f in ex.map(uno, trabajos):
                if f:
                    nuevo[k] = f
                    filas.append(f)
        self.work.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(nuevo, ensure_ascii=False, indent=1), encoding="utf-8")
        return filas

    # ------------------------------------------------------------- etapa 1

    def etapa1(self, f):
        S = self.S
        r = {"interprete": None, "detalle": None, "cuelgue": None, "thumb": None}
        if f.get("error"):
            r.update(interprete="error", detalle=f["error"])
            return r
        base = f["slug"]
        pngs = [self.work / "shots" / f"{base}_{n}.png" for n in S.cuadros1]
        pngs[0].parent.mkdir(parents=True, exist_ok=True)
        t0 = time.time()
        salida = ""
        for p, n in zip(pngs, S.cuadros1):
            p.unlink(missing_ok=True)
            rc, salida, _ = correr([S.herramienta("shot"), f["rom"], p, "--frames", n, "--input", self.guion],
                                   cwd=S.root, timeout=300)
            m = re.search(r"CPU detenido en el cuadro (\d+)(?: \(([^)]*)\))?", salida)
            if m:
                r["cuelgue"] = {"cuadro": int(m.group(1)), "motivo": m.group(2) or ""}
            if m or not p.exists():
                break
        r["interprete_s"] = round(time.time() - t0, 2)
        hechas = [p for p in pngs if p.exists()]
        if not hechas:
            r["interprete"] = "error"
            r["detalle"] = (salida.strip().splitlines() or ["sin salida"])[-1][:200]
            return r
        # Una captura aislada puede caer en una transición a negro: se queda la más colorida
        an = [analizar(p) for p in hechas]
        mejor = max(range(len(an)), key=lambda i: (an[i][0], -an[i][1]))
        colores, dom = an[mejor]
        thumb = self.out / "thumbs" / f"{base}.png"
        miniatura(hechas[mejor], thumb, S.escala_mini)
        r.update(colores=colores, dominante=dom, thumb=thumb.name, capturas=[a[0] for a in an])
        if r["cuelgue"]:
            r["interprete"] = "se cuelga"
            c = r["cuelgue"]
            r["detalle"] = f"CPU detenido en el cuadro {c['cuadro']}" + (f" ({c['motivo']})" if c["motivo"] else "")
        else:
            r["interprete"] = "pantalla en blanco" if dom > 0.99 or colores < 3 else "ok"
        return r

    # ------------------------------------------------------------- etapa 2

    def etapa2(self, f, hilos_make):
        S = self.S
        base = f["slug"]
        carpeta = self.work / "out" / base
        traza = self.work / "trazas" / f"{base}.txt"
        traza.parent.mkdir(parents=True, exist_ok=True)
        shutil.rmtree(carpeta, ignore_errors=True)
        carpeta.parent.mkdir(parents=True, exist_ok=True)   # nesrecomp solo crea el último nivel con rutas C:\...
        traza.write_text("")
        r = {"fases": [], "version": self.version}
        t0 = time.time()
        for ronda in (1, 2):
            fa = {"ronda": ronda}
            r["fases"].append(fa)
            argv = [S.herramienta("recomp"), f["rom"], "-o", carpeta, "--name", base]
            if ronda == 2:
                argv += ["--trace", traza]
            for _ in range(3):
                rc, o, dt = correr(argv, cwd=S.root)
                # En Windows un archivo recién borrado puede seguir «pendiente de borrar» un momento
                if rc == 0 or "no se pudo crear" not in o:
                    break
                time.sleep(2)
            fa["recomp_s"] = round(dt, 1)
            if rc != 0:
                r.update(resultado="falla", detalle=f"{S.id}recomp: " + o.strip()[-300:])
                break
            fa["instrucciones"] = num(r"código: (\d+) instrucciones", o, int)
            fa["pct_rom"] = num(r"bytes, ([\d.]+)% (?:de la ROM|del PRG|de P)", o)
            fa["funciones"] = num(r"(\d+) funciones", o, int)
            rc, o, dt = make(carpeta, f"difftest{EXE}", hilos_make)
            if rc != 0 or not (carpeta / f"difftest{EXE}").exists():
                # Con otro juego compilando al lado, gcc puede quedarse sin memoria en los archivos grandes: se
                # reintenta de a un archivo (make sigue con lo que ya compiló)
                fa["reintento"] = True
                rc, o, dt2 = make(carpeta, f"difftest{EXE}", 1)
                dt += dt2
            fa["compilacion_s"] = round(dt, 1)
            if rc != 0 or not (carpeta / f"difftest{EXE}").exists():
                # El mensaje de gcc, no la línea de make que solo dice "Error 1"
                lin = [l.strip() for l in o.splitlines() if l.strip() and not l.startswith("make")]
                err = [l for l in lin if re.search(r"error|fatal|memory|memoria|killed", l, re.I)]
                r.update(resultado="falla", detalle="compilación: " + " | ".join(err[:3] or lin[-3:] or [o.strip()[-300:]])[:400])
                break
            fa["semillas"] = []
            mal = False
            for s in SEMILLAS:
                unc = carpeta / f"unc_{s}.txt"
                rc, o, dt = correr([carpeta / f"difftest{EXE}", f["rom"], "--frames", S.cuadros_diff, "--input",
                                    self.entradas_diff, "--seed", s, "--uncovered", unc], cwd=carpeta, timeout=2400)
                d = {"semilla": s, "ok": rc == 0, "segundos": round(dt, 1)}      # 1 con diferencias, 2 con error
                d["recompilado_pct"] = num(S.re_metrica, o)
                d["interpretadas"] = num(r"interpretadas: (\d+)", o, int) or 0
                if not d["ok"]:
                    lin = [l.strip() for l in o.splitlines()
                           if re.search(r"diferencia|difiere|tras ejecutar|recompilado=", l, re.I)]
                    d["detalle"] = (" | ".join(lin) or o.strip()[-200:])[:400]
                    mal = "diferencia" if rc == 1 else "falla"     # difftest: 1 con diferencias; 2, 124... no corrió
                    if rc != 1:
                        d["detalle"] = f"difftest (código {rc}): " + d["detalle"]
                elif unc.exists():
                    with open(traza, "a") as tf:
                        tf.write(unc.read_text())
                fa["semillas"].append(d)
            if mal:
                r.update(resultado=mal, detalle=next(d["detalle"] for d in fa["semillas"] if not d["ok"]))
                break
            lineas = sorted(set(traza.read_text().split()))
            traza.write_text("\n".join(lineas) + ("\n" if lineas else ""))
            fa["traza"] = len(lineas)
            if not lineas:          # nada cayó al intérprete en ROM: la segunda ronda sería idéntica
                r["resultado"] = "ok"
                break
        else:
            r["resultado"] = "ok"
        r["total_s"] = round(time.time() - t0, 1)
        return r

    def velocidad(self, f, r):
        """difftest --bench del proyecto ya compilado. De una ROM a la vez: con otras compilando al lado no sirve."""
        carpeta = self.work / "out" / f["slug"]
        exe = carpeta / f"difftest{EXE}"
        if not exe.exists():
            return
        rc, o, _ = correr([exe, f["rom"], "--frames", CUADROS_BENCH, "--input", self.entradas_diff, "--bench"],
                          cwd=carpeta, timeout=1800)
        r["velocidad_x"] = num(r"recompilado\s+[\d.]+ s\s+\(([\d.]+)x", o)
        r["velocidad_interp_x"] = num(r"intérprete\s+[\d.]+ s\s+\(([\d.]+)x", o)
        if r.get("resultado") == "ok" and r["velocidad_x"] is not None:
            shutil.rmtree(carpeta, ignore_errors=True)       # los proyectos pesan; se conservan los que fallan

    # ------------------------------------------------------------- selección

    def elegir(self, filas, n):
        """Por categoría soportada: primero una ROM de cada combinación distinta (tamaño, batería...), las que
        corrieron bien en la etapa 1 antes. Sin n, el cupo de cada categoría; con n, por turnos entre categorías
        hasta juntar n (0: todas)."""
        S = self.S
        por = {}
        for f in filas:
            r = self.res.get(self.clave(f), {})
            if S.soportado(f) and not f.get("error") and r.get("interprete") not in ("error",):
                por.setdefault(S.categoria(f), []).append(f)
        colas = {}
        for c, lst in por.items():
            lst.sort(key=lambda f: (self.res.get(self.clave(f), {}).get("interprete") != "ok", S.combinacion(f), f["juego"]))
            vistos, prim, resto = set(), [], []
            for f in lst:
                k = S.combinacion(f)
                (resto if k in vistos else prim).append(f)
                vistos.add(k)
            colas[c] = prim + resto
        orden = sorted(colas, key=S.orden_categoria)
        if n is None:
            return [f for c in orden for f in colas[c][:S.cupo.get(c, S.cupo_def)]]
        if n == 0:
            return [f for c in orden for f in colas[c]]
        sel = []
        while len(sel) < n and any(colas.values()):
            for c in orden:
                if colas[c] and len(sel) < n:
                    sel.append(colas[c].pop(0))
        return sel


def leer_lista(path):
    """Una ROM por línea (el nombre del archivo o del juego; # comenta)"""
    return {l.strip() for l in pathlib.Path(path).read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.lstrip().startswith("#")}


def main(S, argv=None):
    ap = argparse.ArgumentParser(prog="tools/compat/run.py", description=f"Prueba de compatibilidad de {S.nombre}")
    ap.add_argument("carpetas", nargs="*", help="carpetas con zips o ROMs (por omisión roms/)")
    ap.add_argument("--solo", choices=("etapa1", "etapa2"))
    ap.add_argument("--etapa2", type=int, metavar="N",
                    help="cuántas ROMs pasan a la etapa 2, por turnos entre categorías (0: todas; sin esto, el cupo de cada categoría)")
    ap.add_argument("--lista", metavar="ARCHIVO", help="la etapa 2 con estas ROMs (una por línea) en vez de elegirlas")
    ap.add_argument("--juego", metavar="TEXTO", help="solo las ROMs cuyo nombre contiene esto (todas pasan a la etapa 2)")
    ap.add_argument("--categoria", metavar="A,B", help=f"solo estas categorías ({S.categoria_nombre.lower()})")
    ap.add_argument("--hilos", type=int, default=2, help="ROMs en paralelo en la etapa 2 (la etapa 1 usa todos los núcleos)")
    ap.add_argument("--salida", default=str(S.root / "docs" / "compat"), help="dónde van results.json y las miniaturas")
    ap.add_argument("--rehacer", action="store_true", help="repetir también las ROMs que ya tienen resultado de esta versión")
    ap.add_argument("--oraculos", action="store_true", help="comparar contra los emuladores de referencia (si la consola los tiene)")
    a = ap.parse_args(argv)
    utf8()
    if a.oraculos and not S.oraculos:
        sys.exit(f"{S.nombre} no tiene oráculos para --oraculos")
    P = Prueba(S, a.salida)
    guion, diff = S.guiones()
    P.work.mkdir(parents=True, exist_ok=True)
    P.guion.write_text(guion)
    P.entradas_diff.write_text(diff)
    t0 = time.time()
    filas = P.escanear(a.carpetas or [S.root / "roms"])
    if a.juego:
        filas = [f for f in filas if a.juego.lower() in f["juego"].lower()]
    if a.categoria:
        cats = {c.strip().lower() for c in a.categoria.split(",")}
        filas = [f for f in filas if str(S.categoria(f)).lower() in cats]
    cuenta = {}
    for f in filas:
        cuenta[S.categoria(f)] = cuenta.get(S.categoria(f), 0) + 1
    print(f"{S.nombre} {P.version}: {len(filas)} ROMs ({time.time() - t0:.0f} s leyendo cabeceras)", flush=True)
    for c in sorted(cuenta, key=S.orden_categoria):
        print(f"  {S.nombre_categoria(c):24} {cuenta[c]:5}", flush=True)
    for f in filas:
        P.fila(f)

    if a.solo != "etapa2":
        pend = [f for f in filas if a.rehacer or P.res[P.clave(f)].get("version1") != P.version
                or not P.res[P.clave(f)].get("interprete")]
        print(f"etapa 1: {len(pend)} ROMs ({len(filas) - len(pend)} ya tenían resultado de esta versión)", flush=True)
        t1 = time.time()
        with cf.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            for i, (f, r) in enumerate(zip(pend, ex.map(P.etapa1, pend)), 1):
                fila = P.res[P.clave(f)]
                fila.update({k: v for k, v in r.items() if v is not None})
                for k in ("detalle", "cuelgue"):
                    if r.get(k) is None:
                        fila.pop(k, None)
                fila["version1"] = P.version
                if i % 25 == 0 or i == len(pend):
                    P.guardar()
                    print(f"  {i}/{len(pend)} ({time.time() - t1:.0f} s)", flush=True)

    if a.solo != "etapa1":
        if a.lista:
            quiero = leer_lista(a.lista)
            sel = [f for f in filas if f["archivo"] in quiero or f["juego"] in quiero]
            faltan = quiero - {f["archivo"] for f in sel} - {f["juego"] for f in sel}
            for x in sorted(faltan):
                print(f"  (no está en la colección: {x})", flush=True)
        elif a.juego:
            sel = [f for f in filas if not f.get("error")]
        else:
            sel = P.elegir(filas, a.etapa2)
        pend = [f for f in sel if a.rehacer or (P.res[P.clave(f)].get("recompilador") or {}).get("version") != P.version]
        print(f"etapa 2: {len(sel)} ROMs, {len(pend)} por hacer ({len(sel) - len(pend)} ya tenían resultado de esta versión)",
              flush=True)
        hilos_make = max(2, (os.cpu_count() or 4) // max(1, a.hilos))
        t2 = time.time()

        def una(f):
            try:
                r = P.etapa2(f, hilos_make)
            except Exception as e:  # noqa: BLE001
                r = {"resultado": "falla", "detalle": f"compat.py: {e!r}"[:300], "fases": [], "version": P.version}
            return f, r
        hechas = 0
        with cf.ThreadPoolExecutor(max(1, a.hilos)) as ex:
            for f, r in ex.map(una, pend):
                P.res[P.clave(f)]["recompilador"] = r
                P.guardar()
                hechas += 1
                print(f"  [{hechas}/{len(pend)} {time.time() - t2:.0f} s] {str(S.categoria(f))[:8]:8} {f['juego'][:60]:60} "
                      f"{r.get('resultado')}", flush=True)
        medir = [f for f in sel if (P.res[P.clave(f)].get("recompilador") or {}).get("resultado") == "ok"
                 and P.res[P.clave(f)]["recompilador"].get("velocidad_x") is None]
        print(f"velocidad (difftest --bench, de una en una): {len(medir)} ROMs", flush=True)
        for f in medir:
            r = P.res[P.clave(f)]["recompilador"]
            P.velocidad(f, r)
            P.guardar()
            print(f"  {f['juego'][:60]:60} {r.get('velocidad_x')}x / {r.get('velocidad_interp_x')}x", flush=True)
        if a.oraculos:
            print(f"oráculos: {len(sel)} ROMs", flush=True)
            for f in sel:
                P.res[P.clave(f)]["oraculos"] = S.oraculos(f, P.guion)
                P.guardar()
    # Las miniaturas que ya no usa ninguna fila (de corridas viejas)
    usadas = {r.get("thumb") for r in P.res.values()}
    for p in (P.out / "thumbs").glob("*.png"):
        if p.name not in usadas:
            p.unlink()
    P.guardar()
    print(f"listo en {time.time() - t0:.0f} s; escrito {P.res_path}. Después: python3 tools/compat/report.py"
          + ("" if a.salida == str(S.root / "docs" / "compat") else f" --salida {a.salida}"), flush=True)


# --------------------------------------------------------------------------- informe

def estado(S, r):
    """Etiqueta global de una ROM, de mejor a peor"""
    if not S.soportado(r):
        return "sin soporte"
    i = r.get("interprete")
    if i == "error":
        return "no carga"
    rc = r.get("recompilador")
    if rc and rc.get("resultado"):
        return {"ok": "recompilado", "diferencia": "diferencia"}.get(rc["resultado"], "falla")
    return {"ok": "intérprete", "se cuelga": "se cuelga", None: "sin probar"}.get(i, "pantalla en blanco")


def e(s):
    return html.escape(str(s))


def informe(S, argv=None):
    ap = argparse.ArgumentParser(prog="tools/compat/report.py", description=f"Informe de compatibilidad de {S.nombre}")
    ap.add_argument("--salida", default=str(S.root / "docs" / "compat"))
    a = ap.parse_args(argv)
    utf8()
    D = pathlib.Path(a.salida)
    datos = json.loads((D / "results.json").read_text(encoding="utf-8"))
    rs = [S.migrar(r) for r in (datos["roms"] if isinstance(datos, dict) else datos)]
    meta = datos if isinstance(datos, dict) else {}
    for r in rs:
        r["estado"] = estado(S, r)
    rs.sort(key=lambda r: r["juego"].lower())
    ver = meta.get("version") or version(S.root)
    hoy = meta.get("fecha") or datetime.date.today().isoformat()
    tot = len(rs)
    sop = [r for r in rs if S.soportado(r)]
    corren = [r for r in sop if r.get("interprete") == "ok"]
    deep = [r for r in rs if (r.get("recompilador") or {}).get("resultado")]
    deep_ok = [r for r in deep if r["recompilador"]["resultado"] == "ok"]

    def peor(r):
        fa = (r.get("recompilador") or {}).get("fases") or []
        s = [d["recompilado_pct"] for d in (fa[-1].get("semillas", []) if fa else []) if d.get("recompilado_pct") is not None]
        return min(s) if s else None
    met = [peor(r) for r in deep_ok if peor(r) is not None]
    vel = sorted(r["recompilador"]["velocidad_x"] for r in deep_ok if r["recompilador"].get("velocidad_x"))
    vel_med = f"{vel[len(vel) // 2]:.0f}×" if vel else "—"
    colecciones = sorted({r.get("coleccion") or "" for r in rs} - {""})

    def resumen(clave, orden, nombre):
        grupos = {}
        for r in rs:
            g = grupos.setdefault(clave(r), {"n": 0, "ok": 0, "blanco": 0, "cuelga": 0, "carga": 0, "e2": 0, "e2ok": 0,
                                              "sop": False})
            g["n"] += 1
            g["sop"] |= S.soportado(r)
            i = r.get("interprete")
            g["ok"] += i == "ok"
            g["blanco"] += i == "pantalla en blanco"
            g["cuelga"] += i == "se cuelga"
            g["carga"] += i == "error"
            if (r.get("recompilador") or {}).get("resultado"):
                g["e2"] += 1
                g["e2ok"] += r["recompilador"]["resultado"] == "ok"
        filas = []
        for k in sorted(grupos, key=orden):
            g = grupos[k]
            pct = 100 * g["ok"] / g["n"]
            cls = "none" if not g["sop"] else ("ok" if pct >= 95 else "mid" if pct >= 80 else "bad")
            e2 = f'{g["e2ok"]} / {g["e2"]}' if g["e2"] else "—"
            filas.append(f'<tr class="{cls}"><td>{e(nombre(k))}</td><td class="n">{g["n"]}</td><td>{"sí" if g["sop"] else "no"}</td>'
                         f'<td class="n pct">{g["ok"]} <span class="mut">({pct:.0f}%)</span></td><td class="n">{g["blanco"] or ""}</td>'
                         f'<td class="n">{g["cuelga"] or ""}</td><td class="n">{g["carga"] or ""}</td><td class="n">{e2}</td></tr>')
        return "".join(filas)

    cab = ('<thead><tr><th>{}</th><th class="n">ROMs</th><th>Soportado</th><th class="n">Corren (etapa 1)</th>'
           '<th class="n">En blanco</th><th class="n">Se cuelgan</th><th class="n">No cargan</th>'
           '<th class="n">Recompiladas ok (etapa 2)</th></tr></thead>')
    tablas = [(f"Por {S.categoria_nombre.lower()}", S.categoria_nombre,
               resumen(S.categoria, S.orden_categoria, S.nombre_categoria))]
    for titulo, clave, orden, nombre in S.otros_resumenes():
        tablas.append((f"Por {titulo.lower()}", titulo, resumen(clave, orden, nombre)))
    html_tablas = "".join(f'<h2>{e(t)}</h2><div class="wrap"><table>{cab.format(e(c))}<tbody>{b}</tbody></table></div>'
                          for t, c, b in tablas)

    hay_oraculos = any(r.get("oraculos") for r in rs)
    filas_j = []
    for r in rs:
        rc = r.get("recompilador") or {}
        cob = pct = velo = ""
        if rc.get("fases"):
            fa = rc["fases"][-1]
            if fa.get("pct_rom") is not None:
                cob = f'{fa["pct_rom"]:.1f}% · {fa.get("funciones") or 0} fn'
            p = peor(r)
            if p is not None:
                pct = f"{p:.1f}%"
            if rc.get("velocidad_x"):
                velo = f'{rc["velocidad_x"]:.0f}× <span class="mut">/ {rc.get("velocidad_interp_x") or 0:.0f}×</span>'
        det = rc.get("detalle") or r.get("detalle") or ""
        if not S.soportado(r) and r.get("interprete"):
            det = f'intérprete: {r["interprete"]}' + (f" · {det}" if det else "")
        img = f'<img loading="lazy" src="thumbs/{e(r["thumb"])}" alt="">' if r.get("thumb") else ""
        sub = S.subtitulo(r)
        ora = "".join(f"<td class=\"n\">{c}</td>" for c in S.columnas_oraculos(r)) if hay_oraculos else ""
        filas_j.append(
            f'<tr data-estado="{e(r["estado"])}" data-cat="{e(S.categoria(r))}">'
            f'<td>{img}</td><td>{e(r["juego"])}' + (f'<div class="mut sm">{e(sub)}</div>' if sub else "") + '</td>'
            f'<td>{e(S.nombre_categoria(S.categoria(r)))}</td><td class="n">{e(S.tamano(r))}</td>'
            f'<td class="sm">{e(", ".join(S.caracteristicas(r)))}</td>'
            f'<td><span class="b {e(r["estado"].replace(" ", "-"))}">{e(r["estado"])}</span></td>'
            f'<td class="n sm">{cob}</td><td class="n">{pct}</td><td class="n">{velo}</td>{ora}<td class="det">{e(det)}</td></tr>')

    cuenta = {}
    for r in rs:
        cuenta[r["estado"]] = cuenta.get(r["estado"], 0) + 1
    chips = "".join(f'<button class="chip" data-f="{e(k)}"><span class="b {e(k.replace(" ", "-"))}">{e(k)}</span> <b>{cuenta[k]}</b></button>'
                    for k in ESTADOS + ["sin probar"] if k in cuenta)
    cats = sorted({S.categoria(r) for r in rs}, key=S.orden_categoria)
    opciones = "".join(f'<option value="{e(c)}">{e(S.nombre_categoria(c))}</option>' for c in cats)
    met_nombre = "Ciclos recompilados" if S.metrica == "ciclos" else "Instrucciones recompiladas"
    cab_ora = ("<th class=\"n\">Genesis Plus GX<br><span class=\"mut\">RAM / imagen %</span></th>"
               "<th class=\"n\">BlastEm<br><span class=\"mut\">RAM / imagen %</span></th>") if hay_oraculos else ""
    origen = (f"de {', '.join(colecciones)}" if colecciones else "")
    doc = f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Compatibilidad {e(S.nombre)}</title>
<style>
:root{{--bg:#fff;--fg:#1c1e21;--mut:#666;--line:#ddd;--card:#f6f7f9;--ok:#1a7f37;--mid:#9a6700;--bad:#cf222e;--none:#6e7781}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149;--none:#8b949e}}}}
:root[data-theme="dark"]{{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149;--none:#8b949e}}
body{{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1280px;margin:auto}} h1{{margin:0 0 4px}} h2{{margin-top:36px}} code{{font-size:13px}}
.mut{{color:var(--mut)}} .sm{{font-size:12px}} .n{{text-align:right;font-variant-numeric:tabular-nums}}
.cards{{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px;min-width:150px}}
.card b{{display:block;font-size:26px}}
table{{border-collapse:collapse;width:100%}} th,td{{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}}
th{{position:sticky;top:0;background:var(--bg);font-size:13px;z-index:1}} td.det{{font-size:12px;color:var(--mut);max-width:280px;overflow-wrap:anywhere}}
.wrap{{overflow-x:auto}} img{{image-rendering:pixelated;border-radius:3px;display:block;height:64px;width:auto}}
tr.ok .pct{{color:var(--ok);font-weight:600}} tr.mid .pct{{color:var(--mid);font-weight:600}} tr.bad .pct{{color:var(--bad);font-weight:600}} tr.none td{{color:var(--none)}}
.b{{padding:2px 8px;border-radius:10px;font-size:12px;border:1px solid currentColor;white-space:nowrap}}
.recompilado{{color:var(--ok)}} .intérprete{{color:var(--mid)}} .sin-soporte,.sin-probar{{color:var(--none)}}
.diferencia,.falla,.no-carga,.pantalla-en-blanco,.se-cuelga{{color:var(--bad)}}
.chip{{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:14px;padding:4px 10px;margin:0 6px 6px 0;cursor:pointer;font:inherit}}
.chip.on{{outline:2px solid var(--fg)}} .ctl{{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:8px}}
input,select{{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);font:inherit}}
input{{width:260px;max-width:100%}}
</style></head><body><main>
<h1>Compatibilidad de {e(S.nombre)}</h1>
<div class="mut">{tot} ROMs {e(origen)} · versión {e(ver)} · {e(hoy)}</div>
<div class="cards">
<div class="card"><b>{tot}</b>ROMs probadas</div>
<div class="card"><b>{len(sop)}</b>con {e(S.categoria_nombre.lower())} soportado</div>
<div class="card"><b>{len(corren)}</b>corren en el intérprete</div>
<div class="card"><b>{len(deep_ok)} / {len(deep)}</b>recompiladas sin diferencias</div>
<div class="card"><b>{(sum(met) / len(met)) if met else 0:.1f}%</b>{e(met_nombre.lower())} (media)</div>
<div class="card"><b>{vel_med}</b>velocidad mediana recompilado</div>
</div>
<h2>Cómo se probó</h2>
<p><b>Etapa 1, todas las ROMs:</b> cada ROM corre en el intérprete (<code>{e(S.id)}shot</code>) con entradas guionadas (Start, botones,
flechas) y se toman capturas en los cuadros {", ".join(map(str, S.cuadros1))}; la miniatura es la más colorida. «Corre» significa que cargó,
el CPU no se detuvo y la captura no es de un solo color; no garantiza que el juego se vea bien: las miniaturas hay que revisarlas a ojo.</p>
<p><b>Etapa 2, una selección por {e(S.categoria_nombre.lower())}:</b> <code>{e(S.id)}recomp</code> sin traza (solo análisis estático),
compilar el proyecto generado y correr <code>difftest</code> con las semillas {" y ".join(map(str, SEMILLAS))} ({S.cuadros_diff} cuadros,
entradas fijas más un mando aleatorio), que compara {e(S.compara)} contra el intérprete. Si algo cayó al intérprete en ROM, se recompila
con esas direcciones (<code>--uncovered</code> → <code>--trace</code>) y se repite. «Recompilado» = sin diferencias en todas las rondas.
«{e(met_nombre)}» es el peor caso de las semillas de la última ronda (lo que falta suele ser código en RAM). La velocidad es la de
<code>difftest --bench</code> ({CUADROS_BENCH} cuadros, recompilado / intérprete, sin ventana ni audio).</p>
<p class="mut">Para repetirlo: <code>python3 tools/compat/run.py CARPETA</code> y <code>python3 tools/compat/report.py</code>
(el motor es común a los tres recompiladores: <code>tools/retrorecomp/compat.py</code>).</p>
{html_tablas}
<h2>Por juego</h2>
<div class="ctl">{chips}</div>
<div class="ctl"><input id="q" placeholder="Buscar juego…" aria-label="Buscar juego">
<select id="m" aria-label="{e(S.categoria_nombre)}"><option value="">{e(S.categoria_nombre)}: todos</option>{opciones}</select>
<span class="mut" id="cnt"></span></div>
<div class="wrap"><table id="t"><thead><tr><th></th><th>Juego</th><th>{e(S.categoria_nombre)}</th><th class="n">{e(S.tamano_titulo())}</th>
<th>Características</th><th>Estado</th><th class="n">Código descubierto</th><th class="n">{e(met_nombre)}</th><th class="n">Velocidad</th>{cab_ora}<th>Detalle</th></tr></thead>
<tbody>{"".join(filas_j)}</tbody></table></div>
<script>
let f=null;const rows=[...document.querySelectorAll('#t tbody tr')],q=document.getElementById('q'),m=document.getElementById('m'),cnt=document.getElementById('cnt');
function ap(){{const s=q.value.toLowerCase();let n=0;rows.forEach(r=>{{r.hidden=(f&&r.dataset.estado!==f)||(m.value&&r.dataset.cat!==m.value)||(s&&!r.textContent.toLowerCase().includes(s));n+=!r.hidden}});cnt.textContent=n+' de '+rows.length}}
document.querySelectorAll('.chip').forEach(c=>c.onclick=()=>{{f=f===c.dataset.f?null:c.dataset.f;document.querySelectorAll('.chip').forEach(x=>x.classList.toggle('on',x.dataset.f===f));ap()}});
q.oninput=ap;m.onchange=ap;ap();
</script></main></body></html>
"""
    (D / "index.html").write_text(doc, encoding="utf-8")
    print(f"escrito {D / 'index.html'} ({tot} ROMs; {len(deep_ok)}/{len(deep)} recompiladas sin diferencias)")
