# Compatibilidad

Registro de pruebas de GENRecomp con los 23 juegos de `roms/` (Sega Genesis, NTSC/EE. UU.). El resultado
completo, con una miniatura de cada juego, está en [`compat/index.html`](compat/index.html) (datos crudos
en [`compat/results.json`](compat/results.json)). Las ROMs no se versionan.

## Resumen (11 de octubre de 2026)

| | |
|---|---|
| ROMs probadas | 23 (de 512 KB a 5 MB; con SRAM, EEPROM serie y el mapper de SSF2) |
| Corren en el intérprete (etapa 1) | 23 de 23 |
| Recompiladas sin traza y comparadas contra el intérprete (etapa 2) | **23 de 23 sin diferencias**, en las dos rondas |
| Ciclos en código recompilado (peor semilla) | 99,1 % de media; 100 % en 12 juegos; lo más bajo, Gunstar Heroes (92,3 %), Vectorman (93,9 %) y Contra Hard Corps (95,7 %): código que corre desde la RAM |
| Instrucciones de ROM que caen al intérprete tras la segunda ronda | 0 en los 23 |
| Velocidad recompilada (`bench`, 1800 cuadros) | 23,6x a 45,9x el tiempo real, 30x de media (el intérprete: 16x a 33x) |
| Compilar el proyecto generado (`make -j8`) | 16 a 130 s (Sonic & Knuckles, el más grande) |
| Parecido con los oráculos (cuadros 600 y 1200, entradas guionadas) | Genesis Plus GX: RAM 99,24 %, imagen 90,0 %; BlastEm: RAM 99,18 %, imagen 89,4 %. Ellos entre sí: 99,81 % y 97,0 % |

## Colección No-Intro (12 de octubre de 2026)

La colección No-Intro de Mega Drive / Genesis (728 ROMs, casi todas de EE. UU. o mundiales), con el informe
completo en [`compat-nointro/index.html`](compat-nointro/index.html) (datos en
[`compat-nointro/results.json`](compat-nointro/results.json)). Se corrió con
`python3 tools/compat/run.py "<carpeta No-Intro>" --etapa2 250 --salida docs/compat-nointro`.

| | |
|---|---|
| Corren en el intérprete (etapa 1) | 704 de 728; 22 quedan en blanco y en 2 el CPU se detiene (doble falta) |
| Recompiladas sin traza y comparadas contra el intérprete (etapa 2) | **250 de 250 sin diferencias** (elegidas por turnos entre tamaños, SRAM, EEPROM y SSF2); 146 necesitaron la segunda ronda |
| Ciclos en código recompilado (peor semilla) | 98,4 % de media; 100 % en 163 juegos y 99 % o más en 208. Lo más bajo: Slaughter Sport (9,4 %), F-117 Night Storm (41,4 %), Super Fantasy Zone (75,3 %) y OutRun (78,4 %) |
| Velocidad recompilada (`difftest --bench`, 1800 cuadros) | 23,4x el tiempo real de mediana (13,8x a 45,4x); el intérprete, 17,4x |
| Compilar el proyecto generado | 174 s de mediana; hasta 19 minutos |

Las 4 que la primera noche fallaron al compilar (Duke Nukem 3D, Eliminate Down, P.T.O. y Sonic & Knuckles de Sega
Channel) compilan bien solas: gcc se quedó sin memoria con dos proyectos grandes a la vez (un solo archivo de
P.T.O. llega a 930 MB). La prueba ahora reintenta de a un archivo.

Las 24 de la etapa 1 que no corren, contra Genesis Plus GX y BlastEm con el mismo guion (cuadros 450, 900 y 1500):

- **Fallas nuestras (17, issue #17)**: los dos oráculos muestran imagen y nosotros no. Al Michaels Announces HardBall III,
  College Slam, Evander Holyfield's 'Real Deal' Boxing, Fatal Rewind (doble falta), Frank Thomas Big Hurt
  Baseball, Greatest Heavyweights, Lemmings 2, Madden NFL 98, NFL Quarterback Club, NFL Quarterback Club 96,
  Sesame Street Counting Cafe, Shadow of the Beast, Star Odyssey (doble falta en el cuadro 3), Time Killers,
  Triple Play - Gold Edition, Triple Play 96 y VS Puyo Puyo Sun. Hay grupos con el mismo motor: los de Acclaim
  de 4 MB (College Slam, Frank Thomas, los dos NFL Quarterback Club) quedan con la pantalla partida en dos
  colores, y los dos Triple Play igual.
- **Como Genesis Plus GX, no como BlastEm (4)**: Outback Joey, Outworld 2375 A.D., Taz-Mania y Xenon 2 (Evercade)
  quedan en blanco en Genesis Plus GX y en GENRecomp, y BlastEm sí los muestra.
- **Igual que los dos oráculos (3)**: Kid Chameleon (Mega Drive PlayTV), Street Smart y The Terminator también
  quedan en blanco en ellos con estas entradas; no son fallas del emulador.

## Cómo se probó

`tools/compat/` (Python 3 sin dependencias, desde la raíz del repo y con `make` ya hecho). El motor es el mismo
de NESRecomp y GBRecomp, `tools/retrorecomp/compat.py`; `run.py` solo dice lo propio del Genesis (cabecera con
`genrecomp --info`, categorías de cartucho, cupo) y los oráculos:

```
python3 tools/compat/run.py [carpeta ...] [--oraculos]
python3 tools/compat/report.py                                   # docs/compat/index.html
```

La carpeta (por omisión `roms/`) puede tener `.bin`, `.md`, `.gen`, `.smd` o `.zip`, así que sirve igual para
una colección No-Intro. Opciones: `--solo etapa1|etapa2`, `--etapa2 N` (cuántas ROMs pasan a la etapa 2, por
turnos entre categorías: hasta 512 KB, 1 MB, 2 MB, 3-4 MB, SRAM, EEPROM, SSF2), `--lista ARCHIVO` (la etapa 2 con
esas ROMs), `--juego TEXTO`, `--categoria`, `--hilos N`, `--salida DIR` y `--rehacer`. `results.json` se guarda
después de cada ROM: si la corrida se corta, la misma orden sigue donde iba (salta las ROMs que ya tienen
resultado del mismo commit).

**Etapa 1 (todas).** `genshot` corre la ROM en el intérprete con entradas guionadas (Start, A, B, C, derecha
e izquierda) y toma capturas a los cuadros 450, 900 y 1500. Una ROM «corre» si carga y alguna captura no es
casi de un solo color. Es una comprobación gruesa: no valida que el juego se vea o suene bien (eso se mira en
las miniaturas).

**Etapa 2.** `genrecomp` **sin traza**, compilar el proyecto generado, `difftest` con las semillas 1 y 2
(1800 cuadros, entradas fijas más un mando aleatorio, comparando tras cada función 68000, RAM, VDP, Z80,
YM2612, PSG, mandos, cartucho, imagen y audio contra el intérprete), y una segunda ronda recompilando con lo
que `difftest --uncovered` vio caer al intérprete. Al final, de una ROM a la vez, `difftest --bench` mide la
velocidad del recompilado y del intérprete (los números de arriba son de `bench` y `genbench`, con la versión
anterior de la prueba).

**Oráculos (`--oraculos`).** La RAM del 68000 y la imagen a los cuadros 600 y 1200, con las entradas de la
etapa 1, contra Genesis Plus GX y BlastEm (`tools/oracles/`, ver `ARQUITECTURA.md`), en las ROMs de la etapa 2.

## Hallazgos

- **Recompilador.** Ninguna diferencia entre el código recompilado y el intérprete en ningún juego, ni en la
  primera ronda (sin traza). El análisis estático encuentra casi todo (fase 7): después de la segunda ronda no
  queda ninguna instrucción de ROM en el intérprete; lo que no corre recompilado es código copiado a la RAM
  (Gunstar Heroes, Vectorman, Contra Hard Corps, Comix Zone), que el método no puede cubrir.
- **Temporización contra los oráculos.** Los oráculos mostraron que el 68000 corría algo más rápido que en la
  consola (Sonic 1 terminaba su suma de verificación del arranque unos cuadros antes). Se agregaron la FIFO
  del VDP y el refresco de la DRAM, que acercaron el resultado a los dos emuladores (ver «Temporización» en
  `ARQUITECTURA.md`). Queda una brecha: en Sonic 1 y 2, Road Rash II, Wonder Boy in Monster World y Contra
  Hard Corps los dos oráculos coinciden entre sí en el cuadro 1200 y nosotros no. Investigado en Wonder Boy:
  al arrancar terminamos de cargar el título alrededor de un cuadro antes, una pulsación de Start durante el
  logo de SEGA cae en otro momento y desde ahí el juego sigue otro camino (la RAM hasta ese punto es
  igual salvo contadores corridos). No es una falla del juego: con las mismas entradas en otro momento, los
  tres emuladores reaccionan igual. Igualarlo exige afinar más la temporización (el orden de los accesos del
  68000 dentro de cada instrucción, la posición exacta de VINT/HINT en la línea, las esperas del bus del Z80).
- **Sin miniaturas en blanco ni errores de carga** en los 23.

## Pendiente

- Las 17 ROMs de la colección No-Intro que no corren en el intérprete y sí en los dos oráculos, y las 4 que
  BlastEm muestra y Genesis Plus GX no (arriba).
- PAL (312 líneas, 50 Hz): los juegos solo europeos y la demo Overdrive 2.
- La brecha de temporización fina contra los oráculos (arriba).
