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


def almacen(p, sis, rom, carpeta, cuadros=300):
    """El almacén de trazas por SHA1 (shared/trazas.h y trazas.py) con una ROM de prueba del sistema `sis`:
    - trazas.py agregar y --traces-dir dan el mismo --codemap que --trace con la misma traza;
    - trazas.py sondear deja sondeo.trace en la carpeta del juego, y el recompilador la suma.
    `carpeta` es de trabajo (se borra y se vuelve a crear)"""
    import shutil
    if not os.path.isfile(rom):
        p.falla(f"almacén de trazas: falta {rom}")
        return
    shutil.rmtree(carpeta, ignore_errors=True)
    os.makedirs(carpeta)
    recomp, traza, dir_ = exe(f"{sis}recomp"), f"{carpeta}/plana.trace", f"{carpeta}/almacen"

    def codemap(destino, *opciones):
        c, o = rr.correr([recomp, rom, *opciones, "--codemap", destino], env=dict(os.environ, RETROCOMP_TRAZAS=""))
        return c, o, open(destino, "rb").read() if os.path.isfile(destino) else b""

    rr.correr([exe(f"{sis}trace"), rom, traza, "--frames", str(cuadros), "--seed", "1"])
    c1, _, con_trace = codemap(f"{carpeta}/trace.cm", "--trace", traza)
    c2, _ = rr.correr([sys.executable, "../shared/trazas.py", "agregar", "--dir", dir_, rom, traza])
    c3, o_alm, con_almacen = codemap(f"{carpeta}/almacen.cm", "--traces-dir", dir_)
    p.comprobar(c1 == c2 == c3 == 0 and con_trace and con_trace == con_almacen and "1 trazas del almacén" in o_alm,
                f"{sis}recomp --traces-dir: el almacén por SHA1 da lo mismo que --trace")
    sondeo = f"{carpeta}/sondeo"
    c4, o_s = rr.correr([sys.executable, "../shared/trazas.py", "sondear", "--dir", sondeo, "--cuadros", str(cuadros), rom])
    hecho = [os.path.join(r, "sondeo.trace") for r, _, a in os.walk(sondeo) if "sondeo.trace" in a]
    c5, o_r, _ = codemap(f"{carpeta}/sondeo.cm", "--traces-dir", sondeo)
    p.comprobar(c4 == c5 == 0 and len(hecho) == 1 and f"{os.sep}{sis}{os.sep}" in hecho[0] and "1 trazas del almacén" in o_r,
                "trazas.py sondear deja sondeo.trace en el almacén y el recompilador la suma",
                "trazas.py sondear deja sondeo.trace en el almacén y el recompilador la suma:\n" + (o_s + o_r)[-600:])


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
