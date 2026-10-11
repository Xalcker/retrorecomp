#!/usr/bin/env python3
"""Oráculo TriCNES: el emulador de referencia del autor de AccuracyCoin, ciclo por ciclo. Se usa su núcleo
(Emulator.cs y mappers/, licencia MIT) sin su ventana, con tricnes_ramdump.cs, y da la misma salida que ramdump.

  tools/oracles/tricnes.py ramdump rom.nes cuadros [boton:desde-hasta ...]   (salida idéntica a ./ramdump)

La primera vez clona TriCNES en un commit fijo a build/oracles/ y lo compila. Si no hay un SDK de .NET instalado,
baja uno portátil con el instalador oficial (dotnet-install) a build/oracles/dotnet, sin tocar el sistema."""
import os, platform, shutil, subprocess, sys, urllib.request

REPO = "https://github.com/100thCoin/TriCNES"
COMMIT = "42944e6d608158eb78c7e435c6aa43e9482be1a9"
AQUI = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(os.path.dirname(os.path.dirname(AQUI)), "build", "oracles")
SRC, BUILD, DOTNET_DIR = (os.path.join(DIR, d) for d in ("tricnes-src", "tricnes", "dotnet"))
WIN = platform.system() == "Windows"
ENTORNO = {**os.environ, "DOTNET_CLI_TELEMETRY_OPTOUT": "1", "DOTNET_NOLOGO": "1",
           "DOTNET_SKIP_FIRST_TIME_EXPERIENCE": "1"}


def fuente():
    if not os.path.isdir(SRC):
        print(f"clonando {REPO}", file=sys.stderr)
        subprocess.run(["git", "clone", "-q", REPO, SRC], check=True)
    if subprocess.run(["git", "-C", SRC, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip() != COMMIT:
        subprocess.run(["git", "-C", SRC, "fetch", "-q", "origin", COMMIT], check=True)
        subprocess.run(["git", "-C", SRC, "checkout", "-q", COMMIT], check=True)


def dotnet():
    """Un dotnet con SDK: el del sistema si lo hay, si no el portátil de build/oracles/dotnet."""
    local = os.path.join(DOTNET_DIR, "dotnet.exe" if WIN else "dotnet")
    if os.path.exists(local): return local
    sistema = shutil.which("dotnet")
    if sistema and subprocess.run([sistema, "--list-sdks"], capture_output=True, text=True).stdout.strip(): return sistema
    script = os.path.join(DIR, "dotnet-install." + ("ps1" if WIN else "sh"))
    os.makedirs(DIR, exist_ok=True)
    print("descargando el SDK de .NET 10 (portátil) a build/oracles/dotnet", file=sys.stderr)
    urllib.request.urlretrieve("https://dot.net/v1/" + os.path.basename(script), script)
    if WIN: cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script]
    else: cmd = ["bash", script]
    subprocess.run(cmd + ["-Channel" if WIN else "--channel", "10.0", "-InstallDir" if WIN else "--install-dir", DOTNET_DIR,
                          "-NoPath" if WIN else "--no-path"], check=True, stdout=sys.stderr)
    return local


def compilar():
    dll = os.path.join(BUILD, "bin", "tricnes_ramdump.dll")
    harness = os.path.join(AQUI, "tricnes_ramdump.cs")
    if os.path.exists(dll) and os.path.getmtime(dll) > os.path.getmtime(harness): return dll
    fuente()
    os.makedirs(BUILD, exist_ok=True)
    archivos = [harness] + [os.path.join(SRC, f) for f in ("Emulator.cs", "6502Documentation.cs")] + sorted(
        os.path.join(SRC, "mappers", f) for f in os.listdir(os.path.join(SRC, "mappers")) if f.endswith(".cs"))
    with open(os.path.join(BUILD, "tricnes_ramdump.csproj"), "w") as f:
        f.write('<Project Sdk="Microsoft.NET.Sdk">\n  <PropertyGroup>\n    <OutputType>Exe</OutputType>\n'
                '    <TargetFramework>net10.0</TargetFramework>\n    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n'
                '    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n    <Nullable>disable</Nullable>\n'
                '    <NoWarn>$(NoWarn);CS0162;CS0168;CS0169;CS0219;CS0414;CS0649;CS8981</NoWarn>\n  </PropertyGroup>\n'
                '  <ItemGroup>\n' + "".join(f'    <Compile Include="{a}" />\n' for a in archivos) + '  </ItemGroup>\n</Project>\n')
    print("compilando TriCNES", file=sys.stderr)
    r = subprocess.run([dotnet(), "build", BUILD, "-c", "Release", "-o", os.path.join(BUILD, "bin"), "-v", "q"],
                       env=ENTORNO, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stdout + r.stderr)
    return dll


def ramdump(rom, cuadros, *botones):
    dll = compilar()
    r = subprocess.run([dotnet(), dll, os.path.abspath(rom), str(int(cuadros)), *botones], env=ENTORNO,
                       capture_output=True, text=True)
    if r.returncode: sys.exit(r.stderr)
    sys.stdout.write(r.stdout)


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] != "ramdump": sys.exit(__doc__)
    ramdump(*sys.argv[2:])
