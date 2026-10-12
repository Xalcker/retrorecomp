# NEORecomp

Recompilador estático de juegos de Neo Geo, con la misma arquitectura que
[GENRecomp](https://github.com/Xalcker/GENRecomp), [NESRecomp](https://github.com/Xalcker/NESrecomp) y
[GBRecomp](https://github.com/Xalcker/GBRecomp): toma un juego y genera un proyecto C99 portable que se
compila en un ejecutable nativo con SDL2. Es híbrido (código recompilado más un intérprete 68000 de
respaldo), sin datos del juego ni de la BIOS en el proyecto generado (valida sus SHA1) y con `difftest`
para comprobar el código recompilado contra el intérprete.

Primero el AES (la consola), NTSC y los juegos sin cifrar. El MVS, los juegos cifrados (desde 1999) y PAL
vienen después.

**Estado: fases 0 a 3 de 10** (carga de juegos y BIOS; el 68000 y el Z80, que pasan sus SingleStepTests; el bus, la BIOS y las interrupciones; el video y la ventana: Metal Slug, Samurai Shodown II y Neo Turf Masters se ven bien en la ventana `neo`, todavía sin sonido). El plan completo está en [docs/PLAN.md](docs/PLAN.md) y
cómo está hecho, en [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).

## Compilar

Los mismos paquetes que en GENRecomp, NESRecomp y GBRecomp: compilador C99, `make`, `pkg-config` y SDL2
(para la ventana `neo`).

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

```
git submodule update --init     # la GUI común (tools/retrorecomp)
make            # neo (la ventana), neorecomp, neorun y las pruebas
make testroms   # una sola vez: las SingleStepTests del 68000 y del Z80 (~450 MB, comprimidas)
make test       # pruebas
make dist       # dist/neorecomp-<versión>-<sistema>.tar.gz
```

## Lo que hace hoy

```
./neo roms/mslug.zip
```

El intérprete con ventana: menús de juego (abrir, reiniciar, pausa, tarjeta de memoria), video (tamaño,
pantalla completa, escala entera, aspecto 4:3), captura (F12) e idioma. Teclado: flechas, Z X C V =
A B C D, Enter = Start, Mayús = Select (Ayuda → Controles). La BIOS se busca junto al juego y en `roms/`
(o `--bios archivo`); si no está, la pide una vez y guarda una copia en la carpeta de datos.

```
./neorecomp roms/mslug.zip
```

Lee el juego (un `.neo`, un zip al estilo MAME o una carpeta con los chips), lo normaliza y muestra el
formato, el SHA1 de la imagen normalizada y de cada región (P, S, M1, V-A, V-B, C), la cabecera de `$100`
(NGH, tamaño, RAM de respaldo, DIP, las entradas USER, PLAYER_START, DEMO_END y COIN_SOUND) y los
vectores. Busca la BIOS del AES junto al juego o en `roms/` (`aes.zip` de MAME, `neo-epo.bin` o
`neo-po.bin`; también `--bios archivo` o la variable `NEO_BIOS`) y la valida por su SHA1. La tabla L0
(reducción vertical de los sprites, un chip de la placa) se calcula y se compara con la del chip.
`--neo salida.neo` escribe el juego como `.neo`.

## Herramientas

| Comando | Qué hace |
|---|---|
| `neorecomp juego [--info] [--bios archivo] [--neo salida.neo]` | Información del juego y de la BIOS; exportar a `.neo`. El recompilador (`-o`) llega en la fase 6. |
| `neo juego [--bios archivo] [--scale N]` | Ventana SDL2 con el intérprete (ver arriba). |
| `neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR] [--fix] [--shot archivo.png] [--shots carpeta --every N] [boton:desde-hasta]` | Corre sin ventana con el intérprete y resume lo que pasó: instrucciones, excepciones, el Z80, las variables de la BIOS, dónde se va el tiempo, (`--fix`) la capa fija como texto y (`--shot`) la imagen. Botones: `up down left right a b c d start select`. |
| `carttest`, `bustest`, `lspctest`, `m68ktest`, `z80test` | Pruebas (`make test`): la carga, el bus y la consola, el dibujo, y las SingleStepTests del 68000 (por el intérprete y por el camino del código generado) y del Z80. |

## Juegos y BIOS

Ni los juegos ni la BIOS van en el repositorio: se ponen en `roms/` (no se versiona).

- **BIOS:** la del AES, `neo-epo.bin` (exportación) o `neo-po.bin` (Japón), sueltas o dentro del
  `aes.zip` de MAME. No hay una BIOS del AES de EE. UU.: la de exportación declara la región Europa.
- **Juegos:** `.neo`, zip de MAME o carpeta. Los juegos cifrados (NEO-CMC, SMA, PCM2, PVC: casi todos
  desde 1999) y los pocos con protección del programa todavía no.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `neo.h`, `neo.c`, `bus.c` (mapa de memoria), `lspc.c` y `lspc_render.c` (el video), `z80bus.c`, `ym2610.c`, `cart.c` (formatos e imagen normalizada), `bios.c` (BIOS y tabla L0), `zip.c` (zip e inflate), `neodb.c` (base de datos de juegos), `m68k_ops.h`/`cpu68k.c` (el 68000) y `z80.c` (el Z80), los de GENRecomp; `sha1`, `console.h`, `trace_line.h` (común con los otros recompiladores). |
| `src/neorecomp/` | El recompilador. |
| `src/platform/` | La ventana con SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `icono.h`) y lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (común). |
| `src/tools/` | `neorun`, `carttest`, `bustest`, `lspctest`, `m68ktest`, `z80test`, `embed`. |
| `tools/` | `retrorecomp/`: la GUI común con los otros recompiladores (submódulo); `make_icon.py` (los iconos). |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh`, `sstz80.sh`, `testroms.sh` (`make testroms`), `contract.sh` (el mismo archivo en los cuatro repos). |
| `testroms/` | Pruebas libres (`make testroms`): las SingleStepTests del 68000 y del Z80. No se versionan. |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md`. |

## Créditos

- Interfaz basada en Moskva 89, GBRecomp, NESRecomp y GENRecomp (MIT).
- Icono: `NEOGEO.png` (mando del Neo Geo AES, serie TriMui) de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), con licencia
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Se incluye tal cual en `src/platform/icono.h`;
  `src/platform/icono.ico` es un recorte llevado a 256x256 (`tools/make_icon.py`).
- Los formatos y el comportamiento del hardware salen de la [NeoGeo Development Wiki](https://wiki.neogeodev.org).

## Licencia

NEORecomp se distribuye con licencia [MIT](LICENSE), la misma que GENRecomp, NESRecomp, GBRecomp y
retrorecomp. La licencia cubre el código del recompilador y del runtime, no los juegos, la BIOS ni el código
que se genera a partir de ellos: ese código es una traducción del programa del juego, así que sirve para
jugar tu propia copia, pero no distribuyas el proyecto ni el ejecutable de un juego comercial.
