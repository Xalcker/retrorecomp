#!/usr/bin/env python3
"""Prueba de humo del informe de compatibilidad (compat.py), sin ROMs ni recompiladores: arma un results.json
sintético para cada uno de los cuatro sistemas, con una fila por estado, genera su informe y comprueba que los cuatro
salgan gemelos (las mismas columnas salvo las propias de cada consola), que las cabeceras de los oráculos sean las
del sistema y que no quede ninguna ruta local en lo que se publica (retrorecomp#25).

Los Sistema de aquí imitan los de tools/compat/run.py de cada recompilador en lo que cambia el informe. Al dar de alta
un recompilador nuevo, agrega el suyo a SISTEMAS y corre esto antes de publicar su primer informe.

    python3 tests/compat_test.py
"""
import json, os, pathlib, re, sys, tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import compat  # noqa: E402

fallas = 0


def T(c, msg):
    global fallas
    if not c:
        print(f"FALLA {msg}")
        fallas += 1


class NES(compat.Sistema):
    id, nombre, consola, exts, categoria_nombre = "nes", "NESRecomp", "NES", (".nes",), "Mapper"

    def soportado(self, r):
        return r.get("mapper") != 105

    def categoria(self, r):
        return r.get("mapper", -1)

    def orden_categoria(self, c):
        return (c,)

    def tamano_titulo(self):
        return "PRG/CHR KB"


class GB(compat.Sistema):
    id, nombre, consola, exts, categoria_nombre = "gb", "GBRecomp", "Game Boy", (".gb", ".gbc"), "MBC"

    def soportado(self, r):
        return r.get("mbc") != "MBC7"

    def categoria(self, r):
        return r.get("mbc", "?")

    def tamano_titulo(self):
        return "ROM/RAM KB"


class GEN(compat.Sistema):
    id, nombre, consola, exts, categoria_nombre = "gen", "GENRecomp", "Sega Genesis", (".md",), "Cartucho"
    botones = ("B", "C")
    nombres_oraculos = ("Genesis Plus GX", "BlastEm")

    def oraculos(self, f, guion):
        return {}

    def categoria(self, r):
        return r.get("cat", "1 MB")

    def columnas_oraculos(self, r):
        p = (r.get("oraculos") or {}).get("1200") or {}
        return [f'{100 * p[n]["ram"]:.1f} / {100 * p[n]["img"]:.0f}' if n in p else "—"
                for n in ("genrecomp-gpgx", "genrecomp-blastem")]


class NEO(compat.Sistema):
    id, nombre, consola, exts, categoria_nombre = "neo", "NEORecomp", "Neo Geo (AES)", (".neo",), "Programa"
    juego_zip = True
    cuadros1 = (1200, 2400, 3600)
    nombres_oraculos = ("Geolith", "FBNeo")

    def oraculos(self, f, guion):
        return {}

    def soportado(self, r):
        return not r.get("no_soportado")

    def categoria(self, r):
        return "cifrado" if r.get("no_soportado") else "2 MB"

    def columnas_oraculos(self, r):
        p = (r.get("oraculos") or {}).get("2400") or {}
        return [f'{100 * p[n]["ram"]:.1f} / {100 * p[n]["img"]:.0f}' if n in p else "—"
                for n in ("neorecomp-geolith", "neorecomp-fbneo")]


SISTEMAS = (NES, GB, GEN, NEO)
TODOS_ORACULOS = {n for S in SISTEMAS for n in S.nombres_oraculos}


def filas(S):
    """Una fila por estado del informe, con lo propio de cada consola para que sea soportada o no"""
    def fase(ok=True, pct=99.5):
        return {"ronda": 1, "pct_rom": 40.0, "funciones": 12,
                "semillas": [{"semilla": s, "ok": ok, "recompilado_pct": pct, "interpretadas": 3} for s in compat.SEMILLAS]}
    propio = {"nes": ({"mapper": 4}, {"mapper": 105}), "gb": ({"mbc": "MBC5"}, {"mbc": "MBC7"}),
              "gen": ({"cat": "1 MB"}, {"cat": "1 MB"}), "neo": ({}, {"no_soportado": "NEO-CMC"})}[S.id]
    rs = [
        {"juego": "Recompilado", "interprete": "ok", "thumb": "a.png",
         "recompilador": {"resultado": "ok", "fases": [fase()], "velocidad_x": 40.0, "velocidad_interp_x": 3.0}},
        {"juego": "Intérprete", "interprete": "ok"},
        {"juego": "Diferencia", "interprete": "ok",
         "recompilador": {"resultado": "diferencia", "detalle": "DIFERENCIA en el cuadro 12", "fases": [fase(False)]}},
        {"juego": "Falla", "interprete": "ok", "recompilador": {"resultado": "falla", "detalle": "compilación: error"}},
        {"juego": "En blanco", "interprete": "pantalla en blanco"},
        {"juego": "Se cuelga", "interprete": "se cuelga", "detalle": "CPU detenido en el cuadro 30"},
        {"juego": "No carga", "interprete": "error", "detalle": "no carga"},
        {"juego": "Sin probar"},
    ]
    for r in rs:
        r.update(propio[0], archivo=r["juego"] + ".zip", coleccion="colección")
    if S.id not in ("gen",):     # GEN soporta todos los cartuchos que reconoce
        rs.append(dict(propio[1], juego="Sin soporte", archivo="sin.zip", coleccion="colección", interprete="ok"))
    if S.nombres_oraculos:
        n = {"gen": "1200", "neo": "2400"}[S.id]
        pre = {"gen": ("genrecomp-gpgx", "genrecomp-blastem"), "neo": ("neorecomp-geolith", "neorecomp-fbneo")}[S.id]
        rs[0]["oraculos"] = {n: {p: {"ram": 0.995, "img": 1.0} for p in pre}}
    return rs


def celdas(fila_html, etiqueta):
    return len(re.findall(rf"<{etiqueta}[ >]", fila_html))


def cabeceras_por_juego(doc):
    m = re.search(r'<table id="t"><thead><tr>(.*?)</tr></thead>', doc, re.S)
    return [re.sub(r"<[^>]+>", "", h) for h in re.findall(r"<th[^>]*>(.*?)</th>", m.group(1), re.S)] if m else []


def main():
    globales = {}
    with tempfile.TemporaryDirectory() as tmp:
        for cls in SISTEMAS:
            S = cls(tmp)
            D = pathlib.Path(tmp) / S.id
            D.mkdir()
            rs = filas(S)
            (D / "results.json").write_text(json.dumps({"sistema": S.id, "fecha": "2026-10-14", "version": "abc1234",
                                                        "roms": rs}, ensure_ascii=False), encoding="utf-8")
            compat.informe(S, ["--salida", str(D)])
            doc = (D / "index.html").read_text(encoding="utf-8")
            esperados = set(compat.ESTADOS + ["sin probar"]) - ({"sin soporte"} if S.id == "gen" else set())
            vistos = set(re.findall(r'<button class="chip" data-f="([^"]+)"', doc))
            T(vistos == esperados, f"{S.id}: chips {sorted(vistos)}, esperados {sorted(esperados)}")
            cab = cabeceras_por_juego(doc)
            ora = [h.replace("RAM / imagen %", "") for h in cab if "RAM / imagen" in h]
            T(ora == list(S.nombres_oraculos), f"{S.id}: oráculos en el informe {ora}, el sistema dice {list(S.nombres_oraculos)}")
            ajenos = [n for n in TODOS_ORACULOS - set(S.nombres_oraculos) if n in doc]
            T(not ajenos, f"{S.id}: el informe nombra oráculos de otra consola: {ajenos}")
            filas_html = re.findall(r"<tr data-estado=.*?</tr>", doc, re.S)
            T(len(filas_html) == len(rs), f"{S.id}: {len(filas_html)} filas, esperadas {len(rs)}")
            mal = [f for f in filas_html if celdas(f, "td") != len(cab)]
            T(not mal, f"{S.id}: {len(mal)} filas con un número de celdas distinto de las {len(cab)} cabeceras")
            T(not compat.rutas_locales(json.loads((D / "results.json").read_text(encoding="utf-8"))),
              f"{S.id}: el results.json sintético tiene rutas")
            # Lo que no es propio de la consola (categoría, tamaño, oráculos) es igual en los cuatro
            globales[S.id] = [h for h in cab if h not in (S.categoria_nombre, S.tamano_titulo()) and "RAM / imagen" not in h]
            print(f"ok    {S.id}: informe con {len(rs)} filas, {len(cab)} columnas"
                  + (f", oráculos {' y '.join(S.nombres_oraculos)}" if S.nombres_oraculos else ""))

        primero = next(iter(globales))
        for sid, cab in globales.items():
            T(cab == globales[primero], f"{sid}: columnas comunes {cab} distintas de las de {primero} {globales[primero]}")

        # Rutas: el motor las quita al guardar y las detecta al leer
        S = GB(tmp)
        P = compat.Prueba(S, pathlib.Path(tmp) / "guardar")
        col = pathlib.Path(tmp) / "No-Intro" / "Nintendo - Game Boy"
        P.agregar_prefijo(col)
        rom = S.root.resolve() / "build" / "compat" / "roms" / "gtetris123456.gb"
        P.res["GB/Tetris.zip"] = {"juego": "Tetris", "archivo": "Tetris.zip", "coleccion": "GB",
                                  "detalle": f"no se pudo abrir {rom}",
                                  "recompilador": {"detalle": f"gbrecomp: {col.resolve() / 'Tetris.zip'}: error"}}
        P.guardar()
        guardado = json.loads(P.res_path.read_text(encoding="utf-8"))
        T(not compat.rutas_locales(guardado), f"guardar dejó rutas: {compat.rutas_locales(guardado)}")
        T(guardado["roms"][0]["detalle"] == "no se pudo abrir build/compat/roms/gtetris123456.gb",
          f"guardar: detalle {guardado['roms'][0]['detalle']!r}")
        viejo = [{"zip": r"E:\Roms\No-Intro\Nintendo - Game Boy\Tetris (World).zip"}, {"detalle": "/Users/x/GBRecomp/a"},
                 {"detalle": "/home/x/a"}, {"detalle": "C:/msys64/x"}, {"detalle": "cuadro 12: A/B y 3:4, $C0:1234"}]
        hall = compat.rutas_locales(viejo)
        T([d for d, _ in hall] == ["[0].zip", "[1].detalle", "[2].detalle", "[3].detalle"],
          f"rutas_locales encontró {hall}")
        print("ok    rutas locales: se quitan al guardar y se detectan al leer")
    print(f"compat: {'sin fallas' if not fallas else f'{fallas} fallas'}")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
