#!/usr/bin/env python3
"""Oráculo Mesen2: corre una ROM en Mesen2 sin ventana (--testrunner) y da el mismo resultado que nuestras
herramientas, para comparar contra una referencia. Mesen2 es GPL-3: no va en el repo; se descarga al usarlo
(versión fija, con su SHA-256) a build/oracles/.

  tools/oracles/mesen.py ramdump rom.nes cuadros [boton:desde-hasta ...]   (salida idéntica a ./ramdump)

Probado en Windows; en Linux necesita SDL2 y en macOS no está probado."""
import hashlib, json, os, platform, stat, subprocess, sys, tempfile, urllib.request, zipfile

VERSION = "2.1.1"
PAQUETES = {  # (sistema, arquitectura) -> (archivo, sha256)
    ("Windows", "x64"): ("Mesen_2.1.1_Windows.zip", "23ccc2bc060b663c68dad3a8c5d6da7d23a50f872d04f135bafa2b04ff7d5cbe"),
    ("Linux", "x64"): ("Mesen_2.1.1_Linux_x64.zip", "7a9947575cc198209f743fef83fb2b702b786ea705506bdf3f2aea01ab7c1ce9"),
    ("Linux", "arm64"): ("Mesen_2.1.1_Linux_ARM64.zip", "b0e028f616ab61ae3247f720a80cbe696a29fcbbf01aec0f0103b3b66b3f4dad"),
    ("Darwin", "x64"): ("Mesen_2.1.1_macOS_x64_Intel.zip", "538f3d072891b1546c961e53f8deb0718d7b273573b16e3318db8f1e9b1ac96d"),
    ("Darwin", "arm64"): ("Mesen_2.1.1_macOS_ARM64_AppleSilicon.zip", "c16068b3d934f7b993b782a0652b8f0e7fc0cde7464862fea91d5e76b68a813f"),
}
AQUI = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(os.path.dirname(os.path.dirname(AQUI)), "build", "oracles", f"mesen-{VERSION}")


def buscar():
    for raiz, _, archivos in os.walk(DIR):
        for a in archivos:
            if a in ("Mesen.exe", "Mesen"): return os.path.join(raiz, a)
    return None


def ejecutable():
    """Devuelve la ruta de Mesen, descargándolo la primera vez."""
    if buscar(): return buscar()
    arq = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    clave = (platform.system(), arq)
    if clave not in PAQUETES: sys.exit(f"Mesen2 {VERSION} no tiene paquete para {clave}")
    nombre, sha = PAQUETES[clave]
    url = f"https://github.com/SourMesen/Mesen2/releases/download/{VERSION}/{nombre}"
    print(f"descargando {url}", file=sys.stderr)
    datos = urllib.request.urlopen(url).read()
    if hashlib.sha256(datos).hexdigest() != sha: sys.exit(f"{nombre}: el SHA-256 no coincide")
    os.makedirs(DIR, exist_ok=True)
    zpath = os.path.join(DIR, nombre)
    with open(zpath, "wb") as f: f.write(datos)
    with zipfile.ZipFile(zpath) as z: z.extractall(DIR)
    os.remove(zpath)
    exe = buscar()
    if not exe: sys.exit(f"no encontré Mesen dentro de {DIR}")
    os.chmod(exe, os.stat(exe).st_mode | stat.S_IXUSR)
    return exe


def correr(rom, script, entorno):
    exe = ejecutable()
    # Con settings.json junto al ejecutable, Mesen queda en modo portátil y no abre el asistente inicial.
    # Lo reescribe al cerrar (y apaga la E/S de Lua), así que se regenera en cada corrida. Sin "Port1" no hay
    # control conectado; la RAM en ceros y la alineación CPU/PPU fija hacen que cada corrida dé lo mismo.
    with open(os.path.join(os.path.dirname(exe), "settings.json"), "w") as f:
        json.dump({"Version": VERSION, "ConfigUpgrade": 5,
                   "Preferences": {"AutomaticallyCheckForUpdates": False, "SingleInstance": False},
                   "Nes": {"Port1": {"Type": "NesController"}, "RamPowerOnState": "AllZeros",
                           "RandomizeCpuPpuAlignment": False, "RandomizeMapperPowerOnState": False},
                   "Debug": {"ScriptWindow": {"AllowIoOsAccess": True}}}, f)
    r = subprocess.run([exe, "--testrunner", os.path.abspath(rom), os.path.join(AQUI, script)],
                       env={**os.environ, **entorno}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    if r.returncode: sys.exit(f"Mesen terminó con código {r.returncode}")


def ramdump(rom, cuadros, *botones):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "ram.txt")
        correr(rom, "mesen_ramdump.lua", {"ORACLE_OUT": out, "ORACLE_FRAMES": str(int(cuadros)),
                                          "ORACLE_HOLD": " ".join(botones)})
        if not os.path.exists(out): sys.exit("Mesen no escribió la RAM (¿falló el script de Lua?)")
        sys.stdout.write(open(out).read())


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] != "ramdump": sys.exit(__doc__)
    ramdump(*sys.argv[2:])
