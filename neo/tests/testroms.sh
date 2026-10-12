#!/bin/sh
# make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
# Llegan con la fase 1 (las SingleStepTests del 68000 y del Z80, los mismos commits que GENRecomp) y
# las siguientes (ver "Pruebas libres" en docs/PLAN.md).
cd "$(dirname "$0")/.." || exit 2
echo "(todavía no hay pruebas libres que bajar: llegan en la fase 1)"
