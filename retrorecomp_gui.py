#!/usr/bin/env python3
"""retrorecomp: una sola GUI local para NESRecomp, GBRecomp, GENRecomp, NEORecomp y SMSRecomp (solo biblioteca
estándar de Python).

Una página servida en 127.0.0.1 que detecta el sistema de cada ROM (NES, Game Boy, Game Boy Color,
Super Game Boy, Sega Genesis, Neo Geo, Master System, Game Gear) y llama a las herramientas del recompilador que corresponda: mapa del
descubrimiento con clic por celda, avance por ROM, trazas, guiones de entradas, recompilar, difftest, bench y
el ciclo completo, con la salida en vivo.

    python3 retrorecomp_gui.py [--nes DIR] [--gb DIR] [--gen DIR] [--neo DIR] [--sms DIR] [--roms DIR ...] [--port 8765] [--no-browser]

Dónde busca los recompiladores: --nes/--gb/--gen/--neo/--sms, las variables
NESRECOMP_DIR/GBRECOMP_DIR/GENRECOMP_DIR/NEORECOMP_DIR/SMSRECOMP_DIR, la carpeta que contiene a retrorecomp si es
uno de ellos (submódulo) o sus hermanas NESRecomp, GBRecomp, GENRecomp, NEORecomp y SMSRecomp.
"""
import argparse, glob, json, os, re, secrets, shlex, subprocess, sys, threading, time, webbrowser, zipfile, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, quote, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if os.name == "nt" else ""
WORK = os.path.join(HERE, "build", "gui")
STATE_FILE = os.path.join(WORK, "state.json")
# El almacén de trazas por SHA1 (shared/trazas.h): el de RETROCOMP_TRAZAS o uno propio de la GUI, más el repositorio de trazas
# compartidas si está clonado al lado (../recomp-trazas, Xalcker/retrorecomp#16). Al recompilar se suman todos (--traces-dir,
# separados como en PATH) y el ciclo completo (recomp_game.py) guarda lo que aprende de cada juego en el primero
ALMACEN = os.pathsep.join(
    [os.path.abspath(d) for d in (os.environ.get("RETROCOMP_TRAZAS") or os.path.join(WORK, "almacen")).split(os.pathsep) if d]
    + [d for d in [os.path.abspath(os.path.join(HERE, "..", "recomp-trazas"))] if os.path.isdir(d)])
CELL = 256
BANK = 0x4000

lock = threading.RLock()
jobs, job_seq = {}, [0]
cfg = {"token": "", "extra_roms": []}
events_cache = {}
systems = {}          # id del registro -> System

# El registro de sistemas: nombre, extensiones, botones, modelos, BIOS, cuadros, herramientas y guiones de cada
# consola. Lo leen también tests/contract.py, tests/compat_test.py y shared/recomp_game.py.
with open(os.path.join(HERE, "shared", "sistemas.json"), encoding="utf-8") as _f:
    REGISTRO = json.load(_f)


# --------------------------------------------------------------------------- utilidades

# Windows: make y sh no están en el PATH de Windows; los recompiladores se compilan y prueban con MSYS2 UCRT64.
# Esos dos se corren con su bash (en --msys DIR, MSYS2_ROOT o C:\msys64); el resto (los .exe de los
# recompiladores, git) se lanza tal cual.
MSYS = {"root": None}
POSIX_TOOLS = ("make", "sh")


def find_msys(explicit=None):
    for c in (explicit, os.environ.get("MSYS2_ROOT"), r"C:\msys64"):
        if c and os.path.isfile(os.path.join(c, "usr", "bin", "bash.exe")):
            return c
    return None


def posix_path(a):
    """C:\\Users\\x -> C:/Users/x: sh trata la barra invertida como escape."""
    return a.replace("\\", "/") if re.match(r"^[A-Za-z]:\\", a) else a


def host_cmd(argv, env=None):
    """(argv, env) para lanzar argv: en Windows, make y sh dentro de MSYS2 UCRT64 (CHERE_INVOKING conserva la
    carpeta de trabajo del proceso)."""
    if os.name != "nt" or not argv or argv[0] not in POSIX_TOOLS or not MSYS["root"]:
        return argv, env
    e = dict(env if env is not None else os.environ)
    e.update(MSYSTEM="UCRT64", CHERE_INVOKING="1")
    script = "exec " + " ".join(shlex.quote(posix_path(a)) for a in argv)
    return [os.path.join(MSYS["root"], "usr", "bin", "bash.exe"), "-lc", script], e


def run_quiet(argv, cwd, timeout=60):
    try:
        argv, env = host_cmd(argv)
        p = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")   # los recompiladores escriben UTF-8 (en Windows no es el predeterminado)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except Exception as e:  # noqa: BLE001
        return 127, str(e)


def stem(rom):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.splitext(rom)[0]).strip("_") or "rom"


CHOQUES = set()      # (sistema, nombre base) que comparten dos o más ROMs; lo llena list_roms()


def base_short_name(rom):
    base = re.sub(r"\(.*?\)|\[.*?\]", "", os.path.splitext(rom)[0])
    return re.sub(r"[^a-z0-9]+", "", base.lower())[:40] or "juego"


def short_name(rom, sid=""):
    """Nombre del proyecto (build/out/NOMBRE, --name): el título sin lo que va entre paréntesis. Antes se cortaba
    a 16 caracteres y Sonic The Hedgehog 1, 2 y 3 daban el mismo; ahora solo se agrega un CRC del nombre del
    archivo cuando otra ROM del mismo sistema da el mismo título (regiones, revisiones)."""
    n = base_short_name(rom)
    if (sid, n) in CHOQUES:
        n += format(zlib.crc32(os.path.basename(rom).encode()) & 0xFFFFFF, "06x")
    return n


def resumenes(text, herramienta=None):
    """Las líneas "RESUMEN {json}" de una salida (difftest, bench, --viz, recomp_game.py), como dicts y en orden;
    con `herramienta`, solo las suyas. Así la GUI no lee el texto en español."""
    out = []
    for ln in text.splitlines():
        if ln.startswith("RESUMEN {"):
            try:
                r = json.loads(ln[len("RESUMEN "):])
            except ValueError:
                continue
            if herramienta in (None, r.get("herramienta")):
                out.append(r)
    return out


def safe_name(n, what="nombre"):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,60}", n or ""):
        raise ValueError(f"{what}: solo letras, números, punto, guion y guion bajo")
    return n


def pint(v, lo, hi, what):
    try:
        n = int(v)
    except (TypeError, ValueError):
        raise ValueError(f"{what}: número inválido")
    if not lo <= n <= hi:
        raise ValueError(f"{what}: debe estar entre {lo} y {hi}")
    return n


def load_state():
    try:
        with open(STATE_FILE, encoding="utf-8") as f:
            return json.load(f)
    except Exception:  # noqa: BLE001
        return {}


def update_state(rid, **kv):
    with lock:
        st = load_state()
        st.setdefault(rid, {}).update(kv)
        os.makedirs(WORK, exist_ok=True)
        tmp = STATE_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(st, f, ensure_ascii=False, indent=1)
        os.replace(tmp, STATE_FILE)


# Los botones de todas las consolas; los que cada una acepta están en su entrada del registro
BTN = "(" + "|".join(sorted({b for r in REGISTRO.values() for b in r["botones"]}, key=lambda b: (-len(b), b))) + ")"
PAD = r"(-|\$?[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|" + BTN + r"(\+" + BTN + r")*)"
LINE_OK = re.compile(r"^(#.*|modelo (dmg|cgb|sgb)|paleta [A-Za-z+]+|\d+\s+" + PAD + r"(\s+" + PAD + r")?)?$", re.I)


# --------------------------------------------------------------------------- sistemas

class System:
    """Lo que cambia entre recompiladores; el resto de la GUI es común. Los datos (nombre, extensiones,
    herramientas, modelos, notas, guiones...) salen de la entrada del registro con el mismo id; la subclase pone
    el comportamiento (cabecera de la ROM, mapa, cómo graba trazas) y sus caps propias."""
    id = ""
    caps = {}                        # las de comportamiento; las del registro (model, bios, pad2, game_frames) se suman
    roms_subdirs = False             # busca también en las subcarpetas de roms/ (SMSRecomp: roms/sms y roms/gg)

    def __init_subclass__(cls, **kw):
        super().__init_subclass__(**kw)
        r = REGISTRO[cls.id]
        cls.reg = r
        cls.label, cls.exts, cls.tools = r["nombre"], tuple(r["extensiones"]), tuple(r["herramientas"])
        cls.legend, cls.map_note, cls.input_note = tuple(r["leyenda"]), r["nota_mapa"], r["nota_entradas"]
        cls.models, cls.buttons, cls.presets = tuple(r["modelos"]), frozenset(r["botones"]), r["guiones"]
        cls.recomp, cls.bench_tool, cls.window = cls.id + "recomp", cls.id + "bench", cls.id
        cls.trace_tools = {"trace": cls.id + "trace", "screenshot": cls.id + "shot", "audio": cls.id + "audio"}
        cls.caps = {**cls.caps, "model": bool(r["modelos"]), "bios": r["bios"], "pad2": r["jugadores"] == 2,
                    "game_frames": r["cuadros"]}

    def __init__(self, root):
        self.root = root

    def tool(self, name):
        return os.path.join(self.root, name + EXE)

    def run(self, argv, timeout=60):
        return run_quiet(argv, self.root, timeout)

    def bench_argv(self, rp, pdir, frames, repeat, inp, model, recompiled):
        """El bench de la consola (intérprete) o el del proyecto generado (recompilado): las mismas opciones"""
        exe = os.path.join(pdir, "bench" + EXE) if recompiled else self.tool(self.bench_tool)
        argv = [exe, rp, "--frames", str(frames), "--repeat", str(repeat)]
        return argv + (["--input", inp] if inp else [])

    def tools_status(self):
        return {n: os.path.isfile(self.tool(n)) for n in self.tools}

    def roms_dir(self):
        return os.path.join(self.root, "roms")

    # Iguales en los dos recompiladores (mismos argumentos de recomp_game.py y mismo make test)
    def game_argv(self, rp, name, frames):
        return [sys.executable, os.path.join(HERE, "shared", "recomp_game.py"), rp, name, str(frames)]

    def test_argv(self):
        return ["make", "test"]

    def project_dir(self, name):
        raise NotImplementedError

    def project(self, rom):
        name = short_name(rom, self.id)
        d = self.project_dir(name)
        return {"name": name, "dir": d, "generated": os.path.isfile(os.path.join(d, "Makefile")),
                "built": os.path.isfile(os.path.join(d, name + EXE))}

    def git(self):
        def g(*a):
            rc, o = self.run(["git", *a], 15)
            return o.strip() if rc == 0 else ""
        up = g("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
        behind = ahead = 0
        if up:
            c = g("rev-list", "--left-right", "--count", f"HEAD...{up}").split()
            if len(c) == 2:
                ahead, behind = int(c[0]), int(c[1])
        return {"branch": g("rev-parse", "--abbrev-ref", "HEAD"), "ahead": ahead, "behind": behind,
                "head": g("log", "-1", "--format=%h %s"), "dirty": bool(g("status", "--porcelain"))}

    def repo_inputs(self):
        return sorted(glob.glob(os.path.join(self.root, "tests", "inputs", "*.txt")))


class NES(System):
    id = "nes"
    caps = {"notrace": True, "interp_play": False, "plays_interp": True}
    ORG_COLOR = {46: 0, 51: 1, 226: 2, 201: 3}
    ANSI = re.compile(r"\x1b\[([0-9;]*)m(.)\x1b\[0m|(.)")
    # Lo que cambia en GENRecomp, que copia los formatos de NESRecomp (ver class GEN)
    mem = "PRG"                      # memoria a la que se refieren los desplazamientos
    trace_shift = 13                 # banco de la traza (trace_line.h): desplazamiento >> 13, ventanas de 8 KB

    def project_dir(self, name):
        return os.path.join(self.root, "build", "out", name)


    def info(self, path):
        try:
            with open(path, "rb") as f:
                d = f.read(16)
            size = os.path.getsize(path)
        except OSError:
            return {}
        if d[:4] != b"NES\x1a":
            return {}
        mapper = (d[6] >> 4) | (d[7] & 0xF0)
        return {"title": "", "mapper": f"mapper {mapper}", "banks": d[4], "size": d[4] * 0x4000 or size}

    # -- mapa (de --viz --color)
    def map(self, rom_path, traces, coverage):
        argv = [self.tool(self.recomp), rom_path, "--viz", "--color"]
        for t in traces:
            argv += ["--trace", t]
        if coverage:
            argv += ["--coverage", coverage]
        rc, out = self.run(argv, 120)
        if rc != 0:
            raise RuntimeError(out.strip() or f"falló {self.recomp}")
        banks = []
        for ln in out.splitlines():
            m = re.match(r"^([0-9A-F]{6}) (.*)$", ln)
            if m and "celdas" not in ln:
                cells = []
                for mm in self.ANSI.finditer(m.group(2)):
                    if mm.group(3) is not None:
                        cells.append([0 if mm.group(3) == "." else 100, 0, 0])
                        continue
                    code, ch = mm.group(1), mm.group(2)
                    if code == "41;97":
                        cells.append([0, 0, 1])
                    else:
                        n = int(code.split(";")[-1]) if code else 0
                        cells.append([256 if ch == "#" else 100, self.ORG_COLOR.get(n, 0), 0])
                banks.append({"label": m.group(1), "base": int(m.group(1), 16), "cells": cells})
        # Las cifras salen de la línea RESUMEN de --viz; una fila del mapa por banco
        r = (resumenes(out, "viz") or [{}])[-1]
        stats = {"mapper": r.get("mapper") or self.info(rom_path).get("mapper", ""), "banks": len(banks), "size": r.get("tam")}
        if "instrucciones" in r:
            stats.update(instr=r["instrucciones"], bytes=r["bytes"], pct=r["pct"], cells_code=r["celdas_codigo"], cells=r["celdas"])
        cov = r.get("cobertura")
        if cov:
            tot = cov["total"]
            stats.update(cov_total=tot, cov_missing=tot - cov["halladas"],
                         cov_pct=round(100.0 * cov["halladas"] / tot, 1) if tot else 100.0)
        return {"banks": banks, "stats": stats}

    # -- instrucciones de una celda (de --events)
    def events(self, rom_path, traces):
        tmp = os.path.join(WORK, f"events_{secrets.token_hex(4)}.txt")
        os.makedirs(WORK, exist_ok=True)
        argv = [self.tool(self.recomp), rom_path, "--events", tmp]
        for t in traces:
            argv += ["--trace", t]
        rc, out = self.run(argv, 120)
        if rc != 0:
            raise RuntimeError(out.strip())
        ev = []
        with open(tmp, encoding="utf-8") as f:
            for ln in f:
                if ln.startswith("#"):
                    continue
                p = ln.rstrip("\n").split(" ", 4)
                if len(p) < 5:
                    continue
                off, addr, how, frm, rest = p
                org = next((i for i, n in enumerate(self.legend) if rest.startswith(n + " ")), 0)
                ev.append({"pos": int(off, 16), "addr": addr, "org": org, "how": how,
                           "dis": rest[len(self.legend[org]) + 1:]})
        os.remove(tmp)
        return ev

    def cell(self, rom_path, traces, bank_i, ci, banks):
        ev = get_events(self, rom_path, traces)
        lo = banks[bank_i]["base"] + ci * CELL
        items = sorted((e for e in ev if lo <= e["pos"] < lo + CELL), key=lambda e: e["pos"])
        return {"title": f"{self.mem} ${lo:06X}–${lo + CELL - 1:06X}",
                # banco:dirección como en las trazas (y en GB); a --why se le pasa el desplazamiento
                "events": [{"addr": "%02X:%s" % (e["pos"] >> self.trace_shift, e["addr"]), "org": e["org"], "dis": e["dis"],
                            "why": "%X" % e["pos"]} for e in items]}

    def why(self, rom_path, traces, arg):
        if not re.fullmatch(r"[0-9A-Fa-f]{1,8}", arg or ""):
            raise ValueError("desplazamiento inválido")
        argv = [self.tool(self.recomp), rom_path, "--why", arg]
        for t in traces:
            argv += ["--trace", t]
        # Sin las dos primeras líneas (cabecera de la ROM y «código: ...»)
        return re.sub(r"^.*\n.*código:.*\n", "", self.run(argv, 60)[1])

    # -- comandos
    def recomp_argv(self, rp, pdir, name, tflags):
        return [self.tool(self.recomp), rp, "-o", pdir, "--name", name, *tflags]

    def trace_tool(self, kind):
        return self.trace_tools[kind]

    def visor(self, rp, tflags, cov):
        out = os.path.join(WORK, f"visor_{self.id}_{stem(os.path.basename(rp))}.html")
        argv = [self.tool(self.recomp), rp, "--html", out, *tflags]
        if cov:
            argv += ["--coverage", cov]
        return argv, out

    def record_trace(self, argv, env, path):
        env["NES_TRACE"] = path

    def play_argv(self, rp, proj, interp, model, env, record_trace_path, record_input_path, play_input):
        if interp or not proj["built"]:
            exe = self.tool(self.window)
            if not os.path.isfile(exe):
                raise ValueError(f"falta ./{self.window} (Compilar herramientas)")
        else:
            exe = os.path.join(proj["dir"], proj["name"] + EXE)
        argv = [exe, rp]
        if record_trace_path:
            if exe == self.tool(self.window):
                self.record_trace(argv, env, record_trace_path)
            else:
                # El recompilado graba lo que tuvo que pasarle al intérprete: justo lo que vale la pena compartir (#16)
                argv += ["--uncovered", record_trace_path]
        if record_input_path:
            argv += ["--record", record_input_path]
        if play_input:
            argv += ["--play", play_input]
        return argv


class GEN(NES):
    """GENRecomp copia los formatos de NESRecomp (mapa de --viz --color, --events, --why, --html,
    trazas BANCO:DIRECCION, recomp_game.py y bench en el proyecto): solo cambian los nombres,
    la cabecera de la ROM y cómo graba trazas la ventana."""
    id = "gen"
    mem = "ROM"
    trace_shift = 19                 # ventanas de 512 KB, las del mapper de SSF2

    def info(self, path):
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                d = f.read(0x200 + 0x200)
        except OSError:
            return {}
        if os.path.splitext(path)[1].lower() == ".smd":     # 512 bytes del copiador y bloques entrelazados
            return {"title": "", "mapper": "Genesis (.smd)", "banks": max(1, (size - 0x200) // BANK), "size": size - 0x200}
        if d[0x100:0x104] != b"SEGA" and d[0x101:0x105] != b"SEGA":
            return {}
        title = d[0x150:0x180].decode("latin-1", "replace").strip() or d[0x120:0x150].decode("latin-1", "replace").strip()
        return {"title": " ".join(title.split()), "mapper": "Genesis", "banks": max(1, size // BANK), "size": size}


    def record_trace(self, argv, env, path):
        argv += ["--trace", path]


class NEO(GEN):
    """NEORecomp copia los formatos de GENRecomp: cambian los nombres, que el juego es un zip de MAME (o un
    .neo) y que el mapa de un programa de más de 1 MB tiene celdas de más de 256 bytes (las mismas 64 por
    fila), así que la celda se busca con el tamaño de ese mapa. Los datos del juego (tamaño de P, nombre)
    salen de neorecomp --info."""
    id = "neo"
    mem = "P"
    trace_shift = 20                 # bancos de 1 MB (P1 y cada banco de P2); la BIOS es el banco FF
    info_cache = {}

    @staticmethod
    def map_cell(size):
        """Bytes por celda del mapa de neorecomp --viz: 256 hasta 1 MB; más, para unas 64 filas de 64"""
        c = 256
        while size // c > 4096:
            c *= 2
        return c

    def info(self, path):
        try:
            key = (path, os.path.getmtime(path))
        except OSError:
            return {}
        with lock:
            if key in self.info_cache:
                return self.info_cache[key]
        rc, o = self.run([self.tool(self.recomp), path, "--info"], 120)
        m = re.search(r"^\s+P\s+(\d+) (KB|MB)", o, re.M)
        if rc != 0 or not m:
            return {}
        size = int(m.group(1)) * (1 << (20 if m.group(2) == "MB" else 10))
        t = re.search(r"^juego:\s+(.*?)(?: \(|$)", o, re.M)
        title = t.group(1).strip() if t and "no está en la base" not in t.group(1) else ""
        row = 64 * self.map_cell(size)
        r = {"title": title, "mapper": "Neo Geo", "banks": max(1, -(-size // row)), "size": size}
        with lock:
            self.info_cache[key] = r
        return r


    def cell(self, rom_path, traces, bank_i, ci, banks):
        cell = self.map_cell(self.info(rom_path).get("size", 0))
        ev = get_events(self, rom_path, traces)
        lo = bank_i * 64 * cell + ci * cell
        items = sorted((e for e in ev if lo <= e["pos"] < lo + cell), key=lambda e: e["pos"])
        return {"title": f"{self.mem} ${lo:06X}–${lo + cell - 1:06X}",
                "events": [{"addr": "%02X:%s" % (e["pos"] >> self.trace_shift, e["addr"]), "org": e["org"], "dis": e["dis"],
                            "why": "%X" % e["pos"]} for e in items]}



class SMS(GEN):
    """SMSRecomp copia los formatos de GENRecomp (mapa de --viz --color, --events, --why, --html, trazas
    BANCO:DIRECCION con bancos de 16 KB, recomp_game.py y bench en el proyecto; lo del recompilador llega en su
    fase 5): cambian los nombres, la cabecera TMR SEGA, que hay dos consolas (--model sms|gg, como el modelo del
    Game Boy) y que la ventana graba trazas con --trace."""
    id = "sms"
    roms_subdirs = True
    mem = "ROM"
    trace_shift = 14                 # las páginas de 16 KB del mapper

    @staticmethod
    def tmr_sega(d):
        """Dónde está la cabecera TMR SEGA ($7FF0, $3FF0 o $1FF0), o None"""
        return next((a for a in (0x7FF0, 0x3FF0, 0x1FF0) if d[a:a + 8] == b"TMR SEGA"), None)

    def info(self, path):
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                if size % BANK == 512:              # cabecera de copiador
                    f.seek(512)
                    size -= 512
                d = f.read(0x8000)
        except OSError:
            return {}
        gg = os.path.splitext(path)[1].lower() == ".gg"
        if self.tmr_sega(d) is None and not gg and os.path.splitext(path)[1].lower() != ".sms":
            return {}
        return {"title": "", "mapper": "Game Gear" if gg else "Master System", "banks": max(1, -(-size // BANK)), "size": size}


    def bench_argv(self, rp, pdir, frames, repeat, inp, model, recompiled):
        return super().bench_argv(rp, pdir, frames, repeat, inp, model, recompiled) + (["--model", model] if model else [])

    def play_argv(self, rp, proj, interp, model, env, record_trace_path, record_input_path, play_input):
        argv = super().play_argv(rp, proj, interp, model, env, record_trace_path, record_input_path, play_input)
        return argv + (["--model", model] if model else [])


class GB(System):
    id = "gb"
    caps = {"notrace": True, "interp_play": True, "plays_interp": False, "unresolved": True}
    MBC = {0x00: "ROM", 0x01: "MBC1", 0x02: "MBC1+RAM", 0x03: "MBC1+RAM+BAT", 0x05: "MBC2", 0x06: "MBC2+BAT",
           0x0F: "MBC3+TIMER+BAT", 0x10: "MBC3+TIMER+RAM+BAT", 0x11: "MBC3", 0x12: "MBC3+RAM", 0x13: "MBC3+RAM+BAT",
           0x19: "MBC5", 0x1A: "MBC5+RAM", 0x1B: "MBC5+RAM+BAT", 0x1C: "MBC5+RUMBLE", 0x1E: "MBC5+RUMBLE+RAM+BAT",
           0x22: "MBC7", 0xFC: "Pocket Camera", 0xFD: "TAMA5", 0xFE: "HuC3", 0xFF: "HuC1"}

    def project_dir(self, name):
        return os.path.join(self.root, "build", "out", name)

    def info(self, path):
        try:
            with open(path, "rb") as f:
                d = f.read(0x150)
            size = os.path.getsize(path)
        except OSError:
            return {}
        if len(d) < 0x150:
            return {}
        title = d[0x134:0x144].split(b"\0")[0].decode("ascii", "replace").strip()
        return {"title": title, "mapper": self.MBC.get(d[0x147], "0x%02X" % d[0x147]), "banks": max(1, size // BANK), "size": size}

    def events(self, rom_path, traces):
        tmp = os.path.join(WORK, f"events_{secrets.token_hex(4)}.txt")
        os.makedirs(WORK, exist_ok=True)
        argv = [self.tool("gbrecomp"), rom_path, "--why", "00:0000", "--events", tmp]
        for t in traces:
            argv += ["--trace", t]
        rc, out = self.run(argv, 180)
        if rc != 0 or not os.path.isfile(tmp):
            raise RuntimeError(out.strip() or "falló gbrecomp")
        insns, bad, data, tables, org = [], [], [], 0, 0
        with open(tmp, encoding="utf-8") as f:
            for ln in f:
                if ln.startswith("#"):
                    continue
                p = ln.split()
                if not p:
                    continue
                if p[0] == "recorrido" and len(p) >= 3:
                    org = {"arranque": 0, "irq": 1, "tabla": 2, "traza": 3}.get(p[2], 0)
                elif p[0] == "insn" and len(p) >= 3:
                    b, a = p[1].split(":")
                    insns.append((int(b, 16), int(a, 16), int(p[2]), org))
                elif p[0] == "args" and len(p) >= 3:
                    b, a = p[1].split(":")
                    data.append((int(b, 16), int(a, 16), int(p[2])))
                elif p[0] in ("jp-hl", "sin-banco") and len(p) >= 2:
                    b, a = p[1].split(":")
                    bad.append((int(b, 16), int(a, 16)))
                elif p[0] == "tabla":
                    tables += 1
        os.remove(tmp)
        return {"insns": insns, "bad": bad, "data": data, "tables": tables}

    def map(self, rom_path, traces, coverage):
        ev = get_events(self, rom_path, traces)
        info = self.info(rom_path)
        nb, ncell = info.get("banks", 2), BANK // CELL
        cat = [[[0] * 5 for _ in range(ncell)] for _ in range(nb)]
        covered = set()
        for b, a, n, o in ev["insns"]:
            if b >= nb:
                continue
            for k in range(n):
                covered.add((b, a + k))
                c = ((a & 0x3FFF) + k) // CELL
                if c < ncell:
                    cat[b][c][o] += 1
        for b, a, n in ev["data"]:
            if b < nb:
                for k in range(n):
                    c = ((a & 0x3FFF) + k) // CELL
                    if c < ncell:
                        cat[b][c][4] += 1
        miss_cells, missing, total = set(), 0, 0
        if coverage:
            with open(coverage, encoding="utf-8") as f:
                for ln in f:
                    m = re.match(r"\s*([0-9A-Fa-f]+):([0-9A-Fa-f]{4})", ln)
                    if m:
                        b, a = int(m.group(1), 16), int(m.group(2), 16)
                        total += 1
                        if (b, a) not in covered:
                            missing += 1
                            if b < nb:
                                miss_cells.add((b, (a & 0x3FFF) // CELL))
        bad_cells = {(b, (a & 0x3FFF) // CELL) for b, a in ev["bad"] if b < nb}
        banks = []
        for b in range(nb):
            cells = []
            for c in range(ncell):
                n = cat[b][c]
                best = max(range(5), key=lambda k: n[k])
                cells.append([min(sum(n), CELL), best, (1 if (b, c) in miss_cells else 0) | (2 if (b, c) in bad_cells else 0)])
            banks.append({"label": "%02X" % b, "base": 0 if b == 0 else 0x4000, "bank": b, "cells": cells})
        nbytes = sum(n for _, _, n, _ in ev["insns"])
        stats = {**info, "instr": len(ev["insns"]), "bytes": nbytes, "pct": round(100.0 * nbytes / max(1, info.get("size", 1)), 2),
                 "tables": ev["tables"], "unresolved": len(ev["bad"])}
        if coverage:
            stats.update(cov_total=total, cov_missing=missing, cov_pct=round(100.0 * (total - missing) / total, 1) if total else 100.0)
        return {"banks": banks, "stats": stats}

    def cell(self, rom_path, traces, bank_i, ci, banks):
        ev = get_events(self, rom_path, traces)
        bank = banks[bank_i]["bank"]
        lo = banks[bank_i]["base"] + ci * CELL
        items = sorted((a, n, o) for b, a, n, o in ev["insns"] if b == bank and lo <= a < lo + CELL)
        dis = {}
        if items:
            rc, out = self.run([self.tool("gbrecomp"), rom_path, "--dis", f"{bank:02X}:{items[0][0]:04X}:{len(items) + 40}"], 30)
            for ln in out.splitlines():
                m = re.match(r"^[0-9A-F]+:([0-9A-F]{4})\s+(.*?)\s*(;.*)?$", ln)
                if m:
                    dis[int(m.group(1), 16)] = m.group(2)
        return {"title": f"{bank:02X}:{lo:04X}–{lo + CELL - 1:04X}",
                "events": [{"addr": "%02X:%04X" % (bank, a), "org": o, "dis": dis.get(a, ""), "why": "%02X:%04X" % (bank, a)} for a, n, o in items]}

    def why(self, rom_path, traces, arg):
        if not re.fullmatch(r"[0-9A-Fa-f]{1,3}:[0-9A-Fa-f]{4}", arg or ""):
            raise ValueError("dirección inválida (use BB:AAAA)")
        argv = [self.tool("gbrecomp"), rom_path, "--why", arg]
        for t in traces:
            argv += ["--trace", t]
        return self.run(argv, 60)[1]

    def recomp_argv(self, rp, pdir, name, tflags):
        return [self.tool("gbrecomp"), rp, "-o", pdir, "--name", name, *tflags]

    def trace_tool(self, kind):
        return {"trace": "gbtrace", "screenshot": "gbshot", "audio": "gbaudio"}[kind]

    def bench_argv(self, rp, pdir, frames, repeat, inp, model, recompiled):
        return super().bench_argv(rp, pdir, frames, repeat, inp, model, recompiled) + (["--model", model] if model else [])

    def play_argv(self, rp, proj, interp, model, env, record_trace_path, record_input_path, play_input):
        # Con el intérprete (o sin proyecto compilado): gb, como nes en NESRecomp. Si el GBRecomp
        # es anterior y no tiene gb, el juego recompilado con --interp.
        gb = self.tool("gb")
        if (interp or not proj["built"]) and os.path.isfile(gb):
            argv = [gb, rp]
        elif not proj["built"]:
            raise ValueError("el proyecto no está compilado (Recompilar primero) y falta ./gb (Compilar herramientas)")
        else:
            argv = [os.path.join(proj["dir"], proj["name"] + EXE), rp]
            if interp:
                argv.append("--interp")
        if model:
            argv += ["--model", model]
        if record_trace_path:
            argv += ["--uncovered", record_trace_path]
        if record_input_path:
            argv += ["--record", record_input_path]
        if play_input:
            argv += ["--play", play_input]
        return argv


def get_events(system, rom_path, traces):
    key = (system.id, rom_path, tuple((t, os.path.getmtime(t)) for t in traces), os.path.getmtime(system.tool(system.recomp)))
    with lock:
        if key in events_cache:
            return events_cache[key]
    ev = system.events(rom_path, traces)
    with lock:
        events_cache.clear()
        events_cache[key] = ev
    return ev


# --------------------------------------------------------------------------- detección de repos y ROMs

def find_repo(name, env, explicit, marker):
    cands = [explicit, os.environ.get(env), os.path.join(HERE, name[:-len("Recomp")].lower())]   # monorepo: gb/, nes/...
    parent = os.path.dirname(HERE)
    for up in (parent, os.path.dirname(parent)):
        cands.append(up)                                  # retrorecomp dentro del repo (submódulo)
        cands.append(os.path.join(os.path.dirname(up), name))   # y el otro recompilador, hermano de ese repo
    cands += [os.path.join(os.path.dirname(HERE), name), os.path.join(os.path.dirname(HERE), name.lower())]
    for c in cands:
        if c and os.path.isfile(os.path.join(c, "Makefile")) and os.path.isfile(os.path.join(c, marker)):
            return os.path.abspath(c)
    return None


def detect_system(path):
    try:
        with open(path, "rb") as f:
            d = f.read(0x8200)
    except OSError:
        return None
    if d[:4] == b"NES\x1a":
        return "nes"
    # Neo Geo: un .neo, o un zip de MAME con su programa (un .p1; los de las BIOS, aes.zip y neogeo.zip, no)
    if d[:4] == b"NEO\x01":
        return "neo"
    if d[:4] == b"PK\x03\x04":
        try:
            import zipfile
            with zipfile.ZipFile(path) as z:
                return "neo" if any(n.lower().endswith((".p1", ".ep1")) for n in z.namelist()) else None
        except (OSError, zipfile.BadZipFile):
            return None
    if len(d) >= 0x134 and d[0x104:0x108] == bytes([0xCE, 0xED, 0x66, 0x66]):
        return "gb"
    # Master System y Game Gear: "TMR SEGA" en $7FF0, $3FF0 o $1FF0 (también tras 512 bytes de copiador)
    if SMS.tmr_sega(d) is not None or SMS.tmr_sega(d[512:]) is not None:
        return "sms"
    # Genesis: "SEGA" en la cabecera ($100), o un .smd (cabecera de copiador AA BB y bloques entrelazados)
    if d[0x100:0x104] == b"SEGA" or d[0x101:0x105] == b"SEGA" or (len(d) > 9 and d[8:10] == b"\xaa\xbb"):
        return "gen"
    # Sin cabecera reconocible, por la extensión (los del Game Gear no siempre traen la suya). Un .bin sin "SEGA"
    # no se adivina, y un .zip o un .neo del Neo Geo se reconocen solo por su contenido.
    ext = os.path.splitext(path)[1].lower()
    if ext in (".bin", ".zip", ".neo"):
        return None
    return next((sid for sid, r in REGISTRO.items() if ext in r["extensiones"]), None)


ROMS = {}   # id -> (system, ruta)


def list_roms():
    found = {}
    for sysm in systems.values():
        dirs = [sysm.roms_dir()] + cfg["extra_roms"]
        for d in dirs:
            pats = [os.path.join(d, "*")] + ([os.path.join(d, "*", "*")] if sysm.roms_subdirs and d == sysm.roms_dir() else [])
            for p in (q for pat in pats for q in glob.glob(pat)):
                if os.path.isfile(p) and os.path.splitext(p)[1].lower() in sysm.exts and detect_system(p) == sysm.id:
                    found[f"{sysm.id}:{os.path.basename(p)}"] = (sysm, p)
    ROMS.clear()
    ROMS.update(found)
    vistos = {}
    for s, p in found.values():
        k = (s.id, base_short_name(os.path.basename(p)))
        vistos[k] = vistos.get(k, 0) + 1
    CHOQUES.clear()
    CHOQUES.update(k for k, n in vistos.items() if n > 1)
    # built: el proyecto generado de esa ROM (build/out/NOMBRE) ya tiene su ejecutable
    res = []
    for i, (s, p) in sorted(found.items(), key=lambda x: x[1][1].lower()):
        pr = s.project(os.path.basename(p))
        res.append({"id": i, "system": s.id, "label": s.label, "name": os.path.basename(p), "size": os.path.getsize(p),
                    "project": pr["name"], "built": pr["built"]})
    return res


def rom_of(rid):
    if rid not in ROMS:
        list_roms()
    if rid not in ROMS:
        raise ValueError("ROM desconocida: " + str(rid))
    return ROMS[rid]


def base_name(rid):
    return os.path.basename(rom_of(rid)[1])


# --------------------------------------------------------------------------- trazas y entradas

def trace_dir(rid):
    return os.path.join(WORK, "traces", stem(rid.replace(":", "_")))


def list_traces(rid):
    out = []
    for p in sorted(glob.glob(os.path.join(trace_dir(rid), "*.trace"))):
        with open(p, "rb") as f:
            n = sum(1 for _ in f)
        out.append({"name": os.path.basename(p), "lines": n})
    return out


def trace_files(rid, names):
    out = []
    for n in names or []:
        if os.path.basename(n) != n or not n.endswith(".trace"):
            raise ValueError("traza inválida")
        p = os.path.join(trace_dir(rid), n)
        if os.path.isfile(p):
            out.append(p)
    return out


def input_dir(rid):
    return os.path.join(WORK, "inputs", stem(rid.replace(":", "_")))


def list_inputs(rid):
    sysm = rom_of(rid)[0]
    return ["repo:" + os.path.basename(p) for p in sysm.repo_inputs()] + \
           ["mia:" + os.path.basename(p) for p in sorted(glob.glob(os.path.join(input_dir(rid), "*.txt")))]


def resolve_input(rid, ref):
    if not ref:
        return None
    kind, _, name = ref.partition(":")
    if kind not in ("repo", "mia") or os.path.basename(name) != name or not name.endswith(".txt"):
        raise ValueError("entrada inválida")
    p = os.path.join(rom_of(rid)[0].root, "tests", "inputs", name) if kind == "repo" else os.path.join(input_dir(rid), name)
    if not os.path.isfile(p):
        raise ValueError("no existe la entrada " + ref)
    return p


def save_input(rid, name, text):
    sysm = rom_of(rid)[0]
    name = safe_name(name[:-4] if name.endswith(".txt") else name)
    lines = [l.rstrip() for l in text.splitlines()]
    for i, l in enumerate(lines, 1):
        # Botones que esa consola no tiene (se revisan las palabras después del número de cuadro; los valores
        # numéricos y «-» no son botones)
        words = set(re.split(r"[\s+]+", l.strip().upper())[1:]) if re.match(r"\d", l.strip()) else set()
        bad_btn = {w for w in words if re.fullmatch(BTN, w)} - sysm.buttons
        if (not LINE_OK.match(l.strip()) or (sysm.id != "gb" and re.match(r"(modelo|paleta)\b", l.strip(), re.I))
                or bad_btn):
            raise ValueError(f"línea {i} inválida: {l[:40]} (use «cuadro BOTONES», p. ej. 300 START+A, o «-»)")
    os.makedirs(input_dir(rid), exist_ok=True)
    with open(os.path.join(input_dir(rid), name + ".txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return "mia:" + name + ".txt"


# --------------------------------------------------------------------------- BIOS

bios_path = [""]      # BIOS elegida en la GUI (Neo Geo); vacía = la búsqueda normal del runtime


def bios_env():
    """NEO_BIOS para los pasos y para jugar; solo si hay una BIOS elegida en la GUI y existe (la propia
    se ensambla en un paso anterior del mismo trabajo)"""
    return {"NEO_BIOS": bios_path[0]} if bios_path[0] and os.path.isfile(bios_path[0]) else {}


BIOS_NAMES = ("aes.zip", "neo-epo.bin", "neo-po.bin", "neobios.bin")      # los de bios_find del runtime


def bios_steps(sysm, rp):
    """Pasos que ensamblan la BIOS propia (tools/neobios/asm68k.py, lo que hace make neobios) cuando no hay
    ninguna: ni la elegida en la GUI, ni $NEO_BIOS, ni junto al juego o en roms/. La deja elegida en la GUI."""
    if not sysm.caps.get("bios") or bios_path[0] or os.environ.get("NEO_BIOS"):
        return []
    for d in (os.path.dirname(rp), os.path.join(sysm.root, "roms"), "roms"):
        if any(os.path.isfile(os.path.join(d, n)) for n in BIOS_NAMES):
            return []
    out = os.path.join(sysm.root, "build", "neobios.bin")
    src = os.path.join(sysm.root, "tools", "neobios")
    if not os.path.isfile(os.path.join(src, "asm68k.py")):
        return []
    bios_path[0] = out
    if os.path.isfile(out):
        return []
    os.makedirs(os.path.dirname(out), exist_ok=True)
    return [{"argv": [sys.executable, "-I", os.path.join(src, "asm68k.py"), os.path.join(src, "neobios.s"), out, "--swap",
                      "--list", os.path.join(sysm.root, "build", "neobios.lst")], "cwd": sysm.root}]


def set_bios(path):
    path = (path or "").strip().strip('"')
    if path and not os.path.isfile(path):
        raise ValueError("no existe la BIOS: " + path)
    bios_path[0] = os.path.abspath(path) if path else ""
    return {"bios": bios_path[0]}


# --------------------------------------------------------------------------- trabajos

class Job:
    def __init__(self, title, steps, rid=None, kind="", cwd=None):
        with lock:
            job_seq[0] += 1
            self.id = job_seq[0]
        self.title, self.steps, self.rid, self.kind, self.cwd = title, steps, rid, kind, cwd
        self.log, self.size = [], 0
        self.status, self.rc = "running", None
        self.t0, self.t1 = time.time(), None
        self.proc, self.stopped, self.step_i = None, False, 0
        self.step_rc = []
        self.step_cmd = [" ".join(os.path.basename(a) if os.path.isabs(a) else a for a in st["argv"][:2]).replace(EXE, "") for st in steps]

    def emit(self, text):
        with lock:
            self.log.append(text)
            self.size += len(text)

    def text_from(self, pos):
        with lock:
            t = "".join(self.log)
        return t[pos:], len(t)

    def run(self):
        rc = 0
        for i, st in enumerate(self.steps):
            if self.stopped:
                rc = -1
                break
            self.step_i = i
            cwd = st.get("cwd") or self.cwd
            strip = lambda t: t.replace(cwd + os.sep, "") if cwd else t  # noqa: E731
            self.emit(f"\n$ {strip(' '.join(shlex.quote(a) for a in st['argv']))}\n")
            try:
                e = dict(os.environ)
                e.update(bios_env())
                e.update(st.get("env") or {})
                argv, e = host_cmd(st["argv"], e)
                self.proc = subprocess.Popen(argv, cwd=cwd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             text=True, bufsize=1, encoding="utf-8", errors="replace")
                for ln in self.proc.stdout:
                    self.emit(strip(ln))
                rc = self.proc.wait()
            except Exception as ex:  # noqa: BLE001
                self.emit(f"error: {ex}\n")
                rc = 127
            self.step_rc.append(rc)
            if rc != 0:
                break
        self.rc, self.t1 = rc, time.time()
        self.status = "stopped" if self.stopped else ("ok" if rc == 0 else "error")
        self.emit(f"\n[{self.status}] rc={rc} en {self.t1 - self.t0:.1f} s\n")
        self.finish()

    def finish(self):
        """Resumen de lo que importa en state.json (el avance de cada ROM)."""
        if not self.rid:
            return
        text = "".join(self.log)
        rec = {"when": int(time.time()), "status": self.status}
        # La línea RESUMEN de la herramienta (la última): la métrica común de difftest y bench (shared/metrica.h)
        r = (resumenes(text, {"difftest": "difftest", "bench": "bench", "recomp_game": "recomp_game"}.get(self.kind, "")) or [{}])[-1]
        met = r.get("metrica")
        if met:
            rec["pct_nativas"] = met["nativas_pct"]
            rec["ciclos"] = met["ciclos"]
        if self.kind == "difftest":
            if met:
                rec["pct_recomp"] = met["ciclos"]["nativo"]
            if "cuadros" in r or "cuadro" in r:
                rec["frames"] = r.get("cuadros", r.get("cuadro"))      # los comparados, o el de la diferencia
            rec["ok"] = self.status == "ok" and r.get("ok") is True
            update_state(self.rid, difftest=rec)
        elif self.kind == "bench":
            if "x_tiempo_real" in r:
                rec["speed"] = r["x_tiempo_real"]
            update_state(self.rid, bench=rec)
        elif self.kind == "recomp_game":
            rec["ok"] = self.status == "ok" and r.get("evento") == "fin" and r.get("ok") is True
            update_state(self.rid, recomp_game=rec)
        elif self.kind == "recomp":
            update_state(self.rid, recomp=rec)

    def info(self):
        return {"id": self.id, "title": self.title, "rom": self.rid, "kind": self.kind, "status": self.status,
                "rc": self.rc, "t0": self.t0, "t1": self.t1, "step": self.step_i, "steps": len(self.steps), "size": self.size,
                "step_list": [{"cmd": c, "rc": self.step_rc[i] if i < len(self.step_rc) else None} for i, c in enumerate(self.step_cmd)]}


def start_job(title, steps, rid=None, kind="", cwd=None):
    j = Job(title, steps, rid, kind, cwd)
    with lock:
        jobs[j.id] = j
    threading.Thread(target=j.run, daemon=True).start()
    return j


def model_args(p, sysm=None):
    m = p.get("model")
    if not m:
        return []
    if m not in (sysm.models if sysm else ()):
        raise ValueError("modelo inválido")
    return ["--model", m]


def make_job(p):
    kind, rid, traces = p.get("kind"), p.get("rom"), p.get("traces") or []
    if kind in ("build", "test", "git_sync"):
        which = [s for s in systems.values() if p.get("system") in (None, "", s.id)]
        steps = []
        for s in which:
            if kind == "build":
                steps.append({"argv": ["make", "-j8"], "cwd": s.root})
            elif kind == "test":
                steps.append({"argv": s.test_argv(), "cwd": s.root})
            else:
                steps += [{"argv": ["git", "fetch", "--all", "--prune"], "cwd": s.root}, {"argv": ["git", "merge", "--ff-only", "@{u}"], "cwd": s.root}]
        return start_job({"build": "make", "test": "pruebas", "git_sync": "git fetch + fast-forward"}[kind], steps, kind=kind)
    sysm, rp = rom_of(rid)
    proj = sysm.project(base_name(rid))
    if kind == "prepare":
        # Modo fácil: deja la ROM lista para jugar nativa, haciendo solo lo que falta
        # (herramientas, proyecto generado y compilado) y con todas las trazas que ya haya
        traces = [t["name"] for t in list_traces(rid)]
    name = safe_name(p.get("name") or proj["name"])
    pdir = sysm.project_dir(name)
    tflags = [x for t in trace_files(rid, traces) for x in ("--trace", t)] + ["--traces-dir", ALMACEN]
    inp = resolve_input(rid, p.get("input"))
    frames = lambda d: pint(p.get("frames") or d, 1, 2000000, "cuadros")  # noqa: E731
    cwd = sysm.root
    if kind == "prepare":
        steps = []
        if not all(sysm.tools_status().values()):
            steps.append({"argv": ["make", "-j8"], "cwd": sysm.root})
        if not proj["built"]:
            steps += bios_steps(sysm, rp)
            steps += [{"argv": sysm.recomp_argv(rp, pdir, name, tflags)}, {"argv": ["make", "-C", pdir, "-j8"]}]
        return start_job(f"preparar {name}", steps, rid, "prepare", cwd)
    if kind == "recomp":
        steps = bios_steps(sysm, rp) + [{"argv": sysm.recomp_argv(rp, pdir, name, tflags)}]
        if p.get("compile", True):
            steps.append({"argv": ["make", "-C", pdir, "-j8"]})
        return start_job(f"recompilar {name}", steps, rid, "recomp", cwd)
    if kind == "difftest":
        argv = [os.path.join(pdir, "difftest" + EXE), rp, "--frames", str(frames(1500))]
        if str(p.get("seed", "")).strip():
            argv += ["--seed", str(pint(p["seed"], 0, 2**31, "semilla"))]
        if inp:
            argv += ["--input", inp]
        if sysm.caps["model"]:
            argv += model_args(p, sysm)
        if p.get("uncovered"):
            os.makedirs(trace_dir(rid), exist_ok=True)
            argv += ["--uncovered", os.path.join(trace_dir(rid), safe_name(p.get("uncovered_name") or "difftest") + ".trace")]
        return start_job(f"difftest {name}", [{"argv": argv}], rid, "difftest", cwd)
    if kind == "bench":
        argv = sysm.bench_argv(rp, pdir, frames(3000), pint(p.get("repeat") or 3, 1, 20, "repeticiones"), inp,
                               (model_args(p, sysm) or [None, None])[1], bool(p.get("recompiled")))
        return start_job("bench", [{"argv": argv}], rid, "bench", cwd)
    if kind == "recomp_game":
        env = {"NOTRACE": "1"} if p.get("notrace") and sysm.caps["notrace"] else {}
        env["RETROCOMP_TRAZAS"] = ALMACEN
        if inp:     # unas «Entradas» elegidas (p. ej. una partida grabada) sustituyen a las guionadas del ciclo
            env.update(INPUT_TRACE=posix_path(inp), INPUT_DIFFTEST=posix_path(inp))
        return start_job(f"ciclo completo {name}", bios_steps(sysm, rp) + [{"argv": sysm.game_argv(rp, name, frames(6000)), "env": env}],
                         rid, "recomp_game", cwd)
    if kind in ("trace", "screenshot", "audio"):
        tn = safe_name(p.get("trace_name") or "guiada")
        out = {"trace": os.path.join(trace_dir(rid), (tn[:-6] if tn.endswith(".trace") else tn) + ".trace"),
               "screenshot": os.path.join(WORK, f"shot_{stem(rid.replace(':', '_'))}.png"),
               "audio": os.path.join(WORK, f"audio_{stem(rid.replace(':', '_'))}.wav")}[kind]
        os.makedirs(os.path.dirname(out), exist_ok=True)
        tool = sysm.trace_tool(kind)
        argv = [sysm.tool(tool), rp, out, "--frames", str(frames(600))]
        if inp:
            argv += ["--input", inp]
        elif str(p.get("seed", "")).strip():
            argv += ["--seed", str(pint(p["seed"], 0, 2**31, "semilla"))]
        if sysm.caps["model"]:
            argv += model_args(p, sysm)
        return start_job(f"{tool} {base_name(rid)}", [{"argv": argv}], rid, kind, cwd)
    if kind == "visor":
        os.makedirs(WORK, exist_ok=True)
        cov = (trace_files(rid, [p.get("coverage")]) or [None])[0] if p.get("coverage") else None
        argv, out = sysm.visor(rp, tflags, cov)
        return start_job("visor / eventos", [{"argv": argv}], rid, "visor", cwd)
    raise ValueError("trabajo desconocido: " + str(kind))


def play(p):
    rid = p.get("rom")
    sysm, rp = rom_of(rid)
    proj = sysm.project(base_name(rid))
    env = dict(os.environ)
    env.update(bios_env())
    trace = rec_in = None
    if p.get("record_trace"):
        os.makedirs(trace_dir(rid), exist_ok=True)
        trace = os.path.join(trace_dir(rid), safe_name(p.get("trace_name") or "jugada") + ".trace")
    if p.get("record_input"):
        os.makedirs(input_dir(rid), exist_ok=True)
        rec_in = os.path.join(input_dir(rid), safe_name(p.get("input_name") or "jugada") + ".txt")
    argv = sysm.play_argv(rp, proj, bool(p.get("interp")), (model_args(p, sysm) or [None, None])[1], env, trace, rec_in,
                          resolve_input(rid, p.get("input")))
    # Iguales en los cuatro (retrorecomp#43); un ejecutable anterior tomaría la opción como la ROM
    if p.get("fullscreen"):
        argv.append("--fullscreen")
    if p.get("no_menu"):
        argv.append("--no-menu")
    subprocess.Popen(argv, cwd=sysm.root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {"ok": True, "trace": trace}


def merge_traces(rid, names, out_name):
    files = trace_files(rid, names)
    if len(files) < 2:
        raise ValueError("elija al menos dos trazas")
    lines = set()
    for f in files:
        with open(f, encoding="utf-8") as fh:
            lines |= {l.strip() for l in fh if l.strip()}
    out = os.path.join(trace_dir(rid), safe_name(out_name) + ".trace")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(lines)) + "\n")
    return len(lines)


def share_traces(rid, names, alias):
    """Una aportación para el repositorio de trazas compartidas (#16, docs/TRAZAS.md): trazas.py compartir junta las marcadas
    en build/gui/compartir/<sistema>/<SHA1>/ y esa carpeta queda en un .zip. No sube nada"""
    files = trace_files(rid, names)
    if not files:
        raise ValueError("marque al menos una traza")
    sysm, rp = rom_of(rid)
    dest = os.path.join(WORK, "compartir")
    argv = [sys.executable, os.path.join(HERE, "shared", "trazas.py"), "compartir", "--dir", dest]
    if alias:
        argv += ["--alias", alias]
    r = subprocess.run(argv + [rp, *files], cwd=sysm.root, capture_output=True, text=True, encoding="utf-8", errors="replace")
    m = re.search(r"^(\w+)/([0-9a-f]{40}): ", r.stdout, re.M)
    if r.returncode != 0 or not m:
        raise ValueError((r.stderr or r.stdout).strip()[-300:] or "trazas.py compartir falló")
    carpeta = os.path.join(dest, m.group(1), m.group(2))
    zip_path = os.path.join(dest, f"{m.group(1)}-{m.group(2)[:12]}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for n in sorted(os.listdir(carpeta)):
            z.write(os.path.join(carpeta, n), f"{m.group(1)}/{m.group(2)}/{n}")
    return {"zip": zip_path}


def api_map(rid, traces, coverage):
    sysm, rp = rom_of(rid)
    tf = trace_files(rid, traces)
    cov = (trace_files(rid, [coverage]) or [None])[0] if coverage else None
    m = sysm.map(rp, tf, cov)
    m["legend"] = list(sysm.legend)
    m["system"] = sysm.id
    return m


def api_cell(rid, traces, bank_i, ci):
    sysm, rp = rom_of(rid)
    n = sysm.info(rp).get("banks", 2)
    if bank_i < 0 or bank_i >= max(n, 1) or not 0 <= ci < BANK // CELL:
        raise ValueError("celda fuera de rango")
    # NES, Genesis y Master System: una fila por cada 16 KB (de PRG o de ROM); GB: una por banco (el banco 0 en $0000, los demás en
    # $4000); Neo Geo: NEO.cell busca con el tamaño de celda de su mapa
    banks = [{"bank": b, "base": b * BANK if sysm.id in ("nes", "gen", "neo", "sms") else (0 if b == 0 else 0x4000)} for b in range(n)]
    return sysm.cell(rp, trace_files(rid, traces), bank_i, ci, banks)


# --------------------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "RetroRecompGUI"

    def log_message(self, *a):
        pass

    def _ok_host(self):
        return (self.headers.get("Host") or "").split(":")[0] in ("127.0.0.1", "localhost", "[::1]")

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _auth(self, q=None):
        tok = self.headers.get("X-Token") or (q or {}).get("t", [""])[0]
        return secrets.compare_digest(tok, cfg["token"])

    def do_GET(self):
        if not self._ok_host():
            return self._send(403, {"error": "host"})
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/":
            if not self._auth(q):
                return self._send(403, "Abra la dirección que imprime el programa (lleva ?t=...).", "text/plain; charset=utf-8")
            page = open(os.path.join(HERE, "index.html"), encoding="utf-8").read()
            return self._send(200, page.replace("__TOKEN__", cfg["token"]), "text/html; charset=utf-8")
        if not self._auth(q):
            return self._send(403, {"error": "token"})
        try:
            return self._get(u.path, q)
        except Exception as e:  # noqa: BLE001
            return self._send(400, {"error": str(e)})

    def _file(self, path, ctype, missing):
        if not os.path.isfile(path):
            return self._send(404, missing, "text/plain; charset=utf-8")
        with open(path, "rb") as f:
            return self._send(200, f.read(), ctype)

    def _get(self, path, q):
        one = lambda k, d="": q.get(k, [d])[0]  # noqa: E731
        if path == "/api/state":
            rid = one("rom") or None
            st = {"systems": {s.id: {"label": s.label, "short": s.reg["sigla"], "exts": list(s.exts), "root": s.root, "git": s.git(), "tools": s.tools_status(), "caps": s.caps,
                                     "bench_tool": s.bench_tool, "trace_tool": s.trace_tool("trace"), "map_note": s.map_note, "input_note": s.input_note,
                                     "legend": list(s.legend), "presets": s.presets, "models": list(s.models)} for s in systems.values()},
                  "roms": list_roms(), "progress": load_state(), "bios": bios_path[0],
                  "jobs": [j.info() for j in sorted(jobs.values(), key=lambda j: -j.id)[:30]]}
            if rid:
                s, rp = rom_of(rid)
                st.update(traces=list_traces(rid), inputs=list_inputs(rid), project=s.project(base_name(rid)),
                          rom_info=s.info(rp), system=s.id)
            return self._send(200, st)
        if path == "/api/map":
            return self._send(200, api_map(one("rom"), q.get("trace", []), one("coverage")))
        if path == "/api/cell":
            return self._send(200, api_cell(one("rom"), q.get("trace", []), int(one("bank", "0")), int(one("ci", "0"))))
        if path == "/api/why":
            s, rp = rom_of(one("rom"))
            return self._send(200, {"text": s.why(rp, trace_files(one("rom"), q.get("trace", [])), one("arg"))})
        if path == "/api/input":
            with open(resolve_input(one("rom"), one("name")), encoding="utf-8") as f:
                return self._send(200, {"text": f.read()})
        if path == "/api/job":
            j = jobs.get(int(one("id", "0")))
            if not j:
                return self._send(404, {"error": "no existe"})
            text, pos = j.text_from(int(one("from", "0")))
            return self._send(200, {**j.info(), "text": text, "pos": pos})
        rid = one("rom")
        key = stem(rid.replace(":", "_")) if rid else ""
        if path == "/files/shot":
            return self._file(os.path.join(WORK, f"shot_{key}.png"), "image/png", "sin captura")
        if path == "/files/audio":
            return self._file(os.path.join(WORK, f"audio_{key}.wav"), "audio/wav", "sin audio")
        if path == "/files/visor":
            s, rp = rom_of(rid)
            return self._file(os.path.join(WORK, f"visor_{s.id}_{stem(os.path.basename(rp))}.html"), "text/html; charset=utf-8", "genere primero el visor")
        return self._send(404, {"error": "no existe"})

    def do_POST(self):
        if not self._ok_host() or not self._auth():
            return self._send(403, {"error": "token"})
        try:
            p = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
            path = urlparse(self.path).path
            if path == "/api/job":
                return self._send(200, make_job(p).info())
            if path == "/api/job/stop":
                j = jobs.get(int(p.get("id", 0)))
                if j and j.status == "running":
                    j.stopped = True
                    if j.proc:
                        j.proc.terminate()
                return self._send(200, {"ok": True})
            if path == "/api/bios":
                return self._send(200, set_bios(p.get("path")))
            if path == "/api/play":
                return self._send(200, play(p))
            if path == "/api/input":
                return self._send(200, {"name": save_input(p["rom"], p.get("name") or "mia", p.get("text") or "")})
            if path == "/api/input/delete":
                f = resolve_input(p["rom"], p["name"])
                if p["name"].startswith("mia:"):
                    os.remove(f)
                return self._send(200, {"ok": True})
            if path == "/api/trace/merge":
                return self._send(200, {"lines": merge_traces(p["rom"], p.get("names"), p.get("out") or "unida")})
            if path == "/api/trace/share":
                return self._send(200, share_traces(p["rom"], p.get("names"), p.get("alias") or ""))
            if path == "/api/trace/delete":
                for f in trace_files(p["rom"], [p["name"]]):
                    os.remove(f)
                return self._send(200, {"ok": True})
            return self._send(404, {"error": "no existe"})
        except Exception as e:  # noqa: BLE001
            return self._send(400, {"error": str(e)})


CLASES = {c.id: c for c in (NES, GB, GEN, NEO, SMS)}     # el comportamiento de cada entrada del registro


def main():
    nombres = ", ".join(r["repo"] for r in REGISTRO.values())
    ap = argparse.ArgumentParser(description="GUI local para " + nombres)
    for sid, r in REGISTRO.items():
        ap.add_argument("--" + sid, help="carpeta de " + r["repo"])
    ap.add_argument("--roms", action="append", default=[], help="carpeta extra con ROMs (se puede repetir)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--msys", help="Windows: carpeta de MSYS2 (por omisión MSYS2_ROOT o C:\\msys64); make y sh corren ahí, en UCRT64")
    a = ap.parse_args()
    if os.name == "nt":
        MSYS["root"] = find_msys(a.msys)
    for sid, r in REGISTRO.items():
        root = find_repo(r["repo"], r["repo"].upper() + "_DIR", getattr(a, sid), os.path.join("src", sid + "recomp", sid + "recomp.c"))
        if root and sid in CLASES:
            systems[sid] = CLASES[sid](root)
    if not systems:
        sys.exit(f"no encontré {nombres}: use " + "/".join("--" + sid for sid in REGISTRO) + " o " +
                 "/".join(r["repo"].upper() + "_DIR" for r in REGISTRO.values()))
    cfg["extra_roms"] = [os.path.abspath(r) for r in a.roms]
    cfg["token"] = secrets.token_urlsafe(16)
    os.makedirs(WORK, exist_ok=True)
    srv = None
    for port in range(a.port, a.port + 20):
        try:
            srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
            break
        except OSError:
            continue
    if not srv:
        sys.exit("no hay un puerto libre")
    url = f"http://127.0.0.1:{srv.server_address[1]}/?t={cfg['token']}"
    print(f"retrorecomp: {url}", flush=True)
    for s in systems.values():
        print(f"  {s.label}: {s.root}", flush=True)
    if os.name == "nt":
        print(f"  MSYS2 (make, sh): {MSYS['root']}" if MSYS["root"] else
              "  aviso: no encontré MSYS2 (use --msys DIR o MSYS2_ROOT): «Compilar herramientas», las pruebas y el "
              "ciclo completo necesitan make y sh de MSYS2 UCRT64", flush=True)
    if not a.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    for j in jobs.values():
        if j.status == "running" and j.proc:
            j.proc.terminate()


if __name__ == "__main__":
    main()
