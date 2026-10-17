#!/usr/bin/env python3
"""Prueba de contrato: las herramientas sin ventana de NESRecomp, GBRecomp, GENRecomp, NEORecomp y SMSRecomp aceptan
las mismas opciones comunes (--frames, --input, --seed). Sin ROMs: ejecuta cada herramienta sin argumentos y
comprueba que su ayuda las menciona. También comprueba que el código compartido (SHARED, p. ej. el
mando aleatorio random_pad.h) sea idéntico en todos los que encuentre, y que la prueba de compatibilidad sea gemela
(retrorecomp#25): report.py igual salvo el nombre del Sistema, ningún run.py que fije lo que el motor hace común y
los oráculos con nombre. Lo de los datos publicados (docs/compat) sale como «pendiente», sin contar como falla: se
arregla al volver a correr la prueba. Corre con los recompiladores ya compilados (make).

    python3 tests/contract.py [--nes DIR] [--gb DIR] [--gen DIR] [--neo DIR] [--sms DIR]
"""
import argparse, json, os, re, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import compat  # noqa: E402
import retrorecomp_gui as rr  # noqa: E402

# Los roles de las herramientas, los mismos en cada sistema: nombre del rol -> opciones que debe mencionar su ayuda.
# Con PREFIJO, la herramienta se llama <sistema><rol> (gentrace, nesshot...); sin él, se llama como el rol (statetest).
# Un sistema puede no tener un rol a propósito: va en AUSENTES con el motivo, y el contrato deja de pedirlo.
ENTRADAS = ("--frames", "--input", "--seed")
ROLES = {"trace": ENTRADAS, "shot": ENTRADAS, "audio": ENTRADAS, "ramdump": ENTRADAS,
         "bench": (*ENTRADAS, "--repeat", "--profile", "--hash"), "run": ("--frames",)}
ROLES_SIN_PREFIJO = {"statetest": (), "inputtest": ()}
AUSENTES = {("gb", "run"): "gbtest corre las ROMs de prueba sin ventana y gb es la ventana; no hay un corredor aparte",
            ("nes", "run"): "nestest corre las ROMs de prueba sin ventana y nes es la ventana; no hay un corredor aparte"}
# El recompilador de cada sistema (<sistema>recomp): las opciones de análisis que su ayuda debe mencionar
RECOMP_OPCIONES = ("--info", "--viz", "--why", "--events", "--html", "--dis", "--codemap", "--coverage")

COMMON = {sid: {**{sid + rol: opts for rol, opts in ROLES.items() if (sid, rol) not in AUSENTES},
                **ROLES_SIN_PREFIJO, sid + "recomp": RECOMP_OPCIONES}
          for sid in rr.REGISTRO}

# Archivos que todos tienen tal cual: las entradas guionadas del ciclo
SHARED = (os.path.join("tests", "inputs", "ciclo_traza.txt"), os.path.join("tests", "inputs", "ciclo_difftest.txt"))

# Lo que antes se copiaba en cada repo y ahora se toma de shared/ por el submódulo tools/retrorecomp
MOVIDOS = (os.path.join("src", "platform", "random_pad.h"), os.path.join("src", "runtime", "trace_line.h"),
           os.path.join("src", "runtime", "sha1.c"), os.path.join("src", "runtime", "sha1.h"),
           *(os.path.join("src", "platform", f) for f in ("ui_menu.c", "ui_menu.h", "ui_font.c", "ui_font.h", "png.c", "png.h", "files.h", "files.c", "export.c", "export.h")),
           os.path.join("src", "runtime", "console.h"), os.path.join("src", "tools", "embed.c"), os.path.join("src", "tools", "json.h"),
           # Los núcleos del 68000 y del Z80 (GENRecomp y NEORecomp)
           *(os.path.join("src", "runtime", f) for f in ("m68k.h", "m68k_ops.h", "m68k.c", "z80.h", "z80.c")),
           *(os.path.join("src", "tools", f) for f in ("m68ktest.c", "z80test.c")),
           *(os.path.join("src", r, f) for r in ("genrecomp", "neorecomp") for f in ("m68k_dis.c", "m68k_dis.h")),
           # Las preferencias, las opciones comunes de las herramientas, la mezcla de GEN y NEO y sus oráculos
           os.path.join("src", "platform", "settings.c"), os.path.join("src", "platform", "cli.h"),
           os.path.join("src", "platform", "inputs.h"), os.path.join("src", "tools", "cli.h"), os.path.join("src", "runtime", "audio.c"),
           os.path.join("tools", "oracles", "retro_run.c"), os.path.join("tools", "oracles", "libretro_min.h"),
           # El PSG (SN76489) de GENRecomp, común con SMSRecomp
           os.path.join("src", "runtime", "psg.c"),
           # El visor de --html (y el código en RAM, del que GBRecomp tenía la primera versión)
           *(os.path.join("src", r, "visor_html.h") for r in ("gbrecomp", "genrecomp", "neorecomp", "smsrecomp")),
           os.path.join("src", "platform", "ram_code.h"), os.path.join("src", "runtime", "ram_code.h"))

# Los movidos que algún repo conserva a propósito (hoy ninguno: GBRecomp tenía sus inputs.h y cli.h)
PROPIOS = {}

# Archivos que solo tienen algunos: idénticos entre los que los tienen (hoy ninguno)
SOLO = {}

# Las entradas guionadas del ciclo son las mismas en NES, Game Boy, Genesis y Master System; el Neo Geo tiene las suyas
# (la BIOS del AES tarda ~1200 cuadros en dejar el juego, así que todo va desplazado)
PROPIOS_NEO = (os.path.join("tests", "inputs", "ciclo_traza.txt"), os.path.join("tests", "inputs", "ciclo_difftest.txt"))

# Lo que el motor de compatibilidad fija para los cinco: un run.py no lo redefine
COMUNES_COMPAT = ("cuadros_diff", "metrica", "re_metrica")


def compat_gemelo(roots):
    """(revisados, fallas, pendientes) de la prueba de compatibilidad de cada repo"""
    checked = fails = 0
    pendientes = []
    reports = {}
    for s, root in roots.items():
        if not root:
            continue
        run = os.path.join(root, "tools", "compat", "run.py")
        rep = os.path.join(root, "tools", "compat", "report.py")
        if not (os.path.isfile(run) and os.path.isfile(rep)):
            print(f"FALLA {s}: falta tools/compat/run.py o report.py")
            fails += 1
            continue
        texto = open(run, encoding="utf-8").read()
        checked += 1
        fijos = [c for c in COMUNES_COMPAT if re.search(rf"^\s+(?:\w+\s*,\s*)*{c}\b[^=\n]*=", texto, re.M)]
        if fijos:
            print(f"FALLA {s}: tools/compat/run.py fija {', '.join(fijos)}, que es común (compat.py)")
            fails += 1
        if "def oraculos" in texto and "nombres_oraculos" not in texto:
            print(f"FALLA {s}: tools/compat/run.py tiene oráculos sin nombres_oraculos (las cabeceras del informe)")
            fails += 1
        if not fijos and not ("def oraculos" in texto and "nombres_oraculos" not in texto):
            print(f"ok    {s}/tools/compat/run.py (solo lo propio de la consola)")
        # report.py: el mismo en todos salvo el nombre de la subclase de Sistema
        reports[s] = re.sub(rf"\b{s.upper()}\b", "SISTEMA", open(rep, encoding="utf-8").read().replace("\r\n", "\n"))
        # Los datos publicados: una sola carpeta, el formato del motor común y sin rutas locales
        docs = os.path.join(root, "docs")
        otras = sorted(d for d in os.listdir(docs) if d.startswith("compat-")) if os.path.isdir(docs) else []
        if otras:
            pendientes.append(f"{s}: docs/{', docs/'.join(otras)} debe entrar en docs/compat (campo coleccion)")
        res = os.path.join(docs, "compat", "results.json")
        if os.path.isfile(res):
            datos = json.load(open(res, encoding="utf-8"))
            if not isinstance(datos, dict):
                pendientes.append(f"{s}: docs/compat/results.json en el formato anterior (sin sistema, fecha y version)")
            rutas = compat.rutas_locales(datos)
            if rutas:
                pendientes.append(f"{s}: docs/compat/results.json tiene {len(rutas)} rutas locales (p. ej. {rutas[0][0]})")
    if len(reports) > 1:
        checked += 1
        primero = next(iter(reports))
        distintos = [s for s in reports if reports[s] != reports[primero]]
        if distintos:
            print(f"FALLA tools/compat/report.py: {', '.join(distintos)} no es igual que en {primero} (salvo el Sistema)")
            fails += 1
        else:
            print(f"ok    tools/compat/report.py (igual en {', '.join(reports)} salvo el Sistema)")
    return checked, fails, pendientes


def registro(roots):
    """(revisados, fallas): cada entrada del registro (shared/sistemas.json) tiene los campos de todas, 
    una clase en la GUI, y sus botones son los que acepta el guion de entradas de su recompilador (INPUT_NAMES)"""
    checked = fails = 0
    campos = set(next(iter(rr.REGISTRO.values())))
    for sid, r in rr.REGISTRO.items():
        checked += 1
        error = None
        if set(r) != campos:
            error = "campos distintos: " + ", ".join(sorted(set(r) ^ campos))
        elif sid not in rr.CLASES:
            error = "la GUI no tiene una clase para este sistema"
        elif roots.get(sid):
            fuentes = [os.path.join(roots[sid], "src", "platform", "input_host.h"), os.path.join(roots[sid], "src", "platform", "inputs.h")]
            texto = next((open(f, encoding="utf-8").read() for f in fuentes if os.path.isfile(f)), "")
            m = re.search(r"INPUT_NAMES\s+(\"[^\n]*)", texto) or re.search(r"\{\s*(\"A\",[^}]*)\}", texto)
            nombres = set(re.findall(r'"([A-Z]+)"', m.group(1))) if m else set()
            if nombres != set(r["botones"]):
                error = f"botones {sorted(r['botones'])}, el recompilador acepta {sorted(nombres)}"
        if error:
            print(f"FALLA registro/{sid}: {error}")
            fails += 1
        else:
            print(f"ok    registro/{sid}")
    return checked, fails


def main():
    ap = argparse.ArgumentParser()
    for sid in rr.REGISTRO:
        ap.add_argument("--" + sid)
    a = ap.parse_args()
    roots = {sid: rr.find_repo(r["repo"], r["repo"].upper() + "_DIR", getattr(a, sid), os.path.join("src", sid + "recomp", sid + "recomp.c"))
             for sid, r in rr.REGISTRO.items()}
    checked, fails = registro(roots)
    for sid, tools in COMMON.items():
        root = roots[sid]
        if not root:
            print(f"FALLA {sid}: sistema no encontrado")
            fails += 1
            continue
        for tool, opts in tools.items():
            exe = os.path.join(root, tool + rr.EXE)
            if not os.path.isfile(exe):
                print(f"FALLA {sid}/{tool}: no está compilado")
                fails += 1
                continue
            p = subprocess.run([exe], cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
            usage = p.stdout + p.stderr
            missing = [o for o in opts if o not in usage]
            checked += 1
            if missing:
                print(f"FALLA {sid}/{tool}: la ayuda no menciona {', '.join(missing)}")
                fails += 1
            else:
                print(f"ok    {sid}/{tool}")
    # Archivos que deben ser idénticos en todos los repositorios (sin contar el fin de línea)
    for shared in (*SHARED, *SOLO):
        found = [s for s in roots if roots[s] and not (s == "neo" and shared in PROPIOS_NEO)
                 and s in SOLO.get(shared, roots)]
        paths = [os.path.join(roots[s], shared) for s in found]
        if len(paths) < 2:
            continue
        checked += 1
        try:
            texts = [open(p, "rb").read().replace(b"\r\n", b"\n") for p in paths]
        except OSError as e:
            print(f"FALLA {shared}: {e}")
            fails += 1
            continue
        distintos = [s for s, t in zip(found, texts) if t != texts[0]]
        if not distintos:
            print(f"ok    {shared} (idéntico en {', '.join(found)})")
        else:
            print(f"FALLA {shared}: {', '.join(distintos)} no es igual que en {found[0]}")
            fails += 1
    # Los archivos movidos a shared/ no deben seguir copiados en los repos
    for s, root in roots.items():
        for m in MOVIDOS:
            if root and s not in PROPIOS.get(m, ()) and os.path.exists(os.path.join(root, m)):
                print(f"FALLA {s}: {m} ya vive en retrorecomp/shared; quita la copia")
                fails += 1
            checked += 1
    c, f, pendientes = compat_gemelo(roots)
    checked += c
    fails += f
    for p in pendientes:
        print(f"pendiente {p}")
    print(f"{checked} herramientas y archivos revisados, {fails} fallas"
          + (f" ({len(pendientes)} pendientes de volver a correr la prueba de compatibilidad)" if pendientes else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
