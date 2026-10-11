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
corre "audiotest (afinación del PSG y del YM2612, panorama, DAC, relajación, temporizador A, ocupado, SSG-EG)" "$(exe audiotest)"
corre "inputtest (entradas grabadas, 12 botones, \$hex, --seed, boton:desde-hasta)" "$(exe inputtest)"
# Estados guardados: con la demo Overdrive (make testroms) y con Sonic 1 si está en roms/
if [ -f testroms/demos/overdrive.bin ]; then corre "statetest con Overdrive (VDP, Z80, YM2612, PSG)" "$(exe statetest)" testroms/demos/overdrive.bin 300 300; fi
for r in roms/Sonic*The*Hedgehog*\(USA,*Europe\).md; do [ -f "$r" ] && corre "statetest con Sonic 1" "$(exe statetest)" "$r" 700 300; done
# Contrato común con NESRecomp y GBRecomp (opciones y comportamiento de las herramientas sin ventana)
r=$(sh tests/contract.sh | tail -1)
case "$r" in *"sin fallas"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
# Z80: SingleStepTests (make testroms)
if [ -f testroms/z80/v1/00.json.gz ]; then
    r=$(sh tests/sstz80.sh | tail -1)
    case "$r" in *"(0 archivos con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
else
    echo "(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)"
fi

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
    for m in 0 1 2 3 4 5 6; do
        "$(exe gen)" "$R" --ui-shot build/shots/ui_$m.png --ui-frames 5 --ui-menu $m >/dev/null 2>&1 \
            && [ -s build/shots/ui_$m.png ] || { echo "FALLA ui menú $m"; ui=1; }
    done
    for a in 5 37 38 39; do     # Cargar estado, Controles, Este juego, Acerca de (A_LOAD_STATE, A_HELP_CONTROLS... en main_sdl.c)
        "$(exe gen)" "$R" --ui-shot build/shots/ui_a$a.png --ui-frames 5 --ui-action $a >/dev/null 2>&1 \
            && [ -s build/shots/ui_a$a.png ] || { echo "FALLA ui diálogo $a"; ui=1; }
    done
    if [ $ui -eq 0 ]; then echo "ok    gen: los 7 menús y los 4 diálogos se dibujan"; else fallas=$((fallas + 1)); fi
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
    # Juegos con EEPROM (NBA Jam, NBA Jam TE, Mega Man Wily Wars): el primer arranque la inicializa y el
    # segundo la acepta (no escribe). En TE depende también de las banderas del desborde de DIVU (k_DIV).
    for nba in "99c5bc57fdea7f9df0cd8dec54160b162342344d:2000:NBA Jam" "ddbf09c5e6ed5d528ef5ec816129a332c685f103:1500:NBA Jam TE" \
               "26ad72719991e94dbe477c78e65d68f4a4fe51e6:1500:Mega Man Wily Wars"; do
        sha=${nba%%:*}; rest=${nba#*:}; nf=${rest%%:*}; nn=${rest#*:}
        "$(exe genrecomp)" "$r" | grep -q "$sha" || continue
        rm -f build/tests/nba.sav
        "$(exe genrun)" "$r" --frames "$nf" --sram build/tests/nba.sav > build/tests/nba1.txt
        "$(exe genrun)" "$r" --frames "$nf" --sram build/tests/nba.sav > build/tests/nba2.txt
        if grep -q "EEPROM: *[1-9][0-9]* bytes" build/tests/nba1.txt && grep -q "EEPROM: *0 bytes" build/tests/nba2.txt; then
            echo "ok    $nn guarda su EEPROM y la acepta al volver a arrancar"
        else echo "FALLA $nn y su EEPROM"; fallas=$((fallas + 1)); fi
    done
done

[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
