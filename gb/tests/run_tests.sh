#!/bin/sh
# Corre las ROMs de prueba (testroms/) que aplican a un DMG y resume.
# Uso: sh tests/run_tests.sh [filtro]
#   filtro: subcadena de la ruta, p. ej. "timer" o "blargg"

cd "$(dirname "$0")/.." || exit 2
GBTEST=./gbtest
[ -x ./gbtest.exe ] && GBTEST=./gbtest.exe
T=testroms
FILTRO="$1"
pasa=0
falla=0
fallidas=""

corre() {
    case "$1" in *"$FILTRO"*) ;; *) return ;; esac
    if "$GBTEST" "$@" > /tmp/gbtest.out 2>&1; then
        pasa=$((pasa + 1))
    else
        falla=$((falla + 1))
        fallidas="$fallidas
  $(tail -n 1 /tmp/gbtest.out)"
    fi
}

# --- Blargg ---
for rom in "$T"/blargg/cpu_instrs/individual/*.gb; do corre "$rom" --seconds 30; done
corre "$T/blargg/cpu_instrs/cpu_instrs.gb" --seconds 60
corre "$T/blargg/instr_timing/instr_timing.gb" --seconds 5
corre "$T/blargg/mem_timing/mem_timing.gb" --seconds 10
corre "$T/blargg/mem_timing-2/mem_timing.gb" --seconds 10 --expect "$T/blargg/mem_timing-2/mem_timing-dmg-cgb.png"
corre "$T/blargg/halt_bug.gb" --seconds 5 --expect "$T/blargg/halt_bug-dmg-cgb.png"

# --- Mooneye: solo las que aplican a DMG (sin -dmg0, -mgb, -sgb, -S) ---
for rom in $(find "$T/mooneye-test-suite/acceptance" "$T/mooneye-test-suite/emulator-only" -name '*.gb' | sort); do
    case "$rom" in
        *-dmg0.gb|*-mgb.gb|*-sgb.gb|*-sgb2.gb|*-S.gb) continue ;;
    esac
    corre "$rom" --mooneye --seconds 20
done

# --- MBC3: bancos (incluye MBC30) y reloj (rtc3test admite color: corre en CGB) ---
corre "$T/mbc3-tester/mbc3-tester.gb" --seconds 2 --expect "$T/mbc3-tester/mbc3-tester-dmg.png"
corre "$T/rtc3test/rtc3test.gb" --input tests/inputs/rtc3test_basic.txt --seconds 16 --expect "$T/rtc3test/rtc3test-basic-tests-cgb.png"
corre "$T/rtc3test/rtc3test.gb" --input tests/inputs/rtc3test_range.txt --seconds 11 --expect "$T/rtc3test/rtc3test-range-tests-cgb.png"
corre "$T/rtc3test/rtc3test.gb" --input tests/inputs/rtc3test_subsecond.txt --seconds 30 --expect "$T/rtc3test/rtc3test-sub-second-writes-cgb.png"

# --- Game Boy Color ---
corre "$T/cgb-acid2/cgb-acid2.gbc" --seconds 3 --expect "$T/cgb-acid2/cgb-acid2.png"
for rom in "$T"/same-suite/dma/gbc_dma_cont.gb "$T"/same-suite/dma/gdma_addr_mask.gb "$T"/same-suite/ppu/blocking_bgpi_increase.gb; do
    corre "$rom" --mooneye --seconds 5
done

# --- dmg-acid2 ---
corre "$T/dmg-acid2/dmg-acid2.gb" --seconds 3 --expect "$T/dmg-acid2/dmg-acid2-dmg.png"

total=$((pasa + falla))
echo "Pasan $pasa de $total"
if [ "$falla" -gt 0 ]; then
    echo "Fallan:$fallidas"
    exit 1
fi
