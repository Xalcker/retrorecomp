#!/usr/bin/env python3
"""retrorecomp: una sola GUI local para NESRecomp y GBRecomp (solo biblioteca estándar de Python).

Una página servida en 127.0.0.1 que detecta el sistema de cada ROM (NES, Game Boy, Game Boy Color,
Super Game Boy) y llama a las herramientas del recompilador que corresponda: mapa del descubrimiento
con clic por celda, avance por ROM, trazas, guiones de entradas, recompilar, difftest, bench y el
ciclo completo, con la salida en vivo.

    python3 retrorecomp_gui.py [--nes DIR] [--gb DIR] [--roms DIR ...] [--port 8765] [--no-browser]

Dónde busca los recompiladores: --nes/--gb, las variables NESRECOMP_DIR/GBRECOMP_DIR, la carpeta
que contiene a retrorecomp si es uno de ellos (submódulo) o sus hermanas NESRecomp y GBRecomp.
"""
import argparse, glob, json, os, re, secrets, shlex, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if os.name == "nt" else ""
WORK = os.path.join(HERE, "build", "gui")
STATE_FILE = os.path.join(WORK, "state.json")
CELL = 256
BANK = 0x4000

lock = threading.RLock()
jobs, job_seq = {}, [0]
cfg = {"token": "", "extra_roms": []}
events_cache = {}
systems = {}          # "nes" / "gb" -> System


# --------------------------------------------------------------------------- utilidades

def run_quiet(argv, cwd, timeout=60):
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=timeout, errors="replace")
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except Exception as e:  # noqa: BLE001
        return 127, str(e)


def stem(rom):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.splitext(rom)[0]).strip("_") or "rom"


def short_name(rom):
    base = re.sub(r"\(.*?\)|\[.*?\]", "", os.path.splitext(rom)[0])
    return re.sub(r"[^a-z0-9]+", "", base.lower())[:16] or "juego"


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


PAD = r"(-|\$?[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|(A|B|SELECT|START|RIGHT|LEFT|UP|DOWN)(\+(A|B|SELECT|START|RIGHT|LEFT|UP|DOWN))*)"
LINE_OK = re.compile(r"^(#.*|modelo (dmg|cgb|sgb)|paleta [A-Za-z+]+|\d+\s+" + PAD + r"(\s+" + PAD + r")?)?$", re.I)


# --------------------------------------------------------------------------- sistemas

class System:
    """Lo que cambia entre recompiladores; el resto de la GUI es común."""
    id = ""
    label = ""
    exts = ()
    recomp = ""                      # ejecutable del recompilador
    tools = ()                       # (nombre, ¿qué hace)
    legend = ()                      # nombres de las categorías del mapa
    caps = {}

    def __init__(self, root):
        self.root = root

    def tool(self, name):
        return os.path.join(self.root, name + EXE)

    def run(self, argv, timeout=60):
        return run_quiet(argv, self.root, timeout)

    def tools_status(self):
        return {n: os.path.isfile(self.tool(n)) for n in self.tools}

    def roms_dir(self):
        return os.path.join(self.root, "roms")

    def project_dir(self, name):
        raise NotImplementedError

    def project(self, rom):
        name = short_name(rom)
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
    id, label, exts = "nes", "NES", (".nes",)
    recomp = "nesrecomp"
    tools = ("nesrecomp", "nes", "nestrace", "nesshot", "nesaudio", "nesbench", "nestest", "ramdump")
    legend = ("flujo desde los vectores", "traza", "tabla de saltos", "especulativo (todos los bancos)")
    caps = {"notrace": True, "model": False, "interp_play": False, "plays_interp": True, "visor": "html", "pad2": True}
    ORG_COLOR = {46: 0, 51: 1, 226: 2, 201: 3}
    ANSI = re.compile(r"\x1b\[([0-9;]*)m(.)\x1b\[0m|(.)")

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
        argv = [self.tool("nesrecomp"), rom_path, "--viz", "--color"]
        for t in traces:
            argv += ["--trace", t]
        if coverage:
            argv += ["--coverage", coverage]
        rc, out = self.run(argv, 120)
        if rc != 0:
            raise RuntimeError(out.strip() or "falló nesrecomp")
        banks, stats = [], {}
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
                continue
            m = re.search(r"mapper (\d+), PRG (\d+) KB, CHR (\d+) KB", ln)
            if m:
                stats.update(mapper=f"mapper {m.group(1)}", banks=int(m.group(2)) // 16, size=int(m.group(2)) * 1024)
            m = re.search(r"código: (\d+) instrucciones \((\d+) bytes, ([\d.]+)% del PRG\)", ln)
            if m:
                stats.update(instr=int(m.group(1)), bytes=int(m.group(2)), pct=float(m.group(3)))
            m = re.search(r"cobertura: (\d+) de (\d+) instrucciones ejecutadas descubiertas sin traza \(([\d.]+)% *\)", ln)
            if m:
                tot = int(m.group(2))
                stats.update(cov_total=tot, cov_missing=tot - int(m.group(1)), cov_pct=float(m.group(3)))
            m = re.search(r"(\d+) de (\d+) celdas con código", ln)
            if m:
                stats.update(cells_code=int(m.group(1)), cells=int(m.group(2)))
        return {"banks": banks, "stats": stats}

    # -- instrucciones de una celda (de --events)
    def events(self, rom_path, traces):
        tmp = os.path.join(WORK, f"events_{secrets.token_hex(4)}.txt")
        os.makedirs(WORK, exist_ok=True)
        argv = [self.tool("nesrecomp"), rom_path, "--events", tmp]
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
        return {"title": f"PRG ${lo:06X}–${lo + CELL - 1:06X}",
                "events": [{"addr": "$" + e["addr"], "org": e["org"], "dis": e["dis"], "why": "%X" % e["pos"]} for e in items]}

    def why(self, rom_path, traces, arg):
        if not re.fullmatch(r"[0-9A-Fa-f]{1,8}", arg or ""):
            raise ValueError("desplazamiento inválido")
        argv = [self.tool("nesrecomp"), rom_path, "--why", arg]
        for t in traces:
            argv += ["--trace", t]
        return re.sub(r"^.*mapper.*\n.*\n", "", self.run(argv, 60)[1])

    # -- comandos
    def recomp_argv(self, rp, pdir, name, tflags):
        return [self.tool("nesrecomp"), rp, "-o", pdir, "--name", name, *tflags]

    def trace_tool(self, kind):
        return {"trace": "nestrace", "screenshot": "nesshot", "audio": "nesaudio"}[kind]

    def bench_argv(self, rp, pdir, frames, repeat, inp, model, recompiled):
        exe = os.path.join(pdir, "bench" + EXE) if recompiled else self.tool("nesbench")
        argv = [exe, rp, "--frames", str(frames), "--repeat", str(repeat)]
        return argv + (["--input", inp] if inp else [])

    def game_argv(self, rp, name, frames):
        return ["sh", "tests/recomp_game.sh", rp, name]

    def test_argv(self):
        return ["make", "test"]

    def visor(self, rp, tflags, cov):
        out = os.path.join(WORK, f"visor_{self.id}_{stem(os.path.basename(rp))}.html")
        argv = [self.tool("nesrecomp"), rp, "--html", out, *tflags]
        if cov:
            argv += ["--coverage", cov]
        return argv, out

    def play_argv(self, rp, proj, interp, model, env, record_trace_path, record_input_path, play_input):
        if interp or not proj["built"]:
            exe = self.tool("nes")
            if not os.path.isfile(exe):
                raise ValueError("falta ./nes (Compilar herramientas)")
        else:
            exe = os.path.join(proj["dir"], proj["name"] + EXE)
        argv = [exe, rp]
        if record_trace_path:
            if exe == self.tool("nes"):
                env["NES_TRACE"] = record_trace_path
            else:
                raise ValueError("el juego recompilado no graba trazas: use «todo con el intérprete»")
        if record_input_path:
            argv += ["--record", record_input_path]
        if play_input:
            argv += ["--play", play_input]
        return argv


class GB(System):
    id, label, exts = "gb", "Game Boy", (".gb", ".gbc", ".sgb")
    recomp = "gbrecomp"
    tools = ("gbrecomp", "gb", "gbtrace", "gbshot", "gbaudio", "gbbench", "gbtest", "gbramdump", "linktest", "vgmcheck")
    legend = ("arranque", "interrupción", "tabla de saltos", "traza", "argumentos en línea (≡)")
    caps = {"notrace": False, "model": True, "interp_play": True, "plays_interp": False, "visor": "events", "pad2": False}
    MBC = {0x00: "ROM", 0x01: "MBC1", 0x02: "MBC1+RAM", 0x03: "MBC1+RAM+BAT", 0x05: "MBC2", 0x06: "MBC2+BAT",
           0x0F: "MBC3+TIMER+BAT", 0x10: "MBC3+TIMER+RAM+BAT", 0x11: "MBC3", 0x12: "MBC3+RAM", 0x13: "MBC3+RAM+BAT",
           0x19: "MBC5", 0x1A: "MBC5+RAM", 0x1B: "MBC5+RAM+BAT", 0x1C: "MBC5+RUMBLE", 0x1E: "MBC5+RUMBLE+RAM+BAT",
           0x22: "MBC7", 0xFC: "Pocket Camera", 0xFD: "TAMA5", 0xFE: "HuC3", 0xFF: "HuC1"}

    def project_dir(self, name):
        return os.path.join(self.root, "out", name)

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
        if recompiled:
            return [os.path.join(pdir, "difftest" + EXE), rp, "--frames", str(frames), "--bench"]
        argv = [self.tool("gbbench"), rp, "--frames", str(frames), "--repeat", str(repeat)]
        return argv + (["--input", inp] if inp else []) + (["--model", model] if model else [])

    def game_argv(self, rp, name, frames):
        return ["sh", "tests/recomp_game.sh", rp, name, str(frames)]

    def test_argv(self):
        return ["sh", "tests/run_tests.sh"]

    def visor(self, rp, tflags, cov):
        out = os.path.join(WORK, f"eventos_{self.id}_{stem(os.path.basename(rp))}.txt")
        return [self.tool("gbrecomp"), rp, "--why", "00:0000", "--events", out, *tflags], out

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
    cands = [explicit, os.environ.get(env)]
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
            d = f.read(0x150)
    except OSError:
        return None
    if d[:4] == b"NES\x1a":
        return "nes"
    if len(d) >= 0x134 and d[0x104:0x108] == bytes([0xCE, 0xED, 0x66, 0x66]):
        return "gb"
    ext = os.path.splitext(path)[1].lower()
    return "nes" if ext == ".nes" else "gb" if ext in (".gb", ".gbc", ".sgb") else None


ROMS = {}   # id -> (system, ruta)


def list_roms():
    found = {}
    for sysm in systems.values():
        dirs = [sysm.roms_dir()] + cfg["extra_roms"]
        for d in dirs:
            for p in glob.glob(os.path.join(d, "*")):
                if os.path.isfile(p) and os.path.splitext(p)[1].lower() in sysm.exts and detect_system(p) == sysm.id:
                    found[f"{sysm.id}:{os.path.basename(p)}"] = (sysm, p)
    ROMS.clear()
    ROMS.update(found)
    return [{"id": i, "system": s.id, "label": s.label, "name": os.path.basename(p), "size": os.path.getsize(p)}
            for i, (s, p) in sorted(found.items(), key=lambda x: x[1][1].lower())]


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
        if not LINE_OK.match(l.strip()) or (sysm.id == "nes" and re.match(r"(modelo|paleta)\b", l.strip(), re.I)):
            raise ValueError(f"línea {i} inválida: {l[:40]} (use «cuadro BOTONES», p. ej. 300 START+A, o «-»)")
    os.makedirs(input_dir(rid), exist_ok=True)
    with open(os.path.join(input_dir(rid), name + ".txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return "mia:" + name + ".txt"


PRESETS = {
    "nes": {"smb": "0 -\n120 START\n125 -\n300 RIGHT\n", "titulo": "0 -\n120 START\n127 -\n300 START\n307 -\n400 A\n407 -\n"},
    "gb": {"partida1": "modelo dmg\n0 -\n122 START\n129 -\n550 START\n557 -\n601 RIGHT\n609 -\n",
           "titulo": "0 -\n120 START\n127 -\n300 START\n307 -\n400 A\n407 -\n"},
}


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
                e.update(st.get("env") or {})
                self.proc = subprocess.Popen(st["argv"], cwd=cwd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             text=True, bufsize=1, errors="replace")
                for ln in self.proc.stdout:
                    self.emit(strip(ln))
                rc = self.proc.wait()
            except Exception as ex:  # noqa: BLE001
                self.emit(f"error: {ex}\n")
                rc = 127
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
        if self.kind == "difftest":
            m = re.search(r"([\d.]+)% (recompilado|de los ciclos)", text)
            if m:
                rec["pct_recomp"] = float(m.group(1))
            m = re.search(r"(\d+) cuadros", text)
            if m:
                rec["frames"] = int(m.group(1))
            rec["ok"] = self.status == "ok" and ("Sin diferencias" in text or "cuadros iguales" in text)
            update_state(self.rid, difftest=rec)
        elif self.kind == "bench":
            m = re.findall(r"([\d.]+)x tiempo real", text)
            if m:
                rec["speed"] = float(m[0])
            update_state(self.rid, bench=rec)
        elif self.kind == "recomp_game":
            rec["ok"] = self.status == "ok" and "Sin diferencias" in text
            update_state(self.rid, recomp_game=rec)
        elif self.kind == "recomp":
            update_state(self.rid, recomp=rec)

    def info(self):
        return {"id": self.id, "title": self.title, "rom": self.rid, "kind": self.kind, "status": self.status,
                "rc": self.rc, "t0": self.t0, "t1": self.t1, "step": self.step_i, "steps": len(self.steps), "size": self.size}


def start_job(title, steps, rid=None, kind="", cwd=None):
    j = Job(title, steps, rid, kind, cwd)
    with lock:
        jobs[j.id] = j
    threading.Thread(target=j.run, daemon=True).start()
    return j


def model_args(p):
    m = p.get("model")
    if not m:
        return []
    if m not in ("dmg", "cgb", "sgb"):
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
    name = safe_name(p.get("name") or proj["name"])
    pdir = sysm.project_dir(name)
    tflags = [x for t in trace_files(rid, traces) for x in ("--trace", t)]
    inp = resolve_input(rid, p.get("input"))
    frames = lambda d: pint(p.get("frames") or d, 1, 2000000, "cuadros")  # noqa: E731
    cwd = sysm.root
    if kind == "recomp":
        steps = [{"argv": sysm.recomp_argv(rp, pdir, name, tflags)}]
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
            argv += model_args(p)
        if p.get("uncovered"):
            os.makedirs(trace_dir(rid), exist_ok=True)
            argv += ["--uncovered", os.path.join(trace_dir(rid), safe_name(p.get("uncovered_name") or "difftest") + ".trace")]
        return start_job(f"difftest {name}", [{"argv": argv}], rid, "difftest", cwd)
    if kind == "bench":
        argv = sysm.bench_argv(rp, pdir, frames(3000), pint(p.get("repeat") or 3, 1, 20, "repeticiones"), inp,
                               (model_args(p) or [None, None])[1], bool(p.get("recompiled")))
        return start_job("bench", [{"argv": argv}], rid, "bench", cwd)
    if kind == "recomp_game":
        env = {"NOTRACE": "1"} if p.get("notrace") and sysm.caps["notrace"] else {}
        return start_job(f"ciclo completo {name}", [{"argv": sysm.game_argv(rp, name, frames(6000)), "env": env}], rid, "recomp_game", cwd)
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
            argv += model_args(p)
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
    trace = rec_in = None
    if p.get("record_trace"):
        os.makedirs(trace_dir(rid), exist_ok=True)
        trace = os.path.join(trace_dir(rid), safe_name(p.get("trace_name") or "jugada") + ".trace")
    if p.get("record_input"):
        os.makedirs(input_dir(rid), exist_ok=True)
        rec_in = os.path.join(input_dir(rid), safe_name(p.get("input_name") or "jugada") + ".txt")
    argv = sysm.play_argv(rp, proj, bool(p.get("interp")), (model_args(p) or [None, None])[1], env, trace, rec_in,
                          resolve_input(rid, p.get("input")))
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
    # NES: una fila por cada 16 KB de PRG; GB: una por banco (el banco 0 en $0000, los demás en $4000)
    banks = [{"bank": b, "base": b * BANK if sysm.id == "nes" else (0 if b == 0 else 0x4000)} for b in range(n)]
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
            st = {"systems": {s.id: {"label": s.label, "root": s.root, "git": s.git(), "tools": s.tools_status(), "caps": s.caps,
                                     "legend": list(s.legend), "presets": PRESETS[s.id]} for s in systems.values()},
                  "roms": list_roms(), "progress": load_state(),
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
            if s.caps["visor"] == "html":
                return self._file(os.path.join(WORK, f"visor_{s.id}_{stem(os.path.basename(rp))}.html"), "text/html; charset=utf-8", "genere primero el visor")
            return self._file(os.path.join(s.root, "tools", "visor.html"), "text/html; charset=utf-8", "este recompilador no trae visor")
        if path == "/files/events":
            s, rp = rom_of(rid)
            return self._file(os.path.join(WORK, f"eventos_{s.id}_{stem(os.path.basename(rp))}.txt"), "text/plain; charset=utf-8", "genere primero los eventos")
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
            if path == "/api/trace/delete":
                for f in trace_files(p["rom"], [p["name"]]):
                    os.remove(f)
                return self._send(200, {"ok": True})
            return self._send(404, {"error": "no existe"})
        except Exception as e:  # noqa: BLE001
            return self._send(400, {"error": str(e)})


def main():
    ap = argparse.ArgumentParser(description="GUI local para NESRecomp y GBRecomp")
    ap.add_argument("--nes", help="carpeta de NESRecomp")
    ap.add_argument("--gb", help="carpeta de GBRecomp")
    ap.add_argument("--roms", action="append", default=[], help="carpeta extra con ROMs (se puede repetir)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    nes = find_repo("NESRecomp", "NESRECOMP_DIR", a.nes, os.path.join("src", "nesrecomp", "nesrecomp.c"))
    gb = find_repo("GBRecomp", "GBRECOMP_DIR", a.gb, os.path.join("src", "gbrecomp", "gbrecomp.c"))
    if nes:
        systems["nes"] = NES(nes)
    if gb:
        systems["gb"] = GB(gb)
    if not systems:
        sys.exit("no encontré NESRecomp ni GBRecomp: use --nes/--gb o NESRECOMP_DIR/GBRECOMP_DIR")
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
