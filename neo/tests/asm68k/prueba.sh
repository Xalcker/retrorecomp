#!/bin/sh
# Prueba de asm68k (tools/neobios/asm68k.py): ensambla prueba.s, desensambla cada instrucción con dis68k
# (build/dis68kbin) y compara con prueba.dis, la salida revisada a mano (igual que el fuente salvo la forma:
# las etiquetas como direcciones, los números chicos en decimal, move a sr/ccr/usp sin tamaño, dbf = dbra,
# and.b #n = andi.b #n).
cd "$(dirname "$0")/../.." || exit 2
python3 tools/neobios/asm68k.py tests/asm68k/prueba.s build/asmtest.bin --list build/asmtest.lst >/dev/null || exit 1
cut -c1-6 build/asmtest.lst | build/dis68kbin build/asmtest.bin C00000 > build/asmtest.dis || exit 1
if cmp -s build/asmtest.dis tests/asm68k/prueba.dis; then
    echo "ok    asm68k: $(wc -l < tests/asm68k/prueba.dis | tr -d ' ') instrucciones ensambladas y desensambladas igual"
else
    echo "FALLA asm68k: el desensamblado no coincide con tests/asm68k/prueba.dis"
    diff tests/asm68k/prueba.dis build/asmtest.dis | head -10
    exit 1
fi
