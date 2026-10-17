#!/usr/bin/env python3
"""Instrumenta un desensamblado RGBDS para saber qué bytes de la ROM son código
y cuáles datos: antes de cada línea que emite bytes inserta una etiqueta
marcadora (local, para no romper el alcance de las etiquetas locales) y anota
su tipo en un archivo aparte. Al ensamblar con --export-all, el .sym da la
dirección de cada marcador.

Uso: python3 instrument.py dir_origen dir_destino tipos.txt archivo.asm...
  Las macros de código y de datos se configuran abajo.
"""
import os
import re
import sys

# Mnemónicos del SM83
CODIGO = set("""ld ldh ldi ldd jp jr call ret reti rst push pop inc dec add adc sub sbc and
    or xor cp rlca rla rrca rra rlc rl rrc rr sla sra srl swap bit set res halt
    stop nop di ei daa cpl scf ccf""".split())
DATOS = set("db dw dl ds incbin".split())
# Macros del desensamblado de Tetris
MACROS_CODIGO = {"wait_for_serial_interrupt"}
MACROS_DATOS = set("notes rest endsection noise call".split())

RE_MACRO = re.compile(r"^\s*([A-Za-z_][\w#]*):\s*MACRO\b|^\s*MACRO\b", re.I)
RE_ETIQUETA = re.compile(r"^([A-Za-z_][\w.]*::?)\s*(.*)$")
RE_EQU = re.compile(r"^\S+\s+(EQU|=|SET)\b", re.I)
RE_LOCAL = re.compile(r"^(\.[\w]+:?:?)\s*(.*)$")
RE_TOKEN = re.compile(r"^\s*([A-Za-z_][\w]*)")


def instrumentar(src, dst, tipos_path, archivos):
    n = 0
    with open(tipos_path, "w", encoding="utf-8", newline="\n") as tipos:
        for archivo in archivos:
            with open(os.path.join(src, archivo), encoding="utf-8", errors="surrogateescape", newline="") as ent, \
                 open(os.path.join(dst, archivo), "w", encoding="utf-8", errors="surrogateescape", newline="") as sal:
                hay_global = False   # ¿ya hay etiqueta global en la sección actual?
                en_macro = False     # dentro de una definición MACRO ... ENDM
                en_rept = 0          # dentro de REPT ... ENDR (no se puede repetir una etiqueta)
                for num, linea in enumerate(ent, 1):
                    cuerpo = re.sub(r";.*$", "", linea).rstrip()

                    if RE_MACRO.match(cuerpo):
                        en_macro = True
                    if en_macro:
                        sal.write(linea)
                        if re.match(r"^\s*ENDM\b", cuerpo, re.I):
                            en_macro = False
                        continue
                    if re.match(r"^\s*SECTION\b", cuerpo, re.I):
                        hay_global = False
                        sal.write(linea)
                        continue

                    # Etiqueta al principio de la línea
                    etiqueta = ""
                    m = RE_ETIQUETA.match(cuerpo)
                    if m and not RE_EQU.match(cuerpo):
                        etiqueta, cuerpo = m.group(1), m.group(2)
                        if not etiqueta.startswith(".") and not re.search(r"\.\w", etiqueta):
                            hay_global = True
                    else:
                        m = RE_LOCAL.match(cuerpo)
                        if m:
                            etiqueta, cuerpo = m.group(1), m.group(2)

                    m = RE_TOKEN.match(cuerpo)
                    tok = m.group(1) if m else None
                    clase = ""
                    if tok is not None:
                        t = tok.lower()
                        if t in ("rept", "for"):
                            clase = "" if en_rept else "C"
                        elif t in CODIGO or t in MACROS_CODIGO:
                            clase = "C"
                        elif t in DATOS or t in MACROS_DATOS:
                            clase = "D"
                    if en_rept:
                        clase = ""
                    if tok is not None and re.fullmatch(r"rept|for", tok, re.I):
                        en_rept += 1
                    if tok is not None and re.fullmatch(r"endr", tok, re.I):
                        en_rept -= 1

                    if clase == "":
                        sal.write(linea)
                        continue

                    # Línea que emite bytes: etiqueta original, marcador y contenido
                    n += 1
                    if etiqueta:
                        sal.write(etiqueta + "\n")
                    if hay_global:
                        sal.write(f".__m{n}\n")
                    else:
                        sal.write(f"__m{n}::\n")
                        hay_global = True
                    tipos.write(f"{n} {clase} {archivo}:{num}\n")
                    resto = re.sub(r"^\s*" + re.escape(etiqueta), "", linea, count=1)
                    sal.write("    " + resto)
                    if not resto.endswith("\n"):
                        sal.write("\n")
    return n


def main(argv):
    if len(argv) < 4:
        sys.exit("uso: instrument.py origen destino tipos.txt archivos...")
    n = instrumentar(argv[0], argv[1], argv[2], argv[3:])
    print(f"marcadores: {n}")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    main(sys.argv[1:])
