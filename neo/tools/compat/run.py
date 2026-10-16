#!/usr/bin/env python3
"""Prueba de compatibilidad de NEORecomp con una colección de juegos (por omisión, los de roms/). El motor es
común a NESRecomp, GBRecomp, GENRecomp y NEORecomp (tools/retrorecomp/compat.py, ver su ayuda); aquí solo va lo
propio del Neo Geo, más los oráculos: con --oraculos, la RAM del 68000 y la imagen a los cuadros 1200 y 2400 de
los juegos de la etapa 2 contra Geolith y FBNeo (tools/oracles/oracle.py, que los baja la primera vez).

Lo propio del Neo Geo: el juego es el zip de MAME entero (o un .neo), los zips de las BIOS (aes.zip,
neogeo.zip) se saltan, los datos salen de neorecomp --info y los cifrados o protegidos quedan «sin soporte».
La BIOS del AES tarda unos 20 s en dejar el juego en su demo: las capturas y los guiones van más tarde. Las
dos BIOS son dos colecciones del mismo informe (docs/compat): «BIOS de SNK» con roms/aes.zip (o NEO_BIOS con la
de SNK) y «BIOS propia» con NEO_BIOS=build/neobios.bin. Los oráculos solo se comparan con la de SNK.

Uso (en la raíz del repo, con todo compilado con "make" y la BIOS del AES en roms/aes.zip o NEO_BIOS):
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                                [--categoria CATEGORIAS] [--hilos N] [--salida DIR] [--rehacer] [--oraculos]
    python3 tools/compat/report.py
Escribe docs/compat/results.json (después de cada juego: si se corta, la misma orden sigue donde iba) y las
miniaturas en docs/compat/thumbs/.
"""
import os, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retrorecomp"))
sys.path.insert(0, str(ROOT / "tools" / "oracles"))
import compat  # noqa: E402

ORDEN = ["hasta 1 MB", "2 MB", "P2 fijo", "P2 con bancos", "cifrado"]
BIOS = ("aes.zip", "neogeo.zip")


def bios_snk():
    """¿Las herramientas usarán la BIOS de SNK (aes.zip, neo-epo.bin...)? Solo si NEO_BIOS la nombra: sin NEO_BIOS,
    la búsqueda del runtime toma primero la propia, neobios.bin (src/runtime/bios.c)"""
    b = os.environ.get("NEO_BIOS", "")
    return bool(b) and pathlib.Path(b).name.lower() != "neobios.bin"


class NEO(compat.Sistema):
    id, nombre, consola, exts = "neo", "NEORecomp", "Neo Geo (AES)", (".neo",)
    juego_zip = True
    categoria_nombre = "Programa"
    botones = ("A", "B")
    cuadros1 = (1200, 2400, 3600)
    compara = ("68000, ciclos, RAM, VRAM, LSPC, paleta, latch y banco de P2, tarjeta, Z80 y su RAM, YM2610, mezcla y "
               "bus abierto tras cada función 68000; imagen y audio al final de cada cuadro")
    escala_mini = 2
    cupo = {"hasta 1 MB": 4, "2 MB": 5, "P2 fijo": 4, "P2 con bancos": 5}

    def es_juego(self, p):
        return p.name.lower() not in BIOS

    def coleccion(self, carpeta):
        """La BIOS con la que corre: los mismos juegos con la de SNK y con la propia son dos colecciones del mismo
        informe y no se deduplican (NEORecomp#15)"""
        return "BIOS de SNK" if bios_snk() else "BIOS propia"

    def guiones(self):
        """Start y A pasan de la demo y de los menús a la partida; después golpes y movimiento (las entradas de
        tests/coverage.sh). En difftest se suma el mando aleatorio de cada semilla."""
        t = [("START", 1200, 1206), ("START", 1500, 1506)]
        t += [("A", k, k + 11) for k in range(1700, 2201, 100)]
        t += [("START", 2300, 2306)] + [("A", k, k + 11) for k in range(2400, 2701, 100)]
        t += [("RIGHT", 2800, 3401), ("B", 3100, 3106), ("C", 3200, 3206), ("LEFT", 3400, 3901), ("A", 3600, 3606)]
        d = [("START", 1200, 1206), ("START", 1500, 1506), ("A", 1600, 1611), ("A", 1700, 1711), ("RIGHT", 1650, 1801),
             ("B", 1720, 1731)]
        return compat.guion_texto(t, 3900), compat.guion_texto(d, 1800)

    def leer(self, rom):
        rc, o, _ = compat.correr([self.herramienta("recomp"), rom, "--info"], cwd=self.root, timeout=120)
        if rc != 0:
            return {"error": (o.strip().splitlines() or ["no carga"])[-1][:200]}

        def campo(n):
            m = re.search(rf"^{n}:\s*(.*)$", o, re.M)
            return m.group(1).strip() if m else ""
        m = re.search(r"^\s+P\s+(\d+) (KB|MB)", o, re.M)
        no = re.search(r"^no se puede: (.*)$", o, re.M)
        b = re.search(r"^P2:\s+(\d+) bancos", o, re.M)
        r = {"sha1": campo("SHA1"), "nombre": campo("juego"), "ngh": (re.search(r"NGH \$(\w+)", o) or [None, ""])[1],
             "rom_kb": (int(m.group(1)) << (10 if m.group(2) == "MB" else 0)) if m else 0,
             "bancos": int(b.group(1)) if b else (1 if re.search(r"^P2:", o, re.M) else 0),
             "aviso": (re.search(r"^aviso: (.*)$", o, re.M) or [None, ""])[1]}
        if no:
            r["no_soportado"] = no.group(1)
        return r

    def soportado(self, r):
        return not r.get("no_soportado")

    def migrar(self, r):
        """Las filas de antes de separar las BIOS se corrieron con la de SNK y se llamaban como la carpeta"""
        if r.get("coleccion") == "roms":
            r["coleccion"] = "BIOS de SNK"
        return r

    def categoria(self, r):
        if r.get("no_soportado"):
            return "cifrado"
        if r.get("bancos", 0) > 1:
            return "P2 con bancos"
        if r.get("bancos") == 1 and r.get("rom_kb", 0) < 2048:
            return "P2 fijo"
        return "hasta 1 MB" if r.get("rom_kb", 0) <= 1024 else "2 MB"

    def orden_categoria(self, c):
        return (ORDEN.index(c) if c in ORDEN else 99, c)

    def combinacion(self, r):
        return (r.get("rom_kb", 0), r.get("bancos", 0))

    def tamano(self, r):
        return f'{r.get("rom_kb", "")}'

    def tamano_titulo(self):
        return "P (KB)"

    def caracteristicas(self, r):
        f = []
        if r.get("ngh"):
            f.append(f'NGH {r["ngh"]}')
        if r.get("bancos", 0) > 1:
            f.append(f'{r["bancos"]} bancos de P2')
        if "MVS" in (r.get("aviso") or ""):
            f.append("solo MVS")
        return f

    def subtitulo(self, r):
        return r.get("no_soportado") or ""

    def oraculos(self, f, guion):
        """Solo con la BIOS de SNK: con la propia la RAM difiere desde el arranque, porque su código es otro"""
        if f.get("coleccion") != "BIOS de SNK":
            return None
        import oracle
        res = {}
        for n in (1200, 2400):
            try:
                res[str(n)] = oracle.compare(f["rom"], ["--frames", str(n), "--input", str(guion)],
                                             png_dir=str(self.root / "build" / "compat" / "oraculos" / f"{f['slug']}_{n}"))
            except (SystemExit, Exception) as e:  # noqa: BLE001
                res[str(n)] = {"error": str(e)[:200]}
        return res

    nombres_oraculos = ("Geolith", "FBNeo")

    def columnas_oraculos(self, r):
        cols = []
        for n in ("geolith", "fbneo"):
            p = ((r.get("oraculos") or {}).get("2400") or {}).get(f"neorecomp-{n}")
            cols.append(f'{100 * p["ram"]:.1f} / {100 * (p["img"] or 0):.0f}' if isinstance(p, dict) else "—")
        return cols


if __name__ == "__main__":
    # Con la BIOS de SNK si está en roms/ (la búsqueda tomaría antes la propia); la propia, con NEO_BIOS
    if not os.environ.get("NEO_BIOS") and (ROOT / "roms" / "aes.zip").is_file():
        os.environ["NEO_BIOS"] = str(ROOT / "roms" / "aes.zip")
    compat.main(NEO(ROOT))
