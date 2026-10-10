"""Lee la cabecera de cada ROM (zip con un .gb/.gbc) de uno o más directorios y escribe un JSON con el
tipo de cartucho, tamaños, banderas de color y de Super Game Boy, etc.
Uso: uv run tools/compat/scan.py DIRECTORIO [DIRECTORIO...]   (escribe build/compat/scan.json)"""
import json, pathlib, sys, zipfile
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[2]

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
SOPORTADOS = {"ROM", "MBC1", "MBC2", "MBC3", "MBC5"}
RAM_KB = {0: 0, 1: 2, 2: 8, 3: 32, 4: 128, 5: 64}


def cabecera(rom):
    """Datos de la cabecera ($100-$14F) de una ROM ya leída."""
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
    return {"titulo": titulo, "tipo": t, "mbc": mbc, "soportado": mbc in SOPORTADOS or mbc == "MBC1M",
            "rom_kb": len(rom) // 1024, "ram_kb": 0.5 if mbc == "MBC2" else (RAM_KB.get(h[0x149], 0) if ram else 0),
            "bateria": bool(bat), "rtc": bool(rtc), "rumble": bool(rumble), "cgb": cgb, "sgb": sgb,
            "modelo": modelo, "cabecera_ok": chk == h[0x14D]}


def leer_zip(z):
    with zipfile.ZipFile(z) as f:
        ent = [i for i in f.infolist() if i.filename.lower().endswith((".gb", ".gbc"))]
        if not ent:
            return None, None
        return ent[0].filename, f.read(ent[0])


def main(dirs):
    rows = []
    for d in dirs:
        coleccion = "GBC" if pathlib.Path(d).name.endswith("Color") else "GB"
        for z in sorted(pathlib.Path(d).glob("*.zip")):
            fila = {"zip": str(z), "juego": z.stem, "coleccion": coleccion}
            try:
                entry, rom = leer_zip(z)
                if rom is None:
                    continue
                if len(rom) < 0x150:
                    raise ValueError("ROM demasiado chica")
                fila.update(entry=entry, **cabecera(rom))
            except Exception as e:
                fila["error"] = str(e)
            rows.append(fila)
    out = ROOT / "build" / "compat" / "scan.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    json.dump(rows, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print(f"{len(rows)} ROMs -> {out}")
    for (m, s), n in sorted(Counter((r.get("mbc", "?"), r.get("soportado")) for r in rows).items()):
        print(f"  {m:16} {'' if s else '(sin soporte)':14} {n}")
    for k, n in sorted(Counter(r.get("modelo", "?") for r in rows).items()):
        print(f"  modelo {k}: {n}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
