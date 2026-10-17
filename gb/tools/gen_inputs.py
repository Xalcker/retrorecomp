#!/usr/bin/env python3
"""Genera jugadas pseudoaleatorias reproducibles en formato de entradas grabadas
(shared/inputs.h): un botón distinto cada `paso` cuadros.

Uso: python3 gen_inputs.py desde hasta paso semilla BOTON[,BOTON...] > salida
"""
import sys


def main(argv):
    if len(argv) < 5:
        sys.exit("uso: gen_inputs.py desde hasta paso semilla BOTONES")
    desde, hasta, paso, semilla = (int(x) for x in argv[:4])
    botones = argv[4].split(",")
    x = semilla or 1
    hold = 8 if paso > 10 else (paso // 2 or 1)
    f = desde
    while f < hasta:
        x = (x * 1103515245 + 12345) % 2147483648
        b = botones[(x >> 8) % len(botones)]
        sys.stdout.write(f"{f} {b}\n{f + hold} -\n")
        f += paso


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    main(sys.argv[1:])
