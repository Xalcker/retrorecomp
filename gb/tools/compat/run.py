"""Prueba de compatibilidad de GBRecomp con una colección de ROMs (zips de No-Intro).

Etapa 1 (todas las ROMs): corre la ROM en el intérprete con gbtest, con entradas guionadas, y guarda tres
  capturas (cuadros 450, 900 y 1500). Detecta errores de carga, cuelgues (opcode ilegal) y pantallas en blanco.
Etapa 2 (una selección por MBC y consola): ciclo completo del recompilador: gbrecomp, compilar el proyecto
  generado, difftest con dos semillas, una segunda ronda recompilando con lo que cayó al intérprete
  (--uncovered) y la velocidad (difftest --bench).

Uso (en la raíz del repo, con todo compilado con "make"; primero tools/compat/scan.py):
    uv run tools/compat/run.py [--solo etapa1|etapa2] [--hilos N] [--reusar] [--juego TEXTO]
Escribe docs/compat/results.json y las capturas en docs/compat/thumbs/. Después: uv run tools/compat/report.py
Las ROMs se extraen a build/compat/roms y los proyectos a build/compat/out (no se versionan).
"""
# /// script
# dependencies = ["pillow"]
# ///
import argparse, concurrent.futures as cf, hashlib, json, os, pathlib, re, shutil, subprocess, time, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORK = ROOT / "build" / "compat"
OUT = ROOT / "docs" / "compat"
EXE = ".exe" if os.name == "nt" else ""
MSYS = pathlib.Path(r"C:\msys64")
REUSAR = False
CUADROS1 = (450, 900, 1500)
CUADROS_DIFF = 3600          # un minuto de juego por semilla
CUADROS_BENCH = 1800

# Cuántas ROMs de cada (MBC, modelo) entran a la etapa 2 (las demás combinaciones, 3)
CUPO = {("MBC1", "dmg"): 6, ("MBC1", "sgb"): 4, ("MBC5", "cgb"): 8, ("MBC5", "sgb"): 3, ("ROM", "dmg"): 3}


def guion():
    """Entradas guionadas (formato de src/platform/inputs.h): pasan del título al juego y se mueven un poco."""
    A, B, SELECT, START, RIGHT, LEFT, UP, DOWN = 1, 2, 4, 8, 16, 32, 64, 128
    tramos = [(START, 120, 126), (START, 240, 246), (A, 360, 366), (START, 480, 486), (A, 600, 606),
              (START, 720, 726), (A, 780, 786)]
    for t in range(840, 2101, 60):
        tramos += [(A, t, t + 25), (B, t + 30, t + 50)]
    tramos += [(RIGHT, 840, 1300), (LEFT, 1300, 1500), (RIGHT, 1500, 2400), (UP, 1700, 1710), (DOWN, 1900, 1910)]
    nombres = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN"]
    lineas, prev = [], None
    for f in range(0, 2401):
        pad = 0
        for b, i, j in tramos:
            if i <= f < j:
                pad |= b
        if pad != prev:
            lineas.append(f"{f} " + ("+".join(n for k, n in enumerate(nombres) if pad >> k & 1) or "-"))
            prev = pad
    return "# guion de tools/compat/run.py\n" + "\n".join(lineas) + "\n"


def entorno():
    env = dict(os.environ, MSYSTEM="UCRT64", CHERE_INVOKING="1")
    if os.name == "nt":
        env["PATH"] = str(MSYS / "ucrt64" / "bin") + os.pathsep + str(MSYS / "usr" / "bin") + os.pathsep + env["PATH"]
    return env


def corre(args, cwd=ROOT, timeout=900):
    """Corre un ejecutable sin shell: (código, salida, segundos)."""
    t0 = time.time()
    try:
        p = subprocess.run([str(a) for a in args], cwd=cwd, env=entorno(), capture_output=True, timeout=timeout,
                           text=True, encoding="utf-8", errors="replace")
        return p.returncode, p.stdout + p.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        out = e.stdout if isinstance(e.stdout, str) else (e.stdout or b"").decode("utf-8", "replace")
        return 124, out + "\n(tiempo agotado)", time.time() - t0


def make(carpeta, objetivo, hilos):
    """make en una shell de login de MSYS2 UCRT64 (gcc y el Makefile generado lo necesitan en Windows)."""
    if os.name == "nt":
        cmd = [str(MSYS / "usr" / "bin" / "bash.exe"), "-lc", f'make -s -j{hilos} -C "{carpeta.as_posix()}" CC=gcc {objetivo}']
    else:
        cmd = ["make", "-s", f"-j{hilos}", "-C", str(carpeta), objetivo]
    return corre(cmd, timeout=2400)


def slug(fila):
    """Nombre corto y seguro para archivos. Hay juegos con el mismo nombre en las dos colecciones (Paperboy,
    Road Rash...): los de Game Boy Color empiezan con "c"."""
    nombre = fila["juego"]
    s = re.sub(r"[^a-z0-9]+", "", re.sub(r"\([^)]*\)", "", nombre).lower())[:24] or "rom"
    return ("c" if fila.get("coleccion") == "GBC" else "g") + s + hashlib.md5(nombre.encode()).hexdigest()[:4]


def extraer(fila):
    ext = ".gbc" if fila["entry"].lower().endswith(".gbc") else ".gb"
    dst = WORK / "roms" / (slug(fila) + ext)
    if not dst.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(fila["zip"]) as z:
            dst.write_bytes(z.read(fila["entry"]))
    return dst


def analizar(png):
    """(colores distintos, fracción del color dominante) de una captura."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    colores = im.getcolors(maxcolors=1 << 16) or []
    return len(colores), round(max(c for c, _ in colores) / (im.width * im.height), 3) if colores else 1.0


def miniatura(png, destino):
    from PIL import Image
    destino.parent.mkdir(parents=True, exist_ok=True)
    Image.open(png).convert("RGB").quantize(32).save(destino, optimize=True)


def num(rx, texto, tipo=float):
    m = re.search(rx, texto)
    return tipo(m.group(1)) if m else None


def etapa1(fila):
    rom = extraer(fila)
    base = slug(fila)
    r = {"slug": base}
    pngs = [WORK / "shots" / f"{base}_{n}.png" for n in CUADROS1]
    pngs[0].parent.mkdir(parents=True, exist_ok=True)
    gtest = ROOT / f"gbtest{EXE}"
    t0 = time.time()
    salidas = []
    for p, n in zip(pngs, CUADROS1):
        if REUSAR and p.exists():
            continue
        p.unlink(missing_ok=True)
        rc, o, _ = corre([gtest, rom, "--seconds", f"{n / 59.7275 + 0.001:.4f}", "--input", WORK / "guion.txt",
                          "--dump", p], timeout=300)
        salidas.append(o)
        if "opcode ilegal" in o:
            r["cuelgue"] = "opcode ilegal"
            r["cuelgue_antes_de"] = n
            break
        if not p.exists():
            break
    r["interprete_s"] = round(time.time() - t0, 2)
    hechas = [p for p in pngs if p.exists()]
    if not hechas:
        r["interprete"] = "error"
        r["detalle"] = ((salidas[-1] if salidas else "").strip().splitlines() or ["sin salida"])[-1][:200]
        return r
    # Una captura aislada puede caer en una transición a negro: se queda la más colorida
    an = [analizar(p) for p in hechas]
    mejor = max(range(len(an)), key=lambda i: (an[i][0], -an[i][1]))
    colores, dom = an[mejor]
    thumb = OUT / "thumbs" / f"{base}.png"
    miniatura(hechas[mejor], thumb)
    r.update(colores=colores, dominante=dom, thumb=thumb.name, capturas=[a[0] for a in an])
    if r.get("cuelgue"):
        r["interprete"] = "se cuelga"
        r["detalle"] = f"opcode ilegal antes del cuadro {r['cuelgue_antes_de']}"
    else:
        r["interprete"] = "pantalla en blanco" if dom > 0.995 or colores < 2 else "ok"
        r.pop("detalle", None)
    return r


def etapa2(fila, hilos_make):
    rom = extraer(fila)
    nombre = slug(fila)
    carpeta = WORK / "out" / nombre
    traza = WORK / f"{nombre}.trace"
    shutil.rmtree(carpeta, ignore_errors=True)
    traza.write_text("")
    r = {"fases": []}
    t0 = time.time()
    for ronda in (1, 2):
        f = {"ronda": ronda}
        r["fases"].append(f)
        rc, o, dt = corre([ROOT / f"gbrecomp{EXE}", rom, "-o", carpeta, "--name", nombre, "--trace", traza])
        f["gbrecomp_s"] = round(dt, 1)
        if rc != 0:
            r.update(resultado="falla", detalle="gbrecomp: " + o.strip()[-300:])
            return r
        f["instrucciones"] = num(r"código: (\d+) instrucciones", o, int)
        f["pct_rom"] = num(r"bytes, ([\d.]+)% de la ROM", o)
        f["funciones"] = num(r"en (\d+) funciones", o, int)
        rc, o, dt = make(carpeta, f"difftest{EXE}", hilos_make)
        f["compilacion_s"] = round(dt, 1)
        if rc != 0 or not (carpeta / f"difftest{EXE}").exists():
            err = [l for l in o.splitlines() if "error" in l.lower()]
            r.update(resultado="falla", detalle="compilación: " + (err[0] if err else o.strip()[-300:])[:300])
            return r
        f["semillas"] = []
        mal = False
        for s in (1, 2):
            unc = carpeta / f"unc_{s}.txt"
            rc, o, dt = corre([carpeta / f"difftest{EXE}", rom, "--frames", CUADROS_DIFF, "--seed", s,
                               "--uncovered", unc], cwd=carpeta, timeout=2400)
            d = {"semilla": s, "ok": rc == 0 and "Sin diferencias" in o, "segundos": round(dt, 1)}
            d["recompilado_pct"] = num(r"\(([\d.]+)% recompilado\)", o)
            d["interpretadas"] = num(r"Direcciones de ROM interpretadas: (\d+)", o, int) or 0
            if not d["ok"]:
                lin = [l.strip() for l in o.splitlines() if re.search(r"diferencia|tras ejecutar|recompilado=", l, re.I)]
                d["detalle"] = (" | ".join(lin) or o.strip()[-200:])[:400]
                mal = True
            elif unc.exists():
                with open(traza, "a") as tf:
                    tf.write(unc.read_text())
            f["semillas"].append(d)
        if mal:
            r.update(resultado="diferencia", detalle=next(d["detalle"] for d in f["semillas"] if not d["ok"]))
            break
        lineas = sorted(set(traza.read_text().split()))
        traza.write_text("\n".join(lineas) + ("\n" if lineas else ""))
        f["traza"] = len(lineas)
        if not lineas:          # nada cayó al intérprete en ROM: la segunda ronda sería idéntica
            r["resultado"] = "ok"
            break
    else:
        r["resultado"] = "ok"
    r["total_s"] = round(time.time() - t0, 1)
    return r


def velocidad(fila, r):
    """difftest --bench del proyecto ya compilado. Se corre al final y de a uno: con otros juegos compilando
    o corriendo en paralelo la medida no sirve."""
    rom = extraer(fila)
    carpeta = WORK / "out" / slug(fila)
    if not (carpeta / f"difftest{EXE}").exists():
        return
    rc, o, _ = corre([carpeta / f"difftest{EXE}", rom, "--frames", CUADROS_BENCH, "--bench"], cwd=carpeta)
    r["velocidad_x"] = num(r"recompilado\s+[\d.]+ s\s+\((\d+)x", o, int)
    r["velocidad_interp_x"] = num(r"intérprete\s+[\d.]+ s\s+\((\d+)x", o, int)
    if r["resultado"] == "ok":
        shutil.rmtree(carpeta, ignore_errors=True)      # los proyectos pesan; se conservan los que fallan


def elegir(filas, prev):
    """Por (MBC, modelo) soportado: un cupo de ROMs, variando tamaño de ROM/RAM y batería; primero las que
    corrieron bien en la etapa 1."""
    por = {}
    for f in filas:
        if f.get("soportado"):
            por.setdefault((f["mbc"], f["modelo"]), []).append(f)
    elegidas = []
    for k, lst in sorted(por.items()):
        lst.sort(key=lambda f: (prev.get(f["zip"], {}).get("interprete") != "ok", f["rom_kb"], f["ram_kb"], f["juego"]))
        n = CUPO.get(k, 3)
        vistos, sel = set(), []
        for f in lst:
            c = (f["rom_kb"], f["ram_kb"], f["bateria"], f["cgb"])
            if c not in vistos and len(sel) < n:
                vistos.add(c)
                sel.append(f)
        for f in lst:
            if len(sel) >= n:
                break
            if f not in sel:
                sel.append(f)
        elegidas += sel
    return elegidas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hilos", type=int, default=3, help="juegos en paralelo en la etapa 2")
    ap.add_argument("--solo", choices=["etapa1", "etapa2"])
    ap.add_argument("--reusar", action="store_true", help="no vuelve a correr gbtest si la captura ya existe")
    ap.add_argument("--juego", help="solo los juegos cuyo nombre contiene este texto (la etapa 2 los toma a todos)")
    a = ap.parse_args()
    global REUSAR
    REUSAR = a.reusar
    scan = json.load(open(WORK / "scan.json", encoding="utf-8"))
    filas = [f for f in scan if "mbc" in f]
    if a.juego:
        filas = [f for f in filas if a.juego.lower() in f["juego"].lower()]
    (WORK / "guion.txt").write_text(guion())
    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    res_path = OUT / "results.json"
    prev = {r["zip"]: r for r in json.load(open(res_path, encoding="utf-8"))} if res_path.exists() else {}

    def guardar():
        json.dump(sorted(prev.values(), key=lambda r: r["juego"]), open(res_path, "w", encoding="utf-8"),
                  indent=1, ensure_ascii=False)

    for f in filas:         # la cabecera siempre viene del escaneo más reciente
        r = prev.setdefault(f["zip"], {})
        r.update({k: v for k, v in f.items() if k != "entry"})
    t0 = time.time()
    if a.solo != "etapa2":
        with cf.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            for i, (f, r) in enumerate(zip(filas, ex.map(etapa1, filas)), 1):
                prev[f["zip"]].update(r)
                if i % 50 == 0 or i == len(filas):
                    print(f"etapa 1: {i}/{len(filas)} ({time.time() - t0:.0f} s)", flush=True)
        guardar()
    if a.solo != "etapa1":
        sel = filas if a.juego else elegir(filas, prev)
        print(f"etapa 2: {len(sel)} ROMs", flush=True)
        hilos_make = max(2, (os.cpu_count() or 4) // a.hilos)

        def una(f):
            try:
                r = etapa2(f, hilos_make)
            except Exception as e:
                r = {"resultado": "falla", "detalle": f"run.py: {e!r}"[:300], "fases": []}
            print(f"  {f['mbc']:5} {f['modelo']} {f['juego'][:60]:60} {r.get('resultado')}", flush=True)
            return f["zip"], r
        with cf.ThreadPoolExecutor(a.hilos) as ex:
            for z, r in ex.map(una, sel):
                prev[z]["recompilador"] = r
                guardar()
        print("velocidad (de a uno)", flush=True)
        for f in sel:
            r = prev[f["zip"]]["recompilador"]
            velocidad(f, r)
            print(f"  {f['juego'][:60]:60} {r.get('velocidad_x')}x / {r.get('velocidad_interp_x')}x", flush=True)
            guardar()
    print(f"listo en {time.time() - t0:.0f} s")


main()
