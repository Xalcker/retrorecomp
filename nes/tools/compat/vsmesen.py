"""Fase 4: compara la imagen de NESRecomp con la de Mesen2 (oráculo) en varios cuadros de cada juego.

Para cada ROM corre nesshot (el intérprete) y Mesen2 sin ventana (tools/oracles/mesen.py shot) con las mismas
entradas guionadas que la prueba de compatibilidad, y cuenta los píxeles distintos en cada cuadro. Los cuadros de
Mesen van uno adelante: su cuadro N corresponde a nuestro N - 1 (verificado con las lecturas de $2002, que coinciden
ciclo a ciclo), así que se compara su cuadro N con --frames N - 1 de nesshot. Mesen cambia los botones en la línea 241
(inputPolled) y nesshot al empezar el cuadro, así que nesshot corre con --entradas-241 y las entradas corridas un cuadro
hacia atrás. Sin eso, un juego que lee el control durante el render (Contra) o en el vblank (SMB) los veía un cuadro
antes o después que en Mesen, según el juego.

Con eso, la mayoría de los juegos queda idéntica a Mesen2 al píxel también con botones. Cualquier diferencia que
quede es un desfase de tiempo real (la partida toma otro rumbo a partir de ahí) o un error de imagen. Cada cuadro se
clasifica: igual, pequeña (menos del 1 % de los píxeles: probable desfase) o grande (más: revisar a
mano). Con --sin-botones se comparan el título y la demo, donde una diferencia es un error de imagen.

Uso (con "make" ya hecho):
    uv run tools/compat/vsmesen.py ROM_O_CARPETA... [--cuadros 450,900,1500,2100] [--sin-botones] [--salida build/vsmesen]
Escribe resultados.json y, para cada cuadro distinto, una imagen con las dos capturas lado a lado (nuestra a la
izquierda, Mesen2 a la derecha).
"""
# /// script
# dependencies = ["pillow"]
# ///
import argparse, json, os, pathlib, re, subprocess, sys
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "compat"))
from run import NES, compat                             # las mismas entradas que la prueba de compatibilidad


def tramos(texto):
    """El guion de la etapa 1 (archivo de entradas: «cuadro BOTONES» en cada cambio) en la sintaxis boton:desde-hasta
    que aceptan nesshot y tools/oracles/mesen.py"""
    abiertos, out = {}, []
    for linea in texto.splitlines():
        if not linea.strip() or linea.startswith("#"):
            continue
        f, pad = linea.split()
        f, pad = int(f), set() if pad == "-" else set(pad.split("+"))
        for b in sorted(set(abiertos) - pad):
            out.append(f"{b.lower()}:{abiertos.pop(b)}-{f - 1}")
        for b in sorted(pad - set(abiertos)):
            abiertos[b] = f
    return " ".join(out)


GUION = tramos(compat.guiones(NES(ROOT))[0])

EXE = ".exe" if os.name == "nt" else ""
PEQUENA = 256 * 240 // 100                              # 1 % de los píxeles
DESFASE = -1                                            # nuestro cuadro = el de Mesen + DESFASE
ENTRADAS = -1                                           # nuestras entradas = las de Mesen + ENTRADAS


def corridas(guion, d):
    """Las mismas entradas d cuadros más tarde"""
    return " ".join(f"{m.group(1)}:{int(m.group(2)) + d}-{int(m.group(3)) + d}"
                    for m in re.finditer(r"(\w+):(\d+)-(\d+)", guion))


def diferencia(a, b):
    a, b = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if a.size != b.size: b = b.resize(a.size)
    pa, pb = a.load(), b.load()
    return sum(1 for y in range(a.height) for x in range(a.width) if pa[x, y] != pb[x, y])


def lado_a_lado(a, b, salida):
    a, b = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    out = Image.new("RGB", (a.width * 2 + 8, a.height), (255, 0, 255))
    out.paste(a, (0, 0)); out.paste(b.resize(a.size), (a.width + 8, 0)); out.save(salida)


def clase(d):
    return "?" if d is None else "igual" if d == 0 else "pequeña" if d < PEQUENA else "grande"


def juego(rom, cuadros, dst, guion):
    nombre = rom.stem
    res = {"rom": rom.name, "cuadros": {}}
    for f in cuadros:
        nos, mes = dst / f"{nombre}_{f}_nos.png", dst / f"{nombre}_{f}_mesen.png"
        subprocess.run([str(ROOT / f"nesshot{EXE}"), str(rom), str(nos), "--frames", str(f + DESFASE), "--entradas-241"] + corridas(guion, ENTRADAS).split(),
                       capture_output=True, timeout=600)
        subprocess.run([sys.executable, str(ROOT / "tools" / "oracles" / "mesen.py"), "shot", str(rom), str(mes), str(f)]
                       + guion.split(), capture_output=True, timeout=900)
        if not nos.exists() or not mes.exists():
            res["cuadros"][f] = None
            continue
        d = diferencia(nos, mes)
        res["cuadros"][f] = d
        if d: lado_a_lado(nos, mes, dst / f"{nombre}_{f}_dif.png")
        nos.unlink(); mes.unlink()
    clases = [clase(v) for v in res["cuadros"].values()]
    res["clase"] = "grande" if "grande" in clases else "?" if "?" in clases else "pequeña" if "pequeña" in clases else "igual"
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roms", nargs="+")
    ap.add_argument("--cuadros", default="450,900,1500,2100")
    ap.add_argument("--salida", default=str(ROOT / "build" / "vsmesen"))
    ap.add_argument("--sin-botones", action="store_true", help="sin entradas (título y demo)")
    a = ap.parse_args()
    cuadros = [int(x) for x in a.cuadros.split(",")]
    dst = pathlib.Path(a.salida); dst.mkdir(parents=True, exist_ok=True)
    roms = []
    for r in a.roms:
        p = pathlib.Path(r)
        roms += sorted(p.glob("*.nes")) if p.is_dir() else [p]
    resultados = []
    for rom in roms:
        r = juego(rom, cuadros, dst, "" if a.sin_botones else GUION)
        resultados.append(r)
        print(f"{r['clase']:8s} {rom.name[:60]:60s} " + " ".join(f"{k}:{'-' if v is None else v}" for k, v in r["cuadros"].items()),
              flush=True)
    json.dump(resultados, open(dst / "resultados.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    cuenta = {c: sum(r["clase"] == c for r in resultados) for c in ("igual", "pequeña", "grande", "?")}
    print(f"\n{len(resultados)} juegos: {cuenta['igual']} iguales a Mesen2 en los {len(cuadros)} cuadros, "
          f"{cuenta['pequeña']} con diferencias pequeñas, {cuenta['grande']} con diferencias grandes (revisar), "
          f"{cuenta['?']} sin captura")


if __name__ == "__main__":
    main()
