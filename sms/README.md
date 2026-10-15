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

**Estado: fases 0 a 2 de 8.** Lee el cartucho y lo describe; el Z80 común tiene su semántica en una sola fuente de
verdad; el bus, los mappers, la E/S, las interrupciones y el VDP sin el dibujo corren los juegos con el intérprete
(`smsrun`), todavía sin imagen ni sonido. El plan completo
está en [docs/PLAN.md](docs/PLAN.md).

- **Cartucho:** `.sms` y `.gg` (con o sin cabecera de copiador), la cabecera TMR SEGA con su suma (calculada como
  la BIOS), la de Codemasters y la SDSC, el mapper (Sega, Codemasters o ninguno) y el modelo con que correría el
  juego (Master System II o Game Gear, y el modo SMS del Game Gear).
- **Bus y E/S:** los mappers Sega y Codemasters con la RAM del cartucho (y su partida), los puertos del Master
  System y del Game Gear con su decodificación parcial, los mandos, la detección de consola de exportación y la
  Pausa por NMI. Arranca con el estado que deja la BIOS de EE. UU., sin necesitarla.
- **VDP sin el dibujo:** VRAM, CRAM (la de 12 bits del Game Gear con su latch), registros, contadores V y H y las
  interrupciones de cuadro y de línea, al ciclo de cada acceso a sus puertos. ZEXDOC y ZEXALL pasan sus 79 pruebas
  y los juegos homebrew de `make testroms` encienden la pantalla y atienden sus interrupciones.
- **Z80:** el núcleo común de GENRecomp y NEORecomp (retrorecomp/shared), con la semántica de cada instrucción en
  un solo lugar (`z80_ops.h`) para el intérprete y el código generado, como el 68000 de GENRecomp. Pasa las
  1 604 000 [SingleStepTests](https://github.com/SingleStepTests/z80) por los dos caminos, con su decodificador
  y el ciclo de cada acceso a un puerto.

## Compilar

Los mismos paquetes que en NESRecomp, GBRecomp, GENRecomp y NEORecomp: compilador C99, `make`, `pkg-config` y SDL2
(la ventana llega en la fase 3).

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

```
git submodule update --init     # el código común y la GUI (tools/retrorecomp)
make            # smsrecomp y las pruebas
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
./smsrecomp "roms/juego.sms"
```

Muestra lo que se sabe del cartucho: tamaño y formato, SHA1 y CRC32, la cabecera TMR SEGA (dónde está, código de
producto, versión, región, tamaño y si la suma coincide con la que calcula la BIOS), las cabeceras de Codemasters y
SDSC si las hay, el mapper, y el modelo y el modo con que correría el juego. `--model sms|gg` fuerza el modelo: un
juego de Master System en el Game Gear corre en su modo SMS.

```
./smsrun "roms/juego.sms" --frames 1800 start:120-125
```

Corre el juego sin ventana con el intérprete y resume lo que pasó: ciclos, interrupciones, el mapeo, el estado del
VDP y los PC más frecuentes del último cuadro (los bucles de espera). `--watch DIR` avisa cuando cambia un byte de la
RAM, `--sram ARCHIVO` carga y guarda la RAM del cartucho, `--sram-text` imprime su texto (ahí deja ZEXALL su
resultado) y `--sdsc` lo que el juego escribe en la consola de depuración SDSC. Botones: `up down left right a b
start pause`.

## Herramientas

| Comando | Qué hace |
|---|---|
| `smsrecomp juego.sms [--info] [--model sms\|gg]` | Lo que se sabe del cartucho. La recompilación (`-o carpeta`, `--trace`, `--viz`, `--why`...) llega en la fase 5. |
| `smsrun rom [--frames N] [--model sms\|gg] [--watch DIR] [--sram archivo] [--sram-text] [--sdsc] [boton:desde-hasta]` | Diagnóstico sin ventana (ver arriba). |
| `carttest`, `bustest`, `statetest`, `z80test` | Pruebas; `make test` corre todas, más ZEXDOC y ZEXALL completos. `z80test` (de `tools/retrorecomp/shared`) corre las SingleStepTests del Z80 (`sh tests/sstz80.sh`; por el camino del código generado, `SST_OPTS=--recomp sh tests/sstz80.sh`) y sus pruebas de interrupciones (`--int`, `--nmi`). |

La ventana `sms` y las herramientas sin ventana (`smstrace`, `smsshot`, `smsaudio`, `smsbench`, `smsramdump`, con
las opciones comunes `--frames`, `--input` y `--seed` de los otros cuatro) llegan en las fases 3 y 4.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `sms.h`, `sms.c`, `cart.c`, `smsdb.c`, `bus.c`, `io.c`, `vdp.c`, `z80_host.h` y `z80_host.c`; de `tools/retrorecomp/shared`, común con GENRecomp y NEORecomp: `z80.h`, `z80_ops.h`, `z80.c`, y también `sha1` y `console.h`. |
| `src/smsrecomp/` | El recompilador. |
| `src/tools/` | `smsrun` y las pruebas (`carttest`, `bustest`, `statetest`). |
| `tools/` | `retrorecomp/`: la GUI y el código común con NESRecomp, GBRecomp, GENRecomp y NEORecomp (submódulo). |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md` y `COMPATIBILIDAD.md`. |
| `tests/` | `run_tests.sh` (`make test`), `testroms.sh` (`make testroms`), `sstz80.sh`, y `contract.sh` y `recomp_game.sh` (los mismos archivos en los cinco repos). |
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
  Documented» de Sean Young.

## Licencia

SMSRecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp, GENRecomp, NEORecomp y
retrorecomp. La licencia cubre el código del recompilador y del runtime, no las ROMs ni el código que se genera a
partir de ellas: ese código es una traducción del programa del juego, así que sirve para jugar tu propia copia,
pero no distribuyas el proyecto ni el ejecutable de un juego comercial.
