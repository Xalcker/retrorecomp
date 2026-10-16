#!/usr/bin/env python3
"""Marcador de AccuracyCoin: corre la ROM (Start en la página 1 la hace correr todo), lee los resultados de la RAM
($400-$4FF) y los compara con los nombres del fuente. Uso: tests/accuracycoin.py [cuadros] [-v] [--compara] [--tricnes | --mesen]
xx01 = pasa (el valor >> 2 es el "código de éxito": variante de hardware aceptada); xx10 = falla (el valor >> 2 es la subprueba);
00 = no se ejecutó.
--tricnes / --mesen: el marcador de un oráculo (tools/oracles/) en vez del nuestro. TriCNES es la referencia de
AccuracyCoin (pasa todas); Mesen2 es más rápido pero falla algunas.
--compara: corre el nuestro y el oráculo (TriCNES si no se dice otro) y lista las pruebas en que no coinciden."""
import re, subprocess, sys, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = [a for a in sys.argv[1:] if not a.startswith("-")]
frames = args[0] if args else "12000"
names = {}
for l in open(f"{root}/testroms/accuracycoin/AccuracyCoin.asm", errors="replace"):
    m = re.match(r'\s*(result_\w+)\s*=\s*\$([0-9A-Fa-f]+)', l)
    if m and 0x400 <= int(m.group(2), 16) < 0x500: names[int(m.group(2), 16)] = m.group(1)[7:]
rom = f"{root}/testroms/accuracycoin/AccuracyCoin.nes"

def run(oracle):
    cmd = [sys.executable, f"{root}/tools/oracles/{oracle}.py", "ramdump"] if oracle else [f"{root}/nesramdump"]
    out = subprocess.run(cmd + [rom, frames, "start:100-103"], capture_output=True, text=True).stdout
    return {int(a, 16): int(v, 16) for a, v in (l.split() for l in out.splitlines())}

def state(v): return "pasa" if v & 3 == 1 else f"falla {v >> 2}" if v & 3 == 2 else "no corre"

oracle = "mesen" if "--mesen" in sys.argv else "tricnes" if "--tricnes" in sys.argv or "--compara" in sys.argv else None
if "--compara" in sys.argv:
    ours, ref = run(None), run(oracle)
    title = {"mesen": "Mesen2", "tricnes": "TriCNES"}[oracle]
    diff = [(a, n) for a, n in sorted(names.items()) if state(ours.get(a, 0)) != state(ref.get(a, 0))]
    print(f"AccuracyCoin: pasan {sum(ours.get(a, 0) & 3 == 1 for a in names)} nuestro, "
          f"{sum(ref.get(a, 0) & 3 == 1 for a in names)} {title}; {len(diff)} no coinciden")
    for a, n in diff: print(f"  {n:36s} nuestro: {state(ours.get(a, 0)):10s} {title}: {state(ref.get(a, 0))}")
    sys.exit(0)
ram = run(oracle)
ok = [n for a, n in names.items() if ram.get(a, 0) & 3 == 1]
bad = [(a, n, ram.get(a)) for a, n in sorted(names.items()) if ram.get(a, 0) & 3 == 2]
todo = [n for a, n in sorted(names.items()) if ram.get(a) == 0]
print(f"AccuracyCoin: {len(ok)} pasan, {len(bad)} fallan, {len(todo)} sin ejecutar (de {len(names)})")
for a, n, v in bad: print(f"  FALLA {n:36s} ${a:03X} = {v:02X} (subprueba {v >> 2})")
if todo and "-v" in sys.argv: print("  sin ejecutar:", ", ".join(todo))
sys.exit(0)
