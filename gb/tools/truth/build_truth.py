#!/usr/bin/env python3
"""Construye el mapa "verdadero" de código y datos de Tetris a partir del
desensamblado de kaspermeerts/tetris, para medir el descubrimiento de gbrecomp.

Uso: python3 tools/truth/build_truth.py DESENSAMBLADO RGBDS ROM SALIDA
  DESENSAMBLADO  clon de https://github.com/kaspermeerts/tetris
  RGBDS          carpeta con rgbasm, rgblink, rgbfix y rgbgfx (v0.6.1)
  ROM            Tetris (JUE) (V1.1), para comprobar que el resultado es idéntico
  SALIDA         carpeta de trabajo; deja ahí truth.txt
"""
import filecmp
import os
import shutil
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import instrument  # noqa: E402
import make_truth  # noqa: E402


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__)
    dis, rgbds, rom, salida = (os.path.abspath(a) for a in argv)
    os.makedirs(salida, exist_ok=True)
    shutil.copytree(dis, salida, dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git"))
    os.chdir(salida)

    def rgb(nombre):
        ruta = shutil.which(nombre, path=rgbds)
        if not ruta:
            sys.exit(f"no encuentro {nombre} en {rgbds}")
        return ruta

    def correr(*cmd):
        subprocess.run(cmd, check=True)

    # Gráficos: los PNG traen mosaicos vacíos al final que la ROM no incluye
    gfx = rgb("rgbgfx")
    correr(gfx, "-x", "11", "-o", "gfx/configandgameplay.2bpp", "gfx/configandgameplay.png")
    correr(gfx, "-x", "9", "-o", "gfx/copyrightandtitlescreen.2bpp", "gfx/copyrightandtitlescreen.png")
    correr(gfx, "-x", "1", "-o", "gfx/multiplayerandburan.2bpp", "gfx/multiplayerandburan.png")
    correr(gfx, "-d", "1", "-x", "9", "-o", "gfx/font.1bpp", "gfx/font.png")

    n = instrument.instrumentar(dis, ".", "markers.txt", ["tetris.asm", "audio.asm", "sprites.asm", "music.asm"])
    print(f"marcadores: {n}")

    asm = rgb("rgbasm")
    modulos = ["tetris", "audio", "wram", "hram", "music", "sprites"]
    for f in modulos:
        correr(asm, "-Wno-obsolete", "--export-all", "--halt-without-nop", "--preserve-ld", "-o", f + ".o", f + ".asm")
    correr(rgb("rgblink"), "--dmg", "--tiny", "--sym", "truth.sym", "--map", "truth.map", "-o", "truth.gb",
           *(f + ".o" for f in modulos))
    correr(rgb("rgbfix"), "--validate", "--title", "TETRIS", "--old-licensee", "1", "--rom-version", "1", "truth.gb")

    if filecmp.cmp("truth.gb", rom, shallow=False):
        print("ROM instrumentada idéntica a la original")
    else:
        sys.exit("ERROR: la ROM instrumentada no es idéntica")

    mapa, cuenta = make_truth.construir("markers.txt", "truth.sym", "truth.map", 32768)
    texto = "".join(mapa[:32768])
    with open("truth.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(texto[i:i + 64] + "\n" for i in range(0, len(texto), 64)))
    print(f"código {cuenta['C']} bytes, datos {cuenta['D']}, relleno {cuenta['-']}")
    print(f"mapa en {os.path.join(salida, 'truth.txt')}")


if __name__ == "__main__":
    main(sys.argv[1:])
