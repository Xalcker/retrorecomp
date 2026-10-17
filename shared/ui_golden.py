#!/usr/bin/env python3
"""Capturas de la interfaz contra una referencia: dibuja cada menú y cada diálogo con --ui-shot (sin ventana,
ajustes de fábrica y en español: ver shot_mode en main_sdl.c) y compara el cksum de cada PNG con el de un
archivo de referencia. Es la red de seguridad para tocar main_sdl.c: cualquier cambio visible sale aquí.
    python3 ../shared/ui_golden.py EXE ROM REFERENCIA "MENÚS" "ACCIONES" ["EXTRAS"]   compara
    UI_GOLDEN_UPDATE=1 python3 ../shared/ui_golden.py ...                              reescribe la referencia (revisa las capturas antes)
MENÚS y ACCIONES son listas de números (--ui-menu N, --ui-action N). EXTRAS, capturas con otras opciones:
cada una es una lista de opciones separadas por comas (--no-menu,--ui-menu,1), y su clave, «con» seguido
de esa lista. Las capturas quedan en build/shots/. El «cksum» es el de POSIX (CRC y tamaño), el mismo de siempre.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402


def main(argv):
    rr.utf8()
    if len(argv) < 5:
        print(__doc__, file=sys.stderr)
        return 2
    exe, rom, ref, menus, acciones = argv[:5]
    extras = argv[5] if len(argv) > 5 else ""
    actualizar = bool(os.environ.get("UI_GOLDEN_UPDATE"))
    os.makedirs("build/shots", exist_ok=True)
    nuevo = []          # (clave, suma)
    fallas = 0
    referencia = {}
    if os.path.isfile(ref):
        with open(ref, encoding="utf-8") as f:
            for l in f:
                p = l.split()
                if len(p) >= 3:
                    referencia[p[0]] = f"{p[1]} {p[2]}"

    def captura(clave, *opciones):
        nonlocal fallas
        out = f"build/shots/ui_{clave}.png"
        if os.path.exists(out):
            os.remove(out)
        rr.correr([exe, rom, "--ui-shot", out, "--ui-frames", "5", *opciones])
        if not os.path.isfile(out) or os.path.getsize(out) == 0:
            print(f"FALLA ui {clave}: no dibuja")
            fallas += 1
            return
        suma = rr.cksum(rr.leer(out))
        nuevo.append((clave, suma))
        if os.path.isfile(ref) and not actualizar:
            # Una línea «clave@Sistema» (Darwin, Linux, Windows...) pesa más que la general, por si un sistema
            # dibuja distinto. Hoy ninguno la necesita: lo que se veía distinto en macOS era la captura (la ventana
            # oculta, no una superficie), y los diálogos que muestran rutas del sistema (Este juego) no se prueban
            esperada = referencia.get(f"{clave}@{rr.uname()}") or referencia.get(clave)
            if not esperada:
                print(f"FALLA ui {clave}: sin referencia en {ref}")
                fallas += 1
            elif esperada != suma:
                print(f"FALLA ui {clave}: la captura cambió (esperada {esperada}, salió {suma}; {out})")
                fallas += 1

    for m in menus.split():
        captura(f"menu{m}", "--ui-menu", m)
    for a in acciones.split():
        captura(f"accion{a}", "--ui-action", a)
    for x in extras.split():
        captura(f"con{x}", *x.split(","))
    if actualizar:
        with open(ref, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join(f"{c} {s}\n" for c, s in nuevo))
        print(f"referencia escrita en {ref}")
    elif not os.path.isfile(ref):
        print(f"FALLA ui: no existe {ref} (UI_GOLDEN_UPDATE=1 python3 {sys.argv[0]} ... la crea)")
        fallas += 1
    if fallas:
        print("valores de esta corrida, por si es la referencia de este sistema:")
        for c, s in nuevo:
            print(f"{c}@{rr.uname()} {s}")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
