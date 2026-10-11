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
corre "bustest (RAM, ROM, Z80, mandos de 3 y 6 botones, SRAM, EEPROM, TMSS, SSF2, VINT/HINT)" "$(exe bustest)"
corre "vdptest (puertos, DMA, estado, planos, ventana, sprites, sombra y resalte)" "$(exe vdptest)"

# 68000: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
if [ -f testroms/680x0/68000/v1/NOP.json.gz ]; then
    for modo in "" --recomp; do
        r=$(SST_OPTS=$modo sh tests/sst68k.sh | tail -1)
        case "$r" in *"(0 familias con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
    done
else
    echo "(sin testroms/680x0 se omiten las SingleStepTests del 68000: make testroms)"
fi

# Interfaz: cada menú y cada diálogo se dibuja sin fallar (--ui-shot, sin ventana), con una ROM
# mínima hecha aquí (vectores, "SEGA" en $100 y un bucle en $200)
if [ -x "$(exe gen)" ]; then
    R=build/tests/minima.bin
    mkdir -p build/tests build/shots
    { printf '\000\377\376\000\000\000\002\000'; head -c 248 /dev/zero; printf 'SEGA MEGA DRIVE '
      head -c 240 /dev/zero; printf '\140\376'; head -c 510 /dev/zero; } > "$R"
    ui=0
    for m in 0 1 2 3 4 5; do
        "$(exe gen)" "$R" --ui-shot build/shots/ui_$m.png --ui-frames 5 --ui-menu $m >/dev/null 2>&1 \
            && [ -s build/shots/ui_$m.png ] || { echo "FALLA ui menú $m"; ui=1; }
    done
    for a in 19 20 21; do       # Controles, Este juego, Acerca de
        "$(exe gen)" "$R" --ui-shot build/shots/ui_a$a.png --ui-frames 5 --ui-action $a >/dev/null 2>&1 \
            && [ -s build/shots/ui_a$a.png ] || { echo "FALLA ui diálogo $a"; ui=1; }
    done
    if [ $ui -eq 0 ]; then echo "ok    gen: los 6 menús y los 3 diálogos se dibujan"; else fallas=$((fallas + 1)); fi
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
    # NBA Jam (USA/Europa): el primer arranque inicializa la EEPROM y el segundo la acepta (no escribe)
    if "$(exe genrecomp)" "$r" | grep -q "99c5bc57fdea7f9df0cd8dec54160b162342344d"; then
        rm -f build/tests/nba.sav
        "$(exe genrun)" "$r" --frames 2000 --sram build/tests/nba.sav > build/tests/nba1.txt
        "$(exe genrun)" "$r" --frames 2000 --sram build/tests/nba.sav > build/tests/nba2.txt
        if grep -q "EEPROM: *256 bytes" build/tests/nba1.txt && grep -q "EEPROM: *0 bytes" build/tests/nba2.txt; then
            echo "ok    NBA Jam guarda su EEPROM y la acepta al volver a arrancar"
        else echo "FALLA NBA Jam y su EEPROM"; fallas=$((fallas + 1)); fi
    fi
done

[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
