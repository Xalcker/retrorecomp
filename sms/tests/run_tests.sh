#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md): por ahora el cartucho y el Z80.
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

corre "carttest (cabeceras TMR SEGA, Codemasters y SDSC, copiador, mapper, modelo, CRC32)" "$(exe carttest)"
corre "z80test --int (interrupción desde un HALT en modos 1 y 2, RETI)" "$(exe z80test)" --int
corre "z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)" "$(exe z80test)" --nmi
tiempo "pruebas unitarias"

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

# Las ROMs de roms/ (tuyas, no se versionan): smsrecomp --info no debe fallar con ninguna
for r in roms/*.sms roms/*.gg; do
    [ -f "$r" ] || continue
    corre "smsrecomp --info $(basename "$r")" "$S" "$r" --info
done
tiempo "ROMs de roms/"

echo "      (total: $(($(date +%s) - T0)) s)"
[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
