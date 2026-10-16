#!/usr/bin/env python3
"""Prueba de compatibilidad de NESRecomp con una colección de ROMs. El motor es común a NESRecomp, GBRecomp, GENRecomp y NEORecomp
(tools/retrorecomp/compat.py, ver su ayuda); aquí solo va lo propio de la NES.

Uso (en la raíz del repo, con todo compilado con "make"):
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                                [--categoria MAPPERS] [--hilos N] [--salida DIR] [--rehacer]
    python3 tools/compat/report.py
Escribe docs/compat/results.json (después de cada ROM: si se corta, la misma orden sigue donde iba) y las
miniaturas en docs/compat/thumbs/. Las ROMs se extraen a build/compat/roms (no se versionan).
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retrorecomp"))
import compat  # noqa: E402

SUPPORTED = {0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 18, 19, 21, 22, 23, 24, 25, 26, 34, 66, 69, 71, 75, 79, 85, 87, 95, 115,
             118, 119, 140, 180, 184, 206, 210}
NOMBRES = {0: "NROM", 1: "MMC1", 2: "UxROM", 3: "CNROM", 4: "MMC3", 5: "MMC5", 7: "AxROM", 9: "MMC2", 10: "MMC4",
           11: "Color Dreams", 13: "CPROM", 18: "Jaleco SS88006", 19: "Namco 163", 21: "VRC4", 22: "VRC2", 23: "VRC2/4",
           24: "VRC6", 25: "VRC4", 26: "VRC6", 34: "BNROM/NINA-001", 66: "GxROM", 69: "FME-7", 71: "Camerica", 75: "VRC1",
           79: "NINA-03/06", 85: "VRC7", 87: "Jaleco/Konami 87", 95: "Namco 118", 105: "NES-EVENT", 115: "Kasheng",
           118: "TxSROM", 119: "TQROM", 140: "Jaleco JF-11", 180: "UNROM invertido", 184: "Sunsoft 1", 206: "DxROM",
           210: "Namco 175/340"}


class NES(compat.Sistema):
    id, nombre, consola, exts = "nes", "NESRecomp", "NES", (".nes",)
    categoria_nombre = "Mapper"
    botones = ("A", "B")
    compara = "CPU, ciclos, RAM, PPU, APU, mapper, RAM del cartucho, imagen y audio"
    escala_mini = 2
    cupo = {0: 4, 1: 6, 2: 4, 3: 4, 4: 8, 5: 6, 7: 4}

    def leer(self, rom):
        h = pathlib.Path(rom).read_bytes()[:16]
        if h[:4] != b"NES\x1a":
            return {"error": "sin cabecera iNES"}
        nes2 = (h[7] & 0x0C) == 0x08
        return {"mapper": (h[6] >> 4) | (h[7] & 0xF0) | (((h[8] & 0x0F) << 8) if nes2 else 0),
                "prg_kb": h[4] * 16, "chr_kb": h[5] * 8, "bateria": bool(h[6] & 2), "cuatro_pantallas": bool(h[6] & 8),
                "espejo": "V" if h[6] & 1 else "H", "nes2": nes2}

    def soportado(self, r):
        return r.get("mapper") in SUPPORTED

    def categoria(self, r):
        return r.get("mapper", -1)

    def orden_categoria(self, c):
        return (c,)

    def nombre_categoria(self, c):
        return f"{c} {NOMBRES.get(c, '')}".strip() if c >= 0 else "?"

    def combinacion(self, r):
        return (r.get("prg_kb", 0), r.get("chr_kb", 0), r.get("bateria", False))

    def tamano(self, r):
        return f'{r.get("prg_kb", "")}/{r.get("chr_kb", "")}'

    def tamano_titulo(self):
        return "PRG/CHR KB"

    def caracteristicas(self, r):
        f = []
        if r.get("bateria"):
            f.append("batería")
        if r.get("cuatro_pantallas"):
            f.append("4 pantallas")
        if r.get("chr_kb") == 0:
            f.append("CHR-RAM")
        return f

    def migrar(self, r):
        """De los results.json anteriores al motor común"""
        if "zip" in r:
            r["archivo"] = pathlib.Path(r.pop("zip")).name
        if r.get("interprete") == "pantalla uniforme":
            r["interprete"] = "pantalla en blanco"
        for fa in (r.get("recompilador") or {}).get("fases", []):
            if "pct_prg" in fa:
                fa["pct_rom"] = fa.pop("pct_prg")
            for d in fa.get("semillas", []):
                if "ciclos_recompilados_pct" in d:
                    d["recompilado_pct"] = d.pop("ciclos_recompilados_pct")
        return r


if __name__ == "__main__":
    compat.main(NES(ROOT))
