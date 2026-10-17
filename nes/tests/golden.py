#!/usr/bin/env python3
"""Línea base exacta de NES: hash de todos los cuadros y de todo el audio (nesshot --hash) de cada ROM de roms/
y de las pruebas libres, 1800 cuadros con las mismas entradas. El código común está en shared/golden.py
(opciones, formato de tests/golden.txt y qué cuenta como falla).
    python3 tests/golden.py [--update] [ROM...]
"""
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import golden  # noqa: E402
import rr  # noqa: E402

OPCIONES = ["--frames", "1800", "--seed", "1", "start:120-125", "start:300-305", "start:600-605"]
T = "testroms/nes-test-roms"


def sha1(rom):
    """El SHA1 de la ROM sin la cabecera iNES (el de No-Intro: no cambia si se arregla la cabecera)"""
    return rr.sha1_archivo(rom, 16)


def casos(caso):
    for r in (*sorted(glob.glob("roms/*.nes")), f"{T}/other/nestest.nes", f"{T}/apu_test/rom_singles/1-len_ctr.nes",
              f"{T}/apu_test/rom_singles/7-dmc_basics.nes", f"{T}/ppu_vbl_nmi/rom_singles/05-nmi_timing.nes",
              f"{T}/sprite_hit_tests_2005.10.05/09.timing_basics.nes", f"{T}/mmc3_test_2/rom_singles/3-A12_clocking.nes",
              f"{T}/other/PCM.demo.wgraphics.nes", f"{T}/other/SimpleParallaxDemo.nes"):
        caso(r)


if __name__ == "__main__":
    rr.entrar_a_la_raiz(__file__)
    sys.exit(golden.main("nes", OPCIONES, casos, sha1=sha1))
