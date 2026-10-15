#!/usr/bin/env python3
"""Prueba de compatibilidad de GENRecomp con una colección de ROMs (por omisión, las de roms/). El motor es común a
NESRecomp, GBRecomp, GENRecomp y NEORecomp (tools/retrorecomp/compat.py, ver su ayuda); aquí solo va lo propio del Genesis,
más los oráculos: con --oraculos, la RAM del 68000 y la imagen a los cuadros 600 y 1200 de las ROMs de la etapa 2
contra Genesis Plus GX y BlastEm (tools/oracles/oracle.py, que los baja la primera vez).

Uso (en la raíz del repo, con todo compilado con "make"):
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                                [--categoria CATEGORIAS] [--hilos N] [--salida DIR] [--rehacer] [--oraculos]
    python3 tools/compat/report.py
CARPETA puede tener .bin, .md, .gen, .smd o .zip (se extraen a build/compat/roms/). Escribe docs/compat/results.json
(después de cada ROM: si se corta, la misma orden sigue donde iba) y las miniaturas en docs/compat/thumbs/.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retrorecomp"))
sys.path.insert(0, str(ROOT / "tools" / "oracles"))
import compat  # noqa: E402

ORDEN = ["hasta 512 KB", "1 MB", "2 MB", "3-4 MB", "SRAM", "EEPROM", "SSF2"]


class GEN(compat.Sistema):
    id, nombre, consola, exts = "gen", "GENRecomp", "Sega Genesis", (".bin", ".md", ".gen", ".smd")
    categoria_nombre = "Cartucho"
    botones = ("B", "C")
    compara = "68000, RAM, VDP, Z80, YM2612, PSG, mandos, cartucho, imagen y audio tras cada función 68000"
    escala_mini = 2
    cupo = {"hasta 512 KB": 4, "1 MB": 5, "2 MB": 5, "3-4 MB": 4, "SRAM": 4, "EEPROM": 3, "SSF2": 2}

    def leer(self, rom):
        rc, o, _ = compat.correr([self.herramienta("recomp"), rom, "--info"], cwd=self.root, timeout=60)
        if rc != 0:
            return {"error": (o.strip().splitlines() or ["no carga"])[-1][:200]}

        def campo(n):
            m = re.search(rf"^{n}:\s*(.*)$", o, re.M)
            return m.group(1).strip() if m else ""
        m = re.search(r": (\d+) KB", o)
        return {"sha1": campo("SHA1"), "nombre": campo("nombre"), "serie": campo("serie"), "region": campo("región"),
                "perifericos": campo("periféricos"), "rom_kb": int(m.group(1)) if m else 0, "sram": campo("SRAM"),
                "eeprom": bool(campo("EEPROM")), "ssf2": "mapper" in o}

    def categoria(self, r):
        if r.get("ssf2"):
            return "SSF2"
        if r.get("eeprom"):
            return "EEPROM"
        if r.get("sram") and r["sram"] != "no":
            return "SRAM"
        kb = r.get("rom_kb", 0)
        return "hasta 512 KB" if kb <= 512 else "1 MB" if kb <= 1024 else "2 MB" if kb <= 2048 else "3-4 MB"

    def orden_categoria(self, c):
        return (ORDEN.index(c) if c in ORDEN else 99, c)

    def combinacion(self, r):
        return (r.get("rom_kb", 0), "6" in (r.get("perifericos") or ""), r.get("region", ""))

    def tamano(self, r):
        return f'{r.get("rom_kb", "")}'

    def caracteristicas(self, r):
        f = []
        if r.get("ssf2"):
            f.append("mapper SSF2")
        if r.get("sram") and r["sram"] != "no":
            f.append("SRAM")
        if r.get("eeprom"):
            f.append("EEPROM")
        if "6" in (r.get("perifericos") or ""):
            f.append("6 botones")
        return f

    def migrar(self, r):
        """De los results.json anteriores al motor común"""
        if "archivo" not in r:
            r["archivo"] = r["juego"]
            r["juego"] = pathlib.Path(r["juego"]).stem
        if "etapa2" in r:
            r["recompilador"] = r.pop("etapa2")
        if "kb" in r:
            r["rom_kb"] = r.pop("kb")
        if r.get("interprete") == "pantalla uniforme":
            r["interprete"] = "pantalla en blanco"
        rc = r.get("recompilador") or {}
        if "velocidad_interprete_x" in rc:
            rc["velocidad_interp_x"] = rc.pop("velocidad_interprete_x")
        for fa in rc.get("fases", []):
            for d in fa.get("semillas", []):
                if "ciclos_recompilados_pct" in d:
                    d["recompilado_pct"] = d.pop("ciclos_recompilados_pct")
        return r

    def oraculos(self, f, guion):
        import oracle
        res = {}
        for n in (600, 1200):
            try:
                res[str(n)] = oracle.compare(f["rom"], ["--frames", str(n), "--input", str(guion)],
                                             png_dir=str(self.root / "build" / "compat" / "oraculos" / f"{f['slug']}_{n}"))
            except (SystemExit, Exception) as e:  # noqa: BLE001
                res[str(n)] = {"error": str(e)[:200]}
        return res

    nombres_oraculos = ("Genesis Plus GX", "BlastEm")

    def columnas_oraculos(self, r):
        cols = []
        for n in ("gpgx", "blastem"):
            p = ((r.get("oraculos") or {}).get("1200") or {}).get(f"genrecomp-{n}")
            cols.append(f'{100 * p["ram"]:.1f} / {100 * (p["img"] or 0):.0f}' if isinstance(p, dict) else "—")
        return cols


if __name__ == "__main__":
    compat.main(GEN(ROOT))
