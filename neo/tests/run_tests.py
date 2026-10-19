#!/usr/bin/env python3
"""Batería de make test. Crece con cada fase (docs/PLAN.md): el cartucho, la BIOS y neorecomp --info (fase 0); el 68000 y
el Z80 (fase 1); el bus, la BIOS y las interrupciones (fase 2); el LSPC y la ventana (fase 3); el YM2610 y la mezcla
(fase 4); las herramientas sin ventana, los estados y el contrato común (fase 5); el recompilador (fase 6)."""
import glob
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "shared"))
import rr  # noqa: E402
from pruebas import Pruebas, almacen, borrar, escribir, exe, lineas, texto  # noqa: E402

NULO = rr.subprocess.DEVNULL


def main():
    rr.entrar_a_la_raiz(__file__)
    p = Pruebas("Todo bien.", None)
    T0 = time.time()
    os.makedirs("build/tests", exist_ok=True)
    # La caché de la BIOS recompilada de las pruebas va en build/ (no en la del usuario)
    os.environ["NEO_BIOS_CACHE"] = os.path.abspath("build/bios-cache")
    x = ".exe" if os.path.isfile("./neorecomp.exe") else ""
    neorecomp = exe("neorecomp")

    p.corre("carttest (.neo, zip, carpeta, inflate, sufijos, errores, cifrados, tabla L0, BIOS, identidad del código)", exe("carttest"))
    p.corre("z80test --nmi (NMI, salida del HALT, RETN, INT en modo 1)", exe("z80test"), "--nmi")
    p.corre("z80test --int (interrupción desde un HALT en modos 1 y 2, RETI)", exe("z80test"), "--int")
    p.corre("lspctest (capa fija, sprites, volteos, reducciones, pegajosos, prioridad, autoanimación, límite de 96, colores, por línea)", exe("lspctest"))
    p.corre("audiotest (FM, SSG con ruido y envolvente, ADPCM-A, ADPCM-B, banderas, temporizadores, ocupado)", exe("audiotest"))
    p.corre("inputtest (entradas grabadas, 10 botones, $hex, --seed, boton:desde-hasta)", exe("inputtest"))
    p.corre("bustest (mapa, vectores, P2, mandos, paleta, tarjeta, VRAM, contador de líneas, interrupciones, Z80, YM2610)", exe("bustest"))
    p.debe_fallar("neorecomp rechaza lo que no es un juego", neorecomp, "build/tests/basura.bin")
    p.debe_fallar("neorecomp sin argumentos pide la ROM", neorecomp)
    p.tiempo("pruebas unitarias")

    def sst(cpu, plural, existe, aviso):
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
    sst("z80", "archivos", "testroms/z80/v1/00.json.gz", "(sin testroms/z80 se omiten las SingleStepTests del Z80: make testroms)")
    p.tiempo("SingleStepTests del Z80 (intérprete y --recomp)")
    # 68000: SingleStepTests (make testroms), por el intérprete y por el camino del código generado
    sst("m68k", "familias", "testroms/680x0/68000/v1/NOP.json.gz", "(sin testroms/680x0 se omiten las SingleStepTests del 68000: make testroms)")
    p.tiempo("SingleStepTests del 68000 (intérprete y --recomp)")

    # La búsqueda toma primero la BIOS propia, pero las referencias de las pruebas con los juegos de roms/ se hicieron con
    # la de SNK (nunca va en el repositorio: roms/ o NEO_BIOS): si está, se fija con NEO_BIOS. Sin ella, la propia (make
    # neobios, que se ensambla con Python): así make test corre también en un clon recién hecho y en el CI
    if not os.environ.get("NEO_BIOS"):
        for b in ("roms/aes.zip", "roms/neo-epo.bin", "roms/neo-po.bin"):
            if os.path.isfile(b):
                os.environ["NEO_BIOS"] = os.path.abspath(b)
                break
        if not os.environ.get("NEO_BIOS") and rr.correr(["make", "-s", "neobios"])[0] == 0:
            os.environ["NEO_BIOS"] = os.path.abspath("build/neobios.bin")
            print("(sin la BIOS de SNK en roms/ ni en NEO_BIOS: las pruebas usan la BIOS propia)")

    # Estados guardados: con el juego de prueba del contrato (make testroms) y con Metal Slug si está
    if os.path.isfile("testroms/neo/contrato.neo"):
        p.corre("statetest con el juego de prueba", exe("statetest"), "testroms/neo/contrato.neo", "120", "200")
    if os.path.isfile("roms/mslug.zip"):
        p.corre("statetest con Metal Slug (CPU, Z80, LSPC, YM2610; sin la BIOS en el estado)", exe("statetest"), "roms/mslug.zip", "700", "300")
    p.tiempo("estados guardados")
    # Contrato común con NESRecomp, GBRecomp y GENRecomp (opciones y comportamiento de las herramientas sin ventana)
    ultima = rr.correr([sys.executable, "../shared/contrato.py"])[1].rstrip("\n").split("\n")[-1]
    if "sin fallas" in ultima:
        p.ok(ultima)
    else:
        p.falla(ultima)
    p.tiempo("contrato")

    # Recompilar: proyecto generado, compilado (difftest, sin SDL2) y comparado contra el intérprete. Con el juego de prueba
    # del contrato (make testroms) y con Metal Slug (roms/, sin traza) si están; necesitan la BIOS del AES.
    def recompila(n, rom, nf):
        d = f"build/tests/proy_{n}"
        borrar(d)
        if rr.correr([neorecomp, rom, "-o", d, "--name", n])[0] != 0:
            p.falla(f"neorecomp -o con {n}")
            return
        if rr.correr(["make", "-C", d, "-j8", "-s", f"difftest{x}"])[0] != 0:
            p.falla(f"el proyecto de {n} no compila")
            return
        salida = rr.correr([f"{d}/difftest{x}", rom, str(nf), "--seed", "2", "start:120-125", "start:300-305", "a:400-420"])[1]
        validas = [l for l in salida.splitlines() if l.startswith("difftest") or "DIFERENCIA" in l]
        res = validas[-1] if validas else ""
        if "cuadros iguales" in res:
            p.ok(f"recompilado {n}: {res}")
        else:
            p.falla(f"recompilado {n}: {res}")

    if os.path.isfile("testroms/neo/contrato.neo") and os.path.isfile("roms/aes.zip"):
        recompila("contrato", "testroms/neo/contrato.neo", 600)
    p.tiempo("proyecto generado: contrato")
    if os.path.isfile("roms/mslug.zip") and os.path.isfile("roms/aes.zip"):
        recompila("mslug", "roms/mslug.zip", 1200)
    p.tiempo("proyecto generado: Metal Slug")

    # Código que el juego copia a RAM (retrorecomp shared/ram_code.h), con la BIOS propia: mkcontrato --ram arma una variante
    # del juego de prueba que copia una rutina a $100100, la llama en cada cuadro y le parcha el inmediato. Primera ronda: la
    # RAM va al intérprete y la traza la anota (?? en el byte parchado); segunda, con esa traza: nada de RAM en el intérprete
    # y sin diferencias. difftest, neotrace y el ejecutable escriben las mismas líneas "ram", y el ejecutable conserva las que
    # traía el archivo.
    if os.path.isfile("build/neobios.bin"):
        RR_ = "build/tests/ram.neo"
        d = "build/tests/proy_ram"
        D = "build/tests"
        bios = dict(os.environ, NEO_BIOS="build/neobios.bin")
        ok = False
        s1 = s2 = ""
        borrar(d, f"{D}/ram.unc", f"{D}/ram2.unc")
        if (rr.correr(["make", "-s", f"build/mkcontrato{x}"])[0] == 0
                and rr.correr([f"build/mkcontrato{x}", "--ram", RR_])[0] == 0
                and rr.correr([neorecomp, RR_, "-o", d, "--name", "ram"], env=bios)[0] == 0
                and rr.correr(["make", "-C", d, "-j8", "-s", f"difftest{x}"])[0] == 0):
            _, s1 = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "600", "--seed", "1", "--uncovered", f"{D}/ram.unc"], env=bios)
            escribir(f"{D}/ram1.out", s1.encode())
            unc = lineas(f"{D}/ram.unc")
            if re.search(r"RAM 9[0-9].[0-9]%", s1) and "ram 100100 30 3C 00 ??" in unc and sum(1 for l in unc if l.startswith("ram")) == 6:
                time.sleep(1)       # el make de macOS compara fechas con precisión de un segundo
                _, ap = rr.correr([neorecomp, RR_, "-o", d, "--name", "ram", "--trace", f"{D}/ram.unc"], env=bios)
                if ("aprendido de la traza: 6 instrucciones (1 con operandos variables) en 1 bloques" in ap
                        and rr.correr(["make", "-C", d, "-j8", "-s", f"difftest{x}"])[0] == 0):
                    _, s2 = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "600", "--seed", "1", "--uncovered", f"{D}/ram2.unc"], env=bios)
                    escribir(f"{D}/ram2.out", s2.encode())
                    if (re.search(r"^Métrica: 100.0% de las instrucciones nativas", s2, re.M)
                            and not any(l.startswith("ram") for l in lineas(f"{D}/ram2.unc"))):
                        ok = True
        if ok:
            # --save-out y --save: la tarjeta de memoria de una corrida es el punto de partida de la siguiente
            tarjeta = f"{D}/ram.card"
            borrar(tarjeta)
            c1, _ = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "60", "--seed", "1", "--save-out", tarjeta], env=bios)
            c2, o2 = rr.correr([f"{d}/difftest{x}", RR_, "--frames", "60", "--seed", "1", "--save", tarjeta], env=bios)
            p.comprobar(c1 == 0 and os.path.isfile(tarjeta) and c2 == 0 and "cuadros iguales" in o2, "difftest --save-out y --save con la tarjeta de memoria")
            p.ok("código en RAM: la segunda ronda lo compila (6 instrucciones, 1 parchada) y no queda nada en el intérprete")
        else:
            p.falla("código en RAM")
            sys.stdout.write(s1 + s2)

        def ram_de(ruta):
            return [l for l in lineas(ruta) if l.startswith("ram")] if os.path.exists(ruta) else []

        rr.correr([exe("neotrace"), RR_, f"{D}/ram_neotrace.txt", "--frames", "600"], env=bios)
        if ram_de(f"{D}/ram_neotrace.txt") == ram_de(f"{D}/ram.unc"):
            p.ok("neotrace anota el mismo código de RAM que difftest")
        else:
            p.falla("neotrace y el código de RAM")
        if os.path.isfile(exe("neo")):
            escribir(f"{D}/ram_exe.txt", b"ram 102000 4E 75\n")
            rr.correr([exe("neo"), RR_, "--ui-shot", f"{D}/ram_exe.png", "--ui-frames", "600", "--uncovered", f"{D}/ram_exe.txt"],
                      env=dict(bios, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
            propias = ram_de(f"{D}/ram_exe.txt")
            if [l for l in propias if not l.startswith("ram 102000")] == ram_de(f"{D}/ram.unc") and "ram 102000 4E 75" in propias:
                p.ok("el ejecutable anota el mismo código de RAM y conserva las líneas que traía")
            else:
                p.falla("el ejecutable y el código de RAM")
    p.tiempo("código en RAM")

    # Herramientas de análisis de neorecomp con el juego de prueba (no necesitan la BIOS): --why, --viz, --codemap, --events,
    # --html, --dis, y la traza BANCO:DIRECCION
    if os.path.isfile("testroms/neo/contrato.neo"):
        R = "testroms/neo/contrato.neo"
        an = 0

        def g(*args):
            return rr.correr([neorecomp, R, *args], stderr=NULO)

        if "salto desde" not in g("--why", "400")[1]:
            print("FALLA neorecomp --why"); an = 1
        if "punto de entrada: entrada" not in g("--why", "400")[1]:
            print("FALLA neorecomp --why (entrada de la cabecera)"); an = 1
        if "celdas con código" not in g("--viz")[1]:
            print("FALLA neorecomp --viz"); an = 1
        if not (g("--codemap", "build/tests/contrato.codemap")[0] == 0 and lineas("build/tests/contrato.codemap")[16][:3] == "CCC"):
            print("FALLA neorecomp --codemap"); an = 1
        if not (g("--events", "build/tests/contrato.events")[0] == 0
                and re.search(r"^000400 000400 salto 000122 flujo desde los puntos de entrada move\.b d0", texto("build/tests/contrato.events"), re.M)):
            print("FALLA neorecomp --events"); an = 1
        if not (g("--html", "build/tests/contrato.html")[0] == 0 and '"ins":[' in texto("build/tests/contrato.html")):
            print("FALLA neorecomp --html"); an = 1
        if "move.b d0,($300001).l" not in g("--dis", "400:400:1")[1]:
            print("FALLA neorecomp --dis"); an = 1
        if g("--why", "00:000400")[1].rstrip("\n") != g("--why", "400")[1].rstrip("\n"):
            print("FALLA --why con BANCO:DIRECCION"); an = 1
        if an == 0:
            p.ok("neorecomp --why, --viz, --codemap, --events, --html, --dis")
        else:
            p.fallas += 1
        # El Z80 (la M1) en esas herramientas (--z80), a continuación de P en el mapa
        o = g("--z80", "--events", "build/tests/contrato_z80.events", "--html", "build/tests/contrato_z80.html", "--dis", "z80:0:0:3")[1]
        ev = texto("build/tests/contrato_z80.events") if os.path.exists("build/tests/contrato_z80.events") else ""
        html = texto("build/tests/contrato_z80.html") if os.path.exists("build/tests/contrato_z80.html") else ""
        why = g("--why", "z80:0:0")[1]
        if (re.search(r"^[0-9A-F]{6}  [$]0000  F3 .*di$", o, re.M) and "del Z80" in ev and '"regiones":[[' in html
                and "se tomó como código así" in why and "punto de entrada" in why):
            p.ok("neorecomp --z80: --dis, --events, --html y --why con la M1")
        else:
            p.falla("neorecomp --z80 con la M1")
    p.tiempo("herramientas de análisis")

    # Cobertura sin traza (fase 7): de lo que ejecuta Metal Slug en 3000 cuadros (de la demo a la partida), el análisis
    # estático solo tiene que encontrar el 99 % o más, y todo lo de la BIOS
    if os.path.isfile("roms/mslug.zip") and os.path.isfile("roms/aes.zip"):
        rr.correr([exe("neotrace"), "roms/mslug.zip", "build/tests/mslug_cob.txt", "--frames", "3000", "--seed", "1", "start:1200-1205",
                   "start:1500-1505", "a:1700-1710", "a:1900-1910", "right:2000-2600", "a:2100-2110", "b:2300-2305"])
        out = rr.correr([neorecomp, "roms/mslug.zip", "--coverage", "build/tests/mslug_cob.txt"], stderr=NULO)[1]
        mj = re.search(r"^cobertura: .*\(([0-9.]*)%\).*", out, re.M)
        mb = re.search(r"^cobertura de la BIOS: .*\(([0-9.]*)%\).*", out, re.M)
        pj, pb = (mj.group(1) if mj else ""), (mb.group(1) if mb else "")
        if float(pj or 0) >= 99 and float(pb or 0) >= 100:
            p.ok(f"cobertura sin traza: Metal Slug {pj}%, BIOS {pb}%")
        else:
            p.falla(f"cobertura sin traza: Metal Slug {pj or '?'}% (mínimo 99), BIOS {pb or '?'}% (100)")
    p.tiempo("cobertura")

    # La BIOS propia (fase 10): el ensamblador contra el desensamblador, la tarjeta de memoria contra lo que hace la original,
    # el contrato con ella (no necesita la de SNK) y, con Metal Slug, que arranque y que Start empiece la partida
    if rr.correr(["make", "-s", f"build/dis68kbin{x}", f"neocard{x}", "neobios"])[0] == 0:
        for script in ("tests/asm68k/prueba.py", "tests/neobios/prueba_tarjeta.py"):
            codigo, salida = rr.correr([sys.executable, script])
            sys.stdout.write(salida)
            if codigo != 0:
                p.fallas += 1
        codigo, salida = rr.correr([sys.executable, "../shared/contrato.py"], env=dict(os.environ, NEO_BIOS=os.path.abspath("build/neobios.bin")))
        if "sin fallas" in salida:
            p.ok("contrato común con la BIOS propia")
        else:
            p.falla("contrato común con la BIOS propia")
        if os.path.isfile("roms/mslug.zip"):
            salida = rr.correr([exe("neorun"), "roms/mslug.zip", "--frames", "1200", "start:700-705"],
                               env=dict(os.environ, NEO_BIOS=os.path.abspath("build/neobios.bin")))[1]
            r = next((l for l in salida.splitlines() if "BIOS (RAM)" in l), "")
            if "USER_REQUEST 2, USER_MODE 2" in r:
                p.ok("Metal Slug arranca con la BIOS propia y Start empieza la partida")
            else:
                p.falla(f"Metal Slug con la BIOS propia: {r}")
    else:
        print("(sin python3: no se prueba la BIOS propia)")
    p.tiempo("BIOS propia")

    # Con los juegos y la BIOS de roms/ (no se versionan)
    for r in sorted(glob.glob("roms/*.zip")) + sorted(glob.glob("roms/*.neo")):
        nombre = os.path.basename(r)
        if nombre in ("aes.zip", "neogeo.zip"):
            continue
        codigo, out = rr.correr([neorecomp, r, "--info"])
        if codigo != 0:
            p.falla(f"neorecomp --info {nombre}")
            continue
        m = re.search(r"^SHA1:\s+(\S+)", out, re.M)
        sha = m.group(1) if m else ""
        # Exportado como .neo da la misma imagen (el mismo SHA1)
        rr.correr([neorecomp, r, "--neo", "build/tests/copia.neo"])
        out2 = rr.correr([neorecomp, "build/tests/copia.neo", "--info"], stderr=NULO)[1]
        m2 = re.search(r"^SHA1:\s+(\S+)", out2, re.M)
        sha2 = m2.group(1) if m2 else ""
        if sha and sha == sha2:
            p.ok(f"neorecomp --info y --neo con {nombre} (SHA1 {sha})")
        else:
            p.falla(f"neorecomp --neo con {nombre}: {sha} / {sha2}")
        borrar("build/tests/copia.neo")
    # Metal Slug (MAME mslug): reconocido por la base de datos, con el P1 acomodado
    if os.path.isfile("roms/mslug.zip"):
        out = rr.correr([neorecomp, "roms/mslug.zip"])[1]
        if (re.search(r"^SHA1: *582233c4d95e4a2d80dd78a9de29b92e0f60d40f", out, re.M) and "(mslug," in out
                and "mitades cambiadas" in out and re.search(r"USER *\$0007CC", out)):
            p.ok("Metal Slug: base de datos, SHA1, P1 y entradas")
        else:
            p.falla("Metal Slug con neorecomp --info")
    # La BIOS del AES (aes.zip de MAME): aceptada, y su 000-lo.lo igual a la tabla L0 calculada
    if os.path.isfile("roms/aes.zip") and os.path.isfile("roms/mslug.zip"):
        out = rr.correr([neorecomp, "roms/mslug.zip", "--bios", "roms/aes.zip"])[1]
        if re.search(r"^BIOS: .*neo-epo.bin, AES de exportación", out, re.M) and "igual a la 000-lo.lo" in out:
            p.ok("BIOS de aes.zip aceptada (neo-epo.bin) y tabla L0 igual a la del chip")
        else:
            p.falla("BIOS de aes.zip")
    if os.path.isfile("roms/neogeo.zip") and os.path.isfile("roms/mslug.zip"):
        out = rr.correr([neorecomp, "roms/mslug.zip", "--bios", "roms/neogeo.zip"])[1]
        if "no tiene ninguna BIOS del AES" in out:
            p.ok("neogeo.zip (solo MVS) no se acepta como BIOS del AES")
        else:
            p.falla("neogeo.zip no debería aceptarse")
    # Metal Slug arranca con la BIOS del AES: la BIOS le pasa los vectores, llega a la demo (USER_REQUEST 2) y con Start
    # empieza la partida (USER_MODE 2, un jugador)
    if os.path.isfile("roms/mslug.zip") and os.path.isfile("roms/aes.zip"):
        out = rr.correr([exe("neorun"), "roms/mslug.zip", "--frames", "1800", "start:1500-1505", "--fix"])[1]
        if (re.search(r"^vectores: *del juego", out, re.M) and "USER_REQUEST 2, USER_MODE 2" in out and "CREDITS 04" in out):
            p.ok("Metal Slug arranca con la BIOS, llega a la demo (CREDITS 04) y con Start empieza la partida")
        else:
            p.falla("Metal Slug con la BIOS (neorun)")
    # Imagen: los tres juegos de validación dibujan (40 colores o más en la pantalla) y la ventana dibuja cada menú y cada
    # diálogo sin ventana (--ui-shot)
    os.makedirs("build/shots", exist_ok=True)
    for r, f in (("mslug", 900), ("samsho2", 1200), ("turfmast", 2000)):
        if not (os.path.isfile(f"roms/{r}.zip") and os.path.isfile("roms/aes.zip")):
            continue
        out = rr.correr([exe("neorun"), f"roms/{r}.zip", "--frames", str(f), "--shot", f"build/shots/{r}.png"], stderr=NULO)[1]
        m = re.search(r"^imagen:\s+(\d+)", out, re.M)
        c = int(m.group(1)) if m else 0
        if c >= 40:
            p.ok(f"{r} dibuja (cuadro {f}: {c} colores, build/shots/{r}.png)")
        else:
            p.falla(f"{r} no dibuja (cuadro {f}: {c or '?'} colores)")
    # Sonido: los tres juegos suenan (nivel por segundo de neorun --wav en los primeros 30 s)
    os.makedirs("build/wav", exist_ok=True)
    for r in ("mslug", "samsho2", "turfmast"):
        if not (os.path.isfile(f"roms/{r}.zip") and os.path.isfile("roms/aes.zip")):
            continue
        out = rr.correr([exe("neorun"), f"roms/{r}.zip", "--frames", "1800", "--wav", f"build/wav/{r}.wav"], stderr=NULO)[1]
        niveles = [int(l.split()[4]) for l in out.splitlines() if l.startswith("audio") and len(l.split()) > 4 and l.split()[4].isdigit()]
        m = max(niveles) if niveles else 0
        if m >= 300:
            p.ok(f"{r} suena (nivel máximo {m} en 30 s, build/wav/{r}.wav)")
        else:
            p.falla(f"{r} no suena (nivel máximo {m or '?'})")
    if (os.path.isfile(exe("neo")) and os.path.isfile("roms/mslug.zip") and os.path.isfile("roms/aes.zip")):
        if not (os.path.isfile("tests/ui_golden.txt") or os.environ.get("UI_GOLDEN_UPDATE")):
            print("(sin tests/ui_golden.txt: UI_GOLDEN_UPDATE=1 python3 tests/run_tests.py lo crea)")
        else:
            # Controles (17), Acerca de (19) y la barra oculta con F9 (38), y sin barra con --no-menu (aunque se pida un menú) y
            # con --fullscreen; contra la referencia tests/ui_golden.txt (UI_GOLDEN_UPDATE=1 la reescribe). Este juego (18) no,
            # como en los otros tres: muestra la carpeta de datos, que cambia con el sistema
            codigo, salida = rr.correr([sys.executable, "../shared/ui_golden.py", exe("neo"), "roms/mslug.zip", "tests/ui_golden.txt",
                                        "0 1 2 3 4 5", "17 19 38", "--no-menu --no-menu,--ui-menu,1 --fullscreen"])
            escribir("build/ui_golden.out", salida.encode())
            if codigo == 0:
                p.ok("neo: cada menú, cada diálogo, la barra oculta, --no-menu y --fullscreen coinciden con la referencia (--ui-shot)")
            else:
                sys.stdout.write(salida)
                p.fallas += 1
    p.tiempo("juegos de roms/")

    # difftest --estricto: lo que se trazó queda recompilado
    p.programa(sys.executable, "tests/estricto.py")
    p.tiempo("--estricto")
    # El almacén de trazas por SHA1 (--traces-dir) y el sondeo (trazas.py sondear)
    almacen(p, "neo", "testroms/neo/contrato.neo", "build/tests/almacen")
    p.tiempo("almacén de trazas")

    print(f"      (total: {int(time.time() - T0)} s)")
    print("Todo bien." if p.fallas == 0 else f"{p.fallas} fallas.")
    return 0 if p.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
