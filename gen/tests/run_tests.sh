#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): cartucho, 68000 y bus.
cd "$(dirname "$0")/.." || exit 2
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
fallas=0
corre() {   # descripción comando...
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $d"; else echo "FALLA $d"; fallas=$((fallas + 1)); fi
}

corre "carttest (formatos .bin/.smd, cabecera, suma, SRAM, vectores)" "$(exe carttest)"
mkdir -p build/tests
corre "bustest (RAM, ROM, Z80, mandos de 3 y 6 botones, SRAM, SSF2, VINT/HINT)" "$(exe bustest)"

# 68000: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
if [ -f testroms/680x0/68000/v1/NOP.json.gz ]; then
    for modo in "" --recomp; do
        r=$(SST_OPTS=$modo sh tests/sst68k.sh | tail -1)
        case "$r" in *"(0 familias con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
    done
else
    echo "(sin testroms/680x0 se omiten las SingleStepTests del 68000: make testroms)"
fi

# genrecomp: --info con una ROM que no es de Mega Drive debe fallar sin caerse, y sin ROM, dar el uso
D=build/tests; mkdir -p "$D"
printf 'no es una rom' > "$D/chica.bin"
if "$(exe genrecomp)" "$D/chica.bin" >/dev/null 2>&1; then echo "FALLA genrecomp acepta un archivo de 13 bytes"; fallas=$((fallas + 1))
else echo "ok    genrecomp rechaza un archivo que no es ROM"; fi
if "$(exe genrecomp)" 2>&1 | grep -q "uso:"; then echo "ok    genrecomp sin argumentos da el uso"
else echo "FALLA genrecomp sin argumentos"; fallas=$((fallas + 1)); fi

# Las ROMs de roms/ (tuyas, no se versionan): genrecomp --info no debe fallar con ninguna
for r in roms/*.bin roms/*.md roms/*.gen roms/*.smd; do
    [ -f "$r" ] || continue
    corre "genrecomp --info $(basename "$r")" "$(exe genrecomp)" "$r" --info
    # Sonic 1 (USA/Europa): con Start en el título, el intérprete llega a jugar el nivel (modo $0C en $FFF600)
    if "$(exe genrecomp)" "$r" | grep -q "6ddb7de1e17e7f6cdb88927bd906352030daa194"; then
        if "$(exe genrun)" "$r" --frames 900 --watch F600 start:600-605 | grep -q "FFF600\] = \$0C"; then
            echo "ok    Sonic 1 llega al nivel con el intérprete"
        else echo "FALLA Sonic 1 no llega al nivel con el intérprete"; fallas=$((fallas + 1)); fi
    fi
done

[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
