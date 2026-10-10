# Compatibilidad

Registro de pruebas de NESRecomp contra la colección No-Intro de NES (con cabecera, 732 ROMs).
El resultado completo, con filtros y una miniatura de cada juego, está en [`compat/index.html`](compat/index.html)
(datos crudos en [`compat/results.json`](compat/results.json)). Las ROMs no se versionan: el script las lee de la carpeta
de zips que le indiques y las extrae a `build/compat/roms/`.

## Resumen (10 de octubre de 2026)

| | |
|---|---|
| ROMs probadas | 732 |
| Con mapper soportado | 726. Los otros 6 usan mappers sin soporte: 105 (Nintendo World Championships 1990), 268 (4 ROMs de Retro-Bit, multicarts MMC3 de 2 MB) y 446 (multicart de 1 MB) |
| Corren en el intérprete (etapa 1) | 726 de 726 |
| Capturas casi uniformes | 11 de las 726. Nueve son pantallas de texto o menús sobre negro (Dragon Warrior I a IV, Destiny of an Emperor, Muppet Adventure, Simpsons Bart vs. the World, Flying Dragon) o se ven bien con otras entradas (Gyromite, Gun.Smoke). **Kid Dracula (mapper 23) sí es un fallo**: pantalla gris, ver Hallazgos |
| Recompiladas y comparadas contra el intérprete (etapa 2) | 66 ROMs elegidas, **66 sin diferencias** |
| Mappers cubiertos en la etapa 2 | 0, 1, 2, 3, 4, 5, 7, 9, 13, 18, 19, 23, 34, 66, 69, 75, 87, 95, 115, 118, 119, 184, 206, 210 |
| Mappers soportados sin ROM en la colección | 10, 11, 71, 79, 140, 180 (no se pudieron probar con juegos) |

Mappers agregados en esta ronda (14 ROMs que antes no cargaban): 13 CPROM, 18 Jaleco SS88006, 19 Namco 163, 23 VRC2/VRC4, 75 VRC1,
95 Namcot 3425, 115 Kasheng, 184 Sunsoft 1 y 210 Namco 175/340. Además, el cargador entiende ahora los tamaños de la cabecera NES 2.0
(Galaxian de Evercade tiene un PRG de 8 KB en notación exponente-multiplicador, que se repite hasta 16 KB).

## Cómo se probó

`tools/compat/` (se corre con [uv](https://docs.astral.sh/uv/), desde la terminal MSYS2 UCRT64 y con `make CC=gcc` ya hecho):

```
uv run tools/compat/scan.py "<carpeta de zips>" build/compat_scan.json    # cabeceras iNES -> mappers
uv run tools/compat/run.py  "<carpeta de zips>"                           # etapas 1 y 2 (--solo etapa1|etapa2, --reusar, --hilos N)
uv run tools/compat/report.py                                             # docs/compat/index.html
```

**Etapa 1 (todas las ROMs).** `nesshot` corre la ROM en el intérprete con las entradas guionadas de `tests/recomp_game.sh`
y toma capturas a los cuadros 450, 900 y 1500. Una ROM «corre» si carga y alguna captura no es casi de un solo color.
Es una comprobación gruesa: detecta mappers no soportados, errores de carga y cuelgues evidentes, pero **no** valida que el
juego se vea o suene bien (eso se mira en las miniaturas).

**Etapa 2 (selección por mapper).** Se eligen entre 3 y 8 ROMs por mapper, variando tamaño de PRG/CHR y batería. Para cada
una: traza guionada (`nestrace`), `nesrecomp`, compilar el proyecto generado, `difftest` con las semillas 1 y 2
(1500 cuadros, mando aleatorio más entradas fijas, comparando CPU, RAM, PPU, APU, mapper, imagen y audio), y una segunda
ronda recompilando con lo que `difftest --uncovered` vio caer al intérprete. Después `bench` mide la velocidad.

## Hallazgos

- **Cobertura.** En 54 de las 66 ROMs aprobadas quedan instrucciones de PRG interpretadas tras la primera ronda (en promedio
  ~530 por ROM) y la segunda ronda apenas las baja (~510). Causa, verificada en Gauntlet (MMC3) con `nesrecomp --why`: el
  recompilador asigna **una sola dirección a cada desplazamiento del PRG** (`nesrecomp.c`, cuenta los `conflictos`), y en
  mappers con ventanas intercambiables el mismo byte se ejecuta a veces en `$8003` y a veces en `$A003`. La traza trae la
  dirección con que se ejecutó (`000003:8003`), pero el código ya estaba tomado como `$A003`, así que no se puede
  recompilar y sigue cayendo al intérprete. Por eso la segunda ronda de `recomp_game.sh` aporta poco. Arreglarlo exigiría
  emitir una función por cada par (desplazamiento, dirección); no se hizo. No hubo diferencias entre el intérprete y el
  código recompilado en ninguna ROM.
  **Impacto real: despreciable.** En las ROMs aprobadas, la mediana de ciclos que cae al intérprete es 0 %; MMC1 (6 ROMs)
  y MMC3 (8) quedan en 0,0 % y 0,1 % como máximo, y el peor caso sin contar 3-D WorldRunner es 1,1 % (Gemfire, MMC5).
  Por eso no se justifica la complejidad de emitir una función por cada par (desplazamiento, dirección).
- **3-D WorldRunner** (mapper 2) corre solo el 57-62 % de los ciclos en código recompilado, con 0 instrucciones de PRG
  interpretadas. Medido con un sondeo que cuenta ciclos por página de PC (1500 cuadros): ~28 % de los ciclos se ejecutan en
  `$0000-$0FFF`, es decir, desde RAM (el juego copia rutinas ahí), que el recompilador no puede cubrir. Es un límite del
  método (código generado en tiempo de ejecución), no un fallo; los demás juegos pasan del 98 %.
- **Velocidad** del proyecto generado (MinGW, `-O2`, un solo hilo, medida sin otra carga): mediana 21× tiempo real; entre
  10,7× (juegos MMC5, MMC1 grandes) y 39× (R.C. Pro-Am). Varía de una corrida a otra (una medición anterior dio la mitad
  con la máquina ocupada). El README cita 39× para Super Mario Bros. en otra
  máquina; no son comparables.
- **Kid Dracula (mapper 23) no arranca.** Pantalla gris: el juego se queda en un bucle que interpreta una lista de comandos
  leída por un puntero a `$6943`, en la RAM del cartucho, que está vacía y que nada del juego llegó a escribir. El resto de
  los registros del mapper se comporta como esperaba (el juego escribe bancos de PRG y CHR en el esquema A2/A3 del VRC4e y las
  pruebas sintéticas pasan), así que sospecho que falta algo del VRC2/VRC4 (por ejemplo el latch de `$6000` o el IRQ del VRC4,
  que no está implementado), pero no lo comprobé. Es el único juego de los nuevos que no se ve bien.
- **Ninja JaJaMaru: The Ninja Skill Book** usa mapper 75 con 256 KB de PRG, más de lo que direcciona un VRC1 real (4 bits de banco);
  se aceptan 5 bits y con eso funciona.
- **Aproximaciones en los mappers nuevos** (documentadas en `mapper.c`): Namco 163 sin audio de ondas y sin CHR ROM como
  nametable; VRC2/4 sin el IRQ del VRC4; Jaleco SS88006 e IRQ de Namco 163 con la temporización por ciclos aproximada
  (un ciclo de más o de menos); el espejado de `$E000` del Namco 340 (mapper 210) está tomado de memoria de la documentación.
- **Limitaciones conocidas que aparecen en la lista:** mappers 105, 268 y 446 sin soporte.
- La opción de la etapa 2 de elegir ROMs por mapper no mide fidelidad de audio o video: `difftest` solo prueba que el
  recompilado y el intérprete coinciden. Que el intérprete sea correcto lo cubren las pruebas de `make test`
  (nestest, Blargg...).

## Reproducir con otra colección

`scan.py` y `run.py` aceptan cualquier carpeta de `.zip` con una ROM `.nes` cada uno. La etapa 2 tarda unos 45 minutos
con 4 hilos en una máquina de 12 núcleos.
