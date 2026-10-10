#!/usr/bin/env python3
"""GUI local de GBRecomp: una página servida en 127.0.0.1 (solo biblioteca estándar de Python).

Muestra el avance del descubrimiento (un mapa por banco como el de `gbrecomp --viz`, con clic por celda
y la cadena de `--why`), corre las herramientas (gbrecomp, difftest, gbbench, gbtest, tests/recomp_game.sh,
el juego recompilado...) y muestra su salida en vivo.

    python3 tools/gui/gbrecomp_gui.py [--roms carpeta] [--port 8766] [--no-browser]

Las herramientas se buscan en la raíz del repositorio (`make` las compila desde la propia GUI).
"""
import argparse, glob, json, os, re, secrets, shlex, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = ".exe" if os.name == "nt" else ""
WORK = os.path.join(ROOT, "build", "gui")
TRACES = os.path.join(WORK, "traces")
INPUTS = os.path.join(WORK, "inputs")
STATE_FILE = os.path.join(WORK, "state.json")
ROM_GLOBS = ("*.gb", "*.gbc", "*.sgb")

ORG_NAMES = ["arranque", "interrupción", "tabla", "traza"]
CAT_DATA = 4          # argumentos en línea tras una llamada (≡ en --viz)
CELL = 256            # bytes por celda del mapa
BANK_SIZE = 0x4000

MBC = {0x00: "ROM", 0x01: "MBC1", 0x02: "MBC1+RAM", 0x03: "MBC1+RAM+BAT", 0x05: "MBC2", 0x06: "MBC2+BAT",
       0x0F: "MBC3+TIMER+BAT", 0x10: "MBC3+TIMER+RAM+BAT", 0x11: "MBC3", 0x12: "MBC3+RAM", 0x13: "MBC3+RAM+BAT",
       0x19: "MBC5", 0x1A: "MBC5+RAM", 0x1B: "MBC5+RAM+BAT", 0x1C: "MBC5+RUMBLE", 0x1E: "MBC5+RUMBLE+RAM+BAT",
       0x22: "MBC7", 0xFC: "Pocket Camera", 0xFD: "TAMA5", 0xFE: "HuC3", 0xFF: "HuC1"}

PRESETS = {
    "partida1": "modelo dmg\n0 -\n122 START\n129 -\n550 START\n557 -\n601 RIGHT\n609 -\n",
    "titulo": "0 -\n120 START\n127 -\n300 START\n307 -\n400 A\n407 -\n",
}

lock = threading.RLock()
jobs = {}
job_seq = [0]
cfg = {"roms": os.path.join(ROOT, "roms"), "token": ""}
events_cache = {}


def tool(name):
    return os.path.join(ROOT, name + EXE)


def run_quiet(argv, cwd=ROOT, timeout=60):
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=timeout, errors="replace")
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except Exception as e:  # noqa: BLE001
        return 127, str(e)


# --------------------------------------------------------------------------- estado persistente

def load_state():
    try:
        with open(STATE_FILE, encoding="utf-8") as f:
            return json.load(f)
    except Exception:  # noqa: BLE001
        return {}


def update_state(rom, **kv):
    with lock:
        st = load_state()
        st.setdefault(rom, {}).update(kv)
        os.makedirs(WORK, exist_ok=True)
        tmp = STATE_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(st, f, ensure_ascii=False, indent=1)
        os.replace(tmp, STATE_FILE)


# --------------------------------------------------------------------------- ROMs, trazas, entradas, proyectos

def stem(rom):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.splitext(rom)[0]).strip("_") or "rom"


def short_name(rom):
    base = re.sub(r"\(.*?\)|\[.*?\]", "", os.path.splitext(rom)[0])
    return re.sub(r"[^a-z0-9]+", "", base.lower())[:16] or "juego"


def rom_path(rom):
    if not rom or os.path.basename(rom) != rom:
        raise ValueError("ROM inválida")
    p = os.path.join(cfg["roms"], rom)
    if not os.path.isfile(p):
        raise ValueError("no existe la ROM " + rom)
    return p


def rom_info(rom):
    """Título, tipo de cartucho y bancos, leídos de la cabecera."""
    try:
        with open(rom_path(rom), "rb") as f:
            d = f.read(0x150)
        size = os.path.getsize(rom_path(rom))
    except (OSError, ValueError):
        return {}
    title = d[0x134:0x144].split(b"\0")[0].decode("ascii", "replace").strip() if len(d) >= 0x150 else ""
    cgb = len(d) > 0x143 and d[0x143] in (0x80, 0xC0)
    sgb = len(d) > 0x146 and d[0x146] == 3
    return {"title": title, "mbc": MBC.get(d[0x147], "0x%02X" % d[0x147]) if len(d) > 0x147 else "?",
            "banks": max(1, size // BANK_SIZE), "size": size, "cgb": cgb, "sgb": sgb}


def list_roms():
    seen = {}
    for g in ROM_GLOBS:
        for p in glob.glob(os.path.join(cfg["roms"], g)):
            seen[os.path.basename(p)] = p
    return [{"name": n, "size": os.path.getsize(p)} for n, p in sorted(seen.items(), key=lambda x: x[0].lower())]


def trace_dir(rom):
    return os.path.join(TRACES, stem(rom))


def list_traces(rom):
    out = []
    for p in sorted(glob.glob(os.path.join(trace_dir(rom), "*.trace"))):
        try:
            with open(p, "rb") as f:
                n = sum(1 for _ in f)
        except OSError:
            n = 0
        out.append({"name": os.path.basename(p), "lines": n, "mtime": int(os.path.getmtime(p))})
    return out


def trace_files(rom, names):
    out = []
    for n in names or []:
        if os.path.basename(n) != n or not n.endswith(".trace"):
            raise ValueError("traza inválida")
        p = os.path.join(trace_dir(rom), n)
        if os.path.isfile(p):
            out.append(p)
    return out


def input_dir(rom):
    return os.path.join(INPUTS, stem(rom))


def list_inputs(rom):
    """Entradas grabadas: las del repo (tests/inputs, solo lectura) y las de la GUI."""
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "tests", "inputs", "*.txt"))):
        out.append("repo:" + os.path.basename(p))
    for p in sorted(glob.glob(os.path.join(input_dir(rom), "*.txt"))):
        out.append("mia:" + os.path.basename(p))
    return out


def input_file(ref):
    """'repo:x.txt' o 'mia:x.txt' (esta ROM) -> ruta; '' -> None."""
    if not ref:
        return None
    kind, _, name = ref.partition(":")
    if os.path.basename(name) != name or not name.endswith(".txt") or kind not in ("repo", "mia"):
        raise ValueError("entrada inválida")
    return name, kind


def resolve_input(rom, ref):
    if not ref:
        return None
    name, kind = input_file(ref)
    p = os.path.join(ROOT, "tests", "inputs", name) if kind == "repo" else os.path.join(input_dir(rom), name)
    if not os.path.isfile(p):
        raise ValueError("no existe la entrada " + ref)
    return p


LINE_OK = re.compile(r"^(#.*|modelo (dmg|cgb|sgb)|paleta [A-Za-z+]+|\d+\s+(-|\$?[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|(A|B|SELECT|START|RIGHT|LEFT|UP|DOWN)(\+(A|B|SELECT|START|RIGHT|LEFT|UP|DOWN))*))?$", re.I)


def save_input(rom, name, text):
    name = safe_name(name[:-4] if name.endswith(".txt") else name, "nombre")
    lines = [l.rstrip() for l in text.splitlines()]
    for i, l in enumerate(lines, 1):
        if not LINE_OK.match(l.strip()):
            raise ValueError(f"línea {i} inválida: {l[:40]} (use «cuadro BOTONES», p. ej. 300 START+A, o «-»)")
    os.makedirs(input_dir(rom), exist_ok=True)
    with open(os.path.join(input_dir(rom), name + ".txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return "mia:" + name + ".txt"


def read_input(rom, ref):
    with open(resolve_input(rom, ref), encoding="utf-8") as f:
        return f.read()


def project_info(rom):
    name = short_name(rom)
    out = os.path.join(ROOT, "out", name)
    return {"name": name, "dir": out, "generated": os.path.isfile(os.path.join(out, "Makefile")),
            "built": os.path.isfile(os.path.join(out, name + EXE)),
            "difftest": os.path.isfile(os.path.join(out, "difftest" + EXE))}


def git_info():
    def g(*a):
        rc, o = run_quiet(["git", *a], timeout=15)
        return o.strip() if rc == 0 else ""
    branch = g("rev-parse", "--abbrev-ref", "HEAD")
    up = g("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
    behind = ahead = 0
    if up:
        c = g("rev-list", "--left-right", "--count", f"HEAD...{up}").split()
        if len(c) == 2:
            ahead, behind = int(c[0]), int(c[1])
    return {"branch": branch, "upstream": up, "ahead": ahead, "behind": behind,
            "head": g("log", "-1", "--format=%h %s"), "dirty": bool(g("status", "--porcelain"))}


def tools_status():
    return {n: os.path.isfile(tool(n)) for n in ("gbrecomp", "gbtrace", "gbshot", "gbaudio", "gbbench", "gbtest", "linktest", "vgmcheck")}


# --------------------------------------------------------------------------- descubrimiento (--events)

def run_events(rom, traces, out_file, extra=()):
    argv = [tool("gbrecomp"), rom_path(rom), "--why", "00:0000", "--events", out_file]
    for t in trace_files(rom, traces):
        argv += ["--trace", t]
    return run_quiet(argv + list(extra), timeout=180)


def get_events(rom, traces):
    files = trace_files(rom, traces)
    key = (rom, tuple((f, os.path.getmtime(f)) for f in files), os.path.getmtime(tool("gbrecomp")))
    with lock:
        if key in events_cache:
            return events_cache[key]
    os.makedirs(WORK, exist_ok=True)
    tmp = os.path.join(WORK, f"events_{secrets.token_hex(4)}.txt")
    rc, out = run_events(rom, traces, tmp)
    if rc != 0 or not os.path.isfile(tmp):
        raise RuntimeError(out.strip() or "falló gbrecomp")
    insns, bad, tables, data = [], [], 0, []
    org = 0
    with open(tmp, encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("#"):
                continue
            p = ln.split()
            if not p:
                continue
            if p[0] == "recorrido" and len(p) >= 3:
                org = ORG_NAMES.index(p[2]) if p[2] in ORG_NAMES else 0
                frm = p[4] if len(p) >= 5 else "-"
                how = p[3] if len(p) >= 4 else "-"
                rec = (p[1], org, how, frm)
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
    try:
        os.remove(tmp)
    except OSError:
        pass
    ev = {"insns": insns, "bad": bad, "data": data, "tables": tables}
    with lock:
        events_cache.clear()
        events_cache[key] = ev
    return ev


def read_addrs(path):
    out = set()
    with open(path, encoding="utf-8") as f:
        for ln in f:
            m = re.match(r"\s*([0-9A-Fa-f]+):([0-9A-Fa-f]{4})", ln)
            if m:
                out.add((int(m.group(1), 16), int(m.group(2), 16)))
    return out


def api_map(rom, traces, coverage):
    ev = get_events(rom, traces)
    info = rom_info(rom)
    nb = info.get("banks", 2)
    ncell = BANK_SIZE // CELL
    cat = [[[0] * 5 for _ in range(ncell)] for _ in range(nb)]    # bytes por categoría
    covered = set()                                                # (banco, offset) cubierto por una instrucción
    for b, a, n, o in ev["insns"]:
        off = a & 0x3FFF
        if b >= nb:
            continue
        for k in range(n):
            covered.add((b, a + k))
            c = (off + k) // CELL
            if c < ncell:
                cat[b][c][o] += 1
    for b, a, n in ev["data"]:
        if b >= nb:
            continue
        off = a & 0x3FFF
        for k in range(n):
            c = (off + k) // CELL
            if c < ncell:
                cat[b][c][CAT_DATA] += 1
    miss_cells, missing, total = set(), 0, 0
    if coverage:
        for cf in trace_files(rom, [coverage]):
            for b, a in read_addrs(cf):
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
            tot = sum(n)
            best = max(range(5), key=lambda k: n[k])
            flag = (1 if (b, c) in miss_cells else 0) | (2 if (b, c) in bad_cells else 0)
            cells.append([min(tot, CELL), best, flag])
        banks.append({"bank": b, "base": 0 if b == 0 else 0x4000, "cells": cells})
    nbytes = sum(n for b, a, n, o in ev["insns"])
    stats = {"banks": nb, "instr": len(ev["insns"]), "bytes": nbytes, "pct": round(100.0 * nbytes / max(1, info.get("size", 1)), 2),
             "tables": ev["tables"], "unresolved": len(ev["bad"]), **info}
    if coverage:
        stats.update(cov_total=total, cov_missing=missing, cov_pct=round(100.0 * (total - missing) / total, 1) if total else 100.0)
    return {"banks": banks, "stats": stats}


def parse_addr(s):
    m = re.fullmatch(r"([0-9A-Fa-f]{1,3}):([0-9A-Fa-f]{4})", s or "")
    if not m:
        raise ValueError("dirección inválida (use BB:AAAA)")
    return int(m.group(1), 16), int(m.group(2), 16)


def api_cell(rom, traces, bank, off):
    ev = get_events(rom, traces)
    base = 0 if bank == 0 else 0x4000
    lo = base + (off // CELL) * CELL
    items = [(a, n, o) for b, a, n, o in ev["insns"] if b == bank and lo <= a < lo + CELL]
    items.sort()
    dis = {}
    if items:
        rc, out = run_quiet([tool("gbrecomp"), rom_path(rom), "--dis", f"{bank:02X}:{items[0][0]:04X}:{len(items) + 40}"], timeout=30)
        for ln in out.splitlines():
            m = re.match(r"^[0-9A-F]+:([0-9A-F]{4})\s+(.*?)\s*(;.*)?$", ln)
            if m:
                dis[int(m.group(1), 16)] = m.group(2)
    return {"bank": bank, "lo": lo, "events": [{"addr": "%04X" % a, "len": n, "org": o, "dis": dis.get(a, "")} for a, n, o in items]}


def api_why(rom, traces, bank, addr):
    argv = [tool("gbrecomp"), rom_path(rom), "--why", f"{bank:02X}:{addr:04X}"]
    for t in trace_files(rom, traces):
        argv += ["--trace", t]
    rc, out = run_quiet(argv, timeout=60)
    return {"text": out}


# --------------------------------------------------------------------------- trabajos

class Job:
    def __init__(self, title, steps, rom=None, kind=""):
        with lock:
            job_seq[0] += 1
            self.id = job_seq[0]
        self.title, self.steps, self.rom, self.kind = title, steps, rom, kind
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
            argv = st["argv"]
            self.emit(f"\n$ {' '.join(shlex.quote(a) for a in argv).replace(ROOT + os.sep, '')}\n")
            try:
                e = dict(os.environ)
                e.update(st.get("env") or {})
                self.proc = subprocess.Popen(argv, cwd=st.get("cwd", ROOT), env=e, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True, bufsize=1, errors="replace")
                for ln in self.proc.stdout:
                    self.emit(ln.replace(ROOT + os.sep, ""))
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
        """Guarda un resumen de lo que importa en state.json (el 'avance' de cada ROM)."""
        if not self.rom:
            return
        text = "".join(self.log)
        rec = {"when": int(time.time()), "status": self.status}
        if self.kind == "difftest":
            m = re.search(r"\(([\d.]+)% recompilado\)", text)
            if m:
                rec["pct_recomp"] = float(m.group(1))
            m = re.search(r": (\d+) cuadros", text)
            if m:
                rec["frames"] = int(m.group(1))
            rec["ok"] = self.status == "ok" and "Sin diferencias" in text
            update_state(self.rom, difftest=rec)
        elif self.kind == "bench":
            m = re.findall(r"([\d.]+)x tiempo real", text)
            if m:
                rec["speed"] = float(m[0])
            update_state(self.rom, bench=rec)
        elif self.kind == "recomp_game":
            rec["ok"] = self.status == "ok" and "Sin diferencias" in text
            update_state(self.rom, recomp_game=rec)
        elif self.kind == "recomp":
            m = re.search(r"código: (\d+) instrucciones \((\d+) bytes, ([\d.]+)% de la ROM\)", text)
            if m:
                rec["pct"] = float(m.group(3))
            update_state(self.rom, recomp=rec)

    def info(self):
        return {"id": self.id, "title": self.title, "rom": self.rom, "kind": self.kind, "status": self.status,
                "rc": self.rc, "t0": self.t0, "t1": self.t1, "step": self.step_i, "steps": len(self.steps), "size": self.size}


def start_job(title, steps, rom=None, kind=""):
    j = Job(title, steps, rom, kind)
    with lock:
        jobs[j.id] = j
    threading.Thread(target=j.run, daemon=True).start()
    return j


def pint(v, lo, hi, what):
    try:
        n = int(v)
    except (TypeError, ValueError):
        raise ValueError(f"{what}: número inválido")
    if not lo <= n <= hi:
        raise ValueError(f"{what}: debe estar entre {lo} y {hi}")
    return n


def safe_name(n, what="nombre"):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,60}", n or ""):
        raise ValueError(f"{what}: solo letras, números, punto, guion y guion bajo")
    return n


def model_args(p):
    m = p.get("model")
    if not m:
        return []
    if m not in ("dmg", "cgb", "sgb"):
        raise ValueError("modelo inválido")
    return ["--model", m]


def make_job(p):
    kind, rom, traces = p.get("kind"), p.get("rom"), p.get("traces") or []
    if kind == "build":
        return start_job("make", [{"argv": ["make", "-j8"]}], kind="build")
    if kind == "test":
        return start_job("tests/run_tests.sh", [{"argv": ["sh", "tests/run_tests.sh"]}], kind="test")
    if kind == "git_sync":
        return start_job("git fetch + fast-forward", [{"argv": ["git", "fetch", "--all", "--prune"]},
                                                     {"argv": ["git", "merge", "--ff-only", "@{u}"]}], kind="git")
    rp = rom_path(rom)
    proj = project_info(rom)
    name = safe_name(p.get("name") or proj["name"])
    pdir = os.path.join(ROOT, "out", name)
    tflags = [x for t in trace_files(rom, traces) for x in ("--trace", t)]
    inp = resolve_input(rom, p.get("input"))
    if kind == "recomp":
        steps = [{"argv": [tool("gbrecomp"), rp, "-o", pdir, "--name", name, *tflags]}]
        if p.get("compile", True):
            steps.append({"argv": ["make", "-C", pdir, "-j8"]})
        return start_job(f"recompilar {name}", steps, rom, "recomp")
    if kind == "difftest":
        argv = [os.path.join(pdir, "difftest" + EXE), rp, "--frames", str(pint(p.get("frames"), 1, 2000000, "cuadros"))]
        if str(p.get("seed", "")).strip():
            argv += ["--seed", str(pint(p["seed"], 0, 2**31, "semilla"))]
        if inp:
            argv += ["--input", inp]
        argv += model_args(p)
        if p.get("uncovered"):
            os.makedirs(trace_dir(rom), exist_ok=True)
            argv += ["--uncovered", os.path.join(trace_dir(rom), safe_name(p.get("uncovered_name") or "difftest") + ".trace")]
        return start_job(f"difftest {name}", [{"argv": argv}], rom, "difftest")
    if kind == "bench":
        argv = [tool("gbbench"), rp, "--frames", str(pint(p.get("frames"), 1, 2000000, "cuadros")),
                "--repeat", str(pint(p.get("repeat") or 3, 1, 20, "repeticiones"))]
        if inp:
            argv += ["--input", inp]
        argv += model_args(p)
        if p.get("recompiled"):
            argv = [os.path.join(pdir, "difftest" + EXE), rp, "--frames", str(pint(p.get("frames"), 1, 2000000, "cuadros")), "--bench"]
        return start_job("bench", [{"argv": argv}], rom, "bench")
    if kind == "recomp_game":
        args = ["sh", "tests/recomp_game.sh", rp, name, str(pint(p.get("frames") or 6000, 1, 2000000, "cuadros"))]
        return start_job(f"recomp_game {name}", [{"argv": args}], rom, "recomp_game")
    if kind == "events":
        os.makedirs(WORK, exist_ok=True)
        argv = [tool("gbrecomp"), rp, "--why", "00:0000", "--events", os.path.join(WORK, f"eventos_{stem(rom)}.txt"), *tflags]
        return start_job("eventos (--events)", [{"argv": argv}], rom, "events")
    if kind in ("trace", "screenshot", "audio"):
        # gbtrace / gbshot / gbaudio: mismas opciones que nestrace / nesshot / nesaudio
        tn = safe_name(p.get("trace_name") or "guiada")
        out = {"trace": os.path.join(trace_dir(rom), (tn[:-6] if tn.endswith(".trace") else tn) + ".trace"),
               "screenshot": os.path.join(WORK, f"shot_{stem(rom)}.png"),
               "audio": os.path.join(WORK, f"audio_{stem(rom)}.wav")}[kind]
        os.makedirs(os.path.dirname(out), exist_ok=True)
        tool_name = {"trace": "gbtrace", "screenshot": "gbshot", "audio": "gbaudio"}[kind]
        argv = [tool(tool_name), rp, out, "--frames", str(pint(p.get("frames") or 600, 1, 2000000, "cuadros"))]
        if inp:
            argv += ["--input", inp]
        elif str(p.get("seed", "")).strip():
            argv += ["--seed", str(pint(p["seed"], 0, 2**31, "semilla"))]
        argv += model_args(p)
        return start_job(f"{tool_name} {rom}", [{"argv": argv}], rom, kind)
    raise ValueError("trabajo desconocido: " + str(kind))


def play(p):
    rom = p.get("rom")
    rp = rom_path(rom)
    proj = project_info(rom)
    exe = os.path.join(proj["dir"], proj["name"] + EXE)
    if not os.path.isfile(exe):
        raise ValueError("el proyecto no está compilado (Recompilar primero)")
    argv = [exe, rp, *model_args(p)]
    trace = None
    if p.get("interp"):
        argv.append("--interp")
    if p.get("record_trace"):
        os.makedirs(trace_dir(rom), exist_ok=True)
        trace = os.path.join(trace_dir(rom), safe_name(p.get("trace_name") or "jugada") + ".trace")
        argv += ["--uncovered", trace]
    if p.get("record_input"):
        os.makedirs(input_dir(rom), exist_ok=True)
        argv += ["--record", os.path.join(input_dir(rom), safe_name(p.get("input_name") or "jugada") + ".txt")]
    inp = resolve_input(rom, p.get("input"))
    if inp:
        argv += ["--play", inp]
    subprocess.Popen(argv, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {"ok": True, "trace": trace}


def merge_traces(rom, names, out_name):
    files = trace_files(rom, names)
    if len(files) < 2:
        raise ValueError("elija al menos dos trazas")
    lines = set()
    for f in files:
        lines |= read_addrs(f)
    out = os.path.join(trace_dir(rom), safe_name(out_name) + ".trace")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("".join("%02X:%04X\n" % x for x in sorted(lines)))
    return len(lines)


# --------------------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "GBRecompGUI"

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
            return self._send(200, PAGE.replace("__TOKEN__", cfg["token"]), "text/html; charset=utf-8")
        if not self._auth(q):
            return self._send(403, {"error": "token"})
        try:
            return self._get(u.path, q)
        except Exception as e:  # noqa: BLE001
            return self._send(400, {"error": str(e)})

    def _get(self, path, q):
        one = lambda k, d="": q.get(k, [d])[0]  # noqa: E731
        if path == "/api/state":
            rom = one("rom") or None
            st = {"git": git_info(), "tools": tools_status(), "roms": list_roms(), "roms_dir": cfg["roms"],
                  "presets": PRESETS, "progress": load_state(),
                  "jobs": [j.info() for j in sorted(jobs.values(), key=lambda j: -j.id)[:30]]}
            if rom:
                st["traces"] = list_traces(rom)
                st["inputs"] = list_inputs(rom)
                st["project"] = project_info(rom)
                st["rom_info"] = rom_info(rom)
            return self._send(200, st)
        if path == "/api/map":
            return self._send(200, api_map(one("rom"), q.get("trace", []), one("coverage")))
        if path == "/api/cell":
            return self._send(200, api_cell(one("rom"), q.get("trace", []), int(one("bank", "0"), 16), int(one("off", "0"), 16)))
        if path == "/api/why":
            b, a = parse_addr(one("addr"))
            return self._send(200, api_why(one("rom"), q.get("trace", []), b, a))
        if path == "/api/input":
            return self._send(200, {"text": read_input(one("rom"), one("name"))})
        if path == "/api/job":
            j = jobs.get(int(one("id", "0")))
            if not j:
                return self._send(404, {"error": "no existe"})
            text, pos = j.text_from(int(one("from", "0")))
            return self._send(200, {**j.info(), "text": text, "pos": pos})
        if path == "/files/shot":
            p = os.path.join(WORK, f"shot_{stem(one('rom'))}.png")
            if not os.path.isfile(p):
                return self._send(404, "sin captura", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "image/png")
        if path == "/files/audio":
            p = os.path.join(WORK, f"audio_{stem(one('rom'))}.wav")
            if not os.path.isfile(p):
                return self._send(404, "sin audio", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "audio/wav")
        if path == "/files/events":
            p = os.path.join(WORK, f"eventos_{stem(one('rom'))}.txt")
            if not os.path.isfile(p):
                return self._send(404, "genere primero los eventos", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "text/plain; charset=utf-8")
        if path == "/files/visor":
            with open(os.path.join(ROOT, "tools", "visor.html"), "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
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
    ap = argparse.ArgumentParser(description="GUI local de GBRecomp")
    ap.add_argument("--roms", default=cfg["roms"], help="carpeta con las ROMs (.gb, .gbc); por omisión roms/")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    cfg["roms"] = os.path.abspath(a.roms)
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
    print(f"GBRecomp GUI: {url}\nROMs: {cfg['roms']}\nCtrl+C para salir", flush=True)
    if not a.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    for j in jobs.values():
        if j.status == "running" and j.proc:
            j.proc.terminate()


PAGE = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html"), encoding="utf-8").read()

if __name__ == "__main__":
    main()
