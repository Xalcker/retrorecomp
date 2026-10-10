"""Lee la cabecera iNES de cada zip de un directorio de ROMs y escribe un JSON con mapper, PRG, CHR, etc.
Uso: uv run tools/compat/scan.py DIRECTORIO salida.json"""
import json, sys, zipfile, pathlib

def main(src, out):
    rows = []
    for z in sorted(pathlib.Path(src).glob("*.zip")):
        try:
            with zipfile.ZipFile(z) as f:
                nes = [i for i in f.infolist() if i.filename.lower().endswith(".nes")]
                if not nes:
                    continue
                h = f.open(nes[0]).read(16)
        except Exception as e:
            rows.append({"zip": z.name, "error": str(e)}); continue
        if h[:4] != b"NES\x1a":
            rows.append({"zip": z.name, "error": "sin cabecera iNES"}); continue
        nes2 = (h[7] & 0x0C) == 0x08
        mapper = (h[6] >> 4) | (h[7] & 0xF0) | (((h[8] & 0x0F) << 8) if nes2 else 0)
        rows.append({"zip": z.name, "entry": nes[0].filename, "mapper": mapper, "submapper": (h[8] >> 4) if nes2 else 0,
                     "prg_kb": h[4] * 16, "chr_kb": h[5] * 8, "battery": bool(h[6] & 2), "trainer": bool(h[6] & 4),
                     "four_screen": bool(h[6] & 8), "mirror": "V" if h[6] & 1 else "H", "nes2": nes2,
                     "system": h[7] & 3})
    json.dump(rows, open(out, "w"), indent=1)
    from collections import Counter
    c = Counter(r.get("mapper", -1) for r in rows)
    for m, n in sorted(c.items()):
        print(f"mapper {m:3}: {n}")

main(*sys.argv[1:3])
