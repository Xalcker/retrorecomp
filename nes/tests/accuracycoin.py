#!/usr/bin/env python3
"""Marcador de AccuracyCoin: corre la ROM (Start en la página 1 la hace correr todo), lee los resultados de la RAM
($400-$4FF) y los compara con los nombres del fuente. Uso: tests/accuracycoin.py [cuadros]
xx01 = pasa (el valor >> 2 es el "código de éxito": variante de hardware aceptada); xx10 = falla (el valor >> 2 es la subprueba);
00 = no se ejecutó."""
import re, subprocess, sys, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
frames = sys.argv[1] if len(sys.argv) > 1 else "12000"
names = {}
for l in open(f"{root}/testroms/accuracycoin/AccuracyCoin.asm", errors="replace"):
    m = re.match(r'\s*(result_\w+)\s*=\s*\$([0-9A-Fa-f]+)', l)
    if m and 0x400 <= int(m.group(2), 16) < 0x500: names[int(m.group(2), 16)] = m.group(1)[7:]
out = subprocess.run([f"{root}/ramdump", f"{root}/testroms/accuracycoin/AccuracyCoin.nes", frames, "start:100-103"],
                     capture_output=True, text=True).stdout
ram = {int(a, 16): int(v, 16) for a, v in (l.split() for l in out.splitlines())}
ok = [n for a, n in names.items() if ram.get(a, 0) & 3 == 1]
bad = [(a, n, ram.get(a)) for a, n in sorted(names.items()) if ram.get(a, 0) & 3 == 2]
todo = [n for a, n in sorted(names.items()) if ram.get(a) == 0]
print(f"AccuracyCoin: {len(ok)} pasan, {len(bad)} fallan, {len(todo)} sin ejecutar (de {len(names)})")
for a, n, v in bad: print(f"  FALLA {n:36s} ${a:03X} = {v:02X} (subprueba {v >> 2})")
if todo and "-v" in sys.argv: print("  sin ejecutar:", ", ".join(todo))
sys.exit(0)
