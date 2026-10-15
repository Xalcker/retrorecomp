#!/usr/bin/env python3
"""Prueba de contrato: las herramientas sin ventana de NESRecomp, GBRecomp, GENRecomp y NEORecomp aceptan
las mismas opciones comunes (--frames, --input, --seed). Sin ROMs: ejecuta cada herramienta sin argumentos y
comprueba que su ayuda las menciona. También comprueba que el código compartido (SHARED, p. ej. el
mando aleatorio random_pad.h) sea idéntico en todos los que encuentre, y que la prueba de compatibilidad sea gemela
(retrorecomp#25): report.py igual salvo el nombre del Sistema, ningún run.py que fije lo que el motor hace común y
los oráculos con nombre. Lo de los datos publicados (docs/compat) sale como «pendiente», sin contar como falla: se
arregla al volver a correr la prueba. Corre con los recompiladores ya compilados (make).

    python3 tests/contract.py [--nes DIR] [--gb DIR] [--gen DIR] [--neo DIR]
"""
import argparse, json, os, re, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import compat  # noqa: E402
import retrorecomp_gui as rr  # noqa: E402

COMMON = {  # herramienta -> opciones que debe mencionar su ayuda
    "nes": {"nestrace": ("--frames", "--input", "--seed"), "nesshot": ("--frames", "--input", "--seed"),
            "nesaudio": ("--frames", "--input", "--seed"), "nesbench": ("--frames", "--input", "--repeat", "--seed")},
    "gb": {"gbtrace": ("--frames", "--input", "--seed"), "gbshot": ("--frames", "--input", "--seed"),
           "gbaudio": ("--frames", "--input", "--seed"), "gbbench": ("--frames", "--input", "--repeat", "--seed")},
    "gen": {"gentrace": ("--frames", "--input", "--seed"), "genshot": ("--frames", "--input", "--seed"),
            "genaudio": ("--frames", "--input", "--seed"), "genbench": ("--frames", "--input", "--repeat", "--seed")},
    "neo": {"neotrace": ("--frames", "--input", "--seed"), "neoshot": ("--frames", "--input", "--seed"),
            "neoaudio": ("--frames", "--input", "--seed"), "neobench": ("--frames", "--input", "--repeat", "--seed")},
}

# Archivos que todos tienen tal cual: los envoltorios de las pruebas comunes (el código vive en shared/) y las
# entradas guionadas del ciclo
SHARED = (os.path.join("tests", "contract.sh"), os.path.join("tests", "recomp_game.sh"),
          os.path.join("tests", "inputs", "ciclo_traza.txt"), os.path.join("tests", "inputs", "ciclo_difftest.txt"))

# Lo que antes se copiaba en cada repo y ahora se toma de shared/ por el submódulo tools/retrorecomp
MOVIDOS = (os.path.join("src", "platform", "random_pad.h"), os.path.join("src", "runtime", "trace_line.h"),
           os.path.join("src", "runtime", "sha1.c"), os.path.join("src", "runtime", "sha1.h"),
           *(os.path.join("src", "platform", f) for f in ("ui_menu.c", "ui_menu.h", "ui_font.c", "ui_font.h", "png.c", "png.h", "files.h", "files.c", "export.c", "export.h")),
           os.path.join("src", "runtime", "console.h"), os.path.join("src", "tools", "embed.c"), os.path.join("src", "tools", "json.h"),
           # Los núcleos del 68000 y del Z80 (GENRecomp y NEORecomp)
           *(os.path.join("src", "runtime", f) for f in ("m68k.h", "m68k_ops.h", "cpu68k.c", "z80.h", "z80.c")),
           *(os.path.join("src", "tools", f) for f in ("m68ktest.c", "z80test.c")),
           *(os.path.join("src", r, f) for r in ("genrecomp", "neorecomp") for f in ("dis68k.c", "dis68k.h")),
           # Las preferencias, las opciones comunes de las herramientas, la mezcla de GEN y NEO y sus oráculos
           os.path.join("src", "platform", "settings.c"), os.path.join("src", "platform", "cli.h"),
           os.path.join("src", "platform", "inputs.h"), os.path.join("src", "runtime", "audio.c"),
           os.path.join("tools", "oracles", "retro_run.c"), os.path.join("tools", "oracles", "libretro_min.h"))

# Los movidos que algún repo conserva a propósito: GBRecomp tiene su inputs.h (un jugador, con el modelo en el
# encabezado) y su src/tools/cli.h
PROPIOS = {os.path.join("src", "platform", "inputs.h"): ("gb",)}

# Archivos que solo tienen algunos (las consolas con 68000 y Z80): idénticos entre los que los tienen
SOLO = {os.path.join("tests", "sst68k.sh"): ("gen", "neo"), os.path.join("tests", "sstz80.sh"): ("gen", "neo")}

# Las entradas guionadas del ciclo son las mismas en NES, Game Boy y Genesis; el Neo Geo tiene las suyas
# (la BIOS del AES tarda ~1200 cuadros en dejar el juego, así que todo va desplazado)
PROPIOS_NEO = (os.path.join("tests", "inputs", "ciclo_traza.txt"), os.path.join("tests", "inputs", "ciclo_difftest.txt"))

# Lo que el motor de compatibilidad fija para los cuatro: un run.py no lo redefine
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nes")
    ap.add_argument("--gb")
    ap.add_argument("--gen")
    ap.add_argument("--neo")
    a = ap.parse_args()
    roots = {"nes": rr.find_repo("NESRecomp", "NESRECOMP_DIR", a.nes, os.path.join("src", "nesrecomp", "nesrecomp.c")),
             "gb": rr.find_repo("GBRecomp", "GBRECOMP_DIR", a.gb, os.path.join("src", "gbrecomp", "gbrecomp.c")),
             "gen": rr.find_repo("GENRecomp", "GENRECOMP_DIR", a.gen, os.path.join("src", "genrecomp", "genrecomp.c")),
             "neo": rr.find_repo("NEORecomp", "NEORECOMP_DIR", a.neo, os.path.join("src", "neorecomp", "neorecomp.c"))}
    fails = checked = 0
    for sid, tools in COMMON.items():
        root = roots[sid]
        if not root:
            print(f"{sid}: repositorio no encontrado (omitido)")
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
