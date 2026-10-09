# NESRecomp

Recompilador estático de juegos de NES, basado en la arquitectura de GBRecomp: toma una ROM
(`.nes`) y genera un proyecto C99 portable que se compila en un ejecutable nativo con SDL2. Híbrido
(código recompilado más el intérprete 6502 de respaldo), sin datos de la ROM en el proyecto
generado (valida su SHA1) y con `difftest` para comprobar el código recompilado contra el intérprete.

```
make                                  # nesrecomp, nes (intérprete con ventana) y herramientas
./nestrace juego.nes traza.txt 3000 start:120-125 right:400-3000   # qué código ejecuta el juego
./nesrecomp juego.nes -o juego/ --trace traza.txt
cd juego && make && ./juego juego.nes
./difftest juego.nes 1500 start:150-155 right:700-1500
```

Más en [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).

## Estado

- CPU 6502 con opcodes no oficiales: pasa `nestest` y las 16 `instr_test-v5`.
- PPU 2C02 punto por punto: fondo, sprites, scroll, sprite 0 hit (pasa las 11 `sprite_hit_tests`).
- Mappers: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3 (IRQ por flanco de A12 con filtro), 7 AxROM, 11 Color Dreams,
  34 BNROM/NINA-001, 66 GxROM, 71 Camerica, 79 NINA-03/06, 87, 140, 180, 206 (DxROM), 9 (MMC2), 10 (MMC4), 69 (FME-7,
  sin el audio 5B), 119 (TQROM), 5 (MMC5, con sus dos pulsos pero sin PCM ni ExGrafix) y 118 (TxSROM, solo con `mappertest`). Probados con
  juegos reales: Punch-Out, Fire Emblem, Batman: Return of the Joker, Pin Bot, Castlevania III, Kirby, Battletoads,
  Dragon Warrior III (512 KB), Gauntlet, Micro Machines, Crystal Mines, SMB + Duck Hunt y Tiles of Fate.
- APU: 2 pulsos, triángulo, ruido, DMC, frame counter con IRQ, mezcla no lineal y filtros. Pasa las 8
  `apu_test`; la ventana saca audio por SDL (tecla M = silencio).
- Temporización por ciclo: cada acceso al bus (con lecturas y escrituras falsas) avanza 3 puntos de PPU y
  1 de APU. Pasa `nestest` (incluida la posición de la PPU), `cpu_timing_test`, `instr_timing`,
  `instr_misc`, `branch_timing_tests`, las 10 `ppu_vbl_nmi`, `cpu_interrupts_v2` 1, 2, 3 y 5 y las 11 pruebas viejas del
  frame counter del APU.
- RAM con batería: `juego.sav` en la carpeta de datos del juego (se guarda al salir y cada 5 s si cambia).
- **Interfaz** (SDL2, español e inglés): barra de menús y diálogos de Moskva 89 / GBRecomp; estados guardados (4 ranuras),
  pausa, avance rápido (Tab), 2 jugadores (teclado y mandos), tamaños 1x-6x, pantalla completa, escala entera, aspecto 8:7,
  overscan, paletas (predeterminada, grises, saturada y `paleta.pal` propia), volumen y silenciar canales del APU,
  captura PNG, grabación de audio WAV, grabar/reproducir entradas (`--record`/`--play`), arrastrar una ROM a la ventana.
  El juego recompilado pide la ROM una sola vez (valida el SHA1) y guarda una copia en su carpeta de datos.
- **Recompilador**: descubrimiento desde los vectores y una traza de ejecución, emisión de C por
  instrucción con los mismos accesos al bus que el intérprete, despacho por desplazamiento de PRG
  (sirve con cualquier banco) y `difftest`. Con una traza, Super Mario Bros., Donkey Kong, Arkanoid,
  Contra, Castlevania, Mega Man, Super Mario Bros. 3 y Zelda corren 1500 cuadros con 100 % de los ciclos
  en código recompilado (Zelda 99.7 %) e **idénticos al intérprete** (CPU, RAM, PPU, APU, mapper,
  imagen y audio). **Sin traza**, solo con el análisis estático (vectores, bancos, tablas de saltos), los mismos juegos corren con 97-100 % de
  los ciclos recompilados e idénticos al intérprete; el análisis encuentra el 95-100 % de lo que cada juego ejecuta
  (`tests/coverage.sh`). La traza queda como ayuda para lo que falte.

### Rendimiento

PPU y APU perezosos (solo avanzan cuando hace falta, resultado idéntico bit a bit; ver la arquitectura). Super Mario
Bros.: ~55x tiempo real recompilado y ~42x con el intérprete (antes 13x y 11x); Super Mario Bros. 3 (MMC3) ~25x con el
intérprete (antes 12x). Con el MMC3 la PPU solo va ciclo a ciclo a partir del punto 256 de cada línea, donde se
levanta su IRQ. `tests/golden.sh` vigila que ninguna optimización cambie
ni un byte de imagen o audio.

### Conocido

- AccuracyCoin arranca y corre cientos de pruebas, pero hacia la prueba 154 la pantalla se corrompe (algún comportamiento del
  bus o de la PPU que no se imita); sin analizar.

- `mmc3_test_2/4-scanline_timing` falla en la subprueba 12; `cpu_interrupts_v2/4-irq_and_dma` (IRQ alrededor del DMA
  de OAM y del DMC); DMA del DMC sin los ciclos exactos; `6-MMC3_alt` (otra revisión del chip).
- El código que el descubrimiento no alcanza (y que no está en la traza), o que está en RAM, lo corre el
  intérprete. Es correcto, solo más lento. El descubrimiento especulativo agrega mucho código que nunca
  se ejecuta (SMB3: 69 mil instrucciones, ~13 s de compilación con `make -j8`).

## Herramientas

| Comando | Qué hace |
|---|---|
| `nes juego.nes` | Ventana SDL2 con el intérprete. `NES_TRACE=archivo` guarda la traza mientras juegas. |
| `nesrecomp` | El recompilador. |
| `nestrace rom traza cuadros [boton:desde-hasta ...]` | Traza de ejecución sin ventana. |
| `nesshot rom salida.png cuadros [...]` | Captura sin ventana; imprime el resultado de las ROMs de Blargg. |
| `nesaudio rom salida.wav cuadros [...]` | Audio a WAV, con el nivel por segundo. |
| `nesbench rom [cuadros]` | Velocidad sin ventana. |
| `nestest`, `sramtest` | Pruebas. `make test` corre todas. |

`tests/recomp_game.sh rom nombre` recompila un juego con una traza y lo compara contra el intérprete.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `nes.c`, `cpu6502.c`, `cpu_ops.h`, `ppu.c`, `apu.c`, `mapper.c`, `recomp.h`; `sha1`, `console.h` (de GBRecomp). |
| `src/nesrecomp/` | El recompilador. |
| `src/platform/` | La interfaz SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `files`, `export`, `png`, `inputs.h`), `difftest.c` y `bench.c`. Se copian a cada proyecto generado. |
| `src/tools/` | Herramientas sin ventana y `embed`. |
| `ref_gb/` | Código de GBRecomp como referencia de diseño. No se compila. |
| `tools/` | `visor.html` y `truth/` copiados de GBRecomp (por adaptar a 6502). |
| `testroms/` | ROMs de prueba: `nes-test-roms/` (nestest, Blargg, mmc3...) y `accuracycoin/` (AccuracyCoin, MIT). No se versionan. |
| `roms/` | Tus juegos. No se versionan. |

## Pendiente

- Visualización del descubrimiento (`viz`, `--why` de GBRecomp).
- Más mappers (VRC2/4/6/7, mapper 15), el PCM del MMC5, el audio 5B del FME-7 y ExGrafix del MMC5.

## Créditos

- Interfaz basada en Moskva 89 y GBRecomp (MIT).
- Icono de la ventana: `FC.png` (control de NES) de la serie TriMui de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons) (`src/platform/icono.h`).
  Revisa la licencia de ese repositorio antes de distribuir el proyecto.
- ROMs de prueba de `testroms/`: [christopherpow/nes-test-roms](https://github.com/christopherpow/nes-test-roms)
  (nestest, Blargg y otros; no se versionan).
