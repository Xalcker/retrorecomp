#!/usr/bin/env python3
"""Batería de make test. Crece con cada fase (docs/PLAN.md): el cartucho, el Z80, el bus, la E/S, el VDP, el sonido y la
ventana."""
import glob
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
from pruebas import Pruebas, borrar, escribir, exe, lineas, texto  # noqa: E402

NULO = rr.subprocess.DEVNULL


def main():
    rr.entrar_a_la_raiz(__file__)
    p = Pruebas("Todas las pruebas pasaron.", None)
    T0 = time.time()
    os.makedirs("build/tests", exist_ok=True)
    x = ".exe" if os.path.isfile("./smsrecomp.exe") else ""
    S = exe("smsrecomp")
    rapidas = bool(os.environ.get("RAPIDAS"))

    p.corre("carttest (cabeceras TMR SEGA, Codemasters y SDSC, copiador, mapper, modelo, CRC32)", exe("carttest"))
    p.corre("z80test --int (interrupción desde un HALT en modos 1 y 2, RETI)", exe("z80test"), "--int")
    p.corre("z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)", exe("z80test"), "--nmi")
    p.corre("bustest (memoria, mappers Sega y Codemasters, RAM del cartucho, puertos del SMS y del GG, VDP, contadores, interrupciones, Pausa)", exe("bustest"))
    p.corre("vdptest (modo 4 y TMS, paletas del SMS y del GG, scroll y sus bloqueos, prioridad, sprites, desborde, colisión, ventana del GG)", exe("vdptest"))
    p.corre("audiotest (afinación, volumen, ruido, silencio por canal, estéreo del Game Gear)", exe("audiotest"))
    p.corre("inputtest (entradas grabadas, 8 botones, $hex, --seed sin la Pausa, boton:desde-hasta, --model)", exe("inputtest"))
    p.tiempo("pruebas unitarias")

    # Estados guardados, con el homebrew (make testroms)
    for r in ("testroms/homebrew/waternet.sms", "testroms/homebrew/waternet.gg", "testroms/homebrew/dragon_blaster.sms"):
        if os.path.isfile(r):
            p.corre(f"statetest con {os.path.basename(r)}", exe("statetest"), r, "300", "300")
    p.tiempo("estados guardados")

    # Contrato común con los otros cuatro (opciones y comportamiento de las herramientas sin ventana)
    ultima = rr.correr([sys.executable, "../shared/contrato.py"])[1].rstrip("\n").split("\n")[-1]
    if "sin fallas" in ultima:
        p.ok(ultima)
    else:
        p.falla(ultima)
    # El juego del contrato (make testroms) por las herramientas: la traza tiene la rutina de la página 3 en la ranura 2
    # (03:8000) y la de la página 2 no (el mapper), la RAM tiene lo que dejan la RAM del cartucho, la rutina copiada a
    # $C100 y las interrupciones, y smsshot --expect compara una captura
    C = "testroms/sms/contrato.sms"
    D = "build/tests"
    if os.path.isfile(C):
        rr.correr([exe("smstrace"), C, f"{D}/contrato.trace", "--frames", "60"])
        tr = [l.strip() for l in lineas(f"{D}/contrato.trace")]
        if "03:8000" in tr and "02:8000" not in tr and "00:0038" in tr:
            p.ok("smstrace con el contrato: la página 3 en la ranura 2 (03:8000) y la interrupción (00:0038)")
        else:
            p.falla("smstrace con el contrato")
        vol = rr.correr([exe("smsramdump"), C, "--frames", "60", "pause:30-31"], stderr=NULO)[1]
        ram = "".join(f"{l.split()[1]} " for l in vol.splitlines() if len(l.split()) > 1 and re.fullmatch(r"00C00[345]|00C101", l.split()[0]))
        if ram == "5A A5 01 32 ":
            p.ok("smsramdump con el contrato: mapper, RAM del cartucho, Pausa y el código en RAM")
        else:
            p.falla(f"smsramdump con el contrato ({ram})")
        entradas = ["--frames", "65", "start:30-35", "down:60-69", "a:60-69"]
        rr.correr([exe("smsshot"), C, f"{D}/contrato.png", *entradas])
        if (rr.correr([exe("smsshot"), C, f"{D}/contrato2.png", *entradas, "--expect", f"{D}/contrato.png"])[0] == 0
                and rr.correr([exe("smsshot"), C, f"{D}/contrato3.png", "--frames", "65", "--expect", f"{D}/contrato.png"])[0] != 0):
            p.ok("smsshot --expect: la misma pantalla pasa y otra falla")
        else:
            p.falla("smsshot --expect")
    p.tiempo("contrato")

    # El recompilador con el juego del contrato: lo que lee la GUI (--dis, --why, --events, --viz) y el proyecto generado,
    # compilado y comparado contra el intérprete con difftest (sin diferencias, casi todo en código nativo; lo que queda en
    # el intérprete es la rutina de $C100 con su inmediato parchado, que la guarda deja afuera)
    if os.path.isfile(C):
        rr.correr([exe("smstrace"), C, f"{D}/contrato_ciclo.trace", "--frames", "120", "start:30-35", "down:60-69", "a:60-69", "pause:100-101"])
        ok = (re.search(r"^000000  \$0000  F3 .*di$", rr.correr([S, C, "--dis", "0:0:3"], stderr=NULO)[1], re.M)
              and "banco desde" in rr.correr([S, C, "--trace", f"{D}/contrato_ciclo.trace", "--why", "03:8000"], stderr=NULO)[1]     # LD A,3 / LD ($FFFF),A / CALL $8000
              and rr.correr([S, C, "--trace", f"{D}/contrato_ciclo.trace", "--events", f"{D}/contrato.events"])[0] == 0
              and re.search(r"^00C000 8000 banco [0-9A-F]+ especulativo \(punteros sueltos\) ld a,\$5A$", texto(f"{D}/contrato.events"), re.M)
              and "celdas con código" in rr.correr([S, C, "--viz", "--color"], stderr=NULO)[1])
        if ok:
            p.ok("smsrecomp --dis, --why, --events y --viz con el contrato")
        else:
            p.falla("smsrecomp --dis, --why, --events o --viz")
        P = f"{D}/proy_contrato"
        if (rr.correr([S, C, "-o", P, "--name", "contrato", "--trace", f"{D}/contrato_ciclo.trace"])[0] == 0
                and rr.correr(["make", "-s", "-C", P, "-j8"])[0] == 0):
            _, o = rr.correr([f"{P}/difftest{x}", C, "--frames", "300", "start:30-35", "down:60-69", "a:60-69", "pause:100-101",
                              "--uncovered", f"{D}/contrato.unc"])
            m = re.search(r"^Métrica: ([0-9.]*)% de las instrucciones nativas", o, re.M)
            pct = m.group(1) if m else ""
            if (re.search(r"^difftest: 300 cuadros iguales", o, re.M) and float(pct or 0) >= 95
                    and "ram C100 3E ??" in lineas(f"{D}/contrato.unc")):
                p.ok(f"el contrato recompilado: 300 cuadros iguales al intérprete, {pct}% de las instrucciones nativas")
            else:
                p.falla("el contrato recompilado")
                print("\n".join(o.rstrip("\n").split("\n")[-5:]))
            # --save-out y --save: la RAM del cartucho de una corrida es el punto de partida de la siguiente
            sav = f"{D}/contrato.sav"
            borrar(sav)
            c1, _ = rr.correr([f"{P}/difftest{x}", C, "--frames", "60", "--seed", "1", "--save-out", sav])
            c2, o2 = rr.correr([f"{P}/difftest{x}", C, "--frames", "60", "--seed", "1", "--save", sav])
            p.comprobar(c1 == 0 and os.path.isfile(sav) and c2 == 0 and "cuadros iguales" in o2, "difftest --save-out y --save con el contrato")
        else:
            p.falla("smsrecomp -o o make del proyecto del contrato")
    # El ciclo completo (../shared/recomp_game.py: traza guionada, recompilar, difftest con dos semillas, recompilar con lo
    # que cayó al intérprete) con Dragon Blaster (make testroms). RAPIDAS=1 (el CI de cada PR) lo salta
    if not rapidas and os.path.isfile("testroms/homebrew/dragon_blaster.sms"):
        codigo, o = rr.correr([sys.executable, "../shared/recomp_game.py", "testroms/homebrew/dragon_blaster.sms", "prueba_dragon", "900", "1", "2"])
        escribir(f"{D}/ciclo_dragon.out", o.encode())
        if codigo == 0 and re.search(r"^Sin diferencias\.$", o, re.M):
            ult = [l for l in o.splitlines() if "semilla 2" in l]
            p.ok("ciclo completo con Dragon Blaster: " + (ult[-1].strip() if ult else ""))
        else:
            p.falla("ciclo completo con Dragon Blaster")
            print("\n".join(o.rstrip("\n").split("\n")[-8:]))
    p.tiempo("recompilador")

    # Z80: SingleStepTests (make testroms), por el intérprete y por el camino del código generado (z80_ops.h con los bytes
    # de la instrucción; también el ciclo del acceso a cada puerto); en los dos, el decodificador
    if os.path.isfile("testroms/z80/v1/00.json.gz"):
        for modo in ("", "--recomp"):
            r = rr.correr([sys.executable, "../shared/sst.py", "z80"], env=dict(os.environ, SST_OPTS=modo))[1].rstrip("\n").split("\n")[-1]
            if "(0 archivos con fallas)" in r:
                p.ok(r)
            else:
                p.falla(r)
    else:
        print("(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)")
    p.tiempo("SingleStepTests del Z80 (intérprete y --recomp)")

    # ZEXDOC y ZEXALL (make testroms): el ejercitador de instrucciones de Frank Cringle, en el Master System. Corre ~1,5 horas
    # de consola (unos 330 000 cuadros) y deja su resultado en la RAM del cartucho: «Tests complete», sin «ERROR», y las 79
    # pruebas en «OK». Las dos a la vez. RAPIDAS=1 (el CI de cada PR) las salta
    if not rapidas and os.path.isfile("testroms/zexall/zexall.sms"):
        procesos = {}
        for t in ("zexdoc", "zexall"):
            f = open(f"build/tests/{t}.out", "wb")
            procesos[t] = (subprocess.Popen([os.path.abspath(exe("smsrun")), f"testroms/zexall/{t}.sms", "--frames", "345000", "--sram-text"],
                                            stdout=f, stderr=subprocess.STDOUT), f)
        for t, (proc, f) in procesos.items():
            proc.wait()
            f.close()
        for t in ("zexdoc", "zexall"):
            o = texto(f"build/tests/{t}.out")
            tras = o[o.find("texto de la RAM"):] if "texto de la RAM" in o else ""
            ok = sum(1 for l in tras.splitlines() if l.endswith(" OK"))
            if "Tests complete" in o and "ERROR" not in o and ok == 79:
                p.ok(f"{t}: las 79 pruebas del Z80 en OK")
            else:
                p.falla(f"{t} ({ok} en OK)")
                print("\n".join([l for l in o.splitlines() if "ERROR" in l][:5]))
    p.tiempo("ZEXDOC y ZEXALL")

    def corre_juego(rom, cuadros, entradas, minimo, etiqueta):
        _, o = rr.correr([exe("smsrun"), rom, "--frames", str(cuadros), *entradas])
        m = re.search(r"^interrupciones: *([0-9]*)", o, re.M)
        n = int(m.group(1)) if m and m.group(1) else 0
        if "pantalla encendida" in o and n >= minimo:
            p.ok(f"{os.path.basename(rom)}: pantalla encendida, {n} interrupciones en {cuadros} cuadros")
        else:
            p.falla(os.path.basename(rom))
            print("\n".join(o.splitlines()[:8]))

    # El homebrew enciende la pantalla y atiende una interrupción de cuadro por cuadro (600 cuadros, con Start y A)
    for r in sorted(glob.glob("testroms/homebrew/*.sms")) + sorted(glob.glob("testroms/homebrew/*.gg")):
        corre_juego(r, 600, ["start:120-125", "a:300-305"], 580, "")
    p.tiempo("homebrew")

    # Interfaz: cada menú y cada diálogo se dibuja igual que la referencia tests/ui_golden.txt (--ui-shot, sin ventana; si
    # cambia a propósito: UI_GOLDEN_UPDATE=1 python3 tests/run_tests.py, y revisa build/shots/). La ROM es mínima (32 KB de
    # NOP, la pantalla queda apagada) para no depender de make testroms; como .gg, la misma ROM da la ventana del Game Gear
    if os.path.isfile(exe("sms")):
        os.makedirs("build/shots", exist_ok=True)
        escribir("build/tests/minima.sms", bytes(32768))
        shutil.copyfile("build/tests/minima.sms", "build/tests/minima.gg")
        # Cargar estado (5), Controles (34), Acerca de (36) y la barra oculta con F9 (37), los números de A_LOAD_STATE,
        # A_HELP_CONTROLS... en main_sdl.c (Este juego, 35, no: muestra la carpeta de datos), y sin barra con --no-menu (aunque
        # se pida un menú) y con --fullscreen
        codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", exe("sms"), "build/tests/minima.sms", "tests/ui_golden.txt",
                                    "0 1 2 3 4 5 6", "5 34 36 37", "--no-menu --no-menu,--ui-menu,1 --fullscreen"])
        escribir("build/ui_golden.out", salida.encode())
        if codigo == 0:
            p.ok("sms: los 7 menús, 3 diálogos, la barra oculta, --no-menu y --fullscreen coinciden con la referencia")
        else:
            sys.stdout.write(salida)
            p.fallas += 1
        # El Game Gear (la ventana de 160×144, más angosta): el menú Entrada y los Controles; y un juego de SMS en el Game
        # Gear, que se ve entero (256×192)
        codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", exe("sms"), "build/tests/minima.gg", "tests/ui_golden_gg.txt",
                                    "3", "34", "--model,sms"])
        escribir("build/ui_golden_gg.out", salida.encode())
        if codigo == 0:
            p.ok("sms con el Game Gear: el menú Entrada, los Controles y --model sms coinciden con la referencia")
        else:
            sys.stdout.write(salida)
            p.fallas += 1
    p.tiempo("interfaz")

    # smsrecomp: un archivo que no puede ser una ROM debe fallar sin caerse, y sin ROM, dar el uso
    escribir(f"{D}/chica.sms", b"no es una rom")
    if rr.correr([S, f"{D}/chica.sms"])[0] == 0:
        p.falla("smsrecomp acepta un archivo de 13 bytes")
    else:
        p.ok("smsrecomp rechaza un archivo que no es ROM")
    if "uso:" in rr.correr([S])[1]:
        p.ok("smsrecomp sin argumentos da el uso")
    else:
        p.falla("smsrecomp sin argumentos")
    if "modelo desconocido" in rr.correr([S, f"{D}/chica.sms", "--model", "sg"])[1]:
        p.ok("smsrecomp rechaza un modelo desconocido")
    else:
        p.falla("smsrecomp --model sg")

    # smsrecomp --info con las ROMs de prueba (make testroms): SHA1, suma de la cabecera, mapper y modelo
    def info(rom, sha, mapa, modelo, *opciones):
        if not os.path.isfile(rom):
            return
        o = rr.correr([S, rom, *opciones])[1]
        etiqueta = f"smsrecomp --info {os.path.basename(rom)}" + (" " + " ".join(opciones) if opciones else "")
        if (re.search(rf"^SHA1: *{sha}", o, re.M) and re.search(rf"^mapper: *{re.escape(mapa)}", o, re.M)
                and re.search(rf"^modelo: *{re.escape(modelo)}", o, re.M) and "no coincide" not in o):
            p.ok(f"{etiqueta}: {mapa}, {modelo}")
        else:
            p.falla(etiqueta)
            print(o)

    SMS2 = "sms (Master System II)"
    info("testroms/zexall/zexall.sms", "3cdb58bfe62d5374fd8d7cd3df35c2b4f806924c", "Sega", SMS2)
    info("testroms/zexall/zexdoc.sms", "23e895db089b03d11ea7b448e3f61c4b427279b1", "Sega", SMS2)
    info("testroms/homebrew/dragon_blaster.sms", "d1c1da577dd14178366245bc55de7d39da839086", "sin mapper", SMS2)
    info("testroms/homebrew/food_fighter.sms", "388873a87a38c9a6e4e5fd52b5cac453a3d393b4", "sin mapper", SMS2)
    info("testroms/homebrew/river_strike.sms", "8e1142d1f8c730787f1c7ff1cec7d556335c566e", "sin mapper", SMS2)
    info("testroms/homebrew/waternet.sms", "dfb74cda30ade0c35c409f9d72f6af2b5f30a029", "Sega", SMS2)
    info("testroms/homebrew/waternet.gg", "fd56a237586a20cf3ceea9e3fac20d0a9eae77cf", "Sega", "gg (Game Gear)")
    info("testroms/homebrew/waternet.gg", "fd56a237586a20cf3ceea9e3fac20d0a9eae77cf", "Sega", SMS2, "--model", "sms")
    info("testroms/homebrew/dragon_blaster.sms", "d1c1da577dd14178366245bc55de7d39da839086", "sin mapper", "gg (Game Gear, en su modo SMS)", "--model", "gg")
    p.tiempo("smsrecomp --info")

    # Las ROMs de roms/ y de sus subcarpetas (tuyas, no se versionan): smsrecomp --info no debe fallar con ninguna, y cada
    # una, en 1200 cuadros con Start, enciende la pantalla y atiende al menos una interrupción cada dos cuadros (al
    # principio, mientras cargan, muchos juegos las apagan un rato)
    todas = []
    for patron in ("roms/*.sms", "roms/*.gg", "roms/*/*.sms", "roms/*/*.gg"):
        todas += sorted(glob.glob(patron))
    for r in todas:
        p.corre(f"smsrecomp --info {os.path.basename(r)}", S, r, "--info")
        corre_juego(r, 1200, ["start:120-125", "start:300-305"], 600, "")
    p.tiempo("ROMs de roms/")

    # difftest --estricto: lo que se trazó queda recompilado (y sin traza, el código que falta se detecta)
    p.programa(sys.executable, "tests/estricto.py")
    p.tiempo("--estricto")

    print(f"      (total: {int(time.time() - T0)} s)")
    print("Todas las pruebas pasaron." if p.fallas == 0 else f"{p.fallas} pruebas fallaron.")
    return 0 if p.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
