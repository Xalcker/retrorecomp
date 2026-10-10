# Compatibilidad

Registro de pruebas de NESRecomp contra la colección No-Intro de NES (con cabecera, 732 ROMs).
El resultado completo, con filtros y una miniatura de cada juego, está en [`compat/index.html`](compat/index.html)
(datos crudos en [`compat/results.json`](compat/results.json)). Las ROMs no se versionan: el script las lee de la carpeta
de zips que le indiques y las extrae a `build/compat/roms/`.

## Resumen (9 de octubre de 2026)

| | |
|---|---|
| ROMs probadas | 732 |
| Con mapper soportado | 712 (los otros 20 usan los mappers 13, 18, 19, 23, 75, 95, 105, 115, 184, 210, 268 y 446) |
| Corren en el intérprete (etapa 1) | 711 de 712. La excepción es Galaxian (Evercade), de PRG que no es potencia de 2 |
| Capturas casi uniformes | 10 de las 711, revisadas a ojo: 8 son pantallas de texto o menús sobre negro (Dragon Warrior I a IV, Destiny of an Emperor, Muppet Adventure, Simpsons Bart vs. the World, Flying Dragon). Gyromite y Gun.Smoke se ven bien con otras entradas (menú y título); el negro viene de las entradas guionadas. Ninguna es un fallo conocido |
| Recompiladas y comparadas contra el intérprete (etapa 2) | 52 ROMs elegidas, **51 sin diferencias**; la que falla es Galaxian por el mismo motivo |
| Mappers cubiertos en la etapa 2 | 0, 1, 2, 3, 4, 5, 7, 9, 34, 66, 69, 87, 118, 119, 206 |
| Mappers soportados sin ROM en la colección | 10, 11, 71, 79, 140, 180 (no se pudieron probar con juegos) |

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

- **Cobertura.** En 40 de las 51 ROMs aprobadas quedan instrucciones de PRG interpretadas tras la primera ronda (en promedio
  ~510 por ROM) y la segunda ronda apenas las baja (~480). Causa, verificada en Gauntlet (MMC3) con `nesrecomp --why`: el
  recompilador asigna **una sola dirección a cada desplazamiento del PRG** (`nesrecomp.c`, cuenta los `conflictos`), y en
  mappers con ventanas intercambiables el mismo byte se ejecuta a veces en `$8003` y a veces en `$A003`. La traza trae la
  dirección con que se ejecutó (`000003:8003`), pero el código ya estaba tomado como `$A003`, así que no se puede
  recompilar y sigue cayendo al intérprete. Por eso la segunda ronda de `recomp_game.sh` aporta poco. Arreglarlo exigiría
  emitir una función por cada par (desplazamiento, dirección); no se hizo. No hubo diferencias entre el intérprete y el
  código recompilado en ninguna ROM.
- **3-D WorldRunner** (mapper 2) corre solo el 57-62 % de los ciclos en código recompilado, con 0 instrucciones de PRG
  interpretadas, así que el resto probablemente es código que corre desde RAM (no se verificó). Es el caso más bajo; todos los demás pasan del 98 %.
- **Velocidad** del proyecto generado (MinGW, `-O2`, un solo hilo, medida sin otra carga): mediana 11,7× tiempo real; entre
  4,8× (MMC5: Empereur, Gemfire, Bandit Kings) y 31× (R.C. Pro-Am). El README cita 39× para Super Mario Bros. en otra
  máquina; no son comparables.
- **Limitaciones conocidas que aparecen en la lista:** PRG que no es potencia de 2 (Galaxian Evercade) y los 20 mappers
  sin soporte.
- La opción de la etapa 2 de elegir ROMs por mapper no mide fidelidad de audio o video: `difftest` solo prueba que el
  recompilado y el intérprete coinciden. Que el intérprete sea correcto lo cubren las pruebas de `make test`
  (nestest, Blargg...).

## Reproducir con otra colección

`scan.py` y `run.py` aceptan cualquier carpeta de `.zip` con una ROM `.nes` cada uno. La etapa 2 tarda unos 45 minutos
con 4 hilos en una máquina de 12 núcleos.
