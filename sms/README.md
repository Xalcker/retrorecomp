# SMSRecomp

Recompilador estático de juegos de Sega Master System y Game Gear (EE. UU., NTSC), con la misma arquitectura que
[NESRecomp](https://github.com/Xalcker/NESRecomp), [GBRecomp](https://github.com/Xalcker/GBRecomp),
[GENRecomp](https://github.com/Xalcker/GENRecomp) y [NEORecomp](https://github.com/Xalcker/NEORecomp): tomará una
ROM y generará un proyecto C99 portable que se compila en un ejecutable nativo con SDL2. Será híbrido (código
recompilado más un intérprete Z80 de respaldo), sin datos de la ROM en el proyecto generado (valida su SHA1) y con
`difftest` para comprobar el código recompilado contra el intérprete.

Hay que clonar con el submódulo de retrorecomp, que trae el código que comparten los cinco recompiladores (el Z80,
`random_pad.h`, `trace_line.h`...): `git clone --recursive`, o `git submodule update --init` en un clon ya hecho. El
ZIP de GitHub no lo incluye.

**Estado: fases 0 a 6 de 8.** Recompila juegos de Master System y Game Gear a C99: `smsrecomp` genera un proyecto
que se compila en un ejecutable nativo, con el Z80 en código nativo y la ROM del usuario cargada al arrancar. Con el
intérprete, los juegos corren en la ventana `sms` o sin ella (`smsrun` y las herramientas comunes: `smstrace`,
`smsshot`, `smsaudio`, `smsramdump`, `smsbench`), con el contrato común de los cinco en verde. El proyecto generado
también se arma para el navegador (`make web`) y para Android (`make apk`), con el mando en pantalla. El plan completo
está en [docs/PLAN.md](docs/PLAN.md).

- **Recompilador:** el Z80 con el recompilador común (`z80_recomp.h`, el de GENRecomp y NEORecomp) por vistas (una
  página del mapper en una ranura), las heurísticas de tablas de saltos, la traza y el código que el juego copia a la
  RAM (con una guarda). `difftest` compara el recompilado con el intérprete cuadro a cuadro; los 15 juegos de prueba
  corren sin diferencias con el 99-100 % de los ciclos en código nativo.

- **Cartucho:** `.sms` y `.gg` (con o sin cabecera de copiador), la cabecera TMR SEGA con su suma (calculada como
  la BIOS), la de Codemasters y la SDSC, el mapper (Sega, Codemasters o ninguno) y el modelo con que correría el
  juego (Master System II o Game Gear, y el modo SMS del Game Gear).
- **Bus y E/S:** los mappers Sega y Codemasters con la RAM del cartucho (y su partida), los puertos del Master
  System y del Game Gear con su decodificación parcial, los mandos, la detección de consola de exportación y la
  Pausa por NMI. Arranca con el estado que deja la BIOS de EE. UU., sin necesitarla.
- **VDP:** el modo 4 completo (fondo con scroll y sus bloqueos, prioridad, sprites con su límite, zoom, desborde y
  colisión, 224 líneas), los modos gráficos del TMS9918, la CRAM de 12 bits y la pantalla de 160×144 del Game Gear;
  contadores V y H e interrupciones de cuadro y de línea, al ciclo de cada acceso a sus puertos, calibrados con el
  VDPTEST de FluBBa (todo Ok). ZEXDOC y ZEXALL pasan sus 79 pruebas.
- **Sonido:** el SN76489, el mismo de GENRecomp (retrorecomp/shared), con el estéreo del Game Gear.
- **Ventana `sms`:** la de los otros cuatro (menús, español e inglés, estados guardados, captura, mandos, pantalla
  táctil), con la consola a elegir (Master System o Game Gear) y los pixeles de 8:7 del televisor.
- **Z80:** el núcleo común de GENRecomp y NEORecomp (retrorecomp/shared), con la semántica de cada instrucción en
  un solo lugar (`z80_ops.h`) para el intérprete y el código generado, como el 68000 de GENRecomp. Pasa las
  1 604 000 [SingleStepTests](https://github.com/SingleStepTests/z80) por los dos caminos, con su decodificador
  y el ciclo de cada acceso a un puerto.

## Compilar

Los mismos paquetes que en NESRecomp, GBRecomp, GENRecomp y NEORecomp: compilador C99, `make`, `pkg-config` y SDL2
(para la ventana `sms`).

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

```
git submodule update --init     # el código común y la GUI (tools/retrorecomp)
make            # smsrecomp, la ventana sms y las pruebas
make testroms   # una sola vez: las SingleStepTests del Z80 (comprimidas), ZEXALL y homebrew con licencia MIT
make test       # pruebas
make dist       # dist/smsrecomp-<versión>-<sistema>.tar.gz
```

`EXTRA=1 make testroms` baja además las pruebas de hardware de SMS Power! que no tienen licencia publicada (el
VDPTEST de FluBBa, las de Charles MacDonald, el SMS Test Suite de sverx...), para usarlas en tu máquina.

**Versiones.** La versión es la etiqueta de git más cercana (`git describe`). Al subir una etiqueta `vX.Y.Z` de
`main`, el CI publica un release en GitHub con los paquetes de Linux, macOS y Windows. Igual que en los otros cuatro.

## Lo que hace hoy

```
./sms "roms/juego.sms"
```

Juega con el intérprete en una ventana (o arrastra la ROM a ella). Jugador 1: flechas, Z X = botones 1 y 2, Enter =
Start del Game Gear, Espacio = Pausa del Master System; jugador 2: I J K L y V B; los mandos de SDL también (A = 1,
B = 2, Back = Pausa). P pausa, Tab avance rápido, F5 y F8 estados guardados, F12 captura, F11 pantalla completa.
La partida (la RAM del cartucho), los estados y las preferencias van a la carpeta de datos de SMSRecomp. `--model
sms|gg` (o Entrada → Consola) elige la consola; `--record` y `--play`, un guion de entradas.

```
sh tests/recomp_game.sh "roms/juego.sms" juego
```

Recompila el juego con el ciclo completo: una traza guionada con `smstrace`, `smsrecomp`, `make`, `difftest` con dos
semillas y otra ronda con lo que cayó al intérprete. El proyecto queda en `build/out/juego` (`./juego`, `./difftest`,
`./bench`; ver su `LEEME.txt`). A mano: `./smsrecomp roms/juego.sms -o carpeta --trace traza.txt` y `make -C carpeta`.
`--viz`, `--why`, `--coverage`, `--events`, `--html`, `--codemap` y `--dis` muestran qué se descubrió y por qué (lo
mismo que la GUI de retrorecomp).

```
./smsrecomp "roms/juego.sms"
```

Sin opciones, muestra lo que se sabe del cartucho: tamaño y formato, SHA1 y CRC32, la cabecera TMR SEGA (dónde está, código de
producto, versión, región, tamaño y si la suma coincide con la que calcula la BIOS), las cabeceras de Codemasters y
SDSC si las hay, el mapper, y el modelo y el modo con que correría el juego. `--model sms|gg` fuerza el modelo: un
juego de Master System en el Game Gear corre en su modo SMS.

```
./smsrun "roms/juego.sms" --frames 1800 start:120-125
```

Corre el juego sin ventana con el intérprete y resume lo que pasó: ciclos, interrupciones, el mapeo, el estado del
VDP y los PC más frecuentes del último cuadro (los bucles de espera). `--watch DIR` avisa cuando cambia un byte de la
RAM, `--sram ARCHIVO` carga y guarda la RAM del cartucho, `--sram-text` imprime su texto (ahí deja ZEXALL su
resultado), `--sdsc` lo que el juego escribe en la consola de depuración SDSC, `--shot` y `--shots` la imagen y
`--wav` el sonido. Botones: `up down left right a b start pause`.

## Herramientas

| Comando | Qué hace |
|---|---|
| `smsrecomp juego.sms -o carpeta [--trace archivo]... [--name nombre] [--model sms\|gg]` | Genera el proyecto recompilado (ver «Lo que hace hoy»). Sin `-o`: `--info` (lo que se sabe del cartucho), `--viz [--color]`, `--coverage traza`, `--why BB:DIRECCION`, `--events`, `--html`, `--codemap`, `--dis OFF:DIR:N`, `--no-tables`. |
| `sms [rom] [--model sms\|gg] [--scale N] [--fullscreen] [--no-menu] [--record archivo \| --play archivo] [--trace archivo]` | La ventana (ver arriba); `--trace` guarda lo que se ejecutó, como `smstrace`. |
| `smstrace rom traza.txt`, `smsshot rom salida.png [--expect png] [--hash]`, `smsaudio rom salida.wav`, `smsramdump rom`, `smsbench rom [--repeat N]` | Sin ventana, con las opciones comunes de los otros cuatro (`--frames N`, `--input archivo`, `--seed S`, `boton:desde-hasta`) y `--model sms\|gg`: la traza (`BANCO:DIRECCION`), una captura, el sonido, la RAM y la velocidad del intérprete. |
| `smsrun rom [--frames N] [--model sms\|gg] [--watch DIR] [--sram archivo] [--sram-text] [--sdsc] [--shot png] [--shots dir [--every N]] [--wav archivo] [boton:desde-hasta]` | Diagnóstico sin ventana (ver arriba). |
| `carttest`, `bustest`, `vdptest`, `audiotest`, `statetest`, `inputtest`, `z80test` | Pruebas; `make test` corre todas, más ZEXDOC y ZEXALL completos. `z80test` (de `tools/retrorecomp/shared`) corre las SingleStepTests del Z80 (`sh tests/sstz80.sh`; por el camino del código generado, `SST_OPTS=--recomp sh tests/sstz80.sh`) y sus pruebas de interrupciones (`--int`, `--nmi`). |

`sh tests/contract.sh` es el contrato común de los cinco (opciones y comportamiento, con el juego del contrato,
`testroms/sms/contrato.sms`, que hace `make testroms`); `sh tests/golden.sh`, la línea base exacta (el hash de todos
los cuadros y del audio de cada ROM). La GUI de retrorecomp (`python3 tools/retrorecomp/retrorecomp_gui.py`)
reconoce las ROMs de Master System y Game Gear, también en `roms/sms` y `roms/gg`. Para comparar con emuladores de
referencia: `python3 tools/oracles/oracle.py compare rom.sms --frames 600` (baja Genesis Plus GX y Gearsystem la
primera vez) y la prueba de compatibilidad, `python3 tools/compat/run.py --solo etapa1`.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `sms.h`, `sms.c`, `cart.c`, `smsdb.c`, `bus.c`, `io.c`, `vdp.c`, `vdp_render.c`, `z80_host.h`, `z80_host.c`, `psg_host.h` y `audio_host.h`; de `tools/retrorecomp/shared`, común con GENRecomp y NEORecomp: `z80.h`, `z80_ops.h`, `z80.c`; con GENRecomp, `psg.c`, `psg.h` y `audio.c`; y también `sha1` y `console.h`. |
| `src/platform/` | La ventana `sms` (`main_sdl.c`, `i18n`, `settings.h`, `input_host.h`, el icono; con `-DSMS_RECOMP`, la del juego recompilado), `difftest.c` y `bench.c`; el resto, de `tools/retrorecomp/shared`. |
| `src/smsrecomp/` | El recompilador (`smsrecomp.c`), con el runtime y la plataforma adentro. |
| `src/tools/` | `smsrun` y las pruebas (`carttest`, `bustest`, `vdptest`, `audiotest`, `statetest`). |
| `tools/` | `make_icon.py` (el icono) y `retrorecomp/`: la GUI y el código común con NESRecomp, GBRecomp, GENRecomp y NEORecomp (submódulo). |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md` y `COMPATIBILIDAD.md`. |
| `tests/` | `run_tests.sh` (`make test`), `golden.sh` (la línea base exacta), `coverage.sh` (el descubrimiento sin traza), `ui_golden.txt` y `ui_golden_gg.txt` (la interfaz), `testroms.sh` (`make testroms`), `sstz80.sh`, y `contract.sh` y `recomp_game.sh` (los mismos archivos en los cinco repos). |
| `testroms/` | Pruebas libres (`make testroms`): las SingleStepTests del Z80, ZEXALL y ZEXDOC, el homebrew MIT y, con `EXTRA=1`, `extra/`. No se versionan. |
| `roms/` | Tus juegos. No se versionan. |

## Créditos

- Las ROMs de prueba que baja `make testroms` son de sus autores y no van en el repositorio: las
  [SingleStepTests del Z80](https://github.com/SingleStepTests/z80) (MIT), [ZEXALL para el
  SMS](https://github.com/maxim-zhao/zexall-sms) (Frank D. Cringle, J.G. Harston, Brett K, Maxim, Eric R. Quinn y
  otros; GPL-2.0), [Dragon Blaster](https://github.com/haroldo-ok/dragon-blaster), [Food
  Fighter](https://github.com/haroldo-ok/food-fighter) y [River Strike](https://github.com/haroldo-ok/river-strike)
  de haroldo-ok (MIT), y [Waternet](https://github.com/joyrider3774/waternet) de joyrider3774 (MIT).
- El hardware se implementa desde la documentación pública de [SMS Power!](https://www.smspower.org/Development/)
  (la del VDP de Charles MacDonald, la del SN76489 de Maxim, los mappers y las cabeceras) y «The Undocumented Z80
  Documented» de Sean Young. La temporización del VDP se midió con el VDPTEST de FluBBa.
- El icono es el mando del Master System de la serie TriMui de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons) (CC BY 4.0).
- La barra de menús y los diálogos vienen de Moskva 89, GBRecomp y NESRecomp (MIT), por retrorecomp.

## Licencia

SMSRecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp, GENRecomp, NEORecomp y
retrorecomp. La licencia cubre el código del recompilador y del runtime, no las ROMs ni el código que se genera a
partir de ellas: ese código es una traducción del programa del juego, así que sirve para jugar tu propia copia,
pero no distribuyas el proyecto ni el ejecutable de un juego comercial.
