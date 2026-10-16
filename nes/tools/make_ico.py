# /// script
# dependencies = ["pillow"]
# ///
"""Genera src/platform/icono.ico (el icono del .exe en Windows) a partir del PNG que está en src/platform/icono.h,
con el mismo recorte y margen que usa la ventana (set_window_icon en main_sdl.c).
Uso: uv run tools/make_ico.py"""
import io, pathlib, re
from PIL import Image

root = pathlib.Path(__file__).resolve().parents[1]
header = (root / "src/platform/icono.h").read_text(encoding="utf-8")
data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", header.split("icono_png[]")[1]))
im = Image.open(io.BytesIO(data)).convert("RGBA")

x0, y0, x1, y1 = im.getchannel("A").point(lambda a: 255 if a > 16 else 0).getbbox()
cw, ch = x1 - x0, y1 - y0
side = max(cw, ch) + max(cw, ch) // 8                      # un margen del ~6 % por lado
sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
sq.paste(im.crop((x0, y0, x1, y1)), ((side - cw) // 2, (side - ch) // 2))
sq = sq.resize((256, 256), Image.LANCZOS)
out = root / "src/platform/icono.ico"
sq.save(out, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(f"{out} ({out.stat().st_size} bytes)")
