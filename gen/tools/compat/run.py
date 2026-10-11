#!/usr/bin/env python3
"""Prueba de compatibilidad de GENRecomp con una colección de ROMs (por omisión, las de roms/).

Etapa 1 (todas): la ROM en el intérprete con entradas guionadas, capturas a los cuadros 450, 900 y 1500
  (genshot). Detecta errores de carga y pantallas en blanco; la miniatura más colorida va al informe.
Etapa 2 (todas, o las que diga --etapa2): el ciclo del recompilador sin traza: genrecomp, compilar el
  proyecto generado (difftest y bench), difftest con las semillas 1 y 2 (1800 cuadros: entradas fijas
  más un mando aleatorio, comparando 68000, RAM, VDP, Z80, YM2612, PSG, imagen y audio), una segunda
  ronda con lo que difftest --uncovered vio caer al intérprete, y la velocidad (bench contra genbench).
Oráculos (--oraculos): la RAM del 68000 y la imagen a los cuadros 600 y 1200 contra Genesis Plus GX y
  BlastEm (tools/oracles/oracle.py, que los baja la primera vez).

Uso, desde la raíz del repo y con "make" ya hecho:
    python3 tools/compat/run.py [CARPETA] [--etapa2 N] [--oraculos] [--hilos N] [--solo etapa1]
CARPETA puede tener .bin, .md, .gen, .smd o .zip (se extraen a build/compat/roms/). Escribe
docs/compat/results.json y las miniaturas en docs/compat/thumbs/; después: python3 tools/compat/report.py
"""
import argparse, concurrent.futures as cf, json, os, pathlib, re, shutil, struct, subprocess, sys, time, zipfile, zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORK = ROOT / "build" / "compat"
OUT = ROOT / "docs" / "compat"
EXE = ".exe" if os.name == "nt" else ""
EXTS = (".bin", ".md", ".gen", ".smd")
sys.path.insert(0, str(ROOT / "tools" / "oracles"))
import oracle  # noqa: E402

# Entradas guionadas: pasan del logo y del título al juego y se mueven un poco
GUION = "start:200-205 start:300-305 start:400-405 a:500-505 start:600-605 c:700-705 start:800-805"
for t in range(900, 2101, 60):
    GUION += f" b:{t}-{t + 25} c:{t + 30}-{t + 50}"
GUION += " right:900-1400 left:1400-1700 right:1700-2300"
ENTRADAS_DIFF = "start:150-155 start:350-355 a:450-455 start:650-655 right:700-1500 b:760-800 c:1000-1040 a:900-1000"
FRAMES1 = (450, 900, 1500)


def correr(argv, cwd=ROOT, timeout=900):
    t0 = time.time()
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, timeout=timeout, text=True, encoding="utf-8", errors="replace")
        return p.returncode, p.stdout + p.stderr, time.time() - t0
    except subprocess.TimeoutExpired:
        return 124, "tiempo agotado", time.time() - t0


def herramienta(n):
    return str(ROOT / (n + EXE))


def slug(nombre):
    s = re.sub(r"\([^)]*\)", "", nombre.rsplit(".", 1)[0])
    s = re.sub(r"[^a-z0-9]+", "", s.lower())
    return "g" + (s[:18] or "rom") + format(zlib.crc32(nombre.encode()) & 0xFFFFFF, "06x")


def roms_de(carpeta):
    """(nombre, ruta) de cada ROM; las de los .zip se extraen a build/compat/roms/"""
    res = []
    for p in sorted(pathlib.Path(carpeta).iterdir()):
        if p.suffix.lower() in EXTS:
            res.append((p.name, p))
        elif p.suffix.lower() == ".zip":
            with zipfile.ZipFile(p) as z:
                for n in z.namelist():
                    if n.lower().endswith(EXTS):
                        dst = WORK / "roms" / (slug(p.name) + pathlib.Path(n).suffix.lower())
                        if not dst.exists():
                            dst.parent.mkdir(parents=True, exist_ok=True)
                            dst.write_bytes(z.read(n))
                        res.append((p.name[:-4], dst))
                        break
    return res


def info(rom):
    rc, o, _ = correr([herramienta("genrecomp"), str(rom), "--info"])
    if rc != 0:
        return {"error": o.strip().splitlines()[-1] if o.strip() else "no carga"}
    def campo(n):
        m = re.search(rf"^{n}:\s*(.*)$", o, re.M)
        return m.group(1).strip() if m else ""
    m = re.search(r": (\d+) KB", o)
    return {"sha1": campo("SHA1"), "nombre": campo("nombre"), "serie": campo("serie"), "region": campo("región"),
            "perifericos": campo("periféricos"), "kb": int(m.group(1)) if m else 0, "sram": campo("SRAM"),
            "ssf2": "mapper" in o}


def analizar(png):
    """(colores distintos, fracción del color dominante) de una captura"""
    w, h, filas = oracle.leer_png(str(png))
    cuenta = {}
    for f in filas:
        for x in range(0, w * 3, 3):
            k = f[x:x + 3]
            cuenta[k] = cuenta.get(k, 0) + 1
    return len(cuenta), round(max(cuenta.values()) / (w * h), 3)


def escribir_png(path, w, h, filas):
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + f for f in filas)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                     ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))


def miniatura(png, destino):
    """A la mitad (160x112 en H40), por muestreo"""
    w, h, filas = oracle.leer_png(str(png))
    nf = [bytes(b for x in range(0, w, 2) for b in f[x * 3:x * 3 + 3]) for f in filas[::2]]
    destino.parent.mkdir(parents=True, exist_ok=True)
    escribir_png(destino, (w + 1) // 2, len(nf), nf)


def etapa1(nombre, rom):
    r = {"juego": nombre, **info(rom)}
    if "error" in r:
        r["interprete"] = "error"
        return r
    base = slug(nombre)
    pngs = [WORK / "shots" / f"{base}_{n}.png" for n in FRAMES1]
    pngs[0].parent.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    for p, n in zip(pngs, FRAMES1):
        rc, o, _ = correr([herramienta("genshot"), str(rom), str(p), "--frames", str(n), *GUION.split()], timeout=300)
        if rc != 0:
            r.update(interprete="error", detalle=(o.strip().splitlines() or ["sin salida"])[-1][:200])
            return r
    r["interprete_s"] = round(time.time() - t0, 1)
    an = [analizar(p) for p in pngs]
    mejor = max(range(len(an)), key=lambda i: (an[i][0], -an[i][1]))
    colores, dom = an[mejor]
    thumb = OUT / "thumbs" / f"{base}.png"
    miniatura(pngs[mejor], thumb)
    r.update(colores=colores, dominante=dom, thumb=thumb.name, capturas=[a[0] for a in an])
    r["interprete"] = "pantalla uniforme" if dom > 0.99 or colores < 3 else "ok"
    return r


def num(rx, texto, tipo=float):
    m = re.search(rx, texto)
    return tipo(m.group(1)) if m else None


def etapa2(nombre, rom):
    base = slug(nombre)
    carpeta = WORK / "out" / base
    traza = WORK / f"{base}.trace"
    shutil.rmtree(carpeta, ignore_errors=True)
    traza.write_text("")
    r = {"fases": []}
    t0 = time.time()
    for ronda in (1, 2):
        argv = [herramienta("genrecomp"), str(rom), "-o", str(carpeta), "--name", base]
        if ronda == 2:
            argv += ["--trace", str(traza)]
        rc, o, _ = correr(argv)
        f = {"ronda": ronda}
        if rc != 0:
            r["fases"].append(f)
            r.update(resultado="falla", detalle="genrecomp: " + o.strip()[-300:])
            return r
        f["instrucciones"] = num(r"código: (\d+) instrucciones", o, int)
        f["pct_rom"] = num(r"bytes, ([\d.]+)% de la ROM", o)
        f["funciones"] = num(r"(\d+) funciones", o, int)
        rc, o, dt = correr(["make", "-s", "-C", str(carpeta), "-j8", "difftest", "bench"], timeout=1800)
        f["compilacion_s"] = round(dt, 1)
        if rc != 0:
            err = [l for l in o.splitlines() if "error" in l.lower()]
            r["fases"].append(f)
            r.update(resultado="falla", detalle="compilación: " + (err[0] if err else o.strip()[-300:])[:300])
            return r
        f["semillas"] = []
        mal = False
        for s in (1, 2):
            unc = carpeta / f"unc_{s}.txt"
            rc, o, dt = correr([str(carpeta / ("difftest" + EXE)), str(rom), "1800", *ENTRADAS_DIFF.split(), "--seed", str(s),
                                "--uncovered", str(unc)], timeout=1800)
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
    else:
        r["resultado"] = "ok"
    rc, o, _ = correr([str(carpeta / ("bench" + EXE)), str(rom), "--frames", "1800"])
    r["velocidad_x"] = num(r"\(([\d.]+)x tiempo real", o)
    rc, o, _ = correr([herramienta("genbench"), str(rom), "--frames", "1800"])
    r["velocidad_interprete_x"] = num(r"\(([\d.]+)x tiempo real", o)
    r["total_s"] = round(time.time() - t0, 1)
    shutil.rmtree(carpeta / "build", ignore_errors=True)         # los objetos ocupan mucho
    return r


def oraculos(nombre, rom):
    res = {}
    for n in (600, 1200):
        args = ["--frames", str(n), *GUION.split()]
        try:
            res[str(n)] = oracle.compare(str(rom), args, png_dir=str(WORK / "oraculos" / f"{slug(nombre)}_{n}"))
        except SystemExit as e:
            res[str(n)] = {"error": str(e)}
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("carpeta", nargs="?", default=str(ROOT / "roms"))
    ap.add_argument("--etapa2", type=int, default=-1, help="cuántas ROMs pasan a la etapa 2 (todas si no se dice)")
    ap.add_argument("--oraculos", action="store_true", help="comparar contra Genesis Plus GX y BlastEm")
    ap.add_argument("--hilos", type=int, default=2)
    ap.add_argument("--solo", choices=("etapa1", "etapa2"))
    a = ap.parse_args()
    roms = roms_de(a.carpeta)
    if not roms:
        sys.exit(f"no hay ROMs en {a.carpeta}")
    print(f"{len(roms)} ROMs", flush=True)
    OUT.mkdir(parents=True, exist_ok=True)
    resultados = {}
    previo = OUT / "results.json"
    if previo.exists():
        for r in json.loads(previo.read_text()).get("roms", []):
            resultados[r["juego"]] = r

    def una(nr):
        nombre, rom = nr
        r = resultados.get(nombre, {}) if a.solo == "etapa2" else {}
        if a.solo != "etapa2":
            r = etapa1(nombre, rom)
        i = [n for n, _ in roms].index(nombre)
        if a.solo != "etapa1" and r.get("interprete") != "error" and (a.etapa2 < 0 or i < a.etapa2):
            r["etapa2"] = etapa2(nombre, rom)
        if a.oraculos and r.get("interprete") != "error":
            r["oraculos"] = oraculos(nombre, rom)
        e2 = r.get("etapa2", {})
        print(f"{nombre}: intérprete {r.get('interprete')}, etapa 2 {e2.get('resultado', '-')}"
              + (f", {e2.get('velocidad_x')}x" if e2.get("velocidad_x") else ""), flush=True)
        return r

    with cf.ThreadPoolExecutor(a.hilos) as ex:
        for r in ex.map(una, roms):
            resultados[r["juego"]] = r
    datos = {"fecha": time.strftime("%Y-%m-%d"), "roms": sorted(resultados.values(), key=lambda r: r["juego"])}
    previo.write_text(json.dumps(datos, ensure_ascii=False, indent=1))
    print(f"escrito {previo}")


if __name__ == "__main__":
    main()
