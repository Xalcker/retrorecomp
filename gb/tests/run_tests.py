#!/usr/bin/env python3
"""Corre las ROMs de prueba (testroms/) que aplican a un DMG y resume.
Uso: python3 tests/run_tests.py [filtro]
  filtro: subcadena de la ruta, p. ej. "timer" o "blargg"
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
from pruebas import borrar, escribir, exe, lineas, texto  # noqa: E402

T = "testroms"
FILTRO = sys.argv[1] if len(sys.argv) > 1 else ""
pasa = 0
falla = 0
fallidas = []


def cola(salida, n):
    return "\n".join(salida.rstrip("\n").split("\n")[-n:])


def registrar(ok, detalle=""):
    global pasa, falla
    if ok:
        pasa += 1
    else:
        falla += 1
        fallidas.append(detalle)


def aplica(nombre):
    """¿La prueba entra con el filtro? (el filtro es una subcadena del nombre)"""
    return FILTRO in nombre


def corre(rom, *args):
    """Una ROM de prueba con gbtest"""
    if not aplica(rom):
        return
    codigo, salida = rr.correr([exe("gbtest"), rom, *args])
    registrar(codigo == 0, "  " + cola(salida, 1))


def make_en(nombre):
    return rr.correr(["make", "-s", "-C", f"build/out/{nombre}", "-j4"])


class Registro:
    """La salida acumulada de una cadena de pasos (como el /tmp/gbtest.out del sh: unos pasos la reinician, otros agregan)"""

    def __init__(self):
        self.salida = ""

    def paso(self, resultado, agregar=True):
        codigo, salida = resultado
        self.salida = (self.salida if agregar else "") + salida
        return codigo == 0


def octetos(*trozos):
    """Bytes a partir de números y de bytes ya hechos"""
    b = bytearray()
    for t in trozos:
        b += bytes([t]) if isinstance(t, int) else t
    return bytes(b)


def rom_de_32k(*trozos):
    """Una ROM mínima de 32 KB con el checksum del encabezado ya puesto (0xE7), rellena de ceros"""
    b = octetos(*trozos)
    return b + bytes(32768 - len(b))


ENTRADA = bytes([0x00, 0xC3, 0x50, 0x01])       # nop / jp $0150


def poke(rom, datos, desplazamiento):
    rom[desplazamiento:desplazamiento + len(datos)] = datos


def rom_mbc1(g, misma_cabecera=False):
    """Un MBC1 de 1 MB (64 bancos). En $0150 del banco 0 y del banco G está lo mismo (modo 1, banco alto 1); en $015A, el
    banco 0 tiene un bucle que no debe correr nunca y el banco G llama en bucle a una rutina en $4000 del banco G+3 que
    cuenta en $C000"""
    rom = bytearray(1048576)
    poke(rom, bytes([0x00, 0xC3, 0x50, 0x01, 0xCE]), 0x100)            # nop / jp $0150; un byte del logo
    poke(rom, bytes([0x01, 0x05]), 0x147)                               # MBC1, 1 MB
    poke(rom, bytes([0xE1]), 0x14D)                                     # checksum del encabezado
    poke(rom, bytes([0x3E, 0x01, 0xEA, 0x00, 0x60, 0x3E, 0x01, 0xEA, 0x00, 0x40, 0x18, 0xFE]), 0x150)
    base = g * 16384
    if misma_cabecera:
        poke(rom, bytes([0x00, 0xC3, 0x50, 0x01, 0xCE]), base + 0x100)
    poke(rom, bytes([0x3E, 0x01, 0xEA, 0x00, 0x60, 0x3E, 0x01, 0xEA, 0x00, 0x40, 0x3E, 0x03, 0xEA, 0x00, 0x20, 0xCD, 0x00, 0x40,
                     0x18, 0xF6]), base + 0x150)
    poke(rom, bytes([0x21, 0x00, 0xC0, 0x34, 0x06, 0x20, 0x05, 0x20, 0xFD, 0xC9]), base + 3 * 16384)
    return bytes(rom)


def main():
    rr.entrar_a_la_raiz(__file__)
    x = ".exe" if os.path.isfile("./gbrecomp.exe") else ""
    hay_recomp = os.path.isfile(f"./gbrecomp{x}")
    GB = exe("gb")
    hay_gb = os.path.isfile(GB)

    # --- Blargg ---
    import glob
    for rom in sorted(glob.glob(f"{T}/blargg/cpu_instrs/individual/*.gb")):
        corre(rom, "--seconds", "30")
    corre(f"{T}/blargg/cpu_instrs/cpu_instrs.gb", "--seconds", "60")
    corre(f"{T}/blargg/instr_timing/instr_timing.gb", "--seconds", "5")
    corre(f"{T}/blargg/mem_timing/mem_timing.gb", "--seconds", "10")
    corre(f"{T}/blargg/mem_timing-2/mem_timing.gb", "--seconds", "10", "--expect", f"{T}/blargg/mem_timing-2/mem_timing-dmg-cgb.png")
    corre(f"{T}/blargg/halt_bug.gb", "--seconds", "5", "--expect", f"{T}/blargg/halt_bug-dmg-cgb.png")

    # --- Mooneye: solo las que aplican a DMG (sin -dmg0, -mgb, -sgb, -S) ---
    mooneye = []
    for base in (f"{T}/mooneye-test-suite/acceptance", f"{T}/mooneye-test-suite/emulator-only"):
        for dirpath, _, nombres in os.walk(base):
            mooneye += [os.path.join(dirpath, n).replace("\\", "/") for n in nombres if n.endswith(".gb")]
    for rom in sorted(mooneye):
        if rom.endswith(("-dmg0.gb", "-mgb.gb", "-sgb.gb", "-sgb2.gb", "-S.gb")):
            continue
        corre(rom, "--mooneye", "--seconds", "20")

    # --- MBC3: bancos (incluye MBC30) y reloj (rtc3test admite color: corre en CGB) ---
    corre(f"{T}/mbc3-tester/mbc3-tester.gb", "--seconds", "2", "--expect", f"{T}/mbc3-tester/mbc3-tester-dmg.png")
    corre(f"{T}/rtc3test/rtc3test.gb", "--input", "tests/inputs/rtc3test_basic.txt", "--seconds", "16", "--expect", f"{T}/rtc3test/rtc3test-basic-tests-cgb.png")
    corre(f"{T}/rtc3test/rtc3test.gb", "--input", "tests/inputs/rtc3test_range.txt", "--seconds", "11", "--expect", f"{T}/rtc3test/rtc3test-range-tests-cgb.png")
    corre(f"{T}/rtc3test/rtc3test.gb", "--input", "tests/inputs/rtc3test_subsecond.txt", "--seconds", "30", "--expect", f"{T}/rtc3test/rtc3test-sub-second-writes-cgb.png")

    # --- Game Boy Color ---
    corre(f"{T}/cgb-acid2/cgb-acid2.gbc", "--seconds", "3", "--expect", f"{T}/cgb-acid2/cgb-acid2.png")
    for rom in (f"{T}/same-suite/dma/gbc_dma_cont.gb", f"{T}/same-suite/dma/gdma_addr_mask.gb", f"{T}/same-suite/ppu/blocking_bgpi_increase.gb"):
        corre(rom, "--mooneye", "--seconds", "5")

    # --- GBC en modo de compatibilidad (juegos de DMG en un Game Boy Color) ---
    M = f"{T}/mooneye-test-suite/misc"
    for rom in (f"{M}/boot_regs-cgb.gb", f"{M}/boot_div-cgbABCDE.gb", f"{M}/boot_hwio-C.gb", f"{M}/bits/unused_hwio-C.gb",
                f"{M}/ppu/vblank_stat_intr-C.gb"):
        corre(rom, "--mooneye", "--model", "cgb", "--seconds", "5")

    # --- Super Game Boy: arranque (Mooneye) y multijugador (SameSuite) ---
    A = f"{T}/mooneye-test-suite/acceptance"
    for rom in (f"{A}/boot_regs-sgb.gb", f"{A}/boot_div-S.gb", f"{A}/boot_div2-S.gb", f"{A}/boot_hwio-S.gb",
                f"{T}/same-suite/sgb/command_mlt_req.gb", f"{T}/same-suite/sgb/command_mlt_req_1_incrementing.gb"):
        corre(rom, "--mooneye", "--model", "sgb", "--seconds", "5")

    # --- Super Game Boy multijugador: los controles 2-4 (no es una ROM de prueba: build/sgb_controles lee P1 como un juego) ---
    if aplica("sgb_controles"):
        codigo, salida = rr.correr([exe("build/sgb_controles"), f"{T}/same-suite/sgb/command_mlt_req.gb"])
        mal = next((l for l in salida.splitlines() if "MAL" in l), "")
        registrar(codigo == 0, "  sgb_controles: " + mal)

    # --- inputtest (entradas grabadas, botones, modelo y paleta, --seed, --model) y statetest (guardar un estado,
    #     cargarlo en otra máquina y volver a correr: lo mismo) ---
    def prueba(nombre, prog, *args):
        if not aplica(nombre) or not os.path.isfile(exe(prog)):
            return
        os.makedirs("build/tests", exist_ok=True)
        codigo, salida = rr.correr([exe(prog), *args])
        registrar(codigo == 0, f"  {nombre}: " + cola(salida, 3))

    prueba("inputtest", "inputtest")
    prueba("statetest cpu_instrs", "statetest", f"{T}/blargg/cpu_instrs/cpu_instrs.gb", "300", "300")
    for g in sorted(glob.glob("roms/*.gb")) + sorted(glob.glob("roms/*.gbc")):
        prueba(f"statetest {os.path.basename(g)}", "statetest", g, "300", "300")
        break

    # --- dmg-acid2 ---
    corre(f"{T}/dmg-acid2/dmg-acid2.gb", "--seconds", "3", "--expect", f"{T}/dmg-acid2/dmg-acid2-dmg.png")

    # --- gbrecomp --html (la página común, visor_html.h), --viz --color y --codemap ---
    if hay_recomp and aplica("visor"):
        rom = f"{T}/blargg/cpu_instrs/cpu_instrs.gb"
        os.makedirs("build", exist_ok=True)
        codigo, salida = rr.correr([f"./gbrecomp{x}", rom, "--html", "build/visor.html", "--codemap", "build/codemap.txt"])
        ok = (codigo == 0 and "Descubrimiento GBRecomp" in texto("build/visor.html") and '"ins":[' in texto("build/visor.html")
              and "C" in texto("build/codemap.txt"))
        if ok:
            _, viz = rr.correr([f"./gbrecomp{x}", rom, "--viz", "--viz-time", "0", "--color"])
            escribir("build/viz.txt", viz.encode())
            ok = "\033[" in viz
        registrar(ok, "  gbrecomp --html/--viz --color/--codemap: " + cola(salida, 16))

    # --- gb (el intérprete con ventana): corre y dibuja cada menú sin ventana (--ui-shot) ---
    if hay_gb and aplica("interprete"):
        # Cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (si cambia a propósito:
        # UI_GOLDEN_UPDATE=1 python3 tests/run_tests.py interprete, y revisa build/shots/): Controles (47), Acerca de (49),
        # la barra oculta con F9 (54), y sin barra con --no-menu (aunque se pida un menú) y con --fullscreen
        codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", GB, f"{T}/dmg-acid2/dmg-acid2.gb", "tests/ui_golden.txt",
                                    "0 1 2 3 4 5 6", "47 49 54", "--no-menu --no-menu,--ui-menu,1 --fullscreen"])
        registrar(codigo == 0, "  gb (ventana, capturas): " + cola(salida, 16))

    # --- gbramdump: WRAM y HRAM tras correr la ROM (el equivalente de ramdump de NESRecomp) ---
    if aplica("gbramdump"):
        rom = f"{T}/blargg/cpu_instrs/cpu_instrs.gb"
        _, r1 = rr.correr([exe("gbramdump"), rom, "--frames", "60", "--seed", "3"], stderr=rr.subprocess.DEVNULL)
        _, r2 = rr.correr([exe("gbramdump"), rom, "--frames", "60", "--seed", "3"], stderr=rr.subprocess.DEVNULL)
        registrar(len(r1.rstrip("\n").split("\n")) == 8319 and r1 == r2, "  gbramdump")

    # --- Recompilador: proyecto generado de cpu_instrs, compilado y comparado contra el intérprete con difftest ---
    def recomp_ok(filtro="recomp"):
        return hay_recomp and aplica(filtro)

    if recomp_ok():
        rom = f"{T}/blargg/cpu_instrs/cpu_instrs.gb"
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/cpu_instrs", "--name", "cpu_instrs"]), False)
              and r.paso(make_en("cpu_instrs"))
              and r.paso(rr.correr([f"build/out/cpu_instrs/difftest{x}", rom, "--frames", "3600", "--seed", "1"])))
        registrar(ok, "  recomp cpu_instrs: " + cola(r.salida, 16))
        codigo, salida = rr.correr([f"build/out/cpu_instrs/difftest{x}", rom, "--frames", "5", "--save", "build/cpu_instrs_nada.sav"])
        registrar(codigo == 2 and "no guarda partida" in salida, "  difftest --save rechaza un juego sin partida: " + cola(salida, 2))

    # --- Recompilador: la rutina de DMA de OAM que el juego copia a HRAM. Dos ROMs libres con las dos formas (firstwhite
    #     lleva ld a,N; strikethrough no): gbrecomp la reconoce, el proyecto compila, difftest no encuentra diferencias y
    #     ninguna instrucción de RAM pasa por el intérprete ---
    if recomp_ok():
        for rom in (f"{T}/little-things-gb/firstwhite.gb", f"{T}/strikethrough/strikethrough.gb"):
            n = os.path.basename(rom)[:-3]
            r = Registro()
            ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", f"build/out/{n}", "--name", n]), False)
                  and re.search(r"rutina de DMA en HRAM: [0-9A-F]", r.salida) is not None
                  and r.paso(make_en(n))
                  and r.paso(rr.correr([f"build/out/{n}/difftest{x}", rom, "--frames", "1800", "--seed", "1"]))
                  and "interpretada en RAM" not in r.salida)
            registrar(ok, f"  recomp DMA en HRAM {n}: " + cola(r.salida, 16))

    # --- Recompilador: una interrupción no se atiende antes de la instrucción que sigue a ei. ROM mínima sin pantalla (IE
    #     = VBlank, luego ei / di en bucle): VBlank queda pendiente desde el primer cuadro y, como el di corre antes de que
    #     IME surta efecto, nunca se atiende. El código recompilado sí la atendía: el despachador activaba IME tras el ei y
    #     entraba a la función del di, cuyo CHECK ya veía la interrupción. La ROM se arma aquí mismo (checksum del
    #     encabezado: 0xE7) para no depender de otra descarga ---
    if recomp_ok():
        rom = "build/ei_di.gb"
        escribir(rom, rom_de_32k(bytes(256), ENTRADA, bytes(73), 0xE7, bytes(2),
                                 bytes([0x3E, 0x01, 0xE0, 0xFF, 0xFB, 0xF3, 0x18, 0xFC])))
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/ei_di", "--name", "ei_di"]), False)
              and r.paso(make_en("ei_di"))
              and r.paso(rr.correr([f"build/out/ei_di/difftest{x}", rom, "--frames", "120", "--seed", "1"])))
        registrar(ok, "  recomp ei_di: " + cola(r.salida, 16))

    # --- difftest: el reparto de los ciclos (recompilado, HALT, interrupciones, intérprete). ROM mínima armada aquí
    #     (checksum del encabezado: 0xE7): activa la interrupción de STAT en HBlank y duerme en HALT; en $48 solo hay un
    #     reti. En cada una de las 144 líneas visibles el CPU despierta, el despachador atiende la interrupción (20 ciclos)
    #     y el código recompilado corre reti, jr y halt (32 ciclos): 4.1 % y 6.6 % de los 70224 ciclos del cuadro, y el
    #     resto en HALT ---
    if recomp_ok():
        rom = "build/halt_stat.gb"
        escribir(rom, rom_de_32k(bytes(72), 0xD9, bytes(183), ENTRADA, bytes(73), 0xE7, bytes(2),
                                 bytes([0x3E, 0x08, 0xE0, 0x41, 0x3E, 0x02, 0xE0, 0xFF, 0xFB, 0x76, 0x18, 0xFD])))
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/halt_stat", "--name", "halt_stat"]), False)
              and r.paso(make_en("halt_stat"))
              and r.paso(rr.correr([f"build/out/halt_stat/difftest{x}", rom, "--frames", "120", "--seed", "1"]))
              and re.search(r"^Métrica: .*; ciclos: 6.6% nativo, 0.0% intérprete en ROM, 0.0% intérprete en RAM, "
                            r"4.1% entrada a interrupciones, 89.3% espera \(HALT/STOP\), 0.0% otros$", r.salida, re.M) is not None)
        registrar(ok, "  difftest, reparto de los ciclos: " + cola(r.salida, 16))

    # --- Recompilador: --trace se puede repetir y se leen todas (la GUI de retrorecomp pasa varias, y antes solo contaba
    #     la última). Dos trazas sobre la ROM de ei_di, en este orden: una con código de RAM y otra con un punto de
    #     entrada de ROM. Si solo se leyera la última, el código de RAM no se aprendería ---
    if recomp_ok():
        escribir("build/varias_ram.trace", b"ram C000 06 03\nram C002 05\nram C003 20 FD\n")
        escribir("build/varias_rom.trace", b"00:0150\n")
        codigo, salida = rr.correr([f"./gbrecomp{x}", "build/ei_di.gb", "-o", "build/out/varias_trazas", "--name", "varias_trazas",
                                    "--trace", "build/varias_ram.trace", "--trace", "build/varias_rom.trace"])
        ok = (codigo == 0 and "traza: 1 puntos de entrada" in salida
              and "código en WRAM/HRAM aprendido de la traza: 3 instrucciones" in salida)
        registrar(ok, "  recomp con varias --trace: " + cola(salida, 16))

    # --- Recompilador: código que el juego copia a WRAM, aprendido de la traza. Dos rondas, como ../shared/recomp_game.py:
    #     sin traza, difftest interpreta ese código y apunta sus bytes (--uncovered); con esa traza, gbrecomp lo compila y
    #     difftest ya no lo interpreta. La primera ronda debe interpretar código de RAM y la segunda no (si no, la prueba
    #     no probaría nada). El sleep 1 es por el make 3.81 de macOS, que compara fechas con precisión de un segundo: una
    #     primera ronda rápida deja gen/ram.c regenerado en el mismo segundo que su objeto, make no lo recompila y la
    #     segunda ronda sigue con el código viejo ---
    def ciclo_ram(rom, nombre, cuadros):
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", f"build/out/{nombre}", "--name", nombre]), False)
              and r.paso(make_en(nombre))
              and r.paso(rr.correr([f"build/out/{nombre}/difftest{x}", rom, "--frames", str(cuadros), "--seed", "1",
                                    "--uncovered", f"build/{nombre}.unc"]), False)
              and "interpretada en RAM" in r.salida)
        if ok:
            time.sleep(1)
            ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", f"build/out/{nombre}", "--name", nombre, "--trace", f"build/{nombre}.unc"]), False)
                  and "código en WRAM/HRAM aprendido" in r.salida
                  and r.paso(make_en(nombre))
                  and r.paso(rr.correr([f"build/out/{nombre}/difftest{x}", rom, "--frames", str(cuadros), "--seed", "1"]))
                  and "interpretada en RAM" not in r.salida)
        return ok, r.salida

    if recomp_ok():
        # ROM mínima armada aquí (checksum del encabezado: 0xE7): copia a $C000 una rutina de 21 bytes y la llama en bucle.
        # La rutina tiene un bucle con jr nz, lleva una cuenta en $C020 y la escribe en el operando de su propio ld a,n
        # ($C010), que es lo que cambia en cada llamada
        rom = "build/wram_codigo.gb"
        escribir(rom, rom_de_32k(bytes(256), ENTRADA, bytes(73), 0xE7, bytes(2),
                                 bytes([0x21, 0x70, 0x01, 0x11, 0x00, 0xC0, 0x06, 0x15, 0x2A, 0x12, 0x13, 0x05, 0x20, 0xFA, 0xCD, 0x00,
                                        0xC0, 0x18, 0xFB]),
                                 bytes(13),
                                 bytes([0x06, 0x03, 0x05, 0x20, 0xFD, 0xFA, 0x20, 0xC0, 0x3C, 0xEA, 0x20, 0xC0, 0xEA, 0x10, 0xC0, 0x3E,
                                        0x00, 0xEA, 0x21, 0xC0, 0xC9])))
        ok, salida = ciclo_ram(rom, "wram_codigo", 120)
        registrar(ok, "  recomp código en WRAM (ROM mínima): " + cola(salida, 16))

        def ram_unicas(ruta):
            return sorted({l for l in lineas(ruta) if l.startswith("ram ")}) if os.path.exists(ruta) else []

        # El ejecutable del juego (aquí gb, el intérprete con ventana) también anota ese código cuando se juega con
        # --uncovered, y con las mismas líneas que difftest: las dos herramientas comparten ramtrace.h. Sin esto, jugar
        # para completar la cobertura perdería el código de RAM
        if hay_gb:
            borrar("build/wram_gb.trace")
            rr.correr([GB, rom, "--ui-shot", "build/wram_gb.png", "--ui-frames", "120", "--uncovered", "build/wram_gb.trace"])
            propias = ram_unicas("build/wram_gb.trace")
            registrar(bool(propias) and propias == ram_unicas("build/wram_codigo.unc"),
                      "  código en WRAM anotado por el ejecutable (--uncovered): " + "\n".join(propias[:3]))
        # Y gbtrace, la traza guionada con la que empieza ../shared/recomp_game.py: las mismas líneas también
        if os.path.isfile(exe("gbtrace")):
            codigo, _ = rr.correr([exe("gbtrace"), rom, "build/wram_gbtrace.txt", "--frames", "120"])
            propias = ram_unicas("build/wram_gbtrace.txt")
            registrar(codigo == 0 and bool(propias) and propias == ram_unicas("build/wram_codigo.unc"),
                      "  código en WRAM anotado por gbtrace: " + "\n".join(propias[:3]))
        # Un caso real: interrupt_time, de Blargg, ejecuta desde WRAM unas 600 instrucciones distintas
        ok, salida = ciclo_ram(f"{T}/blargg/interrupt_time/interrupt_time.gb", "interrupt_time", 600)
        registrar(ok, "  recomp código en WRAM (interrupt_time): " + cola(salida, 16))

    # --- Recompilador: otro banco en $0000-$3FFF (el modo 1 del MBC1). Así corre cada juego de un cartucho de varios
    #     juegos (MBC1M, como Bomberman Collection): el menú del banco 0 escribe el modo 1 y los bits altos, y desde la
    #     instrucción siguiente $0000-$3FFF es el banco $10. Ese código se compila en una vista del banco ($0000-$3FFF) y
    #     las trazas lo anotan con su dirección real ("10:015A", no "10:415A"). Antes el despachador no usaba nada en
    #     $0000-$3FFF si el banco no era el 0, y todo el juego pasaba por el intérprete. ROM mínima de 1 MB (MBC1, 64
    #     bancos): en $0150 del banco 0 y del banco G está lo mismo (modo 1, banco alto 1); en $015A, el banco 0 tiene un
    #     bucle que no debe correr nunca y el banco G llama en bucle a una rutina en $4000 del banco G+3 que cuenta en
    #     $C000 ---
    if recomp_ok():
        os.makedirs("build", exist_ok=True)
        # MBC1M: el juego del banco $10 se encuentra sin traza, desde su cabecera, y gbtrace lo anota con su dirección real
        rom = "build/mbc1m.gb"
        escribir(rom, rom_mbc1(16, True))
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/mbc1m", "--name", "mbc1m"]), False)
              and r.paso(make_en("mbc1m"))
              and r.paso(rr.correr([f"build/out/mbc1m/difftest{x}", rom, "--frames", "120", "--seed", "1"]), False)
              and "interpretada en ROM" not in r.salida
              and r.paso(rr.correr([exe("gbtrace"), rom, "build/mbc1m_gbtrace.txt", "--frames", "30"]))
              and "10:015A" in lineas("build/mbc1m_gbtrace.txt") and "10:415A" not in lineas("build/mbc1m_gbtrace.txt"))
        registrar(ok, "  recomp MBC1M (banco $10 en $0000): " + cola(r.salida, 16))
        # MBC1 de 1 MB sin otro juego: el banco $20 en $0000-$3FFF solo se ve en la traza. Dos rondas: la primera lo
        # interpreta y lo anota; con esa traza, la segunda ya no
        rom = "build/mbc1_modo1.gb"
        escribir(rom, rom_mbc1(32))
        r = Registro()
        ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/mbc1_modo1", "--name", "mbc1_modo1"]), False)
              and r.paso(make_en("mbc1_modo1"))
              and r.paso(rr.correr([f"build/out/mbc1_modo1/difftest{x}", rom, "--frames", "120", "--seed", "1",
                                    "--uncovered", "build/mbc1_modo1.unc"]), False)
              and "interpretada en ROM: 20:015A" in r.salida)
        if ok:
            time.sleep(1)
            ok = (r.paso(rr.correr([f"./gbrecomp{x}", rom, "-o", "build/out/mbc1_modo1", "--name", "mbc1_modo1",
                                    "--trace", "build/mbc1_modo1.unc"]), False)
                  and r.paso(make_en("mbc1_modo1"))
                  and r.paso(rr.correr([f"build/out/mbc1_modo1/difftest{x}", rom, "--frames", "120", "--seed", "1"]))
                  and "interpretada en ROM" not in r.salida)
        registrar(ok, "  recomp MBC1 modo 1 (banco $20 en $0000, por la traza): " + cola(r.salida, 16))

    # difftest --estricto: lo que se trazó queda recompilado (y sin traza, el código que falta se detecta)
    codigo, salida = rr.correr([sys.executable, "tests/estricto.py"])
    registrar(codigo == 0, "  difftest --estricto: " + "\n".join(l for l in salida.splitlines() if "FALLA" in l))

    total = pasa + falla
    print(f"Pasan {pasa} de {total}")
    if falla > 0:
        print("Fallan:" + "".join("\n" + f for f in fallidas))
        return 1
    return 0


if __name__ == "__main__":
    rr.utf8()
    sys.exit(main())
