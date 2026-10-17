"""Cobertura del descubrimiento sin traza: qué porcentaje de las instrucciones que el juego ejecutó encuentra solo el análisis
estático, con cada ROM de un sistema. Las trazas se hacen una vez con el *trace del sistema y se guardan en build/traces/.
Común a los cuatro tests/coverage.py que lo usan (cada uno pone sus ROMs, las entradas y el formato de su lista).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402

NULO = rr.subprocess.DEVNULL


def cobertura(sis, roms, frames, entradas, nombre_traza, etiqueta, ancho, corte, unidad, opciones=(), saltar=(), con_bios=False):
    """roms: las rutas (en orden); nombre_traza(ruta) -> nombre del archivo de traza en build/traces; etiqueta(ruta) -> el nombre
    que se imprime; ancho: columnas del nombre y corte: cuántos caracteres se muestran como máximo; unidad: «ROMs» o «juegos» en el total"""
    rr.utf8()
    os.makedirs("build/traces", exist_ok=True)
    total = found = n = btotal = bfound = 0
    for r in roms:
        if not os.path.isfile(r) or os.path.basename(r) in saltar:
            continue
        t = f"build/traces/{nombre_traza(r)}"
        if not (os.path.isfile(t) and os.path.getsize(t) > 0):
            codigo, _ = rr.correr([rr.exe(f"{sis}trace"), r, t, "--frames", str(frames), "--seed", "1", *entradas])
            if codigo != 0:
                continue
        _, salida = rr.correr([rr.exe(f"{sis}recomp"), r, "--coverage", t, *opciones], stderr=NULO)
        linea = next((l for l in salida.splitlines() if l.startswith("cobertura:")), "")
        bios = next((l for l in salida.splitlines() if l.startswith("cobertura de la BIOS:")), "")
        m = re.match(r"cobertura: (\d+) de (\d+)", linea)
        if not m:
            continue
        resto = linea.replace("cobertura: ", "", 1).replace(" instrucciones ejecutadas descubiertas sin traza", "")
        print(f"{etiqueta(r)[:corte]:<{ancho}} {resto}")
        found += int(m.group(1))
        total += int(m.group(2))
        n += 1
        if con_bios and bios:
            mb = re.match(r"cobertura de la BIOS: (\d+) de (\d+)", bios)
            if mb:
                bfound += int(mb.group(1))
                btotal += int(mb.group(2))
    if n > 0:
        print(f"en total: {found} de {total} instrucciones ejecutadas ({100 * found / total:.1f}%) en {n} {unidad}")
    if btotal > 0:
        print(f"BIOS (sin caché): {bfound} de {btotal} instrucciones ejecutadas ({100 * bfound / btotal:.1f}%), sumando las de cada juego")
    return 0
