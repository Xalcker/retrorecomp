# Arquitectura de SMSRecomp

SMSRecomp tomará una ROM de Sega Master System o Game Gear y generará un proyecto C99 que se compila en un
ejecutable nativo. El ejecutable cargará la ROM del usuario en tiempo de ejecución y validará su SHA1: nunca se
distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de NESRecomp, GBRecomp,
GENRecomp y NEORecomp: lo que cambia entre las consolas queda dentro de cada una. Las que todavía no tienen
contenido dicen en qué fase de [PLAN.md](PLAN.md) llegan. Hoy está hecha la fase 0: el cartucho y el Z80 común.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `sms.h` (estado y interfaz), `cart.c` (formato, cabeceras, mapper, modelo), `smsdb.c` (base de datos de juegos), `z80_host.h` (lo que el Z80 común toma de esta consola). De `tools/retrorecomp/shared`, común con GENRecomp y NEORecomp: `z80.h`/`z80.c` (el Z80); también `sha1` y `console.h`. El bus, los mappers, la E/S, el VDP y el PSG llegan en las fases 2 y 3. |
| `src/smsrecomp/` | El recompilador. Hoy, `--info`; la recompilación llega en la fase 5. |
| `src/platform/` | La ventana con SDL2, `difftest` y `bench`: fases 3 a 5. |
| `src/tools/` | `carttest`; de `tools/retrorecomp/shared`, `z80test` (SingleStepTests, con `json.h`). Las herramientas sin ventana (`smstrace`, `smsshot`, `smsaudio`, `smsramdump`, `smsbench`), `smsrun` y las demás pruebas llegan en las fases 2 a 4. |
| `tests/` | `run_tests.sh` (`make test`), `testroms.sh` (`make testroms`), `sstz80.sh` (las SingleStepTests del Z80), y los envoltorios comunes `contract.sh` y `recomp_game.sh` (el mismo archivo en los cinco repos; con efecto desde las fases 4 y 5). |
| `tools/` | `retrorecomp/` (la GUI común y `shared/`, submódulo). `compat/` y `oracles/` llegan en las fases 4 y 7. |

## Una sola fuente de verdad para el CPU

Llega en la fase 1. Hoy el Z80 es el intérprete común de GENRecomp y NEORecomp tal cual (`shared/z80.c`): pasa las
1 604 000 [SingleStepTests del Z80](https://github.com/SingleStepTests/z80) (`z80test`, `tests/sstz80.sh`, en `make
test`) y las pruebas propias de interrupciones (`z80test --int`: la INT desde un `HALT` en los modos 1 y 2;
`--nmi`: la NMI, que aquí es la Pausa). En la fase 1 su semántica pasa a `shared/z80_ops.h`, la usan el intérprete
y el código generado, y `z80test --recomp` corre las mismas pruebas por los dos caminos (ver PLAN.md).

## Temporización

Llega en la fase 2. El reloj es el del Genesis: maestro de 53,693175 MHz, el Z80 a 1/15 (3,579545 MHz), líneas de
342 pixeles de 10 ciclos maestros (3420, 228 del Z80) y 262 líneas: 59 736 ciclos del Z80 por cuadro, 59,92
cuadros por segundo. Todo se contará en ciclos maestros.

## Interrupciones y despachador

Llega en las fases 2 (interrupciones: IM 1 por cuadro y por línea, la NMI de la Pausa) y 5 (despachador del
código recompilado).

## Recompilador

Llega en la fase 5. Hoy `smsrecomp` lee el cartucho y lo describe:

```
smsrecomp juego.sms [--info] [--model sms|gg]
```

### Identidad de las instrucciones

Llega en la fase 5: página de 16 KB × `$4000` + (dirección & `$3FFF`), y el banco de la traza `BANCO:DIRECCION`
es la página (ver PLAN.md, «Identidad del código»).

### Descubrimiento

Llega en la fase 5.

### Herramientas de análisis

Llegan en la fase 5 (`--coverage`, `--why`, `--viz`, `--html`, `--events`, `--codemap`, `--dis`).

### Funciones y emisión

Llega en la fase 5.

### Código que el juego copia a RAM

Llega en la fase 5, con `shared/ram_code.h`.

### El proyecto generado

Llega en la fase 5.

### difftest

Llega en la fase 5.

### Rendimiento

Llega en la fase 5.

## Bancos, mappers y guardado

### Mapa de memoria del Z80

Llega en la fase 2: ROM en las ranuras de `$0000`, `$4000` y `$8000` (el primer KB siempre la página 0 con el
mapper Sega), RAM de 8 KB en `$C000` con su espejo en `$E000`, y los registros del mapper en `$FFFC-$FFFF`.

### Mappers

`cart_detect_mapper` (`cart.c`) elige el mapper al cargar: el de la base de datos (`smsdb.c`, por el SHA1 de la
imagen; empieza vacía y solo lleva juegos comprobados con un archivo real); si no, **Codemasters** si está su
cabecera en `$7FE0` (la suma y su inversa suman `$10000`); si no, **sin mapper** hasta 48 KB (las tres ranuras
fijas) y **Sega** (315-5235) desde ahí. Una cabecera SDSC en el mismo lugar no se confunde con la de Codemasters.
Cómo cambian las ranuras (Sega: `$FFFC-$FFFF`; Codemasters: escrituras en `$0000`, `$4000` y `$8000`) llega en la
fase 2.

### Cartucho

`sms_load` acepta `.sms` y `.gg` (la imagen tal como la ve el Z80) y quita la cabecera de copiador de 512 bytes
si el tamaño es 16 KB × n + 512; el **SHA1 se calcula sobre la imagen sin ella** (y también el CRC32, el de las
listas de No-Intro). La consola no exige ninguna marca, así que se acepta cualquier archivo de 32 bytes a 4 MB.

`cart_parse_headers` lee las tres cabeceras (SMS Power!):

- **TMR SEGA**, buscada en `$7FF0`, `$3FF0` y `$1FF0`, en ese orden, como la BIOS: suma (little-endian en `+$A`),
  código de producto (dos bytes BCD con una cifra más en el nibble alto de `+$E`; las cifras se guardan tal cual
  porque hay homebrew con bytes que no son BCD), versión, región y código de tamaño. La suma se recalcula como la
  BIOS del SMS de exportación: bytes en 16 bits de `$0000-$xFEF` (lo de antes de la cabecera) y, si el tamaño
  declarado pasa de 32 KB, de `$8000` a su final; el código `$D` (48 KB, que la BIOS suma mal) cubre `$0000-$BFEF`;
  si la ROM no llega al tamaño declarado, la suma no se puede comprobar.
- **Codemasters** en `$7FE0`: páginas cubiertas, fecha y hora en BCD, la suma (palabras little-endian de esas
  páginas, sin `$7FF0-$7FFF`) y su inversa.
- **SDSC** en `$7FE0` (o 16 bytes antes de la TMR SEGA si está en `$3FF0` o `$1FF0`): versión, fecha y los textos
  del nombre y del autor.

Las sumas que calcula coinciden con las que declaran todas las ROMs de `make testroms` (ZEXALL, ZEXDOC, el
homebrew) y las pruebas de FluBBa.

### Guardado

Llega en la fase 2 (la RAM del cartucho) y en la 3 (los estados guardados, que validarán el tamaño de `Sms`).

## Video y audio

### VDP

Llega en las fases 2 (registros, contadores, interrupciones) y 3 (el dibujo del modo 4 y de los modos TMS9918, y
el del Game Gear).

### Z80

El núcleo común de `tools/retrorecomp/shared` (ver «Una sola fuente de verdad para el CPU»). Aquí es el CPU
principal; su bus (memoria y puertos) llega en la fase 2.

### Audio

Llega en la fase 3: el SN76489 de `psg.c` de GENRecomp, que pasa a `shared/` con el estéreo del Game Gear, y la
mezcla común (`audio.c`).

## Variantes de la consola y accesorios

`--model sms` es el Master System II (VDP 315-5246) y `--model gg` el Game Gear; la razón de no emular el SMS1 por
ahora está en PLAN.md («Modelos»). `sms_load` elige el modelo: el que se pide; si no, el de la base de datos; si
no, la extensión (`.gg` → gg, `.sms` → sms); si no, la región de la cabecera (5, 6 y 7 son del Game Gear). En el
Game Gear, un cartucho de SMS corre en su modo SMS: el que dice la base de datos, el de región 3 o 4 o, sin
cabecera, el que no es `.gg`. `carttest` prueba cada combinación y `smsrecomp --info` dice con qué modelo y en qué
modo correría el juego.

Lo que hace cada modelo (la ventana de 160×144 y la CRAM de 12 bits del GG, su puerto `$00` y su estéreo, la Pausa
del SMS) llega en las fases 2 y 3; los accesorios (Light Phaser, gafas 3-D, cable de enlace), en la 8.

## Interfaz del ejecutable

Llega en la fase 3: la ventana `sms` con la barra de menús y los diálogos comunes (`ui_menu`, `ui_font`, `player_*.h`
de `tools/retrorecomp/shared`), en español e inglés.

## Empaquetado y pruebas

- **`make dist`:** deja `dist/smsrecomp-<versión>-<sistema>.tar.gz` con `smsrecomp`, el README, la licencia y este
  documento. La versión sale de `git describe`.
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en los otros cuatro), en Linux, macOS y
  Windows: `make`, `make testroms`, `make test` y `make dist`. El contrato común (`sh tests/contract.sh`) entra en
  la fase 4 y `make webtest`/`make apktest` en la 6 (hoy solo avisan en qué fase llegan).
- **`make testroms`** (`tests/testroms.sh`): las pruebas libres en versiones fijas, en `testroms/` (no se
  versionan): las SingleStepTests del Z80 (el mismo commit que GENRecomp y NEORecomp; `SKIP_SST=1` las salta, como
  el CI de macOS y Windows), ZEXALL y ZEXDOC para el SMS (GPL-2.0, v0.21) y juegos homebrew con licencia MIT (Dragon
  Blaster, Food Fighter, River Strike y Waternet para SMS y Game Gear), cada uno con el SHA-256 de lo que se baja.
  Con `EXTRA=1`, también las pruebas de hardware de SMS Power! que no tienen licencia publicada (el VDPTEST de
  FluBBa, las de Charles MacDonald, el SMS Test Suite de sverx...), a `testroms/extra/`: para usarlas en tu
  máquina, nunca en el CI ni en el repositorio.
- **`make test`:** `carttest`, `z80test --int` y `--nmi`, las SingleStepTests del Z80, `smsrecomp` con un archivo
  que no es ROM, sin argumentos y con un modelo desconocido, `smsrecomp --info` con las ROMs de prueba (SHA1, suma,
  mapper y modelo) y con cada ROM de `roms/`.

## Estado de la validación

`make test` (`tests/run_tests.sh`), en Linux, macOS y Windows en cada PR:

- **CPU:** las 1 604 000 SingleStepTests del Z80 (`z80test`, en Linux) y las interrupciones (`--int`, `--nmi`).
- **Cartucho:** `carttest` (cabeceras TMR SEGA en sus tres lugares, Codemasters y SDSC, la suma de cada código de
  tamaño, la cabecera de copiador, el mapper, el modelo y el modo, los archivos que no pueden ser una ROM, el
  CRC32) y `smsrecomp --info` con las ROMs de prueba.

**Todavía no:** todo lo demás (ver [PLAN.md](PLAN.md)).

### Línea base para optimizar el runtime

Llega en la fase 4 (`tests/golden.sh`: hash de todos los cuadros y de todo el audio de cada ROM).
