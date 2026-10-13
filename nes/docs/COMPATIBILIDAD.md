# Compatibilidad

Registro de pruebas de NESRecomp contra la colección No-Intro de NES (con cabecera, 732 ROMs).
El resultado completo, con filtros y una miniatura de cada juego, está en [`compat/index.html`](compat/index.html)
(datos crudos en [`compat/results.json`](compat/results.json)). Las ROMs no se versionan: el script las lee de la carpeta
de zips que le indiques y las extrae a `build/compat/roms/`.

## Resumen (11 de octubre de 2026)

| | |
|---|---|
| ROMs probadas | 732 |
| Con mapper soportado | 726. Los otros 6 usan mappers sin soporte: 105 (Nintendo World Championships 1990), 268 (4 ROMs de Retro-Bit, multicarts MMC3 de 2 MB) y 446 (multicart de 1 MB) |
| Corren en el intérprete (etapa 1) | 726 de 726 |
| Capturas casi uniformes | 11 de las 726: pantallas de texto o menús sobre negro (Dragon Warrior I a IV, Destiny of an Emperor, Muppet Adventure, Simpsons Bart vs. the World, Flying Dragon), que se ven bien con otras entradas (Gyromite, Gun.Smoke) y Kid Dracula, que con la RAM con batería en ceros no pasa del título (igual que en Mesen2; ver abajo) |
| Recompiladas y comparadas contra el intérprete (etapa 2) | 66 ROMs elegidas, **66 sin diferencias** |
| Mappers cubiertos en la etapa 2 | 0, 1, 2, 3, 4, 5, 7, 9, 13, 18, 19, 23, 34, 66, 69, 75, 87, 95, 115, 118, 119, 184, 206, 210 |
| Mappers soportados sin ROM en la colección | 10, 11, 21, 22, 24, 25, 26, 71, 79, 85, 140, 180. Los VRC (21 a 26 y 85) se probaron con ROMs japonesas sueltas y con ROMs sintéticas contra Mesen2 |
| Velocidad del proyecto generado | mediana 22× tiempo real (entre 11× y 40×), igual que el 10 de octubre (21×) pese a todo lo agregado al runtime desde entonces (DMA, audio de expansión, IRQ del VRC4) |

Cambios desde el 10 de octubre: las mismas 715 capturas «ok» y los mismos 66 juegos sin diferencias en la etapa 2.
Unas 100 miniaturas cambian por detalles de tiempo (DMA, `$4017`, `$4016`) que sacan a las partidas de fase en
algún momento de los 900 cuadros. StarTropics y Zoda's Revenge vuelven a funcionar (RAM con batería en ceros).
`run.py` mide ahora la velocidad al final, de una ROM a la vez: medida en paralelo con las compilaciones daba un
tercio (7× de mediana).

Mappers agregados en la ronda del 10 de octubre (14 ROMs que antes no cargaban): 13 CPROM, 18 Jaleco SS88006, 19 Namco 163, 23 VRC2/VRC4, 75 VRC1,
95 Namcot 3425, 115 Kasheng, 184 Sunsoft 1 y 210 Namco 175/340. Además, el cargador entiende ahora los tamaños de la cabecera NES 2.0
(Galaxian de Evercade tiene un PRG de 8 KB en notación exponente-multiplicador, que se repite hasta 16 KB).

## Cómo se probó

`tools/compat/` (Python 3 sin dependencias, desde la raíz del repo y con `make` ya hecho; en Windows el proyecto generado
se compila solo dentro de MSYS2 UCRT64). El motor es el mismo de GBRecomp y GENRecomp, `tools/retrorecomp/compat.py`;
`run.py` solo dice lo propio de la NES (cabecera iNES, mappers soportados, cupo por mapper):

```
python3 tools/compat/run.py "<carpeta de zips>"    # etapas 1 y 2
python3 tools/compat/report.py                     # docs/compat/index.html
```

Opciones: `--solo etapa1|etapa2`, `--etapa2 N` (cuántas ROMs pasan a la etapa 2, por turnos entre mappers; sin esto, el
cupo de cada mapper), `--lista ARCHIVO` (la etapa 2 con esas ROMs), `--juego TEXTO`, `--categoria 13,18` (mappers),
`--hilos N`, `--salida DIR` y `--rehacer`. `results.json` se guarda después de cada ROM: si la corrida se corta, la
misma orden sigue donde iba (salta las ROMs que ya tienen resultado del mismo commit).

Los resultados de abajo son de la versión anterior de la prueba, que en la etapa 2 empezaba con una traza guionada
(`nestrace`); ahora la primera ronda es sin traza, como en los otros dos recompiladores.

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
- **RAM con batería sin estrenar: en ceros, como Mesen2.** Durante un tiempo se inició en `$FF` para que arrancara **Kid Dracula**
  (versión de Castlevania Anniversary Collection, mapper 23), que con ceros interpreta para siempre una lista de comandos guardada ahí.
  La comparación contra Mesen2 (Fase 4) mostró el costo: con `$FF`, **StarTropics** y **Zoda's Revenge** pasan del título a una
  pantalla negra y no salen de ella. Con ceros los dos quedan idénticos a Mesen2. De los 60 juegos con batería, además de esos tres,
  solo cambian por unos pocos píxeles Pirates!, Heroes of the Lance y Kirby's Adventure. Kid Dracula tampoco pasa del título en
  Mesen2: esa versión espera la RAM que le prepara el emulador de la colección.
- **Ninja JaJaMaru: The Ninja Skill Book** usa mapper 75 con 256 KB de PRG, más de lo que direcciona un VRC1 real (4 bits de banco);
  se aceptan 5 bits y con eso funciona.
- **Aproximaciones en los mappers nuevos** (documentadas en `mapper.c`): Namco 163 sin CHR ROM como nametable; Jaleco SS88006 e IRQ de Namco 163 con la temporización por ciclos aproximada
  (un ciclo de más o de menos); el espejado de `$E000` del Namco 340 (mapper 210) está tomado de memoria de la documentación.
- **Limitaciones conocidas que aparecen en la lista:** mappers 105, 268 y 446 sin soporte.
- La opción de la etapa 2 de elegir ROMs por mapper no mide fidelidad de audio o video: `difftest` solo prueba que el
  recompilado y el intérprete coinciden. Que el intérprete sea correcto lo cubren las pruebas de `make test`
  (nestest, Blargg...) y, para la imagen, la comparación contra Mesen2 de la sección siguiente.

## Imagen contra Mesen2 (Fase 4, 11 de octubre de 2026)

`tools/compat/vsmesen.py` corre cada juego en `nesshot` y en [Mesen2](https://github.com/SourMesen/Mesen2) 2.1.1 sin ventana
(oráculo de referencia, que no va en el repo: `tools/oracles/mesen.py` lo descarga con su SHA-256) con las mismas entradas
guionadas, y compara las capturas píxel por píxel. Conjunto: 83 juegos, hasta 3 por mapper de la colección más 33 ROMs sueltas
(incluidas japonesas con VRC2/VRC4/VRC7 y Namco 163).

```
uv run tools/compat/vsmesen.py <carpeta o ROMs> [--sin-botones] [--cuadros 450,900,1500,2100]
```

| | Iguales al píxel en todos los cuadros | Diferencias pequeñas (< 1 %) | Diferencias grandes |
|---|---|---|---|
| Sin botones (título y demo, cuadros 450 y 900) | **76 de 83** | 4 | 3 |
| Con botones (cuadros 450, 900, 1500 y 2100) | **64 de 83** | 10 | 9 |

**Alinear los dos emuladores** costó más que la comparación. Lo que hay que saber para que la comparación tenga sentido:
- El cuadro N de Mesen2 (su `endFrame`, en la línea 240) es nuestro `--frames N - 1`. Se verificó registrando cada lectura de
  `$2002` en los dos: coinciden ciclo a ciclo.
- Mesen2 cambia los botones una vez por cuadro, en la línea 241 (`inputPolled`), y `nesshot` al empezar el cuadro. Con eso, un
  juego que lee el control durante el render (Contra, en la línea 0) y otro que lo lee en el vblank (TMNT) necesitaban
  corrimientos distintos. `nesshot --entradas-241` cambia los botones en la línea 241 como Mesen2, y con eso los dos casos coinciden.
- Los volcados de RAM al final del cuadro no son comparables (nosotros cortamos en la línea 0, Mesen2 en la 240). Para acotar
  una divergencia hay que volcar los dos en el mismo ciclo de CPU.

**Errores encontrados y corregidos:**
- **Énfasis de color** (`$2001` bits 5-7): cada bit atenuaba los otros canales al 75 %; Mesen2 usa 84 % (combinado y truncado).
  720 Degrees, que activa los tres bits, se veía un 20 % más oscuro; ahora es idéntico.
- **RAM con batería en `$FF`**: dejaba en negro StarTropics y Zoda's Revenge (ver Hallazgos). Vuelve a ceros.

**Lo que queda distinto:**
- **Mappy Kids** (Namco 163): el mapa sale revuelto. Usa la CHR ROM como nametable, que nuestro Namco 163 no implementa.
- **Kid Dracula** (Anniversary Collection): trabado en el título en los dos emuladores, con distinta pantalla.
- **Partidas que toman otro rumbo** a partir de algún cuadro con botones (Arkanoid, NES Play Action Football, Nintendo World
  Cup, Pro Sport Hockey, Ninja JaJaMaru-kun): todo idéntico hasta un punto y desde ahí la partida diverge. Puede ser una
  diferencia de temporización real o un número aleatorio que depende de algo que no emulamos igual; falta acotar cada una.
- **Transitorios de uno o pocos cuadros** (Crystalis en un fundido, Pizza Pop! con un color de la paleta, Rolling Thunder) y
  **diferencias de unos pocos píxeles** (Ninja Gaiden 2 px sin botones, Kirby, Metroid, Rad Racer, 1942).

## Reproducir con otra colección

`run.py` acepta cualquier carpeta de `.zip` con una ROM `.nes` cada uno (o de `.nes` sueltas). La etapa 2 tarda unos 45 minutos
con 4 hilos en una máquina de 12 núcleos.
