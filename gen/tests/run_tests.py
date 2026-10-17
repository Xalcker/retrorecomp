#!/usr/bin/env python3
"""Batería de make test. Crece con cada fase (docs/PLAN.md): cartucho, 68000 y bus."""
import glob
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
from pruebas import Pruebas, borrar, escribir, exe, lineas, texto  # noqa: E402

NULO = rr.subprocess.DEVNULL


def hexa(s):
    return bytes(int(b, 16) for b in s.split())


def main():
    rr.entrar_a_la_raiz(__file__)
    p = Pruebas("Todas las pruebas pasaron.", None)
    T0 = time.time()
    x = ".exe" if os.path.isfile("./genrecomp.exe") else ""
    genrecomp = exe("genrecomp")

    p.corre("carttest (formatos .bin/.smd, cabecera, suma, SRAM, vectores)", exe("carttest"))
    os.makedirs("build/tests", exist_ok=True)
    p.corre("z80test --int (interrupción desde un HALT en modos 1 y 2, RETI)", exe("z80test"), "--int")
    p.corre("z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)", exe("z80test"), "--nmi")
    p.corre("bustest (RAM, ROM, Z80, mandos de 3 y 6 botones, SRAM, EEPROM, TMSS, SSF2, VINT/HINT)", exe("bustest"))
    p.corre("vdptest (puertos, DMA, estado, planos, ventana, sprites, sombra y resalte)", exe("vdptest"))
    p.corre("audiotest (afinación del PSG y del YM2612, panorama, DAC, relajación, temporizador A, ocupado, SSG-EG)", exe("audiotest"))
    p.corre("inputtest (entradas grabadas, 12 botones, $hex, --seed, boton:desde-hasta)", exe("inputtest"))
    p.tiempo("pruebas unitarias")
    # Estados guardados: con la demo Overdrive (make testroms) y con Sonic 1 si está en roms/
    if os.path.isfile("testroms/demos/overdrive.bin"):
        p.corre("statetest con Overdrive (VDP, Z80, YM2612, PSG)", exe("statetest"), "testroms/demos/overdrive.bin", "300", "300")
    sonic = sorted(glob.glob("roms/Sonic*The*Hedgehog*(USA,*Europe).md"))
    for r in sonic:
        p.corre("statetest con Sonic 1", exe("statetest"), r, "700", "300")
    p.tiempo("estados guardados")
    # Contrato común con NESRecomp y GBRecomp (opciones y comportamiento de las herramientas sin ventana)
    ultima = rr.correr([sys.executable, "../shared/contrato.py"])[1].rstrip("\n").split("\n")[-1]
    if "sin fallas" in ultima:
        p.ok(ultima)
    else:
        p.falla(ultima)
    p.tiempo("contrato")

    def sst(cpu, archivo, plural, existe, aviso):
        if os.path.isfile(existe):
            for modo in ("", "--recomp"):
                r = rr.correr([sys.executable, "../shared/sst.py", cpu], env=dict(os.environ, SST_OPTS=modo))[1].rstrip("\n").split("\n")[-1]
                if f"(0 {plural} con fallas)" in r:
                    p.ok(r)
                else:
                    p.falla(r)
        else:
            print(aviso)

    # Z80: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
    sst("z80", "00", "archivos", "testroms/z80/v1/00.json.gz", "(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)")
    p.tiempo("SingleStepTests del Z80 (intérprete y --recomp)")
    # 68000: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
    sst("m68k", "NOP", "familias", "testroms/680x0/68000/v1/NOP.json.gz", "(sin testroms/680x0 se omiten las SingleStepTests del 68000: make testroms)")
    p.tiempo("SingleStepTests del 68000 (intérprete y --recomp)")

    # La ROM mínima (1 KB): vectores (SSP $00FFFE00, PC $200), "SEGA MEGA DRIVE " en $100 y un bra.s $200 (60 FE) en $200
    R = "build/tests/minima.bin"
    os.makedirs("build/shots", exist_ok=True)
    escribir(R, bytes([0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x02, 0x00]) + bytes(248) + b"SEGA MEGA DRIVE " + bytes(240)
             + bytes([0x60, 0xFE]) + bytes(510))

    # Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin
    # ventana; si cambia a propósito: UI_GOLDEN_UPDATE=1 python3 tests/run_tests.py, y revisa build/shots/)
    if os.path.isfile(exe("gen")):
        # Cargar estado (5), Controles (37), Acerca de (39) y la barra oculta con F9 (40), los números de A_LOAD_STATE,
        # A_HELP_CONTROLS... en main_sdl.c (Este juego, 38, no: muestra la carpeta de datos), y sin barra con
        # --no-menu (aunque se pida un menú) y con --fullscreen
        codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", exe("gen"), R, "tests/ui_golden.txt", "0 1 2 3 4 5 6",
                                    "5 37 39 40", "--no-menu --no-menu,--ui-menu,1 --fullscreen"])
        escribir("build/ui_golden.out", salida.encode())
        if codigo == 0:
            p.ok("gen: los 7 menús, 3 diálogos, la barra oculta, --no-menu y --fullscreen coinciden con la referencia")
        else:
            sys.stdout.write(salida)
            p.fallas += 1
    p.tiempo("interfaz")

    # genrecomp: --info con una ROM que no es de Genesis debe fallar sin caerse, y sin ROM, dar el uso
    D = "build/tests"
    escribir(f"{D}/chica.bin", b"no es una rom")
    if rr.correr([genrecomp, f"{D}/chica.bin"])[0] == 0:
        p.falla("genrecomp acepta un archivo de 13 bytes")
    else:
        p.ok("genrecomp rechaza un archivo que no es ROM")
    if "uso:" in rr.correr([genrecomp])[1]:
        p.ok("genrecomp sin argumentos da el uso")
    else:
        p.falla("genrecomp sin argumentos")

    # Recompilar: proyecto generado, compilado (difftest, sin SDL2) y comparado contra el intérprete. Con la ROM mínima
    # siempre; con la demo Overdrive (make testroms) y con Sonic 1 (roms/, sin traza: las tablas de saltos las encuentra
    # el análisis) si están.
    def recompila(n, rom, nf, traza=None):
        d = f"build/tests/proy_{n}"
        borrar(d)
        extra = []
        if traza:
            rr.correr([exe("gentrace"), rom, traza, "--frames", str(nf), "--seed", "1", "start:120-125", "start:300-305"])
            extra = ["--trace", traza]
        if rr.correr([genrecomp, rom, "-o", d, "--name", n, *extra])[0] != 0:
            p.falla(f"genrecomp -o con {n}")
            return
        if rr.correr(["make", "-C", d, "-j4", "-s", f"difftest{x}"])[0] != 0:
            p.falla(f"el proyecto de {n} no compila")
            return
        salida = rr.correr([f"{d}/difftest{x}", rom, str(nf), "--seed", "2", "start:120-125", "start:300-305"])[1]
        validas = [l for l in salida.splitlines() if l.startswith("difftest") or "DIFERENCIA" in l]
        res = validas[-1] if validas else ""
        if "cuadros iguales" in res:
            p.ok(f"recompilado {n}: {res}")
        else:
            p.falla(f"recompilado {n}: {res}")

    recompila("minima", R, 60)
    p.tiempo("proyecto generado: minima")

    # Código que el juego copia a RAM (retrorecomp shared/ram_code.h): una ROM hecha aquí copia una rutina a $00FF0000 y a
    # $FFFF8000 (con JSR ($8000).w el PC queda en $FFFF8000), las llama en un bucle y parcha un operando de la primera
    # (addq.b #1,$FF0003). Primera ronda: todo lo de RAM va al intérprete y la traza lo anota (con ?? en el byte
    # parchado); segunda, con esa traza: nada de RAM en el intérprete y sin diferencias. difftest, gentrace y el
    # ejecutable escriben las mismas líneas "ram", y el ejecutable conserva las que traía el archivo.
    RR_ = "build/tests/ram.bin"
    escribir(RR_, bytes([0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x02, 0x00]) + bytes(248) + b"SEGA MEGA DRIVE " + bytes(240)
             + hexa("41 F9 00 00 02 40  43 F9 00 FF 00 00  30 3C 00 09  32 D8  51 C8 FF FC "
                    "41 F9 00 00 02 40  43 F8 80 00  30 3C 00 09  32 D8  51 C8 FF FC "
                    "4E B9 00 FF 00 00  4E B8 80 00  52 39 00 FF 00 03  60 EE  4E 71 4E 71 "
                    "30 3C 00 05  72 00  D2 40  51 C8 FF FC  33 C1 00 FF 10 00  4E 75")
             + bytes(428))
    d = "build/tests/proy_ram"
    borrar(d, f"{D}/ram.unc", f"{D}/ram2.unc")
    ok = False
    s1 = s2 = ""
    if (rr.correr([genrecomp, RR_, "-o", d, "--name", "ram"])[0] == 0
            and rr.correr(["make", "-C", d, "-j4", "-s", f"difftest{x}"])[0] == 0):
        _, s1 = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "60", "--seed", "1", "--uncovered", f"{D}/ram.unc"])
        escribir(f"{D}/ram1.out", s1.encode())
        unc = lineas(f"{D}/ram.unc")
        if ("RAM 100.0%" in s1 and "ram 00FF0000 30 3C 00 ??" in unc and sum(1 for l in unc if l.startswith("ram FFFF80")) == 6):
            time.sleep(1)       # el make de macOS compara fechas con precisión de un segundo
            _, ap = rr.correr([genrecomp, RR_, "-o", d, "--name", "ram", "--trace", f"{D}/ram.unc"])
            if ("aprendido de la traza: 12 instrucciones (1 con operandos variables) en 2 bloques" in ap
                    and rr.correr(["make", "-C", d, "-j4", "-s", f"difftest{x}"])[0] == 0):
                _, s2 = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "60", "--seed", "1", "--uncovered", f"{D}/ram2.unc"])
                escribir(f"{D}/ram2.out", s2.encode())
                if (re.search(r"^Métrica: 100.0% de las instrucciones nativas", s2, re.M)
                        and not any(l.startswith("ram") for l in lineas(f"{D}/ram2.unc"))):
                    ok = True
    if ok:
        p.ok("código en RAM: la segunda ronda lo compila (12 instrucciones, 1 parchada) y no queda nada en el intérprete")
    else:
        p.falla("código en RAM")
        sys.stdout.write(s1 + s2)

    def ram_de(ruta):
        return [l for l in lineas(ruta) if l.startswith("ram")] if os.path.exists(ruta) else []

    rr.correr([exe("gentrace"), RR_, f"{D}/ram_gentrace.txt", "--frames", "60"])
    if ram_de(f"{D}/ram_gentrace.txt") == ram_de(f"{D}/ram.unc"):
        p.ok("gentrace anota el mismo código de RAM que difftest")
    else:
        p.falla("gentrace y el código de RAM")
    if os.path.isfile(exe("gen")):
        escribir(f"{D}/ram_exe.txt", b"ram 00FF2000 4E 75\n")
        rr.correr([exe("gen"), RR_, "--ui-shot", f"{D}/ram_exe.png", "--ui-frames", "60", "--uncovered", f"{D}/ram_exe.txt"],
                  env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
        propias = ram_de(f"{D}/ram_exe.txt")
        if [l for l in propias if "00FF2000" not in l] == ram_de(f"{D}/ram.unc") and "ram 00FF2000 4E 75" in propias:
            p.ok("el ejecutable anota el mismo código de RAM y conserva las líneas que traía")
        else:
            p.falla("el ejecutable y el código de RAM")
    p.tiempo("código en RAM")
    # RAPIDAS=1 (el CI de cada PR): sin el proyecto de Overdrive, lo más caro (corre en main y en el nocturno)
    if not os.environ.get("RAPIDAS") and os.path.isfile("testroms/demos/overdrive.bin"):
        recompila("overdrive", "testroms/demos/overdrive.bin", 600)
    p.tiempo("proyecto generado: overdrive")
    for r in sonic:
        recompila("sonic1", r, 1200)
        # Cobertura sin traza: de lo que ejecuta en 1200 cuadros, el análisis debe encontrar el 99 % o más
        rr.correr([exe("gentrace"), r, "build/tests/sonic1_traza.txt", "--frames", "1200", "--seed", "1", "start:120-125", "start:300-305"])
        cob = rr.correr([genrecomp, r, "--coverage", "build/tests/sonic1_traza.txt"], stderr=NULO)[1]
        c = next((l for l in cob.splitlines() if l.startswith("cobertura")), "")
        if "(99." in c or "(100." in c:
            p.ok(f"Sonic 1, {c}")
        else:
            p.falla(f"Sonic 1, {c}")
    p.tiempo("proyecto generado y cobertura: Sonic 1 (roms/)")

    # Herramientas de análisis de genrecomp con la ROM mínima: --why, --viz, --codemap, --events, --html, --dis
    G = genrecomp
    an = 0

    def salida(*args):
        return rr.correr([G, R, *args], stderr=NULO)

    if "punto de entrada: vector" not in salida("--why", "200")[1]:
        print("FALLA genrecomp --why"); an = 1
    if "celdas con código" not in salida("--viz")[1]:
        print("FALLA genrecomp --viz"); an = 1
    if not (salida("--codemap", f"{D}/minima.codemap")[0] == 0 and lineas(f"{D}/minima.codemap")[8][:3] == "CC."):
        print("FALLA genrecomp --codemap"); an = 1
    if not (salida("--events", f"{D}/minima.events")[0] == 0 and "000200 000200 vector - flujo desde los vectores bra.s $200" in texto(f"{D}/minima.events")):
        print("FALLA genrecomp --events"); an = 1
    if not (salida("--html", f"{D}/minima.html")[0] == 0 and '"ins":[' in texto(f"{D}/minima.html")):
        print("FALLA genrecomp --html"); an = 1
    if "bra.s $200" not in salida("--dis", "200:200:1")[1]:
        print("FALLA genrecomp --dis"); an = 1
    # Traza BANCO:DIRECCION (trace_line.h) y la vieja DESPLAZAMIENTO:DIRECCION se leen igual
    rr.correr([exe("gentrace"), R, f"{D}/minima.trace", "--frames", "10"])
    nueva = [l.strip() for l in lineas(f"{D}/minima.trace")]
    vieja = []
    for l in nueva:
        b, _, a = l.partition(":")
        vieja.append("%06X:%s" % ((int(b, 16) << 19) | (int(a, 16) & 0x7FFFF), a))
    escribir(f"{D}/minima_vieja.trace", "".join(l + "\n" for l in vieja).encode())
    if not ("00:000200" in [l[:9] for l in nueva]
            and salida("--coverage", f"{D}/minima.trace")[1].rstrip("\n") == salida("--coverage", f"{D}/minima_vieja.trace")[1].rstrip("\n")
            and salida("--why", "00:000200")[1].rstrip("\n") == salida("--why", "200")[1].rstrip("\n")):
        print("FALLA traza BANCO:DIRECCION"); an = 1
    if an == 0:
        p.ok("genrecomp --why, --viz, --codemap, --events, --html, --dis y la traza vieja")
    else:
        p.fallas += 1
    # El Z80 en esas herramientas (--z80): el driver de Overdrive, con un sondeo corto
    od = "testroms/demos/overdrive.bin"
    if os.path.isfile(od):
        o = rr.correr([G, od, "--z80", "--z80-frames", "600", "--events", f"{D}/od.events", "--html", f"{D}/od.html", "--dis", "z80:0:0:3"], stderr=NULO)[1]
        ev = [l.split() for l in lineas(f"{D}/od.events") if "del Z80" in l] if os.path.exists(f"{D}/od.events") else []
        html = texto(f"{D}/od.html") if os.path.exists(f"{D}/od.html") else ""
        why = rr.correr([G, od, "--z80-frames", "600", "--why", f"z80:0:{ev[0][1]}"], stderr=NULO)[1] if ev else ""
        if (re.search(r"^[0-9A-F]{6}  [$]0000  ", o, re.M) and ev and '"regiones":[[' in html and '"mapa":' in html
                and "se tomó como código así" in why and "punto de entrada" in why):
            p.ok("genrecomp --z80: --dis, --events, --html y --why con el driver del Z80 de Overdrive")
        else:
            p.falla("genrecomp --z80 con Overdrive")
    p.tiempo("herramientas de análisis")

    # Las ROMs de roms/ (tuyas, no se versionan): genrecomp --info no debe fallar con ninguna
    roms = []
    for patron in ("roms/*.bin", "roms/*.md", "roms/*.gen", "roms/*.smd"):
        roms += sorted(glob.glob(patron))
    for r in roms:
        p.corre(f"genrecomp --info {os.path.basename(r)}", genrecomp, r, "--info")
        info = rr.correr([genrecomp, r])[1]
        # Sonic 1 (USA/Europa): con Start en el título, el intérprete llega a jugar el nivel (modo $0C en $FFF600)
        if "6ddb7de1e17e7f6cdb88927bd906352030daa194" in info:
            if "FFF600] = $0C" in rr.correr([exe("genrun"), r, "--frames", "900", "--watch", "F600", "start:600-605"])[1]:
                p.ok("Sonic 1 llega al nivel con el intérprete")
            else:
                p.falla("Sonic 1 no llega al nivel con el intérprete")
        # Juegos con EEPROM (NBA Jam, NBA Jam TE, Mega Man Wily Wars): el primer arranque la inicializa y el segundo la
        # acepta (no escribe). En TE depende también de las banderas del desborde de DIVU (k_DIV).
        for sha, nf, nn in (("99c5bc57fdea7f9df0cd8dec54160b162342344d", 2000, "NBA Jam"),
                            ("ddbf09c5e6ed5d528ef5ec816129a332c685f103", 1500, "NBA Jam TE"),
                            ("26ad72719991e94dbe477c78e65d68f4a4fe51e6", 1500, "Mega Man Wily Wars")):
            if sha not in info:
                continue
            borrar("build/tests/nba.sav")
            _, n1 = rr.correr([exe("genrun"), r, "--frames", str(nf), "--sram", "build/tests/nba.sav"], stderr=NULO)
            _, n2 = rr.correr([exe("genrun"), r, "--frames", str(nf), "--sram", "build/tests/nba.sav"], stderr=NULO)
            if re.search(r"EEPROM: *[1-9][0-9]* bytes", n1) and re.search(r"EEPROM: *0 bytes", n2):
                p.ok(f"{nn} guarda su EEPROM y la acepta al volver a arrancar")
            else:
                p.falla(f"{nn} y su EEPROM")
    p.tiempo("ROMs de roms/")
    # difftest --estricto: lo que se trazó queda recompilado (y sin traza, el código que falta se detecta)
    p.programa(sys.executable, "tests/estricto.py")
    p.tiempo("--estricto")

    print(f"      (total: {int(time.time() - T0)} s)")
    if p.fallas == 0:
        print("Todas las pruebas pasaron.")
    else:
        print(f"{p.fallas} pruebas fallaron.")
    return 0 if p.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
