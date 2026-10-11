# GENRecomp

Recompilador estático de juegos de Mega Drive / Genesis, con la misma arquitectura que
[NESRecomp](https://github.com/Xalcker/NESrecomp) y [GBRecomp](https://github.com/Xalcker/GBRecomp):
toma una ROM y generará un proyecto C99 portable que se compila en un ejecutable nativo con SDL2.
Será híbrido (código recompilado más un intérprete 68000 de respaldo), sin datos de la ROM en el
proyecto generado (valida su SHA1) y con `difftest` para comprobar el código recompilado contra el
intérprete.

**Estado: fase 3 de 9** (esqueleto, cartucho, 68000, bus y VDP con ventana). El plan completo está en [docs/PLAN.md](docs/PLAN.md).

- **68000:** intérprete con la semántica en un solo lugar (`m68k_ops.h`), que también usará el código
  generado. Pasa las 1 000 058 [SingleStepTests](https://github.com/SingleStepTests/680x0) (estado,
  prefetch, RAM y ciclos) por los dos caminos: el intérprete y el del código generado.
- **Bus:** mapa de memoria, mandos de 3 y 6 botones, bus del Z80, SRAM, EEPROM serie, mapper de SSF2
  y el esqueleto del VDP (registros, líneas, interrupciones). Sin imagen todavía, Sonic 1 llega a jugar
  el nivel con el intérprete, Super Street Fighter II corre con su mapper y NBA Jam guarda su EEPROM.
- **VDP:** puertos, los tres DMA, planos con todos los scrolls, ventana, sprites, prioridades, sombra
  y resalte. Sonic 1, Super Street Fighter II y NBA Jam se ven bien en la ventana `gen` (sin sonido
  todavía).

## Compilar

Los mismos paquetes que en NESRecomp y GBRecomp: compilador C99, `make`, `pkg-config` y SDL2.

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

```
git submodule update --init     # la GUI común (tools/retrorecomp)
make            # gen (la ventana), genrecomp, genrun y las pruebas
make testroms   # una sola vez: las SingleStepTests del 68000 (~200 MB, comprimidas) en testroms/
make test       # pruebas
make dist       # dist/genrecomp-<versión>-<sistema>.tar.gz
```

## Lo que hace hoy

```
./gen "roms/juego.bin"
```

El intérprete con ventana (sin sonido todavía): menús de juego, video (tamaño, pantalla completa,
escala entera, aspecto 4:3), entrada (mando de 3 o 6 botones), captura (F12) e idioma. Teclado:
flechas, Z X C = A B C, Enter = Start, A S D = X Y Z, Mayús = Mode (Ayuda → Controles).

```
./genrecomp "roms/juego.bin"
```

Lee `.bin`, `.md`, `.gen` y `.smd` (deshace el entrelazado; el SHA1 es el de la imagen normalizada,
igual para los dos formatos) y muestra el formato, el SHA1, la cabecera de `$100` (nombres, serie,
región, periféricos), la suma declarada y la calculada, la RAM del cartucho y los vectores de
excepción, que serán las semillas del descubrimiento.

```
./genrun "roms/juego.bin" --frames 1500 --watch F600 start:600-605
```

Corre la ROM sin ventana con el intérprete (los botones `up down left right a b c start x y z mode`
se mantienen en esos cuadros) y resume instrucciones, ciclos, excepciones por vector y en qué PC se le
fue el tiempo al CPU en el último cuadro. `--watch DIR` imprime un byte de la RAM cada vez que cambia
(en Sonic 1, `$FFF600` es el modo de juego: `$04` título, `$0C` nivel). `--sram ARCHIVO` carga la
SRAM o EEPROM del cartucho al empezar y la guarda al terminar. `--tmss` simula una consola con TMSS
(apagado por omisión). `--shot ARCHIVO.png` guarda la imagen del último cuadro y `--shots CARPETA
--every N` una cada N cuadros.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `gen.h`, `gen.c`, `cart.c`, `bus.c`, `io.c`, `vdp.c`, `m68k.h`, `m68k_ops.h`, `cpu68k.c`; `sha1`, `console.h` (de GBRecomp). |
| `src/genrecomp/` | El recompilador. |
| `src/platform/` | La ventana con SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `icono.h`) y `png`, `files`, `export`, `random_pad.h`. |
| `src/tools/` | `genrun`, `carttest`, `m68ktest`, `bustest`, `vdptest`, `embed`. |
| `tools/` | `retrorecomp/`: la GUI común con NESRecomp y GBRecomp (submódulo). |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md`. |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh`, `contract.sh` (el mismo archivo en los tres repos). |
| `testroms/` | Pruebas libres (`make testroms`): las SingleStepTests del 68000. No se versionan. |
| `roms/` | Tus juegos. No se versionan. |

## Créditos

- Interfaz basada en Moskva 89, GBRecomp y NESRecomp (MIT).
- Icono: `MD.png` (mando de Mega Drive, serie TriMui) de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), con
  licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Se incluye tal cual en
  `src/platform/icono.h`; `src/platform/icono.ico` es un recorte llevado a 256x256 (`tools/make_icon.py`).

## Licencia

GENRecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp y retrorecomp.
La licencia cubre el código del recompilador y del runtime, no las ROMs ni el código que se genera a
partir de ellas: ese código es una traducción del programa del juego, así que sirve para jugar tu
propia copia, pero no distribuyas el proyecto ni el ejecutable de un juego comercial.
