#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): el cartucho, el Z80, el bus, la E/S, el VDP, el sonido y
# la ventana.
cd "$(dirname "$0")/.." || exit 2
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
fallas=0
corre() {   # descripción comando...
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $d"; else echo "FALLA $d"; fallas=$((fallas + 1)); fi
}
# Segundos de cada sección (para ver qué pesa en el CI; GENRecomp#5)
T0=$(date +%s); TS=$T0
mkdir -p build/tests
tiempo() { t=$(date +%s); echo "      ($1: $((t - TS)) s)"; TS=$t; }

corre "carttest (cabeceras TMR SEGA, Codemasters y SDSC, copiador, mapper, modelo, CRC32)" "$(exe carttest)"
corre "z80test --int (interrupción desde un HALT en modos 1 y 2, RETI)" "$(exe z80test)" --int
corre "z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)" "$(exe z80test)" --nmi
corre "bustest (memoria, mappers Sega y Codemasters, RAM del cartucho, puertos del SMS y del GG, VDP, contadores, interrupciones, Pausa)" "$(exe bustest)"
corre "vdptest (modo 4 y TMS, paletas del SMS y del GG, scroll y sus bloqueos, prioridad, sprites, desborde, colisión, ventana del GG)" "$(exe vdptest)"
corre "audiotest (afinación, volumen, ruido, silencio por canal, estéreo del Game Gear)" "$(exe audiotest)"
corre "inputtest (entradas grabadas, 8 botones, \$hex, --seed sin la Pausa, boton:desde-hasta, --model)" "$(exe inputtest)"
tiempo "pruebas unitarias"

# Estados guardados, con el homebrew (make testroms)
for r in testroms/homebrew/waternet.sms testroms/homebrew/waternet.gg testroms/homebrew/dragon_blaster.sms; do
    [ -f "$r" ] && corre "statetest con $(basename "$r")" "$(exe statetest)" "$r" 300 300
done
tiempo "estados guardados"

# Contrato común con los otros cuatro (opciones y comportamiento de las herramientas sin ventana)
r=$(sh tests/contract.sh | tail -1)
case "$r" in *"sin fallas"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
# El juego del contrato (make testroms) por las herramientas: la traza tiene la rutina de la página 3 en la
# ranura 2 (03:8000) y la de la página 2 no (el mapper), la RAM tiene lo que dejan la RAM del cartucho, la
# rutina copiada a $C100 y las interrupciones, y smsshot --expect compara una captura
C=testroms/sms/contrato.sms
if [ -f "$C" ]; then
    D=build/tests; mkdir -p "$D"
    "$(exe smstrace)" "$C" "$D/contrato.trace" --frames 60 >/dev/null 2>&1
    if grep -q "^03:8000$" "$D/contrato.trace" && ! grep -q "^02:8000$" "$D/contrato.trace" && grep -q "^00:0038$" "$D/contrato.trace"; then
        echo "ok    smstrace con el contrato: la página 3 en la ranura 2 (03:8000) y la interrupción (00:0038)"
    else echo "FALLA smstrace con el contrato"; fallas=$((fallas + 1)); fi
    ram=$("$(exe smsramdump)" "$C" --frames 60 pause:30-31 | awk '$1 ~ /^00C00[345]$|^00C101$/ {printf "%s ", $2}')
    if [ "$ram" = "5A A5 01 32 " ]; then echo "ok    smsramdump con el contrato: mapper, RAM del cartucho, Pausa y el código en RAM"
    else echo "FALLA smsramdump con el contrato ($ram)"; fallas=$((fallas + 1)); fi
    "$(exe smsshot)" "$C" "$D/contrato.png" --frames 65 start:30-35 down:60-69 a:60-69 >/dev/null 2>&1
    if "$(exe smsshot)" "$C" "$D/contrato2.png" --frames 65 start:30-35 down:60-69 a:60-69 --expect "$D/contrato.png" >/dev/null 2>&1 \
            && ! "$(exe smsshot)" "$C" "$D/contrato3.png" --frames 65 --expect "$D/contrato.png" >/dev/null 2>&1; then
        echo "ok    smsshot --expect: la misma pantalla pasa y otra falla"
    else echo "FALLA smsshot --expect"; fallas=$((fallas + 1)); fi
fi
tiempo "contrato"

# El recompilador con el juego del contrato: lo que lee la GUI (--dis, --why, --events, --viz) y el proyecto generado,
# compilado y comparado contra el intérprete con difftest (sin diferencias, casi todo en código nativo; lo que queda
# en el intérprete es la rutina de $C100 con su inmediato parchado, que la guarda deja afuera)
if [ -f "$C" ]; then
    D=build/tests; R="$(exe smsrecomp)"
    "$(exe smstrace)" "$C" "$D/contrato_ciclo.trace" --frames 120 start:30-35 down:60-69 a:60-69 pause:100-101 >/dev/null 2>&1
    if "$R" "$C" --dis 0:0:3 | grep -q "^000000  \$0000  F3 .*di$" \
            && "$R" "$C" --trace "$D/contrato_ciclo.trace" --why 03:8000 | grep -q "punto de entrada: traza" \
            && "$R" "$C" --trace "$D/contrato_ciclo.trace" --events "$D/contrato.events" >/dev/null && grep -q "^00C000 8000 traza - traza ld a,\$5A$" "$D/contrato.events" \
            && "$R" "$C" --viz --color | grep -q "celdas con código"; then
        echo "ok    smsrecomp --dis, --why, --events y --viz con el contrato"
    else echo "FALLA smsrecomp --dis, --why, --events o --viz"; fallas=$((fallas + 1)); fi
    P=$D/proy_contrato
    if "$R" "$C" -o "$P" --name contrato --trace "$D/contrato_ciclo.trace" >/dev/null && make -s -C "$P" -j8 >/dev/null 2>&1; then
        x=; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) x=.exe;; esac
        o=$("$P/difftest$x" "$C" --frames 300 start:30-35 down:60-69 a:60-69 pause:100-101 --uncovered "$D/contrato.unc" 2>&1)
        pct=$(echo "$o" | sed -n 's/.*, \([0-9.]*\)% de los ciclos).*/\1/p')
        if echo "$o" | grep -q "^difftest: 300 cuadros iguales" && awk -v p="${pct:-0}" 'BEGIN { exit !(p >= 95) }' \
                && grep -q "^ram C100 3E ??$" "$D/contrato.unc"; then
            echo "ok    el contrato recompilado: 300 cuadros iguales al intérprete, $pct% de los ciclos en código nativo"
        else echo "FALLA el contrato recompilado"; echo "$o" | tail -5; fallas=$((fallas + 1)); fi
    else echo "FALLA smsrecomp -o o make del proyecto del contrato"; fallas=$((fallas + 1)); fi
    # make web y make apk en el proyecto (los arman make webtest y make apktest): las reglas de retrorecomp/shared y la
    # copia de la ROM con su extensión (con la de un .gg, el runtime elige la Game Gear)
    G=testroms/homebrew/waternet.gg
    if [ -f "$P/platform/web.mk" ] && [ -f "$P/platform/shell.html" ] && [ -f "$P/platform/android/android.mk" ] \
            && [ -f "$P/platform/android/RecompActivity.java" ] && grep -q "^WEB_CACHE  := rom.sms$" "$P/Makefile" \
            && grep -q "^include platform/android/android.mk$" "$P/Makefile" && grep -q "make apk" "$P/LEEME.txt" \
            && { [ ! -f "$G" ] || { "$R" "$G" -o "$D/proy_gg" --name gg >/dev/null && grep -q "^APK_CACHE  := rom.gg$" "$D/proy_gg/Makefile"; }; }; then
        echo "ok    el proyecto lleva make web y make apk (la copia de la ROM, rom.sms o rom.gg)"
    else echo "FALLA make web o make apk en el proyecto generado"; fallas=$((fallas + 1)); fi
fi
# El ciclo completo (tests/recomp_game.sh: traza guionada, recompilar, difftest con dos semillas, recompilar con lo
# que cayó al intérprete) con Dragon Blaster (make testroms)
if [ -f testroms/homebrew/dragon_blaster.sms ]; then
    if sh tests/recomp_game.sh testroms/homebrew/dragon_blaster.sms prueba_dragon 900 1 2 > build/tests/ciclo_dragon.out 2>&1 \
            && grep -q "^Sin diferencias.$" build/tests/ciclo_dragon.out; then
        echo "ok    ciclo completo con Dragon Blaster: $(grep "semilla 2" build/tests/ciclo_dragon.out | tail -1 | sed 's/^ *//')"
    else echo "FALLA ciclo completo con Dragon Blaster"; tail -8 build/tests/ciclo_dragon.out; fallas=$((fallas + 1)); fi
fi
tiempo "recompilador"

# Z80: SingleStepTests (make testroms), por el intérprete y por el camino del código generado (z80_ops.h con
# los bytes de la instrucción; también el ciclo del acceso a cada puerto); en los dos, el decodificador
if [ -f testroms/z80/v1/00.json.gz ]; then
    for modo in "" --recomp; do
        r=$(SST_OPTS=$modo sh tests/sstz80.sh | tail -1)
        case "$r" in *"(0 archivos con fallas)"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
    done
else
    echo "(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)"
fi
tiempo "SingleStepTests del Z80 (intérprete y --recomp)"

# ZEXDOC y ZEXALL (make testroms): el ejercitador de instrucciones de Frank Cringle, en el Master System. Corre
# ~1,5 horas de consola (unos 330 000 cuadros) y deja su resultado en la RAM del cartucho: «Tests complete», sin
# «ERROR», y las 79 pruebas en «OK». Las dos a la vez
if [ -f testroms/zexall/zexall.sms ]; then
    for t in zexdoc zexall; do "$(exe smsrun)" testroms/zexall/$t.sms --frames 345000 --sram-text > build/tests/$t.out 2>&1 & done
    wait
    for t in zexdoc zexall; do
        ok=$(sed -n '/^texto de la RAM/,$p' build/tests/$t.out | grep -c " OK$")
        if grep -q "Tests complete" build/tests/$t.out && ! grep -q "ERROR" build/tests/$t.out && [ "$ok" -eq 79 ]; then
            echo "ok    $t: las 79 pruebas del Z80 en OK"
        else echo "FALLA $t ($ok en OK)"; grep "ERROR" build/tests/$t.out | head -5; fallas=$((fallas + 1)); fi
    done
fi
tiempo "ZEXDOC y ZEXALL"

# El homebrew enciende la pantalla y atiende una interrupción de cuadro por cuadro (600 cuadros, con Start y A)
for r in testroms/homebrew/*.sms testroms/homebrew/*.gg; do
    [ -f "$r" ] || continue
    o=$("$(exe smsrun)" "$r" --frames 600 start:120-125 a:300-305 2>&1)
    n=$(echo "$o" | sed -n 's/^interrupciones: *\([0-9]*\).*/\1/p')
    if echo "$o" | grep -q "pantalla encendida" && [ "${n:-0}" -ge 580 ]; then echo "ok    $(basename "$r"): pantalla encendida, $n interrupciones en 600 cuadros"
    else echo "FALLA $(basename "$r")"; echo "$o" | head -8; fallas=$((fallas + 1)); fi
done
tiempo "homebrew"

# Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin
# ventana; si cambia a propósito: UI_GOLDEN_UPDATE=1 sh tests/run_tests.sh, y revisa build/shots/). La ROM es
# mínima (32 KB de NOP, la pantalla queda apagada) para no depender de make testroms; como .gg, la misma ROM da
# la ventana del Game Gear
if [ -x "$(exe sms)" ]; then
    mkdir -p build/tests build/shots
    head -c 32768 /dev/zero > build/tests/minima.sms
    cp build/tests/minima.sms build/tests/minima.gg
    # Cargar estado (5), Controles (34), Acerca de (36) y la barra oculta con F9 (37), los números de A_LOAD_STATE,
    # A_HELP_CONTROLS... en main_sdl.c (Este juego, 35, no: muestra la carpeta de datos), y sin barra con --no-menu
    # (aunque se pida un menú) y con --fullscreen
    if sh tools/retrorecomp/shared/ui_golden.sh "$(exe sms)" build/tests/minima.sms tests/ui_golden.txt "0 1 2 3 4 5 6" "5 34 36 37" \
            "--no-menu --no-menu,--ui-menu,1 --fullscreen" > build/ui_golden.out 2>&1; then
        echo "ok    sms: los 7 menús, 3 diálogos, la barra oculta, --no-menu y --fullscreen coinciden con la referencia"
    else cat build/ui_golden.out; fallas=$((fallas + 1)); fi
    # El Game Gear (la ventana de 160×144, más angosta): el menú Entrada y los Controles; y un juego de SMS en el
    # Game Gear, que se ve entero (256×192)
    if sh tools/retrorecomp/shared/ui_golden.sh "$(exe sms)" build/tests/minima.gg tests/ui_golden_gg.txt "3" "34" \
            "--model,sms" > build/ui_golden_gg.out 2>&1; then
        echo "ok    sms con el Game Gear: el menú Entrada, los Controles y --model sms coinciden con la referencia"
    else cat build/ui_golden_gg.out; fallas=$((fallas + 1)); fi
fi
tiempo "interfaz"

# smsrecomp: un archivo que no puede ser una ROM debe fallar sin caerse, y sin ROM, dar el uso
S="$(exe smsrecomp)"
D=build/tests; mkdir -p "$D"
printf 'no es una rom' > "$D/chica.sms"
if "$S" "$D/chica.sms" >/dev/null 2>&1; then echo "FALLA smsrecomp acepta un archivo de 13 bytes"; fallas=$((fallas + 1))
else echo "ok    smsrecomp rechaza un archivo que no es ROM"; fi
if "$S" 2>&1 | grep -q "uso:"; then echo "ok    smsrecomp sin argumentos da el uso"
else echo "FALLA smsrecomp sin argumentos"; fallas=$((fallas + 1)); fi
if "$S" "$D/chica.sms" --model sg 2>&1 | grep -q "modelo desconocido"; then echo "ok    smsrecomp rechaza un modelo desconocido"
else echo "FALLA smsrecomp --model sg"; fallas=$((fallas + 1)); fi

# smsrecomp --info con las ROMs de prueba (make testroms): SHA1, suma de la cabecera, mapper y modelo
info() {    # rom sha1 mapper modelo [opciones]
    rom=$1; sha=$2; map=$3; mod=$4; shift 4
    [ -f "$rom" ] || return 0
    o=$("$S" "$rom" "$@" 2>&1)
    if echo "$o" | grep -q "^SHA1: *$sha" && echo "$o" | grep -q "^mapper: *$map" && echo "$o" | grep -q "^modelo: *$mod" \
            && ! echo "$o" | grep -q "no coincide"; then echo "ok    smsrecomp --info $(basename "$rom")${*:+ $*}: $map, $mod"
    else echo "FALLA smsrecomp --info $(basename "$rom")${*:+ $*}"; echo "$o"; fallas=$((fallas + 1)); fi
}
info testroms/zexall/zexall.sms 3cdb58bfe62d5374fd8d7cd3df35c2b4f806924c "Sega" "sms (Master System II)"
info testroms/zexall/zexdoc.sms 23e895db089b03d11ea7b448e3f61c4b427279b1 "Sega" "sms (Master System II)"
info testroms/homebrew/dragon_blaster.sms d1c1da577dd14178366245bc55de7d39da839086 "sin mapper" "sms (Master System II)"
info testroms/homebrew/food_fighter.sms 388873a87a38c9a6e4e5fd52b5cac453a3d393b4 "sin mapper" "sms (Master System II)"
info testroms/homebrew/river_strike.sms 8e1142d1f8c730787f1c7ff1cec7d556335c566e "sin mapper" "sms (Master System II)"
info testroms/homebrew/waternet.sms dfb74cda30ade0c35c409f9d72f6af2b5f30a029 "Sega" "sms (Master System II)"
info testroms/homebrew/waternet.gg fd56a237586a20cf3ceea9e3fac20d0a9eae77cf "Sega" "gg (Game Gear)"
info testroms/homebrew/waternet.gg fd56a237586a20cf3ceea9e3fac20d0a9eae77cf "Sega" "sms (Master System II)" --model sms
info testroms/homebrew/dragon_blaster.sms d1c1da577dd14178366245bc55de7d39da839086 "sin mapper" "gg (Game Gear, en su modo SMS)" --model gg
tiempo "smsrecomp --info"

# Las ROMs de roms/ y de sus subcarpetas (tuyas, no se versionan): smsrecomp --info no debe fallar con ninguna, y
# cada una, en 1200 cuadros con Start, enciende la pantalla y atiende al menos una interrupción cada dos cuadros (al
# principio, mientras cargan, muchos juegos las apagan un rato)
for r in roms/*.sms roms/*.gg roms/*/*.sms roms/*/*.gg; do
    [ -f "$r" ] || continue
    corre "smsrecomp --info $(basename "$r")" "$S" "$r" --info
    o=$("$(exe smsrun)" "$r" --frames 1200 start:120-125 start:300-305 2>&1)
    n=$(echo "$o" | sed -n 's/^interrupciones: *\([0-9]*\).*/\1/p')
    if echo "$o" | grep -q "pantalla encendida" && [ "${n:-0}" -ge 600 ]; then echo "ok    $(basename "$r"): pantalla encendida, $n interrupciones en 1200 cuadros"
    else echo "FALLA $(basename "$r")"; echo "$o" | head -8; fallas=$((fallas + 1)); fi
done
tiempo "ROMs de roms/"

echo "      (total: $(($(date +%s) - T0)) s)"
[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
