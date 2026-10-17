#!/usr/bin/env python3
"""Pruebas de NESRecomp (make test). Cada una devuelve 0 si pasa."""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import pruebas  # noqa: E402
import rr  # noqa: E402
from pruebas import borrar, escribir, exe, lineas, texto  # noqa: E402

T = "testroms/nes-test-roms"
SHOT = "build/shots/t.png"


def main():
    rr.entrar_a_la_raiz(__file__)
    p = pruebas.Pruebas()
    os.makedirs("build/shots", exist_ok=True)
    x = ".exe" if os.path.isfile("./nesrecomp.exe") else ""     # en Windows el objetivo del Makefile generado es difftest.exe
    hay_recomp = os.path.isfile(exe("nesrecomp"))
    hay_ventana = os.path.isfile(exe("nes"))

    p.programa(exe("nestest"), f"{T}/other/nestest.nes", f"{T}/other/nestest.log")
    p.programa(exe("sramtest"), f"{T}/other/nestest.nes")
    p.programa(exe("mappertest"))
    p.programa(exe("inputtest"))
    for g in sorted(glob.glob("roms/Super*Bros. (Japan*.nes")):
        p.programa(exe("statetest"), g)
    p.programa(exe("statetest"), f"{T}/instr_test-v5/rom_singles/01-basics.nes", "100", "100")     # MMC1, CHR RAM

    def shot(rom, cuadros, env=None):
        kw = {"env": dict(os.environ, **env)} if env else {}
        return rr.correr([exe("nesshot"), rom, SHOT, str(cuadros)], stderr=rr.subprocess.DEVNULL, **kw)[0] == 0

    def serie(archivos, cuadros, etiqueta=""):
        for f in archivos:
            n = os.path.basename(f)
            if shot(f, cuadros):
                print(f"ok   {etiqueta}{n}")
            else:
                print(f"FALLA {etiqueta}{n}")
                p.fallas += 1

    # Blargg: instr_test-v5 (opcodes oficiales, mapper 1/MMC1)
    serie(sorted(glob.glob(f"{T}/instr_test-v5/rom_singles/*.nes")), 3000)
    # MMC3 (mapper 4): revisión normal; la vieja (rev. A / MMC6) con NES_MMC3_ALT=1
    if shot(f"{T}/mmc3_test_2/rom_singles/6-MMC3_alt.nes", 1500, {"NES_MMC3_ALT": "1"}):
        print("ok   mmc3 6-MMC3_alt (rev. vieja)")
    else:
        print("FALLA mmc3 6-MMC3_alt")
        p.fallas += 1
    for t in ("1-cli_latency", "2-nmi_and_brk", "3-nmi_and_irq", "4-irq_and_dma", "5-branch_delays_irq"):
        if shot(f"{T}/cpu_interrupts_v2/rom_singles/{t}.nes", 1500):
            print(f"ok   cpu_interrupts_v2 {t}")
        else:
            print(f"FALLA cpu_interrupts_v2 {t}")
            p.fallas += 1
    for t in ("1-clocking", "2-details", "3-A12_clocking", "4-scanline_timing", "5-MMC3"):
        if shot(f"{T}/mmc3_test_2/rom_singles/{t}.nes", 1500):
            print(f"ok   mmc3 {t}")
        else:
            print(f"FALLA mmc3 {t}")
            p.fallas += 1
    # APU (apu_test de Blargg): longitud, IRQ, jitter, temporización, DMC
    serie(sorted(glob.glob(f"{T}/apu_test/rom_singles/*.nes")), 1500, "apu ")
    # Temporización: ppu_vbl_nmi (10), instr_misc y instr_timing
    serie(sorted(glob.glob(f"{T}/ppu_vbl_nmi/rom_singles/*.nes")) + [f"{T}/instr_misc/instr_misc.nes", f"{T}/instr_timing/instr_timing.nes"], 4000)

    # nesshot --expect y --hash (como gbtest) y nesramdump con las opciones comunes
    R = f"{T}/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes"
    nulo = rr.subprocess.DEVNULL

    def hash_de(*args):
        salida = rr.correr([exe("nesshot"), R, *args, "--hash"], stderr=nulo)[1]
        return next((l[len("hash "):] for l in salida.splitlines() if l.startswith("hash ")), "")

    h1 = hash_de("build/shots/h1.png", "--frames", "120")
    h2 = hash_de("build/shots/h2.png", "120")

    def volcado(*args):
        return rr.correr([exe("nesramdump"), R, *args], stderr=nulo)[1]

    ok = (h1 and h1 == h2
          and rr.correr([exe("nesshot"), R, "build/shots/e.png", "--frames", "120", "--expect", "build/shots/h1.png"], stderr=nulo)[0] == 0
          and rr.correr([exe("nesshot"), R, "build/shots/e.png", "--frames", "2", "--expect", "build/shots/h1.png"], stderr=nulo)[0] != 0
          and len(volcado("--frames", "60").splitlines()) == 2048
          and volcado("60").rstrip("\n") == volcado("--frames", "60").rstrip("\n"))
    p.comprobar(ok, "nesshot --expect/--hash, nesramdump")

    # Recompilador: recompila con traza y compara contra el intérprete (mapper 0 y MMC1)
    if hay_recomp:
        for f in (f"{T}/other/nestest.nes", f"{T}/instr_test-v5/rom_singles/01-basics.nes", f"{T}/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes"):
            codigo, salida = rr.correr([sys.executable, "../shared/recomp_game.py", f, "rt_" + pruebas.nombre_seguro(f), "400"],
                                       env=dict(os.environ, TRACE_FRAMES="400"))
            escribir("build/rt.log", salida.encode())
            if re.search(r"^Sin diferencias", salida, re.M):
                p.ok(f"recomp {os.path.basename(f)}")
            else:
                sys.stdout.write(salida)
                p.falla(f"recomp {os.path.basename(f)}")

    # difftest: --bench, --screens/--every y --executed (con el proyecto de nestest de arriba)
    D = "build/out/rt_nestest"
    nestest = f"{T}/other/nestest.nes"
    if hay_recomp:
        borrar("build/dt_shots")
        os.makedirs("build/dt_shots")
        ok = ("intérprete" in rr.correr([f"{D}/difftest{x}", nestest, "--frames", "200", "--bench"])[1]
              and rr.correr([f"{D}/difftest{x}", nestest, "--frames", "200", "--screens", "build/dt_shots", "--every", "100",
                             "--executed", "build/dt_exec.txt"])[0] == 0
              and os.path.getsize("build/dt_shots/cuadro_000100.png") > 0 and os.path.getsize("build/dt_shots/cuadro_000200.png") > 0
              and "C" in texto("build/dt_exec.txt"))
        p.comprobar(ok, "difftest --bench/--screens/--executed")

    # Descubrimiento: --why, --viz, --events, --html y --codemap
    if hay_recomp:
        rec = exe("nesrecomp")
        ok = ("punto de entrada: vector" in rr.correr([rec, nestest, "--why", "4", "--viz", "--events", "build/ev.txt"])[1]
              and rr.correr([rec, nestest, "--html", "build/ev.html"])[0] == 0 and "const D=" in texto("build/ev.html")
              and rr.correr([rec, nestest, "--codemap", "build/cm.txt"])[0] == 0 and "C" in texto("build/cm.txt")
              and os.path.getsize("build/ev.txt") > 0)
        p.comprobar(ok, "why/viz/events/html/codemap")

    # Traza: el formato viejo (DESPLAZAMIENTO:DIRECCION) se lee igual que el de ahora (BANCO:DIRECCION)
    if hay_recomp and rr.correr([exe("nestrace"), nestest, "build/rt_nueva.trace", "--frames", "300"])[0] == 0:
        nueva = [l.strip() for l in lineas("build/rt_nueva.trace")]
        vieja = []
        for l in nueva:
            b, _, a = l.partition(":")
            vieja.append("%06X:%s" % ((int(b, 16) << 13) | (int(a, 16) & 0x1FFF), a))
        escribir("build/rt_vieja.trace", "".join(l + "\n" for l in vieja).encode())
        primero, antes = nueva[0], vieja[0]

        def salida(*args):
            return rr.correr([exe("nesrecomp"), nestest, *args], stderr=nulo)[1].rstrip("\n")

        ok = (any(re.match(r"0[0-9A-F]:[89A-F]", l) for l in nueva)
              and salida("--coverage", "build/rt_nueva.trace") == salida("--coverage", "build/rt_vieja.trace")
              and salida("--why", primero) == salida("--why", antes.split(":")[0]))
        p.comprobar(ok, "traza BANCO:DIRECCION y la vieja")

    # Código que el juego copia a la RAM (retrorecomp shared/ram_code.h): una ROM NROM hecha aquí copia una rutina de 9
    # bytes (lda #, sta, un bucle con bne, rts) a la RAM interna ($0300) y a la del cartucho ($6100), llama a las dos en un
    # bucle y parcha el inmediato del lda de la primera (inc $0301). Primera ronda: las dos van al intérprete y la traza
    # las anota (?? en el byte parchado); segunda, con esa traza: nada de RAM en el intérprete y sin diferencias.
    # difftest, nestrace y el ejecutable escriben las mismas líneas "ram", y el ejecutable conserva las que traía el archivo.
    if hay_recomp:
        def hexa(s):
            return bytes(int(b, 16) for b in s.split())

        rom_ram = (b"NES\x1a\x01\x01" + bytes(10)
                   + hexa("A2 00  BD 40 C0  9D 00 03  9D 00 61  E8  E0 09  D0 F2  20 00 03  20 00 61  EE 01 03  4C 10 C0")
                   + bytes(36) + hexa("A9 05  85 10  C6 10  D0 FC  60") + bytes(16305)
                   + hexa("00 C0 00 C0 00 C0") + bytes(8192))
        RR = "build/ram.nes"
        escribir(RR, rom_ram)
        d = "build/out/ram"
        ok = False
        borrar(d, "build/ram.unc", "build/ram2.unc")
        salida1 = salida2 = ""
        if (rr.correr([exe("nesrecomp"), RR, "-o", d, "--name", "ram"])[0] == 0
                and rr.correr(["make", "-C", d, "-j4", "-s", f"difftest{x}"])[0] == 0):
            _, salida1 = rr.correr([f"{d}/difftest{x}", RR, "--frames", "60", "--seed", "1", "--uncovered", "build/ram.unc"])
            escribir("build/ram1.out", salida1.encode())
            unc = lineas("build/ram.unc")
            if (re.search(r"RAM del cartucho [1-9]", salida1) and "ram 0300 A9 ??" in unc and "ram 6100 A9 05" in unc
                    and sum(1 for l in unc if l.startswith("ram")) == 10):
                import time
                time.sleep(1)       # el make de macOS compara fechas con precisión de un segundo
                _, ap = rr.correr([exe("nesrecomp"), RR, "-o", d, "--name", "ram", "--trace", "build/ram.unc"])
                if ("aprendido de la traza: 10 instrucciones (1 con operandos variables) en 2 bloques" in ap
                        and rr.correr(["make", "-C", d, "-j4", "-s", f"difftest{x}"])[0] == 0):
                    _, salida2 = rr.correr([f"{d}/difftest{x}", RR, "--frames", "60", "--seed", "1", "--uncovered", "build/ram2.unc"])
                    escribir("build/ram2.out", salida2.encode())
                    if (re.search(r"^Métrica: 100.0% de las instrucciones nativas", salida2, re.M)
                            and not any(l.startswith("ram") for l in lineas("build/ram2.unc"))):
                        ok = True
        if ok:
            p.ok("código en RAM (interna y del cartucho): la segunda ronda lo compila (10 instrucciones, 1 parchada) y no queda nada en el intérprete")
        else:
            sys.stdout.write(salida1 + salida2)
            p.falla("código en RAM")
        rr.correr([exe("nestrace"), RR, "build/ram_nestrace.txt", "--frames", "60"])
        ram_unc = [l for l in lineas("build/ram.unc") if l.startswith("ram")] if os.path.exists("build/ram.unc") else []
        ram_tr = [l for l in lineas("build/ram_nestrace.txt") if l.startswith("ram")] if os.path.exists("build/ram_nestrace.txt") else []
        if ram_tr == ram_unc:
            p.ok("nestrace anota el mismo código de RAM que difftest")
        else:
            p.falla("nestrace y el código de RAM")
        if hay_ventana:
            escribir("build/ram_exe.txt", b"ram 0500 60\n")
            rr.correr([exe("nes"), RR, "--ui-shot", "build/ram_exe.png", "--ui-frames", "60", "--uncovered", "build/ram_exe.txt"],
                      env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
            exe_ram = [l for l in lineas("build/ram_exe.txt") if l.startswith("ram")]
            if [l for l in exe_ram if not l.startswith("ram 0500")] == ram_unc and "ram 0500 60" in exe_ram:
                p.ok("el ejecutable anota el mismo código de RAM y conserva las líneas que traía")
            else:
                p.falla("el ejecutable y el código de RAM")

    # difftest --estricto: lo que se trazó queda recompilado (y sin traza, el código que falta se detecta)
    p.programa(sys.executable, "tests/estricto.py")

    # Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin
    # ventana; si cambia a propósito: UI_GOLDEN_UPDATE=1 python3 tests/run_tests.py, y revisa build/shots/)
    if hay_ventana:
        # Diálogos Controles (40) y Acerca de (42), la barra oculta con F9 (43), y sin barra con --no-menu (aunque se
        # pida un menú) y con --fullscreen
        codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", exe("nes"), nestest, "tests/ui_golden.txt",
                                    "0 1 2 3 4 5 6 7", "40 42 43", "--no-menu --no-menu,--ui-menu,1 --fullscreen"])
        if codigo == 0:
            p.ok("interfaz (8 menús, 2 diálogos, barra oculta, --no-menu y --fullscreen, contra la referencia; «Este juego» depende de la máquina y no entra)")
        else:
            sys.stdout.write(salida)
            p.fallas += 1
    return p.terminar()


if __name__ == "__main__":
    sys.exit(main())
