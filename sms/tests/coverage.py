#!/usr/bin/env python3
"""Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones del juego que se ejecutaron encuentra solo el
análisis estático, con cada juego de roms/ (y sus subcarpetas) y el homebrew. Las trazas se hacen una vez con smstrace y se
guardan en build/traces/: 3000 cuadros con las mismas entradas para todos (Start y el botón 1 para pasar del título y de los
menús, movimiento y botones en la partida) más un mando aleatorio con semilla 1.
  python3 tests/coverage.py [opciones de smsrecomp, p. ej. --verbose]
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import cobertura  # noqa: E402
import rr  # noqa: E402

ENTRADAS = ("start:120-125 start:300-305 a:400-410 a:500-510 start:600-605 right:700-1300 a:800-805 b:900-905 left:1300-1700 "
            "a:1400-1405 right:1700-2300 b:1800-1810 up:2300-2400 down:2400-2500 a:2600-2610").split()


def main(argv):
    rr.entrar_a_la_raiz(__file__)
    roms = []
    for patron in ("roms/*.sms", "roms/*.gg", "roms/*/*.sms", "roms/*/*.gg", "testroms/homebrew/*.sms", "testroms/homebrew/*.gg"):
        roms += sorted(glob.glob(patron))
    nombre = lambda r: os.path.splitext(os.path.basename(r))[0]
    return cobertura.cobertura("sms", roms, 3000, ENTRADAS, lambda r: f"{nombre(r)}.{r.rsplit('.', 1)[-1]}.txt", nombre,
                               40, 40, "juegos", opciones=argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
