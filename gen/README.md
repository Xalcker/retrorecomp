# GENRecomp

Recompilador estático de juegos de Sega Genesis, con la misma arquitectura que
[NESRecomp](https://github.com/Xalcker/NESrecomp) y [GBRecomp](https://github.com/Xalcker/GBRecomp):
toma una ROM y genera un proyecto C99 portable que se compila en un ejecutable nativo con SDL2.
Es híbrido (código recompilado más un intérprete 68000 de respaldo), sin datos de la ROM en el
proyecto generado (valida su SHA1) y con `difftest` para comprobar el código recompilado contra el
intérprete.

Por ahora solo NTSC (el Genesis de EE. UU., 60 Hz): los juegos y demos que exigen PAL esperan a
que esté hecho.

**Estado: las 9 fases del plan están hechas** (falta PAL). Los 23 juegos probados corren en el intérprete
y recompilados sin traza, sin diferencias con el intérprete: ver [docs/COMPATIBILIDAD.md](docs/COMPATIBILIDAD.md). El plan completo está en [docs/PLAN.md](docs/PLAN.md).

- **68000:** intérprete con la semántica en un solo lugar (`m68k_ops.h`), que también usará el código
  generado. Pasa las 1 000 058 [SingleStepTests](https://github.com/SingleStepTests/680x0) (estado,
  prefetch, RAM y ciclos) por los dos caminos: el intérprete y el del código generado.
- **Bus:** mapa de memoria, mandos de 3 y 6 botones, bus del Z80, SRAM, EEPROM serie, mapper de SSF2
  y el esqueleto del VDP (registros, líneas, interrupciones). Sin imagen todavía, Sonic 1 llega a jugar
  el nivel con el intérprete, Super Street Fighter II corre con su mapper y NBA Jam guarda su EEPROM.
- **VDP:** puertos, los tres DMA, planos con todos los scrolls, ventana, sprites, prioridades, sombra
  y resalte. Sonic 1, Super Street Fighter II y NBA Jam se ven bien en la ventana `gen`.
- **Z80 y sonido:** el Z80 pasa las 1 604 000 SingleStepTests; PSG y YM2612 propios, mezclados a
  44 100 Hz. Se oyen la voz "SEGA" de Sonic 1 y la música de los juegos de `roms/`.
- **Recompilador:** `genrecomp -o` genera el proyecto; cada instrucción usa la misma semántica que el
  intérprete. Los 23 juegos de `roms/` dan 1800 cuadros idénticos al intérprete en `difftest`.
- **Cobertura:** sin traza, el análisis encuentra las tablas de saltos y los punteros a código: entre
  el 93 y el 100 % de lo que ejecutan los juegos de `roms/` (Sonic 1: 100 %). `--why`, `--viz`,
  `--html` y `--coverage` muestran qué se encontró y por qué.
- **Velocidad:** recompilado, de 24 a 46 veces el tiempo real, 30 de media (el intérprete, de 16 a 33);
  lo que más pesa ahora es el dibujo del VDP, el Z80 y el YM2612. `sh tests/golden.sh` comprueba que una
  optimización no cambie ni un bit de la imagen ni del sonido.
- **Oráculos:** `tools/oracles/` compara la RAM y la imagen contra Genesis Plus GX y BlastEm (núcleos
  libretro que se bajan a `build/oracles/`, no van en el repo); con ellos se ajustaron la FIFO del VDP y el
  refresco de la DRAM. `tools/compat/run.py` corre la prueba de compatibilidad con una carpeta de ROMs.

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
make testroms   # una sola vez: las SingleStepTests del 68000 y del Z80 (~450 MB, comprimidas) y las demos Overdrive
make test       # pruebas
make dist       # dist/genrecomp-<versión>-<sistema>.tar.gz
```

**Versiones.** La versión es la etiqueta de git más cercana (`git describe`: `v0.1.0`, o `v0.1.0-3-gabc1234` con
commits después de ella). Al subir una etiqueta `vX.Y.Z` de `main` (`git tag v0.2.0 && git push origin v0.2.0`), el CI
publica un release en GitHub con los paquetes de Linux, macOS y Windows. Igual que en NESRecomp y GBRecomp.

## Lo que hace hoy

```
./gen "roms/juego.bin"
```

El intérprete con ventana y sonido: menús de juego (estados con F5/F8 en 4 ranuras), video (tamaño,
pantalla completa, escala entera, aspecto 4:3), audio (volumen y canales), entrada (mando de 3 o 6
botones), captura (F12) e idioma. `--record`/`--play archivo` graban y reproducen los botones y
`--trace archivo` guarda qué instrucciones de ROM se ejecutaron. Teclado:
flechas, Z X C = A B C, Enter = Start, A S D = X Y Z, Mayús = Mode (Ayuda → Controles).

```
./genrecomp "roms/juego.bin" -o juego/
make -C juego -j
./juego/juego "roms/juego.bin"
./juego/difftest "roms/juego.bin" 3600 --seed 2 --uncovered faltante.txt
./genrecomp "roms/juego.bin" -o juego/ --trace faltante.txt      # si faltó algo
```

El recompilador: descubre el código desde los vectores, sigue las tablas de saltos y los punteros a
código (y una traza, para lo que se le escape) y escribe un proyecto con el código recompilado, el runtime, la ventana, `difftest`
y `bench`. El ejecutable no lleva la ROM: la busca o la pide una vez y comprueba su SHA1. `difftest`
corre el código recompilado y el intérprete lado a lado y se detiene en la primera diferencia;
`--uncovered` guarda lo que el intérprete tuvo que ejecutar, para volver a recompilar con `--trace`.

```
./genrecomp "roms/juego.bin"
```

Sin `-o` (o con `--info`), lee `.bin`, `.md`, `.gen` y `.smd` (deshace el entrelazado; el SHA1 es el de la imagen normalizada,
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
--every N` una cada N cuadros. `--wav ARCHIVO.wav` guarda el sonido (y dice su nivel por segundo).

## Herramientas

| Comando | Qué hace |
|---|---|
| `gen juego.bin` | Ventana SDL2 con el intérprete (ver arriba). |
| `genrecomp juego.bin -o carpeta [--name n] [--trace traza]` | El recompilador (sin `-o` ni otras opciones, `--info`). |
| `genrecomp juego.bin --coverage traza [--viz] [--html archivo] [--why BB:DIRECCION] [--events/--codemap archivo] [--dis OFF:DIR:N]` | Qué se descubrió, cómo y qué falta; `sh tests/coverage.sh` lo mide con todas las ROMs de `roms/`. |
| `difftest`, `bench` (en el proyecto generado) | Código recompilado contra el intérprete; velocidad del recompilado. |
| `gentrace rom traza.txt [--frames N] [--input archivo] [--seed S]` | Traza de ejecución sin ventana (líneas `BANCO:DIRECCION`, el formato común con NESRecomp y GBRecomp). |
| `genshot rom salida.png [--frames N] [--input archivo] [--seed S] [--expect esperado.png] [--hash]` | Captura sin ventana; `--expect` pasa si la pantalla final es idéntica al PNG y `--hash` imprime un hash de todos los cuadros y de todo el sonido. |
| `genaudio rom salida.wav [--frames N] [--input archivo] [--seed S]` | Sonido a WAV (estéreo, 44 100 Hz), con el nivel por segundo. |
| `genbench rom [--frames N] [--repeat N] [--input archivo]` | Velocidad sin ventana. |
| `genramdump rom [--frames N] [--input archivo] [--seed S]` | Vuelca la RAM del 68000 tras correr la ROM. |
| `genrun rom [--frames N] [--watch DIR] [--sram archivo] [--tmss] [--shot/--shots/--wav ...]` | Diagnóstico: resume instrucciones, excepciones y dónde se va el tiempo. |
| `carttest`, `m68ktest`, `z80test`, `bustest`, `vdptest`, `audiotest`, `statetest`, `inputtest` | Pruebas; `make test` corre todas. |

**Opciones comunes.** Las herramientas sin ventana aceptan las mismas opciones que las de NESRecomp y
GBRecomp: `--frames N`, `--input archivo` (el guion de `--record`: `cuadro BOTONES [BOTONES]`, ver
`src/platform/inputs.h`), `--seed S` (suma un mando aleatorio reproducible, el mismo de las otras dos
consolas) y, en `genbench`, `--repeat N`. También valen el número de cuadros suelto y
`boton:desde-hasta` (`a b c x y z start mode up down left right`). `sh tests/contract.sh` lo comprueba.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `gen.h`, `gen.c`, `cart.c`, `bus.c`, `io.c`, `eeprom.c`, `vdp.c`, `vdp_render.c`, `m68k.h`, `m68k_ops.h`, `cpu68k.c`, `recomp.h`, `z80.h`, `z80.c`, `z80bus.c`, `psg.c`, `ym2612.c`, `audio.c`; `sha1`, `console.h` (de GBRecomp). |
| `src/genrecomp/` | El recompilador. |
| `src/platform/` | La ventana con SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `icono.h`), `difftest.c` y `bench.c` (también para los proyectos generados) y `png`, `files`, `export`, `random_pad.h`. |
| `src/tools/` | `gentrace`, `genshot`, `genaudio`, `genramdump`, `genrun`, las pruebas y `embed` (`genbench` está en `src/platform/bench.c`). |
| `tools/` | `oracles/` (Genesis Plus GX y BlastEm como referencia), `compat/` (prueba de compatibilidad), `retrorecomp/`: la GUI común con NESRecomp y GBRecomp (submódulo). |
| `docs/` | `PLAN.md`, `ARQUITECTURA.md`, `COMPATIBILIDAD.md` y `compat/` (el informe con miniaturas). |
| `tests/` | `run_tests.sh` (`make test`), `recomp_game.sh` (ciclo completo de un juego: `ROM NOMBRE [CUADROS] [SEMILLAS...]`, como en NESRecomp y GBRecomp), `sst68k.sh`, `golden.sh`, `coverage.sh`, `contract.sh` (el mismo archivo en los tres repos). |
| `testroms/` | Pruebas libres (`make testroms`): las SingleStepTests del 68000 y del Z80 y las demos Overdrive 1 y 2 de Titan (scene.org); a mano, el 240p Test Suite en `testroms/240p/`. No se versionan. |
| `roms/` | Tus juegos. No se versionan. |

## Créditos

- Interfaz basada en Moskva 89, GBRecomp y NESRecomp (MIT).
- Icono: `MD.png` (mando de Genesis, serie TriMui) de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), con
  licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Se incluye tal cual en
  `src/platform/icono.h`; `src/platform/icono.ico` es un recorte llevado a 256x256 (`tools/make_icon.py`).

## Licencia

GENRecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp y retrorecomp.
La licencia cubre el código del recompilador y del runtime, no las ROMs ni el código que se genera a
partir de ellas: ese código es una traducción del programa del juego, así que sirve para jugar tu
propia copia, pero no distribuyas el proyecto ni el ejecutable de un juego comercial.
