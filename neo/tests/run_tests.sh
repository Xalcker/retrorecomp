#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): el cartucho, la BIOS y neorecomp --info
# (fase 0); el 68000 y el Z80 (fase 1); el bus, la BIOS y las interrupciones (fase 2); el LSPC y la
# ventana (fase 3); el YM2610 y la mezcla (fase 4); las herramientas sin ventana, los estados y el
# contrato común (fase 5); el recompilador (fase 6).
cd "$(dirname "$0")/.." || exit 2
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
fallas=0
corre() {   # descripción comando...
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $d"; else echo "FALLA $d"; fallas=$((fallas + 1)); fi
}
falla() {   # descripción comando...: debe fallar
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "FALLA $d"; fallas=$((fallas + 1)); else echo "ok    $d"; fi
}
# Segundos de cada sección (para ver qué pesa)
T0=$(date +%s); TS=$T0
tiempo() { t=$(date +%s); echo "      ($1: $((t - TS)) s)"; TS=$t; }
mkdir -p build/tests
# La caché de la BIOS recompilada de las pruebas va en build/ (no en la del usuario)
NEO_BIOS_CACHE=$PWD/build/bios-cache; export NEO_BIOS_CACHE

corre "carttest (.neo, zip, carpeta, inflate, sufijos, errores, cifrados, tabla L0, BIOS, identidad del código)" "$(exe carttest)"
corre "z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)" "$(exe z80test)" --nmi
corre "lspctest (capa fija, sprites, volteos, reducciones, pegajosos, prioridad, autoanimación, límite de 96, colores, por línea)" "$(exe lspctest)"
corre "audiotest (FM, SSG con ruido y envolvente, ADPCM-A, ADPCM-B, banderas, temporizadores, ocupado)" "$(exe audiotest)"
corre "inputtest (entradas grabadas, 10 botones, \$hex, --seed, boton:desde-hasta)" "$(exe inputtest)"
corre "bustest (mapa, vectores, P2, mandos, paleta, tarjeta, VRAM, contador de líneas, interrupciones, Z80, YM2610)" "$(exe bustest)"
falla "neorecomp rechaza lo que no es un juego" "$(exe neorecomp)" build/tests/basura.bin
falla "neorecomp sin argumentos pide la ROM" "$(exe neorecomp)"
tiempo "pruebas unitarias"

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

# Estados guardados: con el juego de prueba del contrato (make testroms) y con Metal Slug si está
if [ -f testroms/neo/contrato.neo ]; then corre "statetest con el juego de prueba" "$(exe statetest)" testroms/neo/contrato.neo 120 200; fi
if [ -f roms/mslug.zip ]; then corre "statetest con Metal Slug (CPU, Z80, LSPC, YM2610; sin la BIOS en el estado)" "$(exe statetest)" roms/mslug.zip 700 300; fi
tiempo "estados guardados"
# Contrato común con NESRecomp, GBRecomp y GENRecomp (opciones y comportamiento de las herramientas sin ventana)
r=$(sh tests/contract.sh | tail -1)
case "$r" in *"sin fallas"*) echo "ok    $r";; *) echo "FALLA $r"; fallas=$((fallas + 1));; esac
tiempo "contrato"

# Recompilar: proyecto generado, compilado (difftest, sin SDL2) y comparado contra el intérprete. Con el
# juego de prueba del contrato (make testroms) y con Metal Slug (roms/, sin traza) si están; necesitan la
# BIOS del AES.
recompila() {   # nombre juego cuadros
    n=$1; rom=$2; nf=$3
    d=build/tests/proy_$n
    rm -rf "$d"
    if ! "$(exe neorecomp)" "$rom" -o "$d" --name "$n" >/dev/null 2>&1; then echo "FALLA neorecomp -o con $n"; fallas=$((fallas + 1)); return; fi
    # En Windows el objetivo del Makefile generado es difftest.exe (difftest$(EXE))
    x=; [ -x ./neorecomp.exe ] && x=.exe
    if ! make -C "$d" -j8 -s "difftest$x" >/dev/null 2>&1; then echo "FALLA el proyecto de $n no compila"; fallas=$((fallas + 1)); return; fi
    res=$("$d/difftest$x" "$rom" "$nf" --seed 2 start:120-125 start:300-305 a:400-420 2>&1 | grep "^difftest\|DIFERENCIA" | tail -1)
    case "$res" in *"cuadros iguales"*) echo "ok    recompilado $n: $res";; *) echo "FALLA recompilado $n: $res"; fallas=$((fallas + 1));; esac
}
if [ -f testroms/neo/contrato.neo ] && [ -f roms/aes.zip ]; then recompila contrato testroms/neo/contrato.neo 600; fi
tiempo "proyecto generado: contrato"
if [ -f roms/mslug.zip ] && [ -f roms/aes.zip ]; then recompila mslug roms/mslug.zip 1200; fi
tiempo "proyecto generado: Metal Slug"

# Herramientas de análisis de neorecomp con el juego de prueba (no necesitan la BIOS): --why, --viz,
# --codemap, --events, --html, --dis, y la traza BANCO:DIRECCION
if [ -f testroms/neo/contrato.neo ]; then
    R=testroms/neo/contrato.neo; G="$(exe neorecomp)"
    an=0
    "$G" "$R" --why 400 | grep -q "salto desde" || { echo "FALLA neorecomp --why"; an=1; }
    "$G" "$R" --why 400 | grep -q "punto de entrada: entrada" || { echo "FALLA neorecomp --why (entrada de la cabecera)"; an=1; }
    "$G" "$R" --viz | grep -q "celdas con código" || { echo "FALLA neorecomp --viz"; an=1; }
    "$G" "$R" --codemap build/tests/contrato.codemap >/dev/null && [ "$(sed -n 17p build/tests/contrato.codemap | cut -c1-3)" = "CCC" ] || { echo "FALLA neorecomp --codemap"; an=1; }
    "$G" "$R" --events build/tests/contrato.events >/dev/null && grep -q "^000400 000400 salto 000122 flujo desde los puntos de entrada move.b d0" build/tests/contrato.events || { echo "FALLA neorecomp --events"; an=1; }
    "$G" "$R" --html build/tests/contrato.html >/dev/null && grep -q '"ins":\[' build/tests/contrato.html || { echo "FALLA neorecomp --html"; an=1; }
    "$G" "$R" --dis 400:400:1 | grep -q "move.b d0,(\$300001).l" || { echo "FALLA neorecomp --dis"; an=1; }
    [ "$("$G" "$R" --why 00:000400)" = "$("$G" "$R" --why 400)" ] || { echo "FALLA --why con BANCO:DIRECCION"; an=1; }
    if [ $an -eq 0 ]; then echo "ok    neorecomp --why, --viz, --codemap, --events, --html, --dis"; else fallas=$((fallas + 1)); fi
fi
tiempo "herramientas de análisis"

# Cobertura sin traza (fase 7): de lo que ejecuta Metal Slug en 3000 cuadros (de la demo a la partida),
# el análisis estático solo tiene que encontrar el 99 % o más, y todo lo de la BIOS
if [ -f roms/mslug.zip ] && [ -f roms/aes.zip ]; then
    "$(exe neotrace)" roms/mslug.zip build/tests/mslug_cob.txt --frames 3000 --seed 1 start:1200-1205 start:1500-1505 \
        a:1700-1710 a:1900-1910 right:2000-2600 a:2100-2110 b:2300-2305 >/dev/null 2>&1
    out=$("$(exe neorecomp)" roms/mslug.zip --coverage build/tests/mslug_cob.txt 2>/dev/null)
    pj=$(echo "$out" | sed -n 's/^cobertura: .*(\([0-9.]*\)%).*/\1/p')
    pb=$(echo "$out" | sed -n 's/^cobertura de la BIOS: .*(\([0-9.]*\)%).*/\1/p')
    if awk -v j="${pj:-0}" -v b="${pb:-0}" 'BEGIN { exit !(j >= 99 && b >= 100) }'; then
        echo "ok    cobertura sin traza: Metal Slug $pj%, BIOS $pb%"
    else echo "FALLA cobertura sin traza: Metal Slug ${pj:-?}% (mínimo 99), BIOS ${pb:-?}% (100)"; fallas=$((fallas + 1)); fi
fi
tiempo "cobertura"

# La BIOS propia (fase 10): el ensamblador contra el desensamblador, la tarjeta de memoria contra lo que hace la
# original, el contrato con ella (no necesita la de SNK) y, con Metal Slug, que arranque y que Start empiece la
# partida
x=; [ -x ./neorecomp.exe ] && x=.exe
if command -v python3 >/dev/null 2>&1 && make -s "build/dis68kbin$x" "neocard$x" neobios >/dev/null 2>&1; then
    if sh tests/asm68k/prueba.sh; then :; else fallas=$((fallas + 1)); fi
    if sh tests/neobios/prueba_tarjeta.sh; then :; else fallas=$((fallas + 1)); fi
    if NEO_BIOS="$PWD/build/neobios.bin" sh tests/contract.sh 2>&1 | grep -q "sin fallas"; then
        echo "ok    contrato común con la BIOS propia"
    else echo "FALLA contrato común con la BIOS propia"; fallas=$((fallas + 1)); fi
    if [ -f roms/mslug.zip ]; then
        r=$(NEO_BIOS="$PWD/build/neobios.bin" "$(exe neorun)" roms/mslug.zip --frames 1200 start:700-705 2>&1 | grep "BIOS (RAM)")
        case "$r" in *"USER_REQUEST 2, USER_MODE 2"*) echo "ok    Metal Slug arranca con la BIOS propia y Start empieza la partida";;
            *) echo "FALLA Metal Slug con la BIOS propia: $r"; fallas=$((fallas + 1));; esac
    fi
else
    echo "(sin python3: no se prueba la BIOS propia)"
fi
tiempo "BIOS propia"

# Con los juegos y la BIOS de roms/ (no se versionan)
for r in roms/*.zip roms/*.neo; do
    [ -f "$r" ] || continue
    case "$(basename "$r")" in aes.zip|neogeo.zip) continue;; esac
    out=$("$(exe neorecomp)" "$r" --info 2>&1)
    if [ $? -ne 0 ]; then echo "FALLA neorecomp --info $(basename "$r")"; fallas=$((fallas + 1)); continue; fi
    sha=$(echo "$out" | awk '/^SHA1:/ {print $2}')
    # Exportado como .neo da la misma imagen (el mismo SHA1)
    "$(exe neorecomp)" "$r" --neo build/tests/copia.neo >/dev/null 2>&1
    sha2=$("$(exe neorecomp)" build/tests/copia.neo --info 2>/dev/null | awk '/^SHA1:/ {print $2}')
    if [ -n "$sha" ] && [ "$sha" = "$sha2" ]; then echo "ok    neorecomp --info y --neo con $(basename "$r") (SHA1 $sha)"
    else echo "FALLA neorecomp --neo con $(basename "$r"): $sha / $sha2"; fallas=$((fallas + 1)); fi
    rm -f build/tests/copia.neo
done
# Metal Slug (MAME mslug): reconocido por la base de datos, con el P1 acomodado
if [ -f roms/mslug.zip ]; then
    out=$("$(exe neorecomp)" roms/mslug.zip 2>&1)
    echo "$out" | grep -q "^SHA1: *582233c4d95e4a2d80dd78a9de29b92e0f60d40f" && echo "$out" | grep -q "(mslug," \
        && echo "$out" | grep -q "mitades cambiadas" && echo "$out" | grep -q "USER *\$0007CC" \
        && echo "ok    Metal Slug: base de datos, SHA1, P1 y entradas" || { echo "FALLA Metal Slug con neorecomp --info"; fallas=$((fallas + 1)); }
fi
# La BIOS del AES (aes.zip de MAME): aceptada, y su 000-lo.lo igual a la tabla L0 calculada
if [ -f roms/aes.zip ] && [ -f roms/mslug.zip ]; then
    out=$("$(exe neorecomp)" roms/mslug.zip --bios roms/aes.zip 2>&1)
    echo "$out" | grep -q "^BIOS: .*neo-epo.bin, AES de exportación" && echo "$out" | grep -q "igual a la 000-lo.lo" \
        && echo "ok    BIOS de aes.zip aceptada (neo-epo.bin) y tabla L0 igual a la del chip" || { echo "FALLA BIOS de aes.zip"; fallas=$((fallas + 1)); }
fi
if [ -f roms/neogeo.zip ] && [ -f roms/mslug.zip ]; then
    out=$("$(exe neorecomp)" roms/mslug.zip --bios roms/neogeo.zip 2>&1)
    echo "$out" | grep -q "no tiene ninguna BIOS del AES" && echo "ok    neogeo.zip (solo MVS) no se acepta como BIOS del AES" \
        || { echo "FALLA neogeo.zip no debería aceptarse"; fallas=$((fallas + 1)); }
fi
# Metal Slug arranca con la BIOS del AES: la BIOS le pasa los vectores, llega a la demo
# (USER_REQUEST 2) y con Start empieza la partida (USER_MODE 2, un jugador)
if [ -f roms/mslug.zip ] && [ -f roms/aes.zip ]; then
    out=$("$(exe neorun)" roms/mslug.zip --frames 1800 start:1500-1505 --fix 2>&1)
    echo "$out" | grep -q "^vectores: *del juego" && echo "$out" | grep -q "USER_REQUEST 2, USER_MODE 2" \
        && echo "$out" | grep -q "CREDITS 04" \
        && echo "ok    Metal Slug arranca con la BIOS, llega a la demo (CREDITS 04) y con Start empieza la partida" \
        || { echo "FALLA Metal Slug con la BIOS (neorun)"; fallas=$((fallas + 1)); }
fi
# Imagen: los tres juegos de validación dibujan (40 colores o más en la pantalla) y la ventana dibuja cada
# menú y cada diálogo sin ventana (--ui-shot)
mkdir -p build/shots
for z in mslug:900 samsho2:1200 turfmast:2000; do
    r=${z%%:*}; f=${z#*:}
    [ -f "roms/$r.zip" ] && [ -f roms/aes.zip ] || continue
    c=$("$(exe neorun)" "roms/$r.zip" --frames "$f" --shot "build/shots/$r.png" 2>/dev/null | awk '/^imagen:/ {print $2}')
    if [ "${c:-0}" -ge 40 ]; then echo "ok    $r dibuja (cuadro $f: $c colores, build/shots/$r.png)"
    else echo "FALLA $r no dibuja (cuadro $f: ${c:-?} colores)"; fallas=$((fallas + 1)); fi
done
# Sonido: los tres juegos suenan (nivel por segundo de neorun --wav en los primeros 30 s)
mkdir -p build/wav
for r in mslug samsho2 turfmast; do
    [ -f "roms/$r.zip" ] && [ -f roms/aes.zip ] || continue
    m=$("$(exe neorun)" "roms/$r.zip" --frames 1800 --wav "build/wav/$r.wav" 2>/dev/null | awk '/^audio/ { if ($5 > m) m = $5 } END { print m + 0 }')
    if [ "${m:-0}" -ge 300 ]; then echo "ok    $r suena (nivel máximo $m en 30 s, build/wav/$r.wav)"
    else echo "FALLA $r no suena (nivel máximo ${m:-?})"; fallas=$((fallas + 1)); fi
done
if [ -x "$(exe neo)" ] && [ -f roms/mslug.zip ] && [ -f roms/aes.zip ]; then
    ui=0
    for m in 0 1 2 3 4 5; do
        "$(exe neo)" roms/mslug.zip --ui-shot build/shots/ui_$m.png --ui-frames 5 --ui-menu $m >/dev/null 2>&1 \
            && [ -s build/shots/ui_$m.png ] || { echo "FALLA ui menú $m"; ui=1; }
    done
    for a in 17 18 19; do          # Controles, Este juego, Acerca de
        "$(exe neo)" roms/mslug.zip --ui-shot build/shots/ui_a$a.png --ui-frames 5 --ui-action $a >/dev/null 2>&1 \
            && [ -s build/shots/ui_a$a.png ] || { echo "FALLA ui acción $a"; ui=1; }
    done
    if [ $ui = 0 ]; then echo "ok    neo dibuja cada menú y cada diálogo (--ui-shot)"; else fallas=$((fallas + 1)); fi
fi
tiempo "juegos de roms/"

echo "      (total: $(( $(date +%s) - T0 )) s)"
[ $fallas -eq 0 ] && echo "Todo bien." || echo "$fallas fallas."
[ $fallas -eq 0 ]
