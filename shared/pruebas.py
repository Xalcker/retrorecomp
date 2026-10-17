"""El marco de los tests/run_tests.py de cada sistema (lo que los run_tests.sh repetían): contar fallas, imprimir
«ok   …» y «FALLA …», correr programas, medir cuánto tarda cada sección y los pedazos que se repiten (la ROM
armada a mano, la cadena de recompilar y probar un proyecto).

Cada tests/run_tests.py entra a la raíz de su sistema, crea una `Pruebas` y al final llama a `terminar()`.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr  # noqa: E402


class Pruebas:
    def __init__(self, ok_txt="TODO BIEN", mal_txt="HAY FALLAS"):
        rr.utf8()
        self.fallas = 0
        self.ok_txt, self.mal_txt = ok_txt, mal_txt
        self.t = time.time()

    # --- resultados ---
    def ok(self, texto):
        print(f"ok    {texto}", flush=True)

    def falla(self, texto):
        print(f"FALLA {texto}", flush=True)
        self.fallas += 1

    def comprobar(self, condicion, ok, mal=None):
        """ok si la condición es cierta; si no, FALLA con `mal` (por omisión, el mismo texto)"""
        if condicion:
            self.ok(ok)
        else:
            self.falla(mal or ok)
        return bool(condicion)

    def tiempo(self, que):
        """Segundos desde la sección anterior (para ver qué pesa en el CI)"""
        ahora = time.time()
        print(f"      ({que}: {int(ahora - self.t)} s)", flush=True)
        self.t = ahora

    # --- correr programas ---
    def corre(self, descripcion, *cmd, **kw):
        """Corre el programa en silencio: ok si sale con 0"""
        return self.comprobar(rr.correr(cmd, **kw)[0] == 0, descripcion)

    def debe_fallar(self, descripcion, *cmd, **kw):
        """Corre el programa en silencio: ok si sale con otro código que 0"""
        return self.comprobar(rr.correr(cmd, **kw)[0] != 0, descripcion)

    def programa(self, *cmd, **kw):
        """Corre el programa mostrando su salida; si sale con otro código que 0, cuenta una falla"""
        codigo, salida = rr.correr(cmd, **kw)
        sys.stdout.write(salida)
        if codigo != 0:
            self.fallas += 1
        return codigo == 0

    def terminar(self):
        print(self.ok_txt if not self.fallas else self.mal_txt)
        return 1 if self.fallas else 0


def exe(nombre):
    return rr.exe(nombre)


def nombre_seguro(ruta):
    """El nombre de un proyecto a partir de una ROM: sin extensión y con _ en lugar de lo que no sea letra o número"""
    return re.sub(r"[^A-Za-z0-9]", "_", os.path.splitext(os.path.basename(ruta))[0])


def escribir(ruta, datos):
    os.makedirs(os.path.dirname(ruta) or ".", exist_ok=True)
    with open(ruta, "wb") as f:
        f.write(datos)


def texto(ruta):
    with open(ruta, encoding="utf-8", errors="replace") as f:
        return f.read()


def lineas(ruta):
    return texto(ruta).splitlines()


def borrar(*rutas):
    import shutil
    for r in rutas:
        if os.path.isdir(r):
            shutil.rmtree(r, ignore_errors=True)
        elif os.path.exists(r):
            os.remove(r)
