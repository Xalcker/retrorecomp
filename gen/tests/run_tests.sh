#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): cartucho, 68000 y bus.
cd "$(dirname "$0")/.." || exit 2
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
fallas=0
corre() {   # descripción comando...
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $d"; else echo "FALLA $d"; fallas=$((fallas + 1)); fi
}
# Segundos de cada sección (para ver qué pesa en el CI; GENRecomp#5)
T0=$(date +%s); TS=$T0
tiempo() { t=$(date +%s); echo "      ($1: $((t - TS)) s)"; TS=$t; }

corre "carttest (formatos .bin/.smd, cabecera, suma, SRAM, vectores)" "$(exe carttest)"
mkdir -p build/tests
corre "bustest (RAM, ROM, Z80, mandos de 3 y 6 botones, SRAM, EEPROM, TMSS, SSF2, VINT/HINT)" "$(exe bustest)"
corre "vdptest (puertos, DMA, estado, planos, ventana, sprites, sombra y resalte)" "$(exe vdptest)"
corre "audiotest (afinación del PSG y del YM2612, panorama, DAC, relajación, temporizador A, ocupado, SSG-EG)" "$(exe audiotest)"
corre "inputtest (entradas grabadas, 12 botones, \$hex, --seed, boton:desde-hasta)" "$(exe inputtest)"
tiempo "pruebas unitarias"
# Estados guardados: con la demo Overdrive (make testroms) y con Sonic 1 si está en roms/
if [ -f testroms/demos/overdrive.bin ]; then corre "statetest con Overdrive (VDP, Z80, YM2612, PSG)" "$(exe statetest)" testroms/demos/overdrive.bin 300 300; fi
for r in roms/Sonic*The*Hedgehog*\(USA,*Europe\).md; do [ -f "$r" ] && corre "statetest con Sonic 1" "$(exe statetest)" "$r" 700 300; done
tiempo "estados guardados"
# Contrato común con NESRecomp y GBRecomp (opciones y comportamiento de las herramientas sin ventana)
r=$(sh tests/contract.sh | tail -1)
case "$r" in *"sin fallas"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
tiempo "contrato"
# Z80: SingleStepTests (make testroms)
if [ -f testroms/z80/v1/00.json.gz ]; then
    r=$(sh tests/sstz80.sh | tail -1)
    case "$r" in *"(0 archivos con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
else
    echo "(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)"
fi
tiempo "SingleStepTests del Z80"

# 68000: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
if [ -f testroms/680x0/68000/v1/NOP.json.gz ]; then
    for modo in "" --recomp; do
        r=$(SST_OPTS=$modo sh tests/sst68k.sh | tail -1)
        case "$r" in *"(0 familias con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
    done
else
    echo "(sin testroms/680x0 se omiten las SingleStepTests del 68000: make testroms)"
fi
tiempo "SingleStepTests del 68000 (intérprete y --recomp)"

# La ROM mínima (tests/minima.sh: vectores, "SEGA" en $100 y un bucle en $200)
R=build/tests/minima.bin
mkdir -p build/tests build/shots
sh tests/minima.sh "$R"

# Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin
# ventana; si cambia a propósito: UI_GOLDEN_UPDATE=1 sh tests/run_tests.sh, y revisa build/shots/)
if [ -x "$(exe gen)" ]; then
    # Cargar estado (5), Controles (37), Acerca de (39) y la barra oculta con F9 (40), los números de A_LOAD_STATE,
    # A_HELP_CONTROLS... en main_sdl.c (Este juego, 38, no: muestra la carpeta de datos), y sin barra con
    # --no-menu (aunque se pida un menú) y con --fullscreen
    if sh tools/retrorecomp/shared/ui_golden.sh "$(exe gen)" "$R" tests/ui_golden.txt "0 1 2 3 4 5 6" "5 37 39 40" \
            "--no-menu --no-menu,--ui-menu,1 --fullscreen" > build/ui_golden.out 2>&1; then
        echo "ok    gen: los 7 menús, 3 diálogos, la barra oculta, --no-menu y --fullscreen coinciden con la referencia"
    else cat build/ui_golden.out; fallas=$((fallas + 1)); fi
fi
tiempo "interfaz"

# genrecomp: --info con una ROM que no es de Genesis debe fallar sin caerse, y sin ROM, dar el uso
D=build/tests; mkdir -p "$D"
printf 'no es una rom' > "$D/chica.bin"
if "$(exe genrecomp)" "$D/chica.bin" >/dev/null 2>&1; then echo "FALLA genrecomp acepta un archivo de 13 bytes"; fallas=$((fallas + 1))
else echo "ok    genrecomp rechaza un archivo que no es ROM"; fi
if "$(exe genrecomp)" 2>&1 | grep -q "uso:"; then echo "ok    genrecomp sin argumentos da el uso"
else echo "FALLA genrecomp sin argumentos"; fallas=$((fallas + 1)); fi

# Recompilar: proyecto generado, compilado (difftest, sin SDL2) y comparado contra el intérprete.
# Con la ROM mínima siempre; con la demo Overdrive (make testroms) y con Sonic 1 (roms/, sin traza:
# las tablas de saltos las encuentra el análisis) si están.
recompila() {   # nombre rom cuadros [traza]
    n=$1; rom=$2; nf=$3; tr=$4
    d=build/tests/proy_$n
    rm -rf "$d"
    if [ -n "$tr" ]; then "$(exe gentrace)" "$rom" "$tr" --frames "$nf" --seed 1 start:120-125 start:300-305 >/dev/null 2>&1; set -- --trace "$tr"; else set --; fi
    if ! "$(exe genrecomp)" "$rom" -o "$d" --name "$n" "$@" >/dev/null 2>&1; then echo "FALLA genrecomp -o con $n"; fallas=$((fallas + 1)); return; fi
    # En Windows el objetivo del Makefile generado es difftest.exe (difftest$(EXE))
    x=; [ -x ./genrecomp.exe ] && x=.exe
    if ! make -C "$d" -j4 -s "difftest$x" >/dev/null 2>&1; then echo "FALLA el proyecto de $n no compila"; fallas=$((fallas + 1)); return; fi
    dt="$d/difftest$x"
    res=$("$dt" "$rom" "$nf" --seed 2 start:120-125 start:300-305 2>&1 | tail -1)
    case "$res" in *"cuadros iguales"*) echo "ok    recompilado $n: $res";; *) echo "FALLA recompilado $n: $res"; fallas=$((fallas + 1));; esac
}
recompila minima "$R" 60
tiempo "proyecto generado: minima"
[ -f testroms/demos/overdrive.bin ] && recompila overdrive testroms/demos/overdrive.bin 600
tiempo "proyecto generado: overdrive"
for r in roms/Sonic*The*Hedgehog*\(USA,*Europe\).md; do
    [ -f "$r" ] || continue
    recompila sonic1 "$r" 1200
    # Cobertura sin traza: de lo que ejecuta en 1200 cuadros, el análisis debe encontrar el 99 % o más
    "$(exe gentrace)" "$r" build/tests/sonic1_traza.txt --frames 1200 --seed 1 start:120-125 start:300-305 >/dev/null 2>&1
    c=$("$(exe genrecomp)" "$r" --coverage build/tests/sonic1_traza.txt | grep "^cobertura")
    case "$c" in *"(99."*|*"(100."*) echo "ok    Sonic 1, $c";; *) echo "FALLA Sonic 1, $c"; fallas=$((fallas + 1));; esac
done
tiempo "proyecto generado y cobertura: Sonic 1 (roms/)"

# Herramientas de análisis de genrecomp con la ROM mínima: --why, --viz, --codemap, --events, --html, --dis
G="$(exe genrecomp)"
an=0
"$G" "$R" --why 200 | grep -q "punto de entrada: vector" || { echo "FALLA genrecomp --why"; an=1; }
"$G" "$R" --viz | grep -q "celdas con código" || { echo "FALLA genrecomp --viz"; an=1; }
"$G" "$R" --codemap build/tests/minima.codemap >/dev/null && [ "$(sed -n 9p build/tests/minima.codemap | cut -c1-3)" = "CC." ] || { echo "FALLA genrecomp --codemap"; an=1; }
"$G" "$R" --events build/tests/minima.events >/dev/null && grep -q "^000200 000200 vector - flujo desde los vectores bra.s \$200" build/tests/minima.events || { echo "FALLA genrecomp --events"; an=1; }
"$G" "$R" --html build/tests/minima.html >/dev/null && grep -q '"ins":\[' build/tests/minima.html || { echo "FALLA genrecomp --html"; an=1; }
"$G" "$R" --dis 200:200:1 | grep -q "bra.s \$200" || { echo "FALLA genrecomp --dis"; an=1; }
# Traza BANCO:DIRECCION (trace_line.h) y la vieja DESPLAZAMIENTO:DIRECCION se leen igual
"$(exe gentrace)" "$R" build/tests/minima.trace --frames 10 >/dev/null 2>&1
tr -d '\r' < build/tests/minima.trace | while IFS=: read -r b a; do
    printf "%06X:%s\n" $(( (0x$b << 19) | (0x$a & 0x7FFFF) )) "$a"
done > build/tests/minima_vieja.trace
grep -q "^00:000200" build/tests/minima.trace \
    && [ "$("$G" "$R" --coverage build/tests/minima.trace)" = "$("$G" "$R" --coverage build/tests/minima_vieja.trace)" ] \
    && [ "$("$G" "$R" --why 00:000200)" = "$("$G" "$R" --why 200)" ] || { echo "FALLA traza BANCO:DIRECCION"; an=1; }
if [ $an -eq 0 ]; then echo "ok    genrecomp --why, --viz, --codemap, --events, --html, --dis y la traza vieja"; else fallas=$((fallas + 1)); fi
tiempo "herramientas de análisis"

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

tiempo "ROMs de roms/"
echo "      (total: $(($(date +%s) - T0)) s)"
[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
