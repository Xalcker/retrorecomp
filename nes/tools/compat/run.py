"""Prueba de compatibilidad de NESRecomp con una colección de ROMs.

Etapa 1 (todas las ROMs): carga la ROM en el intérprete y la corre 900 cuadros con entradas guionadas,
  guardando una captura (nesshot). Detecta mappers no soportados, errores de carga y pantallas en blanco.
Etapa 2 (una selección por mapper): ciclo completo del recompilador: traza guionada, nesrecomp, compilar el
  proyecto generado, difftest con semillas (dos rondas, la segunda con lo que cayó al intérprete) y bench.

Uso (desde MSYS2 UCRT64, en la raíz del repo, con todo ya compilado con "make"):
    uv run tools/compat/run.py DIRECTORIO_ZIPS [--por-mapper N] [--hilos N] [--solo etapa1]
Escribe docs/compat/results.json y las capturas en docs/compat/thumbs/. Después: uv run tools/compat/report.py
Las ROMs se extraen a build/compat/roms (no se versionan).
"""
# /// script
# dependencies = ["pillow"]
# ///
import argparse, zlib, concurrent.futures as cf, json, os, pathlib, re, shutil, subprocess, sys, time, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORK = ROOT / "build" / "compat"
OUT = ROOT / "docs" / "compat"
SUPPORTED = {0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 18, 19, 21, 22, 23, 24, 25, 26, 34, 66, 69, 71, 75, 79, 85, 87, 95, 115, 118, 119, 140, 180, 184, 206, 210}
# Cuántas ROMs de cada mapper entran a la etapa 2 (los demás, 3 si no se indica)
CUPO = {0: 4, 1: 6, 2: 4, 3: 4, 4: 8, 5: 6, 7: 4}
REUSAR = False
EXE = ".exe" if os.name == "nt" else ""

# Entradas guionadas: pasan del título al juego y se mueven un poco (las mismas de tests/recomp_game.sh)
GUION = "start:200-205 start:300-305 start:400-405 a:500-505 start:600-605 a:700-705 a:800-805"
for t in range(900, 2101, 60):
    GUION += f" a:{t}-{t + 25} b:{t + 30}-{t + 50}"
GUION += " right:900-1400 left:1400-1700 right:1700-2300"
ENTRADAS_DIFF = "start:150-155 start:350-355 a:450-455 start:650-655 right:700-1500 a:760-800 a:1000-1040 b:900-1000 a:1200-1220"


def sh(cmd, cwd=ROOT, timeout=900):
    """Corre un comando en una shell de login de MSYS2 UCRT64 (necesaria para que gcc y make funcionen)."""
    env = dict(os.environ, MSYSTEM="UCRT64", CHERE_INVOKING="1")
    t0 = time.time()
    try:
        p = subprocess.run([r"C:\msys64\usr\bin\bash.exe", "-lc", cmd], cwd=cwd, env=env, capture_output=True,
                           timeout=timeout, text=True, encoding="utf-8", errors="replace")
        return p.returncode, p.stdout + p.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        return 124, (e.stdout or "") if isinstance(e.stdout, str) else "", time.time() - t0


def slug(nombre):
    s = re.sub(r"\([^)]*\)", "", nombre.rsplit(".", 1)[0])
    s = re.sub(r"[^a-z0-9]+", "", s.lower())
    # El CRC del nombre completo evita choques entre variantes (regiones, revisiones, colecciones)
    return "g" + (s[:18] or "rom") + format(zlib.crc32(nombre.encode()) & 0xFFFFFF, "06x")


def extraer(src, fila):
    dst = WORK / "roms" / (slug(fila["zip"]) + ".nes")
    if not dst.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(pathlib.Path(src) / fila["zip"]) as z:
            dst.write_bytes(z.read(fila["entry"]))
    return dst


def cabecera(rom):
    h = rom.read_bytes()[:16]
    nes2 = (h[7] & 0x0C) == 0x08
    return {"mapper": (h[6] >> 4) | (h[7] & 0xF0) | (((h[8] & 0x0F) << 8) if nes2 else 0),
            "prg_kb": h[4] * 16, "chr_kb": h[5] * 8, "bateria": bool(h[6] & 2), "cuatro_pantallas": bool(h[6] & 8),
            "espejo": "V" if h[6] & 1 else "H", "nes2": nes2}


def analizar(png):
    """(colores distintos, fracción del color dominante) de una captura."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    colores = im.getcolors(maxcolors=1 << 20) or []
    return len(colores), round(max(c for c, _ in colores) / (im.width * im.height), 3) if colores else 1.0


def miniatura(png, destino):
    from PIL import Image
    destino.parent.mkdir(parents=True, exist_ok=True)
    Image.open(png).convert("RGB").resize((128, 120), Image.NEAREST).quantize(64).save(destino, optimize=True)


FRAMES1 = (450, 900, 1500)


def etapa1(src, fila):
    rom = extraer(src, fila)
    r = {"zip": fila["zip"], "juego": re.sub(r"\.zip$", "", fila["zip"]), **cabecera(rom)}
    r["soportado"] = r["mapper"] in SUPPORTED
    base = slug(fila["zip"])
    pngs = [WORK / "shots" / f"{base}_{n}.png" for n in FRAMES1]
    pngs[0].parent.mkdir(parents=True, exist_ok=True)
    if REUSAR and all(p.exists() for p in pngs):
        rc, out, dt = 0, "", 0
    else:
        cmd = " && ".join(f'./nesshot{EXE} "{rom.as_posix()}" "{p.as_posix()}" {n} {GUION}' for p, n in zip(pngs, FRAMES1))
        rc, out, dt = sh(cmd, timeout=300)
    r["interprete_s"] = round(dt, 2)
    if rc != 0:
        r["interprete"] = "error"
        r["detalle"] = (out.strip().splitlines() or ["sin salida"])[-1][:200]
    else:
        # Una captura aislada puede caer en una transición a negro: se queda la más colorida de las tres
        an = [analizar(p) for p in pngs]
        mejor = max(range(len(an)), key=lambda i: (an[i][0], -an[i][1]))
        colores, dom = an[mejor]
        thumb = OUT / "thumbs" / f"{base}.png"
        miniatura(pngs[mejor], thumb)
        r.update(colores=colores, dominante=dom, thumb=thumb.name, capturas=[a[0] for a in an])
        # En blanco: en las tres capturas casi todo el cuadro es de un solo color
        r["interprete"] = "pantalla uniforme" if dom > 0.99 or colores < 3 else "ok"
    return r


def num(rx, texto, tipo=float):
    m = re.search(rx, texto)
    return tipo(m.group(1)) if m else None


def etapa2(src, fila, base):
    rom = extraer(src, fila)
    nombre = slug(fila["zip"])
    carpeta = WORK / "out" / nombre
    traza = WORK / f"{nombre}.trace"
    shutil.rmtree(carpeta, ignore_errors=True)
    traza.write_text("")
    r = {"fases": []}
    t0 = time.time()
    rc, o, _ = sh(f'./nestrace{EXE} "{rom.as_posix()}" "{WORK.as_posix()}/{nombre}.guiada" 2400 {GUION}')
    if rc != 0:
        r.update(resultado="falla", detalle="nestrace: " + o.strip()[-200:])
        return r
    traza.write_text((WORK / f"{nombre}.guiada").read_text())
    for ronda in (1, 2):
        for intento in range(3):
            rc, o, dt = sh(f'./nesrecomp{EXE} "{rom.as_posix()}" -o "{carpeta.as_posix()}" --name {nombre} --trace "{traza.as_posix()}"')
            # En Windows un archivo recién borrado puede seguir «pendiente de borrar» un momento y no se deja crear
            if rc == 0 or "no se pudo crear" not in o:
                break
            time.sleep(2)
        f = {"ronda": ronda}
        if rc != 0:
            r["fases"].append(f)
            r.update(resultado="falla", detalle="nesrecomp: " + o.strip()[-300:])
            return r
        f["instrucciones"] = num(r"código: (\d+) instrucciones", o, int)
        f["pct_prg"] = num(r"bytes, ([\d.]+)% del PRG", o)
        f["funciones"] = num(r"(\d+) funciones", o, int)
        rc, o, dt = sh(f'make -s -C "{carpeta.as_posix()}" -j4 CC=gcc 2>&1', timeout=1500)
        f["compilacion_s"] = round(dt, 1)
        if rc != 0 or not (carpeta / f"{nombre}{EXE}").exists():
            err = [l for l in o.splitlines() if "error" in l.lower()]
            r["fases"].append(f)
            r.update(resultado="falla", detalle="compilación: " + (err[0] if err else o.strip()[-300:])[:300])
            return r
        f["semillas"] = []
        mal = False
        for s in (1, 2):
            unc = carpeta / f"unc_{s}.txt"
            rc, o, dt = sh(f'cd "{carpeta.as_posix()}" && ./difftest{EXE} "{rom.as_posix()}" 1500 {ENTRADAS_DIFF} --seed {s} --uncovered unc_{s}.txt',
                           timeout=1500)
            d = {"semilla": s, "ok": rc == 0, "segundos": round(dt, 1)}
            d["ciclos_recompilados_pct"] = num(r"([\d.]+)% de los ciclos", o)
            d["interpretadas"] = num(r"interpretadas: (\d+)", o, int)
            if rc != 0:
                d["detalle"] = " | ".join(l.strip() for l in o.splitlines() if "DIFERENCIA" in l or "difiere" in l)[:300] or o.strip()[-200:]
                mal = True
            elif unc.exists():
                with open(traza, "a") as tf:
                    tf.write(unc.read_text())
            f["semillas"].append(d)
        r["fases"].append(f)
        if mal:
            r.update(resultado="diferencia", detalle=next(d["detalle"] for d in f["semillas"] if not d["ok"]))
            break
        lineas = sorted(set(traza.read_text().split()))
        traza.write_text("\n".join(lineas) + "\n")
    else:
        r["resultado"] = "ok"
    r["total_s"] = round(time.time() - t0, 1)
    return r


def velocidad(fila):
    """bench del proyecto generado. Se corre al final y de una ROM a la vez: con compilaciones en paralelo da un tercio"""
    nombre = slug(fila["zip"])
    carpeta = WORK / "out" / nombre
    rom = WORK / "roms" / (nombre + ".nes")
    rc, o, _ = sh(f'cd "{carpeta.as_posix()}" && ./bench{EXE} "{rom.as_posix()}" 600')
    return num(r"\(([\d.]+)x tiempo real", o)


def elegir(filas):
    """Por mapper soportado: un cupo de ROMs, variando tamaño de PRG/CHR y batería."""
    por = {}
    for f in filas:
        por.setdefault(f["mapper"], []).append(f)
    elegidas = []
    for m, lst in sorted(por.items()):
        if m not in SUPPORTED:
            continue
        lst.sort(key=lambda f: (f["prg_kb"], f["chr_kb"], f["zip"]))
        n = CUPO.get(m, 3)
        vistos, sel = set(), []
        for f in lst:  # primero una de cada combinación distinta
            k = (f["prg_kb"], f["chr_kb"], f["battery"])
            if k not in vistos and len(sel) < n:
                vistos.add(k); sel.append(f)
        for f in lst:
            if len(sel) >= n:
                break
            if f not in sel:
                sel.append(f)
        elegidas += sel
    return elegidas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--hilos", type=int, default=4)
    ap.add_argument("--solo", choices=["etapa1", "etapa2"])
    ap.add_argument("--mappers", help="solo estos mappers, separados por comas (los demás resultados se conservan)")
    ap.add_argument("--reusar", action="store_true", help="no vuelve a correr nesshot si la captura ya existe")
    a = ap.parse_args()
    global REUSAR
    REUSAR = a.reusar
    scan = json.load(open(WORK.parent / "compat_scan.json", encoding="utf-8"))
    filas = [f for f in scan if "mapper" in f]
    if a.mappers:
        solo = {int(m) for m in a.mappers.split(",")}
        filas = [f for f in filas if f["mapper"] in solo]
    (OUT / "thumbs").mkdir(parents=True, exist_ok=True)
    res_path = OUT / "results.json"
    prev = {r["zip"]: r for r in json.load(open(res_path, encoding="utf-8"))} if res_path.exists() else {}
    t0 = time.time()
    if a.solo != "etapa2":
        with cf.ThreadPoolExecutor(8) as ex:
            for i, r in enumerate(ex.map(lambda f: etapa1(a.src, f), filas), 1):
                prev.setdefault(r["zip"], {}).update(r)
                if r["interprete"] != "error":
                    prev[r["zip"]].pop("detalle", None)
                if i % 100 == 0:
                    print(f"etapa 1: {i}/{len(filas)}", flush=True)
        json.dump(list(prev.values()), open(res_path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    if a.solo != "etapa1":
        sel = elegir(filas)
        print(f"etapa 2: {len(sel)} ROMs", flush=True)

        def una(f):
            r = etapa2(a.src, f, prev.get(f["zip"]))
            print(f"  {f['mapper']:3} {f['zip'][:60]:60} {r.get('resultado')}", flush=True)
            return f["zip"], r
        with cf.ThreadPoolExecutor(a.hilos) as ex:
            for z, r in ex.map(una, sel):
                prev[z]["recompilador"] = r
                json.dump(list(prev.values()), open(res_path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        print("velocidad (bench, de una en una)", flush=True)
        for f in sel:
            if prev[f["zip"]]["recompilador"].get("resultado") == "ok":
                prev[f["zip"]]["recompilador"]["velocidad_x"] = velocidad(f)
        json.dump(list(prev.values()), open(res_path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print(f"listo en {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
