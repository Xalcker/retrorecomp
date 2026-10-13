# NEORecomp

Recompilador estático de juegos de Neo Geo, con la misma arquitectura que
[GENRecomp](https://github.com/Xalcker/GENRecomp), [NESRecomp](https://github.com/Xalcker/NESRecomp) y
[GBRecomp](https://github.com/Xalcker/GBRecomp): toma un juego y genera un proyecto C99 portable que se
compila en un ejecutable nativo con SDL2. Es híbrido (código recompilado más un intérprete 68000 de
respaldo), sin datos del juego ni de la BIOS en el proyecto generado (valida sus SHA1) y con `difftest`
para comprobar el código recompilado contra el intérprete.

Primero el AES (la consola), NTSC y los juegos sin cifrar. El MVS, los juegos cifrados (desde 1999) y PAL
vienen después.

**Estado: fases 0 a 9 de 10** (carga de juegos y BIOS; el 68000 y el Z80, que pasan sus SingleStepTests; el bus, la BIOS y las interrupciones; el video, el sonido y la ventana: Metal Slug, Samurai Shodown II y Neo Turf Masters se ven y se oyen en la ventana `neo`; las herramientas sin ventana y el contrato común; el recompilador: los juegos recompilados dan cuadros idénticos al intérprete y corren a 43-55 veces el tiempo real; sin traza, el análisis encuentra el 99 % de lo que ejecutan 8 juegos y el 100 % de la BIOS; los 8 recompilados sin traza no difieren del intérprete y coinciden con Geolith, ver [docs/COMPATIBILIDAD.md](docs/COMPATIBILIDAD.md)). El plan completo está en [docs/PLAN.md](docs/PLAN.md) y
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
make testroms   # una sola vez: las SingleStepTests del 68000 y del Z80 (~450 MB, comprimidas) y el juego de prueba
make test       # pruebas
make dist       # dist/neorecomp-<versión>-<sistema>.tar.gz
```

## Lo que hace hoy

```
./neo roms/mslug.zip
```

El intérprete con ventana y sonido: menús de juego (abrir, reiniciar, pausa, tarjeta de memoria), video
(tamaño, pantalla completa, escala entera, aspecto 4:3), audio (volumen y silenciar FM, SSG, ADPCM-A y
ADPCM-B), estados (F5/F8 en 4 ranuras), captura (F12) e idioma. `--record`/`--play archivo` graban y
reproducen los botones y `--trace archivo` guarda qué instrucciones de ROM se ejecutaron. Teclado: flechas, Z X C V =
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

```
./neorecomp roms/mslug.zip -o mslug/
make -C mslug -j
./mslug/mslug roms/mslug.zip
./mslug/difftest roms/mslug.zip 1800 --seed 2 --uncovered faltante.txt
./neorecomp roms/mslug.zip -o mslug/ --trace faltante.txt      # si faltó algo
```

El recompilador: descubre el código del juego y de la BIOS (cada uno en su espacio), sigue las tablas de
saltos y los punteros a código (y una traza, para lo que se le escape) y escribe un proyecto con el código
recompilado, el runtime, la ventana, `difftest` y `bench`. El ejecutable no lleva el juego ni la BIOS:
busca el juego o lo pide una vez y comprueba su SHA1; la BIOS, como el intérprete. La BIOS se recompila una
vez por versión en `~/.neorecomp/bios/` (o `$NEO_BIOS_CACHE`) y la comparten todos los juegos. `difftest`
corre el código recompilado y el intérprete lado a lado y se detiene en la primera diferencia;
`--uncovered` guarda lo que el intérprete tuvo que ejecutar, para volver a recompilar con `--trace`.
`sh tests/recomp_game.sh roms/juego.zip nombre` hace todo el ciclo.

## Herramientas

**Opciones comunes.** Las herramientas sin ventana aceptan las mismas opciones que las de NESRecomp, GBRecomp y
GENRecomp: `--frames N`, `--input archivo` (el guion de `--record`: `cuadro BOTONES [BOTONES]`, ver
`src/platform/inputs.h`), `--seed S` (suma el mando aleatorio común) y, en `neobench`, `--repeat N`. También
valen el número de cuadros suelto y `boton:desde-hasta` (`up down left right a b c d start select`).
`sh tests/contract.sh` lo comprueba.

| Comando | Qué hace |
|---|---|
| `neorecomp juego -o carpeta [--name n] [--bios archivo] [--trace traza]` | El recompilador (sin `-o` ni otras opciones, `--info`; `--neo` exporta a `.neo`). |
| `neorecomp juego --coverage traza [--viz] [--html archivo] [--why BB:DIRECCION] [--events/--codemap archivo] [--dis OFF:DIR:N]` | Qué se descubrió, cómo y qué falta. |
| `difftest`, `bench` (en el proyecto generado) | Código recompilado contra el intérprete; velocidad del recompilado. |
| `neo juego [--bios archivo] [--scale N] [--record/--play archivo] [--trace archivo]` | Ventana SDL2 con el intérprete (ver arriba). |
| `neotrace juego traza.txt [--frames N] [--input archivo] [--seed S]` | Traza de ejecución sin ventana (líneas `BANCO:DIRECCION`, el formato común; la BIOS en el banco `FF`). |
| `neoshot juego salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]` | Captura sin ventana; `--expect` pasa si la pantalla final es idéntica al PNG y `--hash` imprime un hash de todos los cuadros y de todo el sonido. |
| `neoaudio juego salida.wav [--frames N] [--input archivo] [--seed S]` | Sonido a WAV (estéreo, 44 100 Hz), con el nivel por segundo. |
| `neobench juego [--frames N] [--repeat N] [--input archivo]` | Velocidad sin ventana. |
| `neoramdump juego [--frames N] [--input archivo] [--seed S]` | Vuelca la RAM del 68000 tras correr el juego. |
| `neorun juego [--frames N] [--bios archivo] [--card archivo [--cardlog]] [--watch DIR] [--fix] [--shot archivo.png] [--shots carpeta --every N] [--wav archivo.wav [--mute M]] [boton:desde-hasta]` | Corre sin ventana con el intérprete y resume lo que pasó: instrucciones, excepciones, el Z80, las variables de la BIOS, dónde se va el tiempo, (`--fix`) la capa fija como texto, (`--shot`) la imagen y (`--wav`) el sonido con su nivel por segundo y (`--cardlog`) las llamadas a la tarjeta de memoria. Botones: `up down left right a b c d start select`. |
| `neocard juego --card archivo [--bios archivo] orden ...` | Usa la tarjeta de memoria por la interfaz de la BIOS (`CARD`, `CARD_ERROR`): formatear, buscar, cargar, guardar, borrar, el nombre, dañarla, protegerla. Sirve para comparar lo que hacen la BIOS original y la propia (`tools/neobios/tarjeta.py` muestra lo que importa de una tarjeta). |
| `carttest`, `bustest`, `lspctest`, `audiotest`, `statetest`, `inputtest`, `m68ktest`, `z80test` | Pruebas (`make test`): la carga, el bus y la consola, el dibujo, el sonido, y las SingleStepTests del 68000 (por el intérprete y por el camino del código generado) y del Z80. |
| `sh tests/coverage.sh` | Cuánto de lo que ejecutan los juegos de `roms/` encuentra el análisis sin traza (y de la BIOS). |
| `sh tests/golden.sh [--update]` | Hash de todos los cuadros y del sonido de cada juego: una optimización no cambia ni un bit. |
| `python3 tools/oracles/oracle.py fetch/ramdump/shot/wav/compare/mix` | Oráculos: Geolith y FBNeo (núcleos libretro que se bajan a `build/oracles/`, solo como programas aparte). `compare` mide la RAM y la imagen contra ellos; `mix`, los niveles de cada parte del YM2610. |
| `python3 tools/compat/run.py [--oraculos]`, `report.py` | Prueba de compatibilidad con los juegos de una carpeta (el motor común de `tools/retrorecomp/compat.py`): `docs/compat/`. |

## Juegos y BIOS

Ni los juegos ni la BIOS van en el repositorio: se ponen en `roms/` (no se versiona).

- **BIOS:** la del AES, `neo-epo.bin` (exportación) o `neo-po.bin` (Japón), sueltas o dentro del
  `aes.zip` de MAME. No hay una BIOS del AES de EE. UU.: la de exportación declara la región Europa. O la
  propia (en desarrollo, fase 10): `make neobios` deja `build/neobios.bin`, que se usa con `--bios` o
  `NEO_BIOS` (o como `neobios.bin` en `roms/`, si no hay una de SNK). Con ella corren los 8 juegos probados.
- **Juegos:** `.neo`, zip de MAME o carpeta. Los juegos cifrados (NEO-CMC, SMA, PCM2, PVC: casi todos
  desde 1999) y los pocos con protección del programa todavía no.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `neo.h`, `neo.c`, `bus.c` (mapa de memoria), `lspc.c` y `lspc_render.c` (el video), `ym2610.c` y `audio.c` (el sonido), `z80bus.c`, `cart.c` (formatos e imagen normalizada), `bios.c` (BIOS y tabla L0), `zip.c` (zip e inflate), `neodb.c` (base de datos de juegos), `m68k_ops.h`/`cpu68k.c` (el 68000) y `z80.c` (el Z80), los de GENRecomp; `sha1`, `console.h`, `trace_line.h` (común con los otros recompiladores). |
| `src/neorecomp/` | El recompilador. |
| `src/platform/` | La ventana con SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `icono.h`) y lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (común). |
| `src/tools/` | `neotrace`, `neoshot`, `neoaudio`, `neoramdump`, `neorun`, `neocard`, `mkcontrato`, las pruebas, `embed` (`neobench` está en `src/platform/bench.c`). |
| `tools/` | `retrorecomp/`: la GUI común con los otros recompiladores y el motor de la prueba de compatibilidad (submódulo); `oracles/` (`retro_run`, `oracle.py`); `compat/` (`run.py`, `report.py`); `make_icon.py` (los iconos). |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh`, `sstz80.sh`, `testroms.sh` (`make testroms`), `contract.sh` (el mismo archivo en los cuatro repos), `recomp_game.sh`, `coverage.sh`, `golden.sh` y `golden.txt`. |
| `testroms/` | Pruebas libres (`make testroms`): las SingleStepTests del 68000 y del Z80 y el juego de prueba del contrato (hecho aquí). No se versionan. |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md`, `COMPATIBILIDAD.md` y `compat/` (el informe). |

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
