#!/usr/bin/env python3
"""GUI local de NESRecomp: una página servida en 127.0.0.1 (solo biblioteca estándar de Python).

Muestra el avance del descubrimiento (el mismo mapa que `nesrecomp --viz`, en colores y con clic por
celda), corre las herramientas (nestrace, nesrecomp, difftest, bench, tests/recomp_game.sh, nes...) y
muestra su salida en vivo.

    python3 tools/gui/nesrecomp_gui.py [--roms carpeta] [--port 8765] [--no-browser]

Las herramientas se buscan en la raíz del repositorio (`make` las compila desde la propia GUI).
"""
import argparse, glob, json, os, re, secrets, shlex, shutil, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = ".exe" if os.name == "nt" else ""
WORK = os.path.join(ROOT, "build", "gui")
TRACES = os.path.join(WORK, "traces")
STATE_FILE = os.path.join(WORK, "state.json")

ORG_NAMES = ["flujo desde los vectores", "traza", "tabla de saltos", "especulativo (todos los bancos)"]
ORG_COLOR = {46: 0, 51: 1, 226: 2, 201: 3}
BUTTONS = ("a", "b", "select", "start", "up", "down", "left", "right")

# Entradas guionadas de tests/recomp_game.sh: sirven para pasar del título al juego y moverse un poco
def preset_guiada(frames=2400):
    s = ["start:200-205", "start:300-305", "start:400-405", "a:500-505", "start:600-605", "a:700-705", "a:800-805"]
    for t in range(900, 2100 + 1, 60):
        s += [f"a:{t}-{t + 25}", f"b:{t + 30}-{t + 50}"]
    s += ["right:900-1400", "left:1400-1700", "right:1700-2300"]
    return " ".join(s)

PRESETS = {
    "smb": "start:120-125 right:300-3000 a:700-720 a:1100-1120",
    "guiada": preset_guiada(),
    "titulo": "start:120-125 start:300-305 a:400-405",
}

lock = threading.RLock()
jobs = {}            # id -> Job
job_seq = [0]
cfg = {"roms": os.path.join(ROOT, "roms"), "token": ""}
events_cache = {}    # clave -> lista de eventos


def tool(name):
    return os.path.join(ROOT, name + EXE)


def run_quiet(argv, cwd=ROOT, timeout=60, env=None):
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=timeout, env=env)
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


def save_state(st):
    os.makedirs(WORK, exist_ok=True)
    tmp = STATE_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(st, f, ensure_ascii=False, indent=1)
    os.replace(tmp, STATE_FILE)


def update_state(rom, **kv):
    with lock:
        st = load_state()
        st.setdefault(rom, {}).update(kv)
        save_state(st)


# --------------------------------------------------------------------------- ROMs, trazas y proyectos

def stem(rom):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.splitext(rom)[0]).strip("_") or "rom"


def short_name(rom):
    """Nombre del ejecutable: letras minúsculas y números de las primeras palabras."""
    base = re.sub(r"\(.*?\)|\[.*?\]", "", os.path.splitext(rom)[0])
    n = re.sub(r"[^a-z0-9]+", "", base.lower())[:16]
    return n or "juego"


def rom_path(rom):
    """Ruta de una ROM por nombre de archivo dentro de la carpeta de ROMs (nunca fuera de ella)."""
    if not rom or os.path.basename(rom) != rom:
        raise ValueError("ROM inválida")
    p = os.path.join(cfg["roms"], rom)
    if not os.path.isfile(p):
        raise ValueError("no existe la ROM " + rom)
    return p


def list_roms():
    out = []
    for p in sorted(glob.glob(os.path.join(cfg["roms"], "*.nes")), key=lambda x: x.lower()):
        out.append({"name": os.path.basename(p), "size": os.path.getsize(p)})
    return out


def trace_dir(rom):
    return os.path.join(TRACES, stem(rom))


def list_traces(rom):
    d = trace_dir(rom)
    out = []
    for p in sorted(glob.glob(os.path.join(d, "*.trace"))):
        try:
            with open(p, "rb") as f:
                n = sum(1 for _ in f)
        except OSError:
            n = 0
        out.append({"name": os.path.basename(p), "lines": n, "mtime": int(os.path.getmtime(p))})
    return out


def trace_files(rom, names):
    d = trace_dir(rom)
    out = []
    for n in names or []:
        if os.path.basename(n) != n or not n.endswith(".trace"):
            raise ValueError("traza inválida")
        p = os.path.join(d, n)
        if os.path.isfile(p):
            out.append(p)
    return out


def project_info(rom):
    name = short_name(rom)
    out = os.path.join(ROOT, "build", "out", name)
    return {
        "name": name,
        "dir": out,
        "generated": os.path.isfile(os.path.join(out, "Makefile")),
        "built": os.path.isfile(os.path.join(out, name + EXE)),
        "difftest": os.path.isfile(os.path.join(out, "difftest" + EXE)),
    }


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
    return {
        "branch": branch, "upstream": up, "ahead": ahead, "behind": behind,
        "head": g("log", "-1", "--format=%h %s"), "dirty": bool(g("status", "--porcelain")),
    }


def tools_status():
    names = ["nesrecomp", "nestrace", "nesbench", "nes", "nesshot", "nesaudio", "nestest"]
    return {n: os.path.isfile(tool(n)) for n in names}


# --------------------------------------------------------------------------- mapa del PRG (--viz --color)

ANSI = re.compile(r"\x1b\[([0-9;]*)m(.)\x1b\[0m|(.)")


def run_discovery(rom, traces, extra=()):
    argv = [tool("nesrecomp"), rom_path(rom), "--viz", "--color"]
    for t in trace_files(rom, traces):
        argv += ["--trace", t]
    return argv + list(extra)


def parse_map(text):
    info = {"rows": [], "stats": {}, "header": ""}
    lines = text.splitlines()
    if lines:
        info["header"] = lines[0]
    for ln in lines:
        m = re.match(r"^([0-9A-F]{6}) (.*)$", ln)
        if m and "celdas" not in ln:
            cells = []
            for mm in ANSI.finditer(m.group(2)):
                if mm.group(3) is not None:
                    cells.append(["." if mm.group(3) == "." else ":", -1])
                    continue
                code, ch = mm.group(1), mm.group(2)
                if code == "41;97":
                    cells.append(["X", -1])
                else:
                    n = int(code.split(";")[-1]) if code else 0
                    cells.append([ch, ORG_COLOR.get(n, 0)])
            info["rows"].append({"off": int(m.group(1), 16), "cells": cells})
            continue
        m = re.search(r"mapper (\d+), PRG (\d+) KB, CHR (\d+) KB", ln)
        if m:
            info["stats"].update(mapper=int(m.group(1)), prg_kb=int(m.group(2)), chr_kb=int(m.group(3)))
        m = re.search(r"código: (\d+) instrucciones \((\d+) bytes, ([\d.]+)% del PRG\)", ln)
        if m:
            info["stats"].update(instr=int(m.group(1)), bytes=int(m.group(2)), pct=float(m.group(3)))
        m = re.search(r"cobertura: (\d+) de (\d+) instrucciones ejecutadas descubiertas sin traza \(([\d.]+)%\)", ln)
        if m:
            info["stats"].update(cov_found=int(m.group(1)), cov_total=int(m.group(2)), cov_pct=float(m.group(3)))
        m = re.search(r"(\d+) de (\d+) celdas con código", ln)
        if m:
            info["stats"].update(cells_code=int(m.group(1)), cells=int(m.group(2)))
    return info


def api_map(rom, traces, coverage):
    argv = run_discovery(rom, traces)
    cov = trace_files(rom, [coverage]) if coverage else []
    if cov:
        argv += ["--coverage", cov[0]]
    rc, out = run_quiet(argv, timeout=120)
    if rc != 0:
        return {"error": out.strip() or "falló nesrecomp"}
    return parse_map(out)


def get_events(rom, traces):
    files = trace_files(rom, traces)
    key = (rom, tuple((f, os.path.getmtime(f)) for f in files), os.path.getmtime(tool("nesrecomp")))
    with lock:
        if key in events_cache:
            return events_cache[key]
    os.makedirs(WORK, exist_ok=True)
    tmp = os.path.join(WORK, f"events_{secrets.token_hex(4)}.txt")
    argv = [tool("nesrecomp"), rom_path(rom), "--events", tmp]
    for t in files:
        argv += ["--trace", t]
    rc, out = run_quiet(argv, timeout=120)
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
            org = next((i for i, n in enumerate(ORG_NAMES) if rest.startswith(n + " ")), 0)
            dis = rest[len(ORG_NAMES[org]) + 1:]
            ev.append({"off": int(off, 16), "addr": addr, "how": how, "from": frm, "org": org, "dis": dis})
    try:
        os.remove(tmp)
    except OSError:
        pass
    with lock:
        events_cache.clear()
        events_cache[key] = ev
    return ev


def api_cell(rom, traces, off):
    ev = get_events(rom, traces)
    lo = off & ~0xFF
    return {"off": lo, "events": [e for e in ev if lo <= e["off"] < lo + 256]}


def api_why(rom, traces, off):
    argv = [tool("nesrecomp"), rom_path(rom), "--why", "%X" % off]
    for t in trace_files(rom, traces):
        argv += ["--trace", t]
    rc, out = run_quiet(argv, timeout=60)
    return {"text": out}


# --------------------------------------------------------------------------- trabajos (procesos con salida en vivo)

class Job:
    def __init__(self, title, steps, rom=None, kind=""):
        with lock:
            job_seq[0] += 1
            self.id = job_seq[0]
        self.title, self.steps, self.rom, self.kind = title, steps, rom, kind
        self.log = []          # trozos de texto
        self.size = 0
        self.status = "running"
        self.rc = None
        self.t0 = time.time()
        self.t1 = None
        self.proc = None
        self.stopped = False
        self.step_i = 0

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
            argv, env = st["argv"], st.get("env")
            self.emit(f"\n$ {' '.join(shlex.quote(a) for a in argv).replace(ROOT + os.sep, '')}\n")
            try:
                e = dict(os.environ)
                e.update(env or {})
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
        self.rc = rc
        self.t1 = time.time()
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
            m = re.search(r"difftest: (\d+) cuadros iguales \((\d+) llamadas, (\d+) a código recompilado, ([\d.]+)% de los ciclos\)", text)
            if m:
                rec.update(frames=int(m.group(1)), pct_cycles=float(m.group(4)))
            rec["ok"] = self.status == "ok"
            update_state(self.rom, difftest=rec)
        elif self.kind == "bench":
            m = re.findall(r"([\d.]+)x tiempo real", text)
            if m:
                rec["speed"] = float(m[-1])
            update_state(self.rom, bench=rec)
        elif self.kind == "recomp_game":
            rec["ok"] = self.status == "ok" and "Sin diferencias" in text
            update_state(self.rom, recomp_game=rec)
        elif self.kind == "recomp":
            m = re.search(r"código: (\d+) instrucciones \((\d+) bytes, ([\d.]+)% del PRG\)", text)
            if m:
                rec["pct"] = float(m.group(3))
            update_state(self.rom, recomp=rec)
        elif self.kind == "nestrace":
            update_state(self.rom, nestrace=rec)

    def info(self):
        return {"id": self.id, "title": self.title, "rom": self.rom, "kind": self.kind, "status": self.status,
                "rc": self.rc, "t0": self.t0, "t1": self.t1, "step": self.step_i, "steps": len(self.steps), "size": self.size}


def start_job(title, steps, rom=None, kind=""):
    j = Job(title, steps, rom, kind)
    with lock:
        jobs[j.id] = j
    threading.Thread(target=j.run, daemon=True).start()
    return j


def parse_segments(text):
    """'start:120-125 right:300-600' -> lista validada de 'boton:desde-hasta'."""
    segs = []
    for tok in (text or "").split():
        m = re.fullmatch(r"(a|b|select|start|up|down|left|right):(\d+)-(\d+)", tok)
        if not m:
            raise ValueError(f"entrada inválida: {tok} (use boton:desde-hasta con botones {', '.join(BUTTONS)})")
        segs.append(tok)
    return segs


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


def make_job(p):
    kind = p.get("kind")
    rom = p.get("rom")
    traces = p.get("traces") or []
    if kind == "build":
        return start_job("make", [{"argv": ["make", "-j8"]}], kind="build")
    if kind == "test":
        return start_job("make test", [{"argv": ["make", "test"]}], kind="test")
    if kind == "git_sync":
        return start_job("git fetch + fast-forward", [
            {"argv": ["git", "fetch", "--all", "--prune"]},
            {"argv": ["git", "merge", "--ff-only", "@{u}"]}], kind="git")
    rp = rom_path(rom)
    proj = project_info(rom)
    name = safe_name(p.get("name") or proj["name"])
    pdir = os.path.join(ROOT, "build", "out", name)
    tfiles = trace_files(rom, traces)
    tflags = [x for t in tfiles for x in ("--trace", t)]
    if kind == "nestrace":
        frames = pint(p.get("frames"), 1, 200000, "cuadros")
        segs = parse_segments(p.get("script"))
        tn = safe_name(p.get("trace_name") or "guiada")
        tn = tn[:-6] if tn.endswith(".trace") else tn
        os.makedirs(trace_dir(rom), exist_ok=True)
        out = os.path.join(trace_dir(rom), tn + ".trace")
        return start_job(f"nestrace {rom}", [{"argv": [tool("nestrace"), rp, out, str(frames), *segs]}], rom, "nestrace")
    if kind == "recomp":
        steps = [{"argv": [tool("nesrecomp"), rp, "-o", pdir, "--name", name, *tflags]}]
        if p.get("compile", True):
            steps.append({"argv": ["make", "-C", pdir, "-j8"]})
        return start_job(f"recompilar {name}", steps, rom, "recomp")
    if kind == "difftest":
        frames = pint(p.get("frames"), 1, 200000, "cuadros")
        segs = parse_segments(p.get("script"))
        argv = [os.path.join(pdir, "difftest" + EXE), rp, str(frames), *segs]
        if str(p.get("seed", "")).strip():
            argv += ["--seed", str(pint(p["seed"], 0, 2**31, "semilla"))]
        if p.get("uncovered"):
            argv += ["--uncovered", os.path.join(trace_dir(rom), safe_name(p.get("uncovered_name") or "difftest") + ".trace")]
            os.makedirs(trace_dir(rom), exist_ok=True)
        return start_job(f"difftest {name}", [{"argv": argv}], rom, "difftest")
    if kind == "bench":
        frames = pint(p.get("frames"), 1, 1000000, "cuadros")
        exe = os.path.join(pdir, "bench" + EXE) if p.get("recompiled") else tool("nesbench")
        return start_job("bench", [{"argv": [exe, rp, str(frames)]}], rom, "bench")
    if kind == "recomp_game":
        args = ["sh", "tests/recomp_game.sh", rp, name]
        env = {"NOTRACE": "1"} if p.get("notrace") else {}
        return start_job(f"recomp_game {name}", [{"argv": args, "env": env}], rom, "recomp_game")
    if kind == "visor":
        out = os.path.join(WORK, f"visor_{stem(rom)}.html")
        os.makedirs(WORK, exist_ok=True)
        argv = [tool("nesrecomp"), rp, "--html", out, *tflags]
        cov = trace_files(rom, [p.get("coverage")]) if p.get("coverage") else []
        if cov:
            argv += ["--coverage", cov[0]]
        return start_job("visor HTML", [{"argv": argv}], rom, "visor")
    if kind == "screenshot":
        frames = pint(p.get("frames"), 1, 200000, "cuadros")
        segs = parse_segments(p.get("script"))
        out = os.path.join(WORK, f"shot_{stem(rom)}.png")
        os.makedirs(WORK, exist_ok=True)
        return start_job("nesshot", [{"argv": [tool("nesshot"), rp, out, str(frames), *segs]}], rom, "screenshot")
    raise ValueError("trabajo desconocido: " + str(kind))


def play(p):
    rom = p.get("rom")
    rp = rom_path(rom)
    env = dict(os.environ)
    if p.get("record_trace"):
        os.makedirs(trace_dir(rom), exist_ok=True)
        env["NES_TRACE"] = os.path.join(trace_dir(rom), safe_name(p.get("trace_name") or "jugada") + ".trace")
    if p.get("recompiled"):
        proj = project_info(rom)
        exe = os.path.join(proj["dir"], proj["name"] + EXE)
        if not os.path.isfile(exe):
            raise ValueError("el proyecto recompilado no está compilado")
    else:
        exe = tool("nes")
        if not os.path.isfile(exe):
            raise ValueError("falta ./nes (use «Compilar herramientas»)")
    subprocess.Popen([exe, rp], cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {"ok": True, "trace": env.get("NES_TRACE")}


def merge_traces(rom, names, out_name):
    files = trace_files(rom, names)
    if len(files) < 2:
        raise ValueError("elija al menos dos trazas")
    lines = set()
    for f in files:
        with open(f, encoding="utf-8") as fh:
            lines.update(l.strip() for l in fh if l.strip())
    out = os.path.join(trace_dir(rom), safe_name(out_name) + ".trace")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(lines)) + "\n")
    return len(lines)


def delete_trace(rom, name):
    for f in trace_files(rom, [name]):
        os.remove(f)


# --------------------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "NESRecompGUI"

    def log_message(self, *a):
        pass

    def _ok_host(self):
        h = (self.headers.get("Host") or "").split(":")[0]
        return h in ("127.0.0.1", "localhost", "[::1]")

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
                st["project"] = project_info(rom)
            return self._send(200, st)
        if path == "/api/map":
            return self._send(200, api_map(one("rom"), q.get("trace", []), one("coverage")))
        if path == "/api/cell":
            return self._send(200, api_cell(one("rom"), q.get("trace", []), int(one("off", "0"), 16)))
        if path == "/api/why":
            return self._send(200, api_why(one("rom"), q.get("trace", []), int(one("off", "0"), 16)))
        if path == "/api/job":
            j = jobs.get(int(one("id", "0")))
            if not j:
                return self._send(404, {"error": "no existe"})
            text, pos = j.text_from(int(one("from", "0")))
            return self._send(200, {**j.info(), "text": text, "pos": pos})
        if path == "/files/visor":
            p = os.path.join(WORK, f"visor_{stem(one('rom'))}.html")
            if not os.path.isfile(p):
                return self._send(404, "genere primero el visor", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if path == "/files/shot":
            p = os.path.join(WORK, f"shot_{stem(one('rom'))}.png")
            if not os.path.isfile(p):
                return self._send(404, "sin captura", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "image/png")
        return self._send(404, {"error": "no existe"})

    def do_POST(self):
        if not self._ok_host() or not self._auth():
            return self._send(403, {"error": "token"})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            p = json.loads(self.rfile.read(n) or b"{}")
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
            if path == "/api/trace/merge":
                return self._send(200, {"lines": merge_traces(p["rom"], p.get("names"), p.get("out") or "unida")})
            if path == "/api/trace/delete":
                delete_trace(p["rom"], p["name"])
                return self._send(200, {"ok": True})
            return self._send(404, {"error": "no existe"})
        except Exception as e:  # noqa: BLE001
            return self._send(400, {"error": str(e)})


def main():
    ap = argparse.ArgumentParser(description="GUI local de NESRecomp")
    ap.add_argument("--roms", default=cfg["roms"], help="carpeta con las ROMs (.nes); por omisión roms/")
    ap.add_argument("--port", type=int, default=8765)
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
    print(f"NESRecomp GUI: {url}\nROMs: {cfg['roms']}\nCtrl+C para salir")
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
