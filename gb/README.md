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
make            # gbrecomp, gbtest, linktest y el runtime
make dist       # dist/gbrecomp-<versión>.tar.gz
```

`gbrecomp` es un solo ejecutable que lleva el runtime adentro, así que se
puede copiar a cualquier carpeta. En Windows, el juego generado queda
enlazado de forma estática y no necesita DLLs.

## Uso

```
gbrecomp juego.gb -o salida/ [--name nombre] [--trace archivo] [--viz] [--events archivo]
         [--coverage archivo] [--why BB:AAAA]
```

El descubrimiento estático encuentra casi todo el código. Para lo que quede
(saltos por tablas que no se pudieron resolver), se hace un ciclo con trazas:

1. Juega (o corre `difftest` con entradas aleatorias) con `--uncovered
   faltantes.txt`. Quedan anotadas las direcciones que tuvo que ejecutar el
   intérprete.
2. `gbrecomp juego.gb -o salida/ --trace faltantes.txt` las agrega como
   puntos de entrada.

`tests/recomp_game.sh ROM NOMBRE` hace ese ciclo completo con `difftest`.

Para ver qué encontró el descubrimiento y de dónde salió cada parte:

```
gbrecomp juego.gb -o salida/ --viz                  # mapa animado de la ROM
gbrecomp juego.gb -o salida/ --viz-time 0           # solo el mapa final
gbrecomp juego.gb -o salida/ --events eventos.txt   # cada paso, en texto
```

`--viz` dibuja cada banco como una cuadrícula que se va encendiendo a medida
que aparece código. El color indica de dónde vino ese código: verde para el
arranque, cian para las interrupciones, amarillo para las tablas de punteros y
magenta para la traza. `!` marca los saltos que no se pudieron resolver, que
son los candidatos a necesitar una traza. `--events` anota cada recorrido con
la instrucción que lo agregó (`recorrido 01:5C45 arranque desde 00:0101`), así
que se puede seguir hacia atrás la cadena que llevó a cualquier dirección.

Para depurar el ciclo con trazas:

```
gbrecomp juego.gb -o salida/ --viz --coverage faltantes.txt
gbrecomp juego.gb --why 01:6500
```

`--coverage` toma un archivo de `--uncovered` (lo que el juego ejecutó sin que
se hubiera descubierto), cuenta cuántas de esas direcciones siguen sin
descubrirse y, con `--viz`, las marca con `×` sobre fondo rojo hasta que algún
recorrido llega a ellas. Con `--trace` y `--coverage` sobre el mismo archivo se
ve cómo la traza las va cubriendo. `--why` explica por qué una dirección se tomó
como código: la cadena de saltos y tablas desde un punto de entrada.

```
¿Por qué 00:03A0 es código?
  00:03A0  ldh a, [$FF81]         en el recorrido 00:03A0
  00:03A0  <- entrada 00:0365 de la tabla 00:02FB (en línea)
  00:02FA  rst $28                en el recorrido 00:02F8
  00:02F8  <- salto desde 00:02C7
  ...
  00:0100  <- punto de entrada (arranque)
```

Sirve para revisar código descubierto de más, por ejemplo una tabla de
punteros que se leyó más allá de su final.

El ejecutable generado tiene una barra de menús (la de Moskva 89, dibujada
por el propio juego para verse igual en todos los sistemas; Alt o F10):

| Menú | Opciones |
|---|---|
| **Juego** | Reiniciar (`Ctrl+R`), Pausa (`P`), Guardar y cargar estado (`F5` / `F8`, cuatro ranuras), consola (Game Boy, Game Boy Color o Super Game Boy, según lo que admita el juego), cable link (2 jugadores), Salir |
| **Video** | Borde del SGB, tamaño ×1 a ×6, pantalla completa (`F11`) |
| **Paleta** | En DMG: verde, Pocket o grises. En compatibilidad GBC: la del juego o las 12 combinaciones de botones (`F2` las rota) |
| **Audio** | Sonido (`Ctrl+M`) y volumen |
| **Exportar** | Grabar música en VGM y WAV (`Ctrl+G`), juntar gráficos (`Ctrl+T`), captura de pantalla (`F12`), abrir la carpeta |
| **Idioma** | Español o English |
| **Ayuda** | Controles (`F1`), datos del juego (consola, % recompilado, carpeta de datos), acerca de |

Las preferencias se guardan en `settings.ini`, en la carpeta de datos del
juego, junto con la partida y los estados.

**Multijugador local:**
- **Cable link** (menú Juego, o `--link`): un segundo Game Boy con la misma
  ROM, conectado por cable, a la derecha de la ventana. Lo maneja el jugador
  2, con su propia partida (`<juego>_jugador2.sav`); se oye el audio del
  jugador 1. Con el cable conectado no hay estados guardados (guardarían un
  solo lado).
- **Super Game Boy:** los juegos de varios jugadores (MLT_REQ) leen los
  controles 2 a 4.
- **Controles:** el jugador 1 usa las flechas, X, Z, Enter y Shift derecho, y
  el primer control que se conecte; el jugador 2 usa WASD, G (A), F (B), E
  (Start) y Q (Select), y el segundo control; los jugadores 3 y 4, el tercer y
  cuarto control.

**Exportar** deja los archivos en `exportado/`, dentro de esa misma carpeta:
- **Música:** el VGM guarda cada escritura del juego al chip de sonido con su
  tiempo exacto, así que se oye igual y sin pérdida en foobar2000 (con
  foo_input_vgm) o vgmplay. El WAV guarda el audio ya mezclado. `vgmcheck
  música.vgm música.wav` reproduce el VGM en el APU del runtime y lo compara
  con el WAV.
- **Gráficos:** cada mosaico distinto que pasa por VRAM, en hojas PNG de 4
  tonos de gris, editables. Los colores dependen de la paleta con que el
  juego dibuja cada mosaico; para verlos están las capturas.

Solo se exporta lo que el juego llega a cargar mientras juegas.

Controles: flechas, Z = B, X = A, Enter = Start, Retroceso = Select, o un
control (A y B en la posición de Nintendo).

Opciones de línea de comandos: `--scale N`, `--model`, `--palette`,
`--record archivo` / `--play archivo` (entradas grabadas, reproducibles en
`difftest --input`), `--interp` (todo con el intérprete) y `--forget-rom`.

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

Además, `tests/batch_roms.sh` recompila una lista de juegos y corre `difftest`
60 s con entradas aleatorias en cada uno. Con 33 juegos sin diferencias:
- **Efectos de pantalla:** Link's Awakening, Super Mario Land 2 y Prehistorik
  Man.
- **Doble velocidad del GBC:** Pokémon Cristal, Zelda Oracle of Ages, Shantae,
  Alone in the Dark (portada con imagen de muchos colores) y Metal Gear Solid.
- **Muestra al azar:** 25 juegos de GB y GBC.

Ninguno tuvo diferencias. La cobertura sin traza varía entre 1 % y 99 % según
qué tan indirecto es el código del juego (ver `docs/ARQUITECTURA.md`). Con el
ciclo de trazas, los juegos revisados quedaron entre 97 % y 99.9 %.

La integración continua compila, corre la batería y recompila una ROM de
prueba en Linux, macOS y Windows. En Linux y Windows además se jugaron los
proyectos generados; en macOS todavía no.

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
