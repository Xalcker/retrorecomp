#!/usr/bin/env python3
"""Prueba de compatibilidad de SMSRecomp con una colección de ROMs (por omisión, las de roms/). El motor es común a
NESRecomp, GBRecomp, GENRecomp, NEORecomp y SMSRecomp (tools/retrorecomp/compat.py, ver su ayuda); aquí solo va lo
propio del Master System y el Game Gear, más los oráculos: con --oraculos, la RAM de trabajo y la imagen a los
cuadros 600 y 1200 de las ROMs de la etapa 2 contra Genesis Plus GX y Gearsystem (tools/oracles/oracle.py, que los
baja la primera vez). La etapa 2 (el recompilador) llega con la fase 5 (docs/PLAN.md); hasta entonces, --solo etapa1.

Uso (en la raíz del repo, con todo compilado con "make"):
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                                [--categoria CATEGORIAS] [--hilos N] [--salida DIR] [--rehacer] [--oraculos]
    python3 tools/compat/report.py
CARPETA puede tener .sms, .gg o .zip (se extraen a build/compat/roms/). Escribe docs/compat/results.json (después de
cada ROM: si se corta, la misma orden sigue donde iba) y las miniaturas en docs/compat/thumbs/.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retrorecomp"))
sys.path.insert(0, str(ROOT / "tools" / "oracles"))
import compat  # noqa: E402

ORDEN = ["SMS hasta 48 KB", "SMS 128-256 KB", "SMS 512 KB", "GG hasta 48 KB", "GG 128-256 KB", "GG 512 KB",
         "Codemasters", "GG en modo SMS"]


class SMS(compat.Sistema):
    id, nombre, consola, exts = "sms", "SMSRecomp", "Master System y Game Gear", (".sms", ".gg")
    categoria_nombre = "Cartucho"
    botones = ("A", "B")
    compara = "Z80, RAM, mapper, VDP, PSG, mandos, RAM del cartucho, imagen y audio"
    escala_mini = 2
    cupo = {"SMS hasta 48 KB": 3, "SMS 128-256 KB": 5, "SMS 512 KB": 4, "GG hasta 48 KB": 2, "GG 128-256 KB": 5,
            "GG 512 KB": 3, "Codemasters": 3, "GG en modo SMS": 2}

    def leer(self, rom):
        rc, o, _ = compat.correr([self.herramienta("recomp"), rom, "--info"], cwd=self.root, timeout=60)
        if rc != 0:
            return {"error": (o.strip().splitlines() or ["no carga"])[-1][:200]}

        def campo(n):
            m = re.search(rf"^{n}:\s*(.*)$", o, re.M)
            return m.group(1).strip() if m else ""
        m = re.search(r": (\d+) KB", o)
        modelo = campo("modelo")
        juego = campo("juego")
        return {"sha1": campo("SHA1"), "nombre": "" if juego.startswith("no está") else juego, "producto": campo("producto"), "region": campo("región"),
                "rom_kb": int(m.group(1)) if m else 0, "mapper": campo("mapper"), "gg": modelo.startswith("gg"),
                "modo_sms": "modo SMS" in modelo}

    def categoria(self, r):
        if "Codemasters" in (r.get("mapper") or ""):
            return "Codemasters"
        if r.get("modo_sms"):
            return "GG en modo SMS"
        kb = r.get("rom_kb", 0)
        c = "GG" if r.get("gg") else "SMS"
        return f"{c} hasta 48 KB" if kb <= 48 else f"{c} 128-256 KB" if kb <= 256 else f"{c} 512 KB"

    def orden_categoria(self, c):
        return (ORDEN.index(c) if c in ORDEN else 99, c)

    def combinacion(self, r):
        return (r.get("rom_kb", 0), r.get("mapper", ""), r.get("gg", False), r.get("region", ""))

    def tamano(self, r):
        return f'{r.get("rom_kb", "")}'

    def caracteristicas(self, r):
        f = []
        if r.get("modo_sms"):
            f.append("modo SMS")
        if "Codemasters" in (r.get("mapper") or ""):
            f.append("mapper Codemasters")
        return f

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

    nombres_oraculos = ("Genesis Plus GX", "Gearsystem")

    def columnas_oraculos(self, r):
        cols = []
        for n in ("gpgx", "gearsystem"):
            p = ((r.get("oraculos") or {}).get("1200") or {}).get(f"smsrecomp-{n}")
            cols.append(f'{100 * p["ram"]:.1f} / {100 * (p["img"] or 0):.0f}' if isinstance(p, dict) else "—")
        return cols


if __name__ == "__main__":
    compat.main(SMS(ROOT))
