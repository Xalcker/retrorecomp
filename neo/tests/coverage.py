#!/usr/bin/env python3
"""Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones del juego que se ejecutaron encuentra solo el
análisis estático, con cada juego de roms/; y lo mismo para la BIOS (su análisis sin la caché, como para un usuario nuevo). Las
trazas se hacen una vez con neotrace y se guardan en build/traces/: 6000 cuadros con las mismas entradas para todos (Start y A
para pasar del título y de los menús, golpes y movimiento en la partida) más un mando aleatorio con semilla 1.
  python3 tests/coverage.py [opciones de neorecomp, p. ej. --no-tables o --no-spec]
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import cobertura  # noqa: E402
import rr  # noqa: E402

ENTRADAS = ("start:1200-1205 start:1500-1505 a:1700-1710 a:1800-1810 a:1900-1910 a:2000-2010 a:2100-2110 a:2200-2210 "
            "start:2300-2305 a:2400-2410 a:2500-2510 a:2600-2610 a:2700-2710 right:2800-3400 b:3100-3105 c:3200-3205 "
            "left:3400-3900 a:3600-3605 d:3700-3705 right:4000-4600 a:4200-4210 b:4400-4410 c:4500-4510").split()


def main(argv):
    rr.entrar_a_la_raiz(__file__)
    roms = sorted(glob.glob("roms/*.zip")) + sorted(glob.glob("roms/*.neo"))
    base = lambda r: os.path.splitext(os.path.basename(r))[0]
    return cobertura.cobertura("neo", roms, 6000, ENTRADAS, lambda r: base(r) + ".txt", base, 14, 999, "juegos",
                               opciones=argv, saltar=("aes.zip", "neogeo.zip"), con_bios=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
