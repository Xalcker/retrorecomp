#!/bin/sh
# Pruebas de NESRecomp. Cada una devuelve 0 si pasa.
cd "$(dirname "$0")/.." || exit 1
T=testroms/nes-test-roms
mkdir -p build/shots
fail=0
./nestest $T/other/nestest.nes $T/other/nestest.log || fail=1
./sramtest $T/other/nestest.nes || fail=1
./mappertest || fail=1
for g in roms/Super*Bros.\ \(Japan*.nes; do [ -f "$g" ] && { ./statetest "$g" || fail=1; }; done
./statetest $T/instr_test-v5/rom_singles/01-basics.nes 100 100 || fail=1   # MMC1, CHR RAM
# Blargg: instr_test-v5 (opcodes oficiales, mapper 1/MMC1)
for f in $T/instr_test-v5/rom_singles/*.nes; do
    if ./nesshot "$f" build/shots/t.png 3000 >/dev/null; then echo "ok   $(basename "$f")"
    else echo "FALLA $(basename "$f")"; fail=1; fi
done
# MMC3 (mapper 4): clocking, details, A12 y revisión normal. 4 (temporización fina) y
# 6 (variante alternativa) no pasan todavía.
for t in 1-clocking 2-details 3-A12_clocking 5-MMC3; do
    f=$T/mmc3_test_2/rom_singles/$t.nes
    if ./nesshot "$f" build/shots/t.png 1500 >/dev/null; then echo "ok   mmc3 $t"
    else echo "FALLA mmc3 $t"; fail=1; fi
done
# APU (apu_test de Blargg): longitud, IRQ, jitter, temporización, DMC
for f in $T/apu_test/rom_singles/*.nes; do
    if ./nesshot "$f" build/shots/t.png 1500 >/dev/null; then echo "ok   apu $(basename "$f")"
    else echo "FALLA apu $(basename "$f")"; fail=1; fi
done
# Temporización: ppu_vbl_nmi (10), instr_misc y instr_timing
for f in $T/ppu_vbl_nmi/rom_singles/*.nes $T/instr_misc/instr_misc.nes $T/instr_timing/instr_timing.nes; do
    if ./nesshot "$f" build/shots/t.png 4000 >/dev/null; then echo "ok   $(basename "$f")"
    else echo "FALLA $(basename "$f")"; fail=1; fi
done
# Recompilador: recompila con traza y compara contra el intérprete (mapper 0 y MMC1)
if [ -x ./nesrecomp ]; then
    for f in $T/other/nestest.nes $T/instr_test-v5/rom_singles/01-basics.nes $T/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes; do
        if tests/recomp_game.sh "$f" rt_$(basename "$f" .nes | tr -c 'A-Za-z0-9\n' _) 400 400 | tee build/rt.log | grep -q "cuadros iguales"; then
            echo "ok   recomp $(basename "$f")"
        else cat build/rt.log; echo "FALLA recomp $(basename "$f")"; fail=1; fi
    done
fi
# Descubrimiento: --why, --viz y --events
if [ -x ./nesrecomp ]; then
    ./nesrecomp $T/other/nestest.nes --why 4 --viz --events build/ev.txt 2>&1 | grep -q "punto de entrada: vector" \
        && [ -s build/ev.txt ] && echo "ok   why/viz/events" || { echo "FALLA why/viz/events"; fail=1; }
fi
# Interfaz: cada menú y cada diálogo se dibuja sin fallar (--ui-shot, sin ventana)
if [ -x ./nes ]; then
    for m in 0 1 2 3 4 5 6; do
        ./nes $T/other/nestest.nes --ui-shot build/shots/ui_$m.png --ui-frames 5 --ui-menu $m >/dev/null 2>&1 \
            && [ -s build/shots/ui_$m.png ] || { echo "FALLA ui menú $m"; fail=1; }
    done
    for a in 38 39 40; do
        ./nes $T/other/nestest.nes --ui-shot build/shots/ui_a$a.png --ui-frames 5 --ui-action $a >/dev/null 2>&1 \
            && [ -s build/shots/ui_a$a.png ] || { echo "FALLA ui diálogo $a"; fail=1; }
    done
    echo "ok   interfaz (7 menús, 3 diálogos)"
fi
[ $fail = 0 ] && echo "TODO BIEN" || echo "HAY FALLAS"
exit $fail
