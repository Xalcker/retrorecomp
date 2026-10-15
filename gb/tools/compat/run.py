#!/usr/bin/env python3
"""Prueba de compatibilidad de GBRecomp con una colección de ROMs (zips de No-Intro de Game Boy y Game Boy Color).
El motor es común a NESRecomp, GBRecomp, GENRecomp y NEORecomp (tools/retrorecomp/compat.py, ver su ayuda); aquí
solo va lo propio del Game Boy.

Uso (en la raíz del repo, con todo compilado con "make"):
    python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                                [--categoria MBCS] [--hilos N] [--salida DIR] [--rehacer]
    python3 tools/compat/report.py
Escribe docs/compat/results.json (después de cada ROM: si se corta, la misma orden sigue donde iba) y las
miniaturas en docs/compat/thumbs/. Las ROMs se extraen a build/compat/roms y los proyectos a build/compat/out.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retrorecomp"))
import compat  # noqa: E402

# Tipo de cartucho ($147): (MBC, RAM, batería, RTC, rumble). Los que mbc.c no conoce quedan sin soporte.
TIPOS = {
    0x00: ("ROM", 0, 0, 0, 0), 0x08: ("ROM", 1, 0, 0, 0), 0x09: ("ROM", 1, 1, 0, 0),
    0x01: ("MBC1", 0, 0, 0, 0), 0x02: ("MBC1", 1, 0, 0, 0), 0x03: ("MBC1", 1, 1, 0, 0),
    0x05: ("MBC2", 1, 0, 0, 0), 0x06: ("MBC2", 1, 1, 0, 0),
    0x0B: ("MMM01", 0, 0, 0, 0), 0x0C: ("MMM01", 1, 0, 0, 0), 0x0D: ("MMM01", 1, 1, 0, 0),
    0x0F: ("MBC3", 0, 1, 1, 0), 0x10: ("MBC3", 1, 1, 1, 0), 0x11: ("MBC3", 0, 0, 0, 0),
    0x12: ("MBC3", 1, 0, 0, 0), 0x13: ("MBC3", 1, 1, 0, 0),
    0x19: ("MBC5", 0, 0, 0, 0), 0x1A: ("MBC5", 1, 0, 0, 0), 0x1B: ("MBC5", 1, 1, 0, 0),
    0x1C: ("MBC5", 0, 0, 0, 1), 0x1D: ("MBC5", 1, 0, 0, 1), 0x1E: ("MBC5", 1, 1, 0, 1),
    0x20: ("MBC6", 1, 1, 0, 0), 0x22: ("MBC7", 1, 1, 0, 0), 0xFC: ("POCKET CAMERA", 1, 1, 0, 0),
    0xFD: ("TAMA5", 1, 1, 1, 0), 0xFE: ("HuC3", 1, 1, 1, 0), 0xFF: ("HuC1", 1, 1, 0, 0),
}
SOPORTADOS = {"ROM", "MBC1", "MBC1M", "MBC2", "MBC3", "MBC5"}
RAM_KB = {0: 0, 1: 2, 2: 8, 3: 32, 4: 128, 5: 64}
ORDEN_MBC = ["ROM", "MBC1", "MBC1M", "MBC2", "MBC3", "MBC5"]
CONSOLA = {"dmg": "Game Boy", "sgb": "Super Game Boy", "cgb": "Game Boy Color"}


class GB(compat.Sistema):
    id, nombre, consola, exts = "gb", "GBRecomp", "Game Boy", (".gb", ".gbc")
    categoria_nombre = "MBC"
    botones = ("A", "B")
    compara = "CPU, ciclos, RAM, VRAM, OAM, registros, timer, PPU y RAM del cartucho al final de cada cuadro"
    escala_mini = 1
    cupo = {"ROM": 3, "MBC1": 10, "MBC1M": 2, "MBC2": 3, "MBC3": 6, "MBC5": 11}

    def coleccion(self, carpeta):
        """Hay juegos con el mismo nombre en las dos colecciones (Paperboy, Road Rash...)"""
        return "GBC" if pathlib.Path(carpeta).name.endswith("Color") else "GB"

    def leer(self, rom):
        rom = pathlib.Path(rom).read_bytes()
        if len(rom) < 0x150:
            return {"error": "ROM demasiado chica"}
        h = rom[:0x150]
        t = h[0x147]
        mbc, ram, bat, rtc, rumble = TIPOS.get(t, (f"desconocido ${t:02X}", 0, 0, 0, 0))
        cgb = "solo" if h[0x143] == 0xC0 else ("compatible" if h[0x143] & 0x80 else "no")
        sgb = h[0x146] == 0x03 and h[0x14B] == 0x33
        titulo = bytes(h[0x134:0x144 if cgb == "no" else 0x13F]).split(b"\0")[0].decode("ascii", "replace").strip()
        chk = 0
        for b in h[0x134:0x14D]:
            chk = (chk - b - 1) & 0xFF
        if mbc == "MBC1" and len(rom) == 0x100000 and rom[0x104:0x134] == rom[0x40104:0x40134]:
            mbc = "MBC1M"         # cartucho de varios juegos: otra cabecera con logo en el banco $10
        # Modelo que elige el runtime en automático (gb_init_model)
        modelo = "cgb" if h[0x143] & 0x80 else ("sgb" if sgb else "dmg")
        return {"titulo": titulo, "tipo": t, "mbc": mbc, "rom_kb": len(rom) // 1024,
                "ram_kb": 0.5 if mbc == "MBC2" else (RAM_KB.get(h[0x149], 0) if ram else 0),
                "bateria": bool(bat), "rtc": bool(rtc), "rumble": bool(rumble), "cgb": cgb, "sgb": sgb,
                "modelo": modelo, "cabecera_ok": chk == h[0x14D]}

    def soportado(self, r):
        return r.get("mbc") in SOPORTADOS

    def categoria(self, r):
        return r.get("mbc", "?")

    def orden_categoria(self, c):
        return (ORDEN_MBC.index(c) if c in ORDEN_MBC else 99, c)

    def otros_resumenes(self):
        return [("Consola", lambda r: r.get("modelo", "dmg"), lambda k: list(CONSOLA).index(k), lambda k: CONSOLA[k])]

    def combinacion(self, r):
        return (r.get("modelo", ""), r.get("rom_kb", 0), r.get("ram_kb", 0), r.get("bateria", False))

    def tamano(self, r):
        ram = r.get("ram_kb") or 0
        return f'{r.get("rom_kb", "")}/{f"{ram:g}" if ram else "—"}'

    def tamano_titulo(self):
        return "ROM/RAM KB"

    def caracteristicas(self, r):
        f = []
        if r.get("cgb") == "solo":
            f.append("solo GBC")
        elif r.get("cgb") == "compatible":
            f.append("GBC + GB")
        for k, n in (("sgb", "SGB"), ("bateria", "batería"), ("rtc", "reloj"), ("rumble", "vibración")):
            if r.get(k):
                f.append(n)
        return f

    def subtitulo(self, r):
        return f'{r.get("coleccion", "")} · {CONSOLA.get(r.get("modelo"), "")}'

    def migrar(self, r):
        """De los results.json anteriores al motor común"""
        if "zip" in r:
            r["archivo"] = pathlib.PureWindowsPath(r.pop("zip")).name
        return r


if __name__ == "__main__":
    compat.main(GB(ROOT))
