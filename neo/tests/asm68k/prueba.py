#!/usr/bin/env python3
"""Prueba de asm68k (tools/neobios/asm68k.py): ensambla prueba.s, desensambla cada instrucción con m68k_dis (build/dis68kbin) y
compara con prueba.dis, la salida revisada a mano (igual que el fuente salvo la forma: las etiquetas como direcciones, los
números chicos en decimal, move a sr/ccr/usp sin tamaño, dbf = dbra, and.b #n = andi.b #n)."""
import difflib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "shared"))
import rr  # noqa: E402


def main():
    rr.utf8()
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if rr.correr([sys.executable, "tools/neobios/asm68k.py", "tests/asm68k/prueba.s", "build/asmtest.bin", "--list", "build/asmtest.lst"])[0] != 0:
        return 1
    # Las direcciones de cada instrucción: los primeros 6 caracteres de cada línea del listado
    with open("build/asmtest.lst", encoding="utf-8", errors="replace") as f:
        direcciones = "".join(l.rstrip("\r\n")[:6] + "\n" for l in f)
    codigo, salida = rr.correr([rr.exe("build/dis68kbin"), "build/asmtest.bin", "C00000"], entrada=direcciones.encode())
    if codigo != 0:
        return 1
    # En Windows el .exe escribe CRLF al redirigir su salida y el checkout puede dejar prueba.dis con CRLF: se comparan sin CR
    # los dos lados
    with open("build/asmtest.dis", "w", encoding="utf-8", newline="\n") as f:
        f.write(salida)
    with open("tests/asm68k/prueba.dis", encoding="utf-8", errors="replace") as f:
        esperado = f.read().replace("\r", "")
    if esperado == salida.replace("\r", ""):
        print(f"ok    asm68k: {len(esperado.splitlines())} instrucciones ensambladas y desensambladas igual")
        return 0
    print("FALLA asm68k: el desensamblado no coincide con tests/asm68k/prueba.dis")
    dif = list(difflib.unified_diff(esperado.splitlines(), salida.splitlines(), lineterm=""))
    print("\n".join(dif[:10]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
