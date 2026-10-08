# GBRecomp

Recompilador estático de juegos de Game Boy. Toma una ROM (`.gb` o `.gbc`) sin
desensamblado previo y genera un proyecto en C99 portable que se compila en
un ejecutable nativo para Windows, Linux o macOS. El ejecutable se ve, suena y
se juega igual que el original.

```
gbrecomp juego.gb -o juego/
cd juego && make
./juego
```

- **Híbrido:** el código que se descubre se traduce a C. Lo que no se pudo
  descubrir (saltos indirectos raros, código en RAM, código automodificable)
  lo ejecuta un intérprete SM83 exacto al ciclo, y el control regresa al
  código recompilado en cuanto se puede.
- **Exacto al ciclo:** el código recompilado cuenta los M-ciclos de cada
  acceso al bus. Timer, PPU, APU, serial y DMA avanzan igual que en el
  hardware, así que los efectos a mitad de línea funcionan.
- **Sin datos de la ROM:** el proyecto generado no contiene la ROM. El
  ejecutable la pide una vez, valida su SHA1 y guarda una copia en la carpeta
  de datos del juego.
- **Verificable:** cada proyecto trae `difftest`, que corre el código
  recompilado y el intérprete lado a lado y reporta la primera diferencia
  (registros, RAM, VRAM, OAM, I/O).

## Modelos

| Cabecera del juego | Corre como |
|---|---|
| Admite color (`$143` bit 7) | Game Boy Color, en modo CGB |
| Admite Super Game Boy | Super Game Boy: borde y colores de la SNES (256×224) |
| Ninguno de los dos | Game Boy Color en modo de compatibilidad, coloreado con la paleta que le asigna la boot ROM según el título |

`--model dmg|cgb|sgb` fuerza otro modelo. En el modo de compatibilidad,
`--palette` elige una de las 12 combinaciones de botones del GBC (`up`,
`up+a`, `left+b`, …).

## Compilar GBRecomp

Se necesita un compilador C99, `make` y, para los juegos generados, SDL2.

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 make` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev` |
| macOS | `brew install sdl2 pkg-config` |

```
make            # gbrecomp, gbtest y el runtime
make dist       # dist/gbrecomp-<versión>.tar.gz
```

`gbrecomp` es un solo ejecutable que lleva el runtime adentro, así que se
puede copiar a cualquier carpeta. En Windows, el juego generado queda
enlazado de forma estática y no necesita DLLs.

## Uso

```
gbrecomp juego.gb -o salida/ [--name nombre] [--trace archivo]
```

El descubrimiento estático encuentra casi todo el código. Para lo que quede
(saltos por tablas que no se pudieron resolver), se hace un ciclo con trazas:

1. Juega (o corre `difftest` con entradas aleatorias) con `--uncovered
   faltantes.txt`. Quedan anotadas las direcciones que tuvo que ejecutar el
   intérprete.
2. `gbrecomp juego.gb -o salida/ --trace faltantes.txt` las agrega como
   puntos de entrada.

`tests/recomp_game.sh ROM NOMBRE` hace ese ciclo completo con `difftest`.

Opciones del ejecutable generado: `--scale N`, `--model`, `--palette`,
`--record archivo` / `--play archivo` (entradas grabadas, reproducibles en
`difftest --input`), `--interp` (todo con el intérprete) y `--forget-rom`.
Controles: flechas, Z = B, X = A, Enter = Start, Retroceso = Select.

## Estado

**Pruebas de hardware** (`make test`, con las ROMs de prueba en `testroms/`):
130 de 130.
- Blargg `cpu_instrs`, `instr_timing`, `mem_timing` y `halt_bug`.
- Todas las de aceptación de Mooneye para DMG, más las de MBC1/2/5, las de
  arranque de GBC y SGB, y `dmg-acid2` y `cgb-acid2` (pantalla idéntica).
- `mbc3-tester` y `rtc3test`.
- SameSuite: las de DMA del GBC y las de multijugador del SGB.

Además, Mealybug Tearoom pasa 18 de 24 (`tests/run_mealybug.sh`) y Blargg
`dmg_sound` 9 de 12.

**Juegos probados**, sin diferencias en `difftest`, con el porcentaje de
instrucciones que corrió como código recompilado:

| Juego | Modelo | Recompilado |
|---|---|---|
| Tetris | DMG / compatibilidad GBC | 98.9 % |
| Super Mario Land | DMG / compatibilidad GBC | 93.9 % |
| Pokémon Rojo | SGB (borde y colores) | 97.5 % |
| Wario Land II | SGB | 94.7 % |
| Pokémon Oro, Plata y Amarillo | GBC | sin diferencias |

Linux y Windows están probados: compilación, batería de pruebas, proyectos
generados y `difftest`. macOS solo se compila en la integración continua.

## Límites

- **Descubrimiento:** recompilar estáticamente el 100 % de una ROM arbitraria
  es indecidible. Lo que no se descubre siempre corre bien, en el intérprete;
  solo es más lento.
- **No emulados:** el sonido y los programas de la SNES del Super Game Boy,
  ni su borde de fábrica (está en la ROM de la SNES). Tampoco las rarezas del
  canal de onda del DMG.
- **PPU:** seis casos de Mealybug con cambios de LCDC o de la ventana a mitad
  de línea.
- **Game Boy Color:** la doble velocidad está implementada pero sin un juego
  que la verifique, y faltan algunos detalles finos de la HDMA.
- La lista de pendientes está en los *issues* del repositorio.

## Aspectos legales

GBRecomp no incluye ROMs ni datos de ninguna. El código que genera es una
traducción del programa de la ROM, o sea una obra derivada del juego: sirve
para jugar tu propia copia, pero no distribuyas el proyecto ni el ejecutable
de un juego comercial.

Las paletas del modo de compatibilidad del GBC (`src/runtime/compat.c`)
vienen de la boot ROM libre de [SameBoy](https://github.com/LIJI32/SameBoy),
con licencia MIT; el aviso está en ese archivo.

## Documentación

[`docs/ARQUITECTURA.md`](docs/ARQUITECTURA.md) explica el diseño: emisor
único del SM83, temporización, despachador, heurísticas de descubrimiento,
bancos, PPU, Game Boy Color, Super Game Boy y el estado de la validación.
