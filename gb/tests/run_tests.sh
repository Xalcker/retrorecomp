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

# --- GBC en modo de compatibilidad (juegos de DMG en un Game Boy Color) ---
M=$T/mooneye-test-suite/misc
for rom in "$M"/boot_regs-cgb.gb "$M"/boot_div-cgbABCDE.gb "$M"/boot_hwio-C.gb "$M"/bits/unused_hwio-C.gb \
           "$M"/ppu/vblank_stat_intr-C.gb; do
    corre "$rom" --mooneye --model cgb --seconds 5
done

# --- Super Game Boy: arranque (Mooneye) y multijugador (SameSuite) ---
for rom in "$T"/mooneye-test-suite/acceptance/boot_regs-sgb.gb "$T"/mooneye-test-suite/acceptance/boot_div-S.gb \
           "$T"/mooneye-test-suite/acceptance/boot_div2-S.gb "$T"/mooneye-test-suite/acceptance/boot_hwio-S.gb \
           "$T"/same-suite/sgb/command_mlt_req.gb "$T"/same-suite/sgb/command_mlt_req_1_incrementing.gb; do
    corre "$rom" --mooneye --model sgb --seconds 5
done

# --- Super Game Boy multijugador: los controles 2-4 (no es una ROM de prueba:
#     build/sgb_controles lee P1 como un juego) ---
SGBC=build/sgb_controles
[ -x build/sgb_controles.exe ] && SGBC=build/sgb_controles.exe
if [ -z "$FILTRO" ] || case "sgb_controles" in *"$FILTRO"*) true ;; *) false ;; esac; then
    if "$SGBC" "$T/same-suite/sgb/command_mlt_req.gb" > /tmp/gbtest.out 2>&1; then
        pasa=$((pasa + 1))
    else
        falla=$((falla + 1))
        fallidas="$fallidas
  sgb_controles: $(grep MAL /tmp/gbtest.out | head -1)"
    fi
fi

# --- dmg-acid2 ---
corre "$T/dmg-acid2/dmg-acid2.gb" --seconds 3 --expect "$T/dmg-acid2/dmg-acid2-dmg.png"

# --- gbrecomp --html (visor con los eventos adentro), --viz --color y --codemap ---
if [ -x ./gbrecomp ] || [ -x ./gbrecomp.exe ]; then
    if [ -z "$FILTRO" ] || case "visor" in *"$FILTRO"*) true ;; *) false ;; esac; then
        rom="$T/blargg/cpu_instrs/cpu_instrs.gb"
        mkdir -p build
        if ./gbrecomp "$rom" --html build/visor.html --codemap build/codemap.txt > /tmp/gbtest.out 2>&1 \
            && grep -q 'id="gbrecomp-eventos"' build/visor.html && grep -q '^insn ' build/visor.html \
            && grep -q C build/codemap.txt \
            && ./gbrecomp "$rom" --viz --viz-time 0 --color > build/viz.txt 2>&1 \
            && grep -q "$(printf '\033')\[" build/viz.txt; then
            pasa=$((pasa + 1))
        else
            falla=$((falla + 1))
            fallidas="$fallidas
  gbrecomp --html/--viz --color/--codemap: $(tail -3 /tmp/gbtest.out)"
        fi
    fi
fi

# --- Recompilador: proyecto generado de cpu_instrs, compilado y comparado
#     contra el intérprete con difftest (como en NESRecomp) ---
EXE=
[ -x ./gbrecomp.exe ] && EXE=.exe
if [ -x ./gbrecomp$EXE ] && { [ -z "$FILTRO" ] || case "recomp" in *"$FILTRO"*) true ;; *) false ;; esac; }; then
    rom="$T/blargg/cpu_instrs/cpu_instrs.gb"
    if ./gbrecomp$EXE "$rom" -o out/cpu_instrs --name cpu_instrs > /tmp/gbtest.out 2>&1 \
        && make -s -C out/cpu_instrs -j4 >> /tmp/gbtest.out 2>&1 \
        && out/cpu_instrs/difftest$EXE "$rom" --frames 3600 --seed 1 >> /tmp/gbtest.out 2>&1; then
        pasa=$((pasa + 1))
    else
        falla=$((falla + 1))
        fallidas="$fallidas
  recomp cpu_instrs: $(tail -3 /tmp/gbtest.out)"
    fi
fi

total=$((pasa + falla))
echo "Pasan $pasa de $total"
if [ "$falla" -gt 0 ]; then
    echo "Fallan:$fallidas"
    exit 1
fi
