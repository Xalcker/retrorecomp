#!/bin/sh
# Pruebas de NESRecomp. Cada una devuelve 0 si pasa.
cd "$(dirname "$0")/.." || exit 1
T=testroms/nes-test-roms
mkdir -p build/shots
fail=0
./nestest $T/other/nestest.nes $T/other/nestest.log || fail=1
./sramtest $T/other/nestest.nes || fail=1
./mappertest || fail=1
./inputtest || fail=1
for g in roms/Super*Bros.\ \(Japan*.nes; do [ -f "$g" ] && { ./statetest "$g" || fail=1; }; done
./statetest $T/instr_test-v5/rom_singles/01-basics.nes 100 100 || fail=1   # MMC1, CHR RAM
# Blargg: instr_test-v5 (opcodes oficiales, mapper 1/MMC1)
for f in $T/instr_test-v5/rom_singles/*.nes; do
    if ./nesshot "$f" build/shots/t.png 3000 >/dev/null; then echo "ok   $(basename "$f")"
    else echo "FALLA $(basename "$f")"; fail=1; fi
done
# MMC3 (mapper 4): revisión normal; la vieja (rev. A / MMC6) con NES_MMC3_ALT=1
NES_MMC3_ALT=1 ./nesshot $T/mmc3_test_2/rom_singles/6-MMC3_alt.nes build/shots/t.png 1500 >/dev/null && echo "ok   mmc3 6-MMC3_alt (rev. vieja)" || { echo "FALLA mmc3 6-MMC3_alt"; fail=1; }
for t in 1-cli_latency 2-nmi_and_brk 3-nmi_and_irq 4-irq_and_dma 5-branch_delays_irq; do
    ./nesshot $T/cpu_interrupts_v2/rom_singles/$t.nes build/shots/t.png 1500 >/dev/null && echo "ok   cpu_interrupts_v2 $t" || { echo "FALLA cpu_interrupts_v2 $t"; fail=1; }
done
for t in 1-clocking 2-details 3-A12_clocking 4-scanline_timing 5-MMC3; do
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
# nesshot --expect y --hash (como gbtest) y nesramdump con las opciones comunes
R=$T/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes
h1=$(./nesshot $R build/shots/h1.png --frames 120 --hash | sed -n 's/^hash //p')
h2=$(./nesshot $R build/shots/h2.png 120 --hash | sed -n 's/^hash //p')
if [ -n "$h1" ] && [ "$h1" = "$h2" ] \
    && ./nesshot $R build/shots/e.png --frames 120 --expect build/shots/h1.png >/dev/null \
    && ! ./nesshot $R build/shots/e.png --frames 2 --expect build/shots/h1.png >/dev/null \
    && [ "$(./nesramdump $R --frames 60 | wc -l)" -eq 2048 ] \
    && [ "$(./nesramdump $R 60)" = "$(./nesramdump $R --frames 60)" ]; then echo "ok   nesshot --expect/--hash, nesramdump"
else echo "FALLA nesshot --expect/--hash, nesramdump"; fail=1; fi
# Recompilador: recompila con traza y compara contra el intérprete (mapper 0 y MMC1)
if [ -x ./nesrecomp ]; then
    for f in $T/other/nestest.nes $T/instr_test-v5/rom_singles/01-basics.nes $T/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes; do
        if TRACE_FRAMES=400 tests/recomp_game.sh "$f" rt_$(basename "$f" .nes | tr -c 'A-Za-z0-9\n' _) 400 | tee build/rt.log | grep -q "^Sin diferencias"; then
            echo "ok   recomp $(basename "$f")"
        else cat build/rt.log; echo "FALLA recomp $(basename "$f")"; fail=1; fi
    done
fi
# difftest: --bench, --screens/--every y --executed (con el proyecto de nestest de arriba)
D=build/out/rt_nestest
if [ -x ./nesrecomp ]; then
    rm -rf build/dt_shots; mkdir -p build/dt_shots
    if "$D/difftest" $T/other/nestest.nes --frames 200 --bench | grep -q "intérprete" \
        && "$D/difftest" $T/other/nestest.nes --frames 200 --screens build/dt_shots --every 100 --executed build/dt_exec.txt >/dev/null \
        && [ -s build/dt_shots/cuadro_000100.png ] && [ -s build/dt_shots/cuadro_000200.png ] \
        && grep -q C build/dt_exec.txt; then echo "ok   difftest --bench/--screens/--executed"
    else echo "FALLA difftest --bench/--screens/--executed"; fail=1; fi
fi
# Descubrimiento: --why, --viz, --events, --html y --codemap
if [ -x ./nesrecomp ]; then
    ./nesrecomp $T/other/nestest.nes --why 4 --viz --events build/ev.txt 2>&1 | grep -q "punto de entrada: vector" \
        && ./nesrecomp $T/other/nestest.nes --html build/ev.html >/dev/null && grep -q 'const D=' build/ev.html \
        && ./nesrecomp $T/other/nestest.nes --codemap build/cm.txt >/dev/null && grep -q C build/cm.txt \
        && [ -s build/ev.txt ] && echo "ok   why/viz/events/html/codemap" || { echo "FALLA why/viz/events/html/codemap"; fail=1; }
fi
# Traza: el formato viejo (DESPLAZAMIENTO:DIRECCION) se lee igual que el de ahora (BANCO:DIRECCION)
if [ -x ./nesrecomp ] && ./nestrace $T/other/nestest.nes build/rt_nueva.trace --frames 300 >/dev/null; then
    tr -d '\r' < build/rt_nueva.trace | while IFS=: read -r b a; do
        printf "%06X:%s\n" $(( (0x$b << 13) | (0x$a & 0x1FFF) )) "$a"
    done > build/rt_vieja.trace
    first=$(head -1 build/rt_nueva.trace | tr -d '\r'); old=$(head -1 build/rt_vieja.trace)
    if grep -q "^0[0-9A-F]:[89A-F]" build/rt_nueva.trace \
        && [ "$(./nesrecomp $T/other/nestest.nes --coverage build/rt_nueva.trace)" = "$(./nesrecomp $T/other/nestest.nes --coverage build/rt_vieja.trace)" ] \
        && [ "$(./nesrecomp $T/other/nestest.nes --why "$first")" = "$(./nesrecomp $T/other/nestest.nes --why "${old%%:*}")" ]; then
        echo "ok   traza BANCO:DIRECCION y la vieja"
    else echo "FALLA traza BANCO:DIRECCION y la vieja"; fail=1; fi
fi
# Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin
# ventana; si cambia a propósito: UI_GOLDEN_UPDATE=1 sh tests/run_tests.sh, y revisa build/shots/)
if [ -x ./nes ]; then
    if sh tools/retrorecomp/shared/ui_golden.sh ./nes $T/other/nestest.nes tests/ui_golden.txt "0 1 2 3 4 5 6 7" "40 42" > build/ui_golden.out 2>&1; then
        echo "ok   interfaz (8 menús, 2 diálogos, contra la referencia; «Este juego» depende de la máquina y no entra)"
    else cat build/ui_golden.out; fail=1; fi
fi
[ $fail = 0 ] && echo "TODO BIEN" || echo "HAY FALLAS"
exit $fail
