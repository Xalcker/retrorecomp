#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): el cartucho, la BIOS y neorecomp --info
# (fase 0); el 68000 y el Z80 (fase 1); el bus, la BIOS y las interrupciones (fase 2); el LSPC y la
# ventana (fase 3).
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

corre "carttest (.neo, zip, carpeta, inflate, sufijos, errores, cifrados, tabla L0, BIOS, identidad del código)" "$(exe carttest)"
corre "z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)" "$(exe z80test)" --nmi
corre "lspctest (capa fija, sprites, volteos, reducciones, pegajosos, prioridad, autoanimación, límite de 96, colores, por línea)" "$(exe lspctest)"
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

# El contrato común (tests/contract.sh) necesita neotrace, neoshot, neoaudio y neobench: llegan en la
# fase 5.

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
if [ -x "$(exe neo)" ] && [ -f roms/mslug.zip ] && [ -f roms/aes.zip ]; then
    ui=0
    for m in 0 1 2 3 4; do
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
