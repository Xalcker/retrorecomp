#!/usr/bin/env python3
"""Línea base exacta de Game Boy: hash de todos los cuadros (pantalla del GB y del SGB) y de todo el audio
(gbshot --hash) de cada juego de roms/ y de pruebas de la PPU y del APU de testroms/. El código común está en
shared/golden.py (opciones, formato de tests/golden.txt y qué cuenta como falla).
    python3 tests/golden.py [--update] [ROM...]
Los juegos corren 2389 cuadros (40 s) con tests/inputs/golden.txt; las pruebas, sin entradas, con el modelo de
cada una (la variante del caso). Incluye las de Mealybug que todavía fallan: si un cambio las arregla, la
diferencia aquí es la esperada.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import golden  # noqa: E402
import rr  # noqa: E402

T = "testroms"


def casos(caso):
    for r in sorted(glob.glob("roms/*.gb")) + sorted(glob.glob("roms/*.gbc")):
        caso(r, extra=["--frames", "2389", "--input", "tests/inputs/golden.txt"])
    caso(f"{T}/dmg-acid2/dmg-acid2.gb", "dmg", ["--frames", "179", "--model", "dmg"])
    caso(f"{T}/dmg-acid2/dmg-acid2.gb", "cgb", ["--frames", "179", "--model", "cgb"])
    caso(f"{T}/cgb-acid2/cgb-acid2.gbc", extra=["--frames", "179"])
    caso(f"{T}/cgb-acid-hell/cgb-acid-hell.gbc", extra=["--frames", "179"])
    for t in ("m3_bgp_change_sprites", "m3_lcdc_obj_size_change_scx", "m3_lcdc_win_en_change_multiple_wx",
              "m3_scx_low_3_bits", "m3_scy_change", "m3_window_timing", "m2_win_en_toggle"):
        caso(f"{T}/mealybug-tearoom-tests/ppu/{t}.gb", "dmg", ["--frames", "119", "--model", "dmg"])
    caso(f"{T}/mealybug-tearoom-tests/dma/hdma_timing-C.gb", "cgb", ["--frames", "119", "--model", "cgb"])
    caso(f"{T}/blargg/cgb_sound/cgb_sound.gb", "cgb", ["--frames", "2389", "--model", "cgb"])


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(golden.main("gb", [], casos))
