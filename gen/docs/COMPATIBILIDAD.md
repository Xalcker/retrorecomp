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

## Cómo se probó

`tools/compat/` (Python 3 sin dependencias, desde la raíz del repo y con `make` ya hecho):

```
python3 tools/compat/run.py [carpeta] [--oraculos] [--etapa2 N] [--hilos N] [--solo etapa1|etapa2]
python3 tools/compat/report.py                                   # docs/compat/index.html
```

La carpeta (por omisión `roms/`) puede tener `.bin`, `.md`, `.gen`, `.smd` o `.zip`, así que sirve igual para
una colección No-Intro.

**Etapa 1 (todas).** `genshot` corre la ROM en el intérprete con entradas guionadas (Start, A, B, C, derecha
e izquierda) y toma capturas a los cuadros 450, 900 y 1500. Una ROM «corre» si carga y alguna captura no es
casi de un solo color. Es una comprobación gruesa: no valida que el juego se vea o suene bien (eso se mira en
las miniaturas).

**Etapa 2.** `genrecomp` **sin traza**, compilar el proyecto generado, `difftest` con las semillas 1 y 2
(1800 cuadros, entradas fijas más un mando aleatorio, comparando tras cada función 68000, RAM, VDP, Z80,
YM2612, PSG, mandos, cartucho, imagen y audio contra el intérprete), y una segunda ronda recompilando con lo
que `difftest --uncovered` vio caer al intérprete. Después `bench` y `genbench` miden la velocidad.

**Oráculos (`--oraculos`).** La RAM del 68000 y la imagen a los cuadros 600 y 1200, con las entradas de la
etapa 1, contra Genesis Plus GX y BlastEm (`tools/oracles/`, ver `ARQUITECTURA.md`).

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

- Una colección grande (No-Intro) cuando la haya: los scripts ya la aceptan.
- PAL (312 líneas, 50 Hz): los juegos solo europeos y la demo Overdrive 2.
- La brecha de temporización fina contra los oráculos (arriba).
