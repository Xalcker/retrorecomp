# Plan de SMSRecomp

Recompilador estático de juegos de Sega Master System y Game Gear de EE. UU. (NTSC, 59,92 Hz), el quinto de la
serie, con la misma estructura que [NESRecomp](https://github.com/Xalcker/NESRecomp),
[GBRecomp](https://github.com/Xalcker/GBRecomp), [GENRecomp](https://github.com/Xalcker/GENRecomp) y
[NEORecomp](https://github.com/Xalcker/NEORecomp): runtime con intérprete de respaldo, proyecto C99 generado sin
datos de la ROM (valida su SHA1), `difftest` contra el intérprete y el mismo contrato de línea de comandos
([retrorecomp](https://github.com/Xalcker/retrorecomp)). Herramientas: `smsrecomp`, la ventana `sms`
(intérprete), `smstrace`, `smsshot`, `smsaudio`, `smsbench`, `smsramdump` y `smsrun` (diagnóstico); pruebas
`z80test` (común), `carttest`, `bustest`, `vdptest`, `audiotest`, `statetest` e `inputtest`.

Un solo repositorio para las dos consolas, como GBRecomp lleva DMG, GBC y SGB: el Game Gear es un Master System
por dentro (el mismo Z80, el VDP del SMS2 con la CRAM de 12 bits y una ventana de 160×144, el mismo SN76489 más
el estéreo, el mismo mapa de memoria, los mismos mappers y la misma cabecera), y corre los juegos de SMS en su modo
SMS. Dos repos duplicarían más del 90 % del runtime.

**Estado: fases 0 a 2 hechas.** Cada fase empieza con la aprobación del autor.

## Qué cambia respecto a GENRecomp y GBRecomp

GENRecomp es el más cercano en hardware (Sega, el mismo reloj maestro, el mismo Z80 común, el mismo SN76489);
GBRecomp, como recompilador (un CPU de 8 bits parecido al Z80, bancos de 16 KB, varios modelos en un repo, `rst`
con tablas de saltos, código en RAM).

| | GENRecomp | GBRecomp | SMSRecomp | Consecuencia |
|---|---|---|---|---|
| CPU principal | 68000; el Z80 solo corre el sonido, siempre interpretado | SM83 (un Z80 recortado), con `sm83_emit` como fuente única | **Z80** a 3,579545 MHz (maestro 53,693175 MHz / 15) | El Z80 común (`shared/z80.c`) se parte en una fuente única (`shared/z80_ops.h`) para que el código generado y el intérprete no puedan discrepar (fase 1). |
| Cuadro | 262 líneas × 3420 ciclos maestros | 154 líneas × 456 puntos | 262 líneas × 3420 ciclos maestros = 228 ciclos del Z80 por línea, 59 736 por cuadro | El reloj del Genesis: todo se cuenta en ciclos maestros, como en GENRecomp. |
| Bancos | ROM lineal; el mapper de SSF2 (512 KB) | MBC: banco 0 fijo y uno de 16 KB en `$4000` | Tres ranuras de 16 KB (`$0000`, `$4000`, `$8000`): mapper Sega (`$FFFC-$FFFF`, el primer KB siempre la página 0), Codemasters (`$0000`/`$4000`/`$8000`) o ninguno | Identidad por página de 16 KB, como GBRecomp; una página puede correr en cualquier ranura (ver «Identidad del código»). `MAPCHK` como en NESRecomp. |
| Vectores | 64 en `$000000` | `$0100` y los de interrupción | `$0000` (reset), los `RST` (`$00-$38`), `$0038` (IM 1) y `$0066` (NMI, la Pausa) | Pocas semillas: el descubrimiento depende de las tablas de saltos, como en GBRecomp. |
| E/S | Por memoria (`$A10000`, `$C00000`) | Por memoria (`$FF00`) | **Por puertos** (`IN`/`OUT`): `$3E`/`$3F` control, `$7E`/`$7F` contadores V y H y el PSG, `$BE`/`$BF` el VDP, `$DC`/`$DD` los mandos; en el GG además `$00-$06`. Solo deciden A7, A6 y A0 | Todo lo que depende del tiempo (contadores, interrupciones) pasa por `IN`/`OUT`: el momento del acceso dentro de la instrucción importa (`Z80_IO_AT`). |
| Video | VDP del Genesis (que incluye el modo 4 de SMS sin emular) | PPU | VDP 315-5246 (SMS2): modo 4 (mosaicos de 8×8 de 4 bits, 64 sprites, 8 por línea, scroll con bloqueos, columna izquierda en blanco), los modos del TMS9918, contadores V y H, interrupción de línea. GG: CRAM de 12 bits con latch y ventana de 160×144 | Nuevo, escrito desde la documentación (msvdp de Charles MacDonald). Perezoso de a línea, como en GENRecomp. |
| Sonido | YM2612 y SN76489 (`psg.c`) | APU | SN76489 (el `psg.c` de GENRecomp) más el estéreo por canal del GG (`$06`) | `psg.c` pasa a `shared/` con el estéreo, sin cambiar lo que oye el Genesis (fase 3). Sin FM (el YM2413 es japonés). |
| Mandos | 3 y 6 botones, dos jugadores | Uno, 8 botones | SMS: cruz, botones 1 y 2, Pausa en la consola (NMI), dos jugadores. GG: cruz, 1, 2 y Start (puerto `$00`) | Ver «Mandos y guiones de entradas». |
| Modelos | Uno (NTSC) | DMG, CGB, SGB (`--model`) | SMS2 y Game Gear (`--model sms\|gg`); un cartucho de SMS en el GG corre en su modo SMS | Ver «Modelos». |
| Guardado | SRAM y EEPROM | RAM del cartucho y reloj | RAM del cartucho del mapper Sega (8 o 16 KB en la ranura 2, o en `$C000` con el bit 4 de `$FFFC`); la de Codemasters en `$A000` | La RAM del cartucho se guarda si el juego la usa (no hay cabecera que la declare). |
| BIOS | No hay (TMSS opcional, sin ROM) | Arranque emulado | La del SMS y la del GG comprueban la cabecera; **por omisión no hay BIOS**: se arranca con el estado que deja la de EE. UU. (1.3). Opcional por archivo (`SMS_BIOS`) en la fase 8 | Nunca se distribuye. |

## Formatos y cabecera

**Archivos:** `.sms` y `.gg`, la imagen tal como la ve el Z80. Si el tamaño es 16 KB × n + 512, son 512 bytes de
cabecera de copiador y se quitan. El **SHA1 es el de la imagen sin esa cabecera**. La consola no exige ninguna
marca: se acepta cualquier archivo de 32 bytes o más (la prueba Fade Test del GG mide 50) y hasta 4 MB.

**Cabecera «TMR SEGA»** (SMS Power!, ROMHeader y BIOSes): en `$7FF0`, `$3FF0` o `$1FF0` (la BIOS las busca en
ese orden; en la práctica, siempre `$7FF0`). Suma en `+$A` (palabra little-endian), código de producto en BCD en
`+$C-$D` con una cifra más en el nibble alto de `+$E`, versión en el nibble bajo de `+$E`, región en el nibble alto
de `+$F` (3 SMS de Japón, 4 SMS de exportación, 5 GG de Japón, 6 GG de exportación, 7 GG internacional) y código de
tamaño en el bajo. La suma de la BIOS es la suma de bytes en 16 bits de `$0000-$xFEF` (antes de la cabecera) y,
si la ROM pasa de 32 KB, de `$8000` al final del tamaño declarado; el código `$D` (48 KB) suma `$0000-$BFEF`. Solo
la comprueba la BIOS del SMS de exportación: los juegos de Japón y del GG a menudo no la traen bien.

**Cabecera de Codemasters** en `$7FE0`: páginas que cubre su suma, fecha y hora en BCD, la suma (palabras
little-endian de esas páginas sin `$7FF0-$7FFF`) y su inversa, que suman `$10000`. Es lo que delata su mapper.
**SDSC** en `$7FE0` (homebrew): versión, fecha y punteros al nombre, al autor y a la descripción. Tiene precedencia
sobre la de Codemasters (las dos ocupan el mismo lugar).

**El mapper:** el de la base de datos (`smsdb.c`, por SHA1, solo con juegos comprobados); si no, Codemasters si
está su cabecera; si no, sin mapper hasta 48 KB y Sega desde ahí. Lo que la cabecera no dice (juegos sin ella, el
modo SMS de algunos cartuchos de GG, peculiaridades) va a la base de datos con la ROM real, en las fases 2 y 7.

`smsrecomp juego --info` muestra el formato, el SHA1 y el CRC32, las tres cabeceras (con la suma calculada), el
mapper, el modelo y el modo, y si el juego está en la base de datos. Comprobado con las ROMs de `make testroms`:
las sumas que calcula coinciden con las que declaran ZEXALL, el homebrew y las pruebas de FluBBa.

## Modelos

- `--model sms`: el **Master System II** (VDP 315-5246). Es lo que conviene en EE. UU.: el SMS1 (315-5124)
  difiere en la máscara de la tabla de nombres (bit 0 del registro 2, que solo usa el *Y's* japonés), en el zoom
  de sprites (solo los 4 primeros de la línea) y en que no tiene los modos de 224 y 240 líneas; los juegos de
  Codemasters que usan 224 líneas fallan en un SMS1, y no se encontró ninguno de EE. UU. que dependa del SMS1.
  `sms1` queda para la fase 8, medido con la prueba de compatibilidad.
- `--model gg`: el **Game Gear**. Un cartucho de SMS corre en su modo SMS (en el aparato lo activa una pata del
  cartucho): paleta de 6 bits, 256×192 escalados en la pantalla, y Start da la NMI (la Pausa).
- **Por omisión:** el de la base de datos; si no, la extensión (`.gg` → gg, `.sms` → sms); si no, la región
  de la cabecera (5, 6 y 7 → gg). En el GG, el modo SMS se elige por la región (3 o 4) o, sin cabecera, porque el
  archivo no es `.gg`. `carttest` lo prueba con cada combinación.

El modelo llega a las herramientas con `--model` (por el gancho `CLI_HOST_OPTION` del `cli.h` común, sin tocar
`shared/`) y a la GUI de retrorecomp con su selector de modelo, que hoy está fijo en dmg/cgb/sgb y pasa a ser por
consola (fase 4).

## Mandos y guiones de entradas

`input_host.h` con 8 botones en este orden: `UP DOWN LEFT RIGHT A B START PAUSE`, dos jugadores (la segunda
columna del guion; el GG solo tiene uno).

- **A** es el botón 1 y **B** el botón 2 (en los dos modelos).
- **START** es el Start del Game Gear (puerto `$00`) y, en el Master System, aprieta el botón 1: los juegos de SMS
  piden «PUSH START BUTTON» con el botón 1.
- **PAUSE** es la NMI del Master System (el botón de la consola). En el GG en modo SMS, Start es la Pausa.

Así sirven sin cambios los guiones comunes (`tests/inputs/ciclo_traza.txt` y `ciclo_difftest.txt`, idénticos en
NES, GB y GEN: solo usan A, B, START, LEFT y RIGHT) y el del contrato (`START`, `DOWN+A`): SMSRecomp no necesita
guiones propios como los del Neo Geo. En `compat.Sistema`, `botones = ("A", "B")`. El mando aleatorio de `--seed`
(`random_pad.h`, sin cambios): A, B y Start son A, B y START; PAUSE no lo aprieta (pausaría el juego).

## El Z80 de una sola fuente de verdad (fase 1)

Hasta hoy el Z80 común (`shared/z80.c`) es siempre interpretado: en GENRecomp y NEORecomp solo corre el programa
de sonido. Aquí es el CPU principal y se recompila, así que necesita lo mismo que `m68k_ops.h`: la semántica de
cada instrucción en un solo lugar, usada por el intérprete con operandos leídos al ejecutar y por el código
generado con operandos y PC constantes.

- **`shared/z80_ops.h`:** el cuerpo de `z80.c` (`alu`, `rot`, `exec_main`, `exec_cb`, `exec_ed`, `exec_xycb`...)
  pasa a funciones `always_inline` con un contexto `{g, z, ops, pc, t}`: con `ops == NULL` leen del bus (el
  intérprete); con `ops` y `pc` constantes (los bytes de la instrucción, leídos de la ROM al recompilar), el
  compilador pliega los `switch` y queda la instrucción sola. Devuelven el PC siguiente, como los `k_*` del 68000.
  R sube con cada M1 igual por los dos caminos (los juegos hacen `LD A,R` para el azar); WZ, Q, P, el retraso de
  `EI` y el `HALT` (el arreglo de GENRecomp#15) quedan donde están.
- **`shared/z80.c`:** `z80_reset` y `z80_step` (interrupciones IM 0/1/2, NMI, HALT), sobre `z80_ops.h`.
- **`z80_decode`:** largo, tipo de salto (`JP`, `JR`, `CALL`, `RST`, `RET`, `RETI`/`RETN`, `JP (HL)`/`(IX)`/`(IY)`,
  `DJNZ`, `HALT`), condición, destino constante y ciclos (con y sin salto): lo que usa el descubrimiento.
- **`Z80_IO_AT(g, t)`:** un gancho opcional que dice en qué ciclo de la instrucción ocurre cada `IN`/`OUT` (los
  contadores H y V y las interrupciones del VDP dependen de eso). GENRecomp y NEORecomp no lo definen: para ellos
  no cambia nada. Las SingleStepTests traen la actividad del bus por ciclo, así que se puede comprobar.
- **Sin cambiar `sizeof(Z80)`** (38 bytes; los estados guardados de GEN y NEO lo validan) ni la semántica.

Validación: `z80test --recomp` corre las 1 604 000 SingleStepTests por los dos caminos (como `m68ktest
--recomp`); en GENRecomp y NEORecomp, sus SingleStepTests, `z80test --int` y `--nmi`, `statetest`, `golden.sh` sin
cambiar un bit y `genbench`/`neobench` sin perder velocidad. PR en retrorecomp, y uno en GENRecomp y otro en
NEORecomp que mueven el submódulo.

## Identidad del código

Una instrucción se identifica por su desplazamiento en la ROM: página de 16 KB × `$4000` + (dirección & `$3FFF`).
El banco de la traza `BANCO:DIRECCION` es la página (`trace_line.h` con 14 bits, como GBRecomp), y las filas del
mapa (`--viz`), 16 KB con celdas de 256 bytes. Como el mapper Sega puede poner la misma página en cualquier ranura,
el código de una página se compila para la ranura donde corre (la traza lo dice; las vistas de GBRecomp): el
despachador busca por (página, ranura). Un bloque que escribe en la memoria comprueba después si cambió el mapeo
(`MAPCHK`, como en NESRecomp y en los saltos entre bancos de GBRecomp). El proyecto generado lleva
`-DSMS_MAPPER=<n>`, como el `NES_MAPPER` de NESRecomp.

Semillas del descubrimiento: `$0000`, los `RST` (`$00-$38`), `$0038` y `$0066`. Tablas de saltos (`JP (HL)`,
`JP (IX)`, `RST` con datos detrás, argumentos en línea): las heurísticas genéricas de GBRecomp llevadas al Z80.
Código en RAM (`$C000-$DFFF` y su espejo, la RAM del cartucho): `shared/ram_code.h`, con la clave de cada
instrucción en sus prefijos y opcode (`CB`, `DD`, `ED`, `FD`, y `DD CB d op` con `d` como operando), y en `JR` y
`DJNZ` también el desplazamiento, como las ramas relativas de NESRecomp.

## Lo que pasa a `shared/`

Cada cosa con su PR en retrorecomp y uno por repo que mueve el submódulo (primero se fusiona el de retrorecomp,
cada repo apunta al commit de `main` y luego se fusionan los de los repos). `tests/contract.py` comprueba que nadie
guarde copias.

| Qué | Fase | Repos que lo usan | Cuidado |
|---|---|---|---|
| `z80_ops.h` (y `z80.c` sobre él), `z80_decode`, `z80test --recomp` | 1 | GEN, NEO, SMS | `sizeof(Z80)` igual; SingleStepTests, `statetest` y `golden.sh` de GEN y NEO. |
| `psg.c` y `psg.h` con un `psg_host.h` por consola, y el estéreo del GG | 3 | GEN, SMS | El estéreo va en el byte de relleno de `Psg` (desplazamiento 27): `sizeof(Gen)` no cambia. En GEN queda en `$FF` y debe dar los mismos hashes de audio (`audiotest`, `golden.sh`). |
| `retro_run.c`: `ORACLE_RAM_SIZE` en `oracle_host.h` (hoy exige 64 KB; Gearsystem y Genesis Plus GX dan 8 KB de RAM en el SMS) | 4 | GEN, NEO, SMS | Por omisión, 64 KB: GEN y NEO no cambian. |
| `contract.sh` (rama `src/smsrecomp/smsrecomp.c`, su ROM de contrato), `recomp_game.sh` (`sms`, sus cuadros por omisión), `tests/contract.py` (`COMMON`, raíces, `SOLO` de `sstz80.sh`, `MOVIDOS`), `tests/compat_test.py`, la GUI (clase `SMS`, `--sms`, `SMSRECOMP_DIR`, `detect_system` con `TMR SEGA` en `$7FF0`, `PRESETS`, selector de modelo por consola), `index.html` | 4 | todos | El README de retrorecomp: «los cuatro» → «los cinco», las tablas del contrato y de «Diferencias que la interfaz esconde» con una columna SMS/GG. |
| `oracle.py`, si sale igual al de GEN (si no, se copia y queda un issue) | 7 | GEN, NEO, SMS | |

Lo que ya está en `shared/` y se usa tal cual: el Z80 (fase 0, el intérprete actual), `sha1`, `console.h`,
`embed.c`, `json.h`, `z80test.c`, `sstz80.sh`, `random_pad.h`, `trace_line.h`, `ram_code.h`, `inputs.h`, `cli.h`,
`settings.c`, `audio.c`, la ventana (`ui_menu`, `ui_font`, `png`, `files`, `export`, `wav.h`, `player_*.h`, con la
tabla de botones del SMS/GG en `player_touch.h`), `web/`, `android/`, `contract.sh`, `recomp_game.sh`,
`ui_golden.sh`, `retro_run.c` y `libretro_min.h`.

## Fases

- [x] **0. Plan y esqueleto.** Este plan; el repo con el submódulo `tools/retrorecomp`, el bloque común del
  `Makefile`, el CI (Linux, macOS y Windows; sin el contrato, `webtest` ni `apktest` hasta sus fases), la licencia
  MIT, `.editorconfig`, `.gitattributes`, `.gitignore`; el cartucho (`cart.c`: formatos, cabeceras TMR SEGA,
  Codemasters y SDSC, mapper, modelo; `smsdb.c` vacía); `smsrecomp --info`; `carttest`; el Z80 común tal cual
  (`z80test` con las 1 604 000 SingleStepTests, `--int` y `--nmi`); `tests/testroms.sh` (SingleStepTests, ZEXALL y
  el homebrew MIT; con `EXTRA=1` las pruebas sin licencia); `make test`; `docs/ARQUITECTURA.md` con las secciones
  de los otros cuatro.
- [x] **1. El Z80 de una sola fuente de verdad.** Hecho: `shared/z80_ops.h` (la semántica, con un contexto
  `Z80X` que lleva el PC y, para el código generado, los bytes de la instrucción), `z80.c` sobre él (las
  interrupciones, la NMI y el HALT), `z80_decode` y `Z80_IO_AT` (9 ciclos en `IN`/`OUT (n)`, 13 con prefijo; 10
  en `(C)`; 11 en `INI` y los suyos; 14 en `OUTI` y los suyos). Las 1 604 000 SingleStepTests pasan por los dos
  caminos (`z80test --recomp`, `SST_OPTS=--recomp sh tests/sstz80.sh`), y en los dos también el decodificador
  (largo, destino y ciclos con y sin salto) y, por el camino del código generado, el ciclo de cada acceso a un
  puerto. Con los bytes constantes el compilador pliega la instrucción: `JR NZ` queda en ~19 instrucciones de
  máquina con los dos destinos y sus ciclos como constantes. En GENRecomp y NEORecomp nada cambia: `sizeof(Z80)`
  igual, sus pruebas completas, `golden.sh` bit a bit y la misma velocidad. PR en retrorecomp, GENRecomp y
  NEORecomp.
- [x] **2. Bus, mappers, E/S e interrupciones.** Hecho: las tres ranuras (`bus.c`), los mappers Sega (el KB fijo,
  los registros reflejados en `$DFFC-$DFFF`, al encender 00 00 01 02; la RAM del cartucho en la ranura 2 con sus
  dos bancos y en `$C000`), Codemasters (0, 1, 0 al encender; sin KB fijo; RAM en `$A000` con el bit 7 de `$4000`)
  y sin mapper; las gafas 3-D, ignoradas; la RAM del cartucho y su guardado (`--sram`); los puertos con su
  decodificación por A7, A6 y A0 (`io.c`: `$3E`, `$3F` con TH y TR y la detección de consola de exportación,
  `$DC`/`$DD`, la E/S apagada) y los del GG (`$00` con Start y la región, `$01-$05` sin cable, `$06`; en su modo SMS,
  sin ellos); el estado que deja la BIOS 1.3 (`sms_reset`); el VDP sin el dibujo (`vdp.c`: comando, búfer, registros,
  VRAM, la CRAM del SMS y la del GG con su latch, estado, contadores V y H, la bandera de cuadro en `$C1`, la
  interrupción de línea, la línea IRQ), perezoso de a línea y con el ciclo de cada acceso a un puerto
  (`Z80_IO_AT`); la Pausa por NMI. Estados guardados. `bustest` (con programas de Z80 hechos en la prueba),
  `statetest`, `smsrun` (`--watch`, `--sram`, `--sram-text`, `--sdsc`). **Meta:** ZEXDOC y ZEXALL pasan sus 79
  pruebas (en `make test`, ~30 s); los cinco homebrew de `make testroms` encienden la pantalla y atienden una
  interrupción por cuadro. Con 15 juegos comerciales de EE. UU. (9 de SMS y 6 de GG, del autor, en `roms/`): todos se
  reconocen bien (mapper Sega, Codemasters o ninguno; región; modelo), corren 1800 cuadros llenando la VRAM y la
  CRAM y atienden sus interrupciones (OutRun, Space Harrier, TransBot y Columns, también las de línea); Phantasy Star
  y Shining Force guardan y vuelven a cargar su partida. Micro Machines del GG usa 224 líneas (para la ventana del GG
  en la fase 3). Los juegos del GG no traen suma en la cabecera (`$0000`). Pendiente: calibrar en qué punto de la
  línea pasa cada evento del VDP con el VDPTEST de FluBBa (fase 3) y los oráculos (fase 4).
- [x] **3. VDP, sonido y la ventana.** El dibujo (`vdp_render.c`): el modo 4 completo (mosaicos con volteo,
  prioridad y paleta, sprites con su límite de 8 por línea, zoom, desborde y colisión, columna izquierda en blanco,
  bloqueo del scroll horizontal y vertical, 224 y 240 líneas con la tabla de nombres extendida, el desplazamiento
  de sprites del registro 0) y los modos TMS9918 (gráficos I y II con la paleta fija del SMS2); el GG (CRAM de 12
  bits con latch, la ventana de 160×144 en (48, 24), siempre 192 líneas aunque el juego pida el modo extendido, como
  Micro Machines; el modo SMS). La temporización del VDP, calibrada con las tres páginas del VDPTEST de FluBBa (todo
  Ok): el contador V cambia con el H en `$F4`, que avanza 3/4 por ciclo con el salto de `$93` a `$E9`; la línea INT
  sube 2 ciclos (cuadro) y 3 (línea) después; la colisión se ve en el ciclo de su pixel; TH congela el H solo de 0 a
  1; en las líneas del borde los sprites colisionan pero no desbordan. `vdptest`. El PSG en `shared/` (`psg.c` y
  `psg.h`, con los hooks `PSG_STEREO`, `PSG_MUTED` y `PSG_LEVEL`; PR en retrorecomp y en GENRecomp, cuyo golden sigue
  igual en los 26 casos) con el estéreo del GG (`$06`, solo en su modo GG); `audiotest`. La ventana `sms` con todo lo
  de `shared/` (menús, i18n `LANG_ES`/`LANG_EN`, estados guardados F5/F8, captura PNG, `--record`/`--play`, el mando
  táctil con Pausa en el SMS y Start en el GG, Entrada → Consola y `--model`, pixeles de 8:7 en el SMS), el icono del
  mando del Master System (KyleBing, CC BY 4.0, `tools/make_icon.py`) y su referencia de la interfaz
  (`tests/ui_golden.txt` y `tests/ui_golden_gg.txt`, con una ROM mínima). **Meta:** el VDPTEST (local, `EXTRA=1`)
  da todo Ok y los 15 juegos de `roms/` se ven y se oyen bien. Un solo icono para las dos consolas (el .exe solo
  lleva uno); el sonido a WAV lo guarda `smsrun --wav` (y `smsaudio` en la fase 4), como en GENRecomp, donde la
  ventana tampoco lo exporta.
- [x] **4. Herramientas sin ventana y contrato.** `smstrace` (la traza `BANCO:DIRECCION` con bancos de 16 KB:
  `Cart.trace` en el runtime; también `sms --trace`), `smsshot` (`--expect`, `--hash`), `smsaudio`, `smsbench`,
  `smsramdump`, con `cli.h` e `inputs.h` comunes y `input_host.h`; `--model` en todas; `inputtest`;
  `src/tools/mkcontrato.c` (el juego del contrato, armado en el build sin ensamblador: mapper, RAM del cartucho,
  código en RAM, interrupciones de cuadro y de línea, Pausa, VDP y PSG, todo dependiente de las entradas; y
  `--minima` para `webtest`/`apktest`); `golden.sh` (21 casos); el contrato común en verde y en el CI. En retrorecomp
  (un PR): la rama de SMSRecomp en `contract.sh` y `recomp_game.sh`, `ORACLE_RAM_SIZE` en `retro_run.c`,
  `contract.py` (`COMMON`, raíces, `SOLO` de `sstz80.sh`), `compat_test.py`, la GUI (clase `SMS`, `--sms`,
  `SMSRECOMP_DIR`, `detect_system` con `TMR SEGA`, las subcarpetas de `roms/`, `PRESETS`, el selector de modelo de
  cada consola), `index.html` y el README («los cinco»). `tools/compat/run.py` y `report.py` (la etapa 1 corre); los
  oráculos (`tools/oracles/oracle.py`, Genesis Plus GX y Gearsystem, con `make build/retro_run`).
- [ ] **5. El recompilador.** Descubrimiento desde las semillas, heurísticas de GBRecomp llevadas al Z80, bancos
  y vistas por ranura, emisión con `z80_ops.h`, el proyecto generado (`embed`, `Makefile`, `LEEME.txt`),
  `difftest` y `bench` con la salida común (`difftest: N cuadros iguales (L llamadas, R a código recompilado, P% de
  los ciclos)`, líneas `DIFERENCIA ...`, «Intérprete por zona» antes de la línea final), código en RAM con
  `ram_code.h`, `tests/recomp_game.sh`, `--viz`/`--events`/`--why`/`--coverage`/`--html`/`--codemap`/`--dis`,
  `tests/coverage.sh`. Meta: los juegos de prueba y los comerciales sin diferencias contra el intérprete y el
  99-100 % de los ciclos recompilados, como en los otros.
- [ ] **6. Web y Android.** `make web`, `make apk`, el mando en pantalla; `webtest` y `apktest` en el CI.
- [ ] **7. Compatibilidad.** `tools/compat` (`run.py` con las colecciones SMS y GG, categorías por mapper y
  tamaño: sin mapper, Sega 128/256/512 KB, con RAM del cartucho, Codemasters; `report.py` idéntico), los oráculos,
  `compat_test.py`, `docs/COMPATIBILIDAD.md` y los README de los cinco repos. La corrida completa sobre las
  colecciones No-Intro del autor, con su aprobación (tarda).
- [ ] **8. Después.** BIOS opcional por archivo (`SMS_BIOS`), `sms1`, Light Phaser (como la Zapper de
  `nesshot`), gafas 3-D, el FM (YM2413; el `vrc7.c` de NESRecomp es un OPLL), el cable de enlace del GG, PAL.

## Pruebas libres (`make testroms`)

| Prueba | Licencia | Qué prueba | En el CI | Cómo se obtiene |
|---|---|---|---|---|
| SingleStepTests del Z80 | MIT | Cada instrucción: registros (con WZ, Q, P), RAM, puertos y ciclos | Sí (Linux) | El mismo commit que GEN y NEO. |
| ZEXALL y ZEXDOC para el SMS (maxim-zhao/zexall-sms v0.21) | GPL-2.0 (como datos: no van en el repo) | El Z80 completo, con y sin las banderas no documentadas; escribe el resultado en la SRAM y en la consola SDSC | Desde la fase 2 (tarda: horas en una consola real; se mide) | Release de GitHub, con su SHA-256. |
| Dragon Blaster, Food Fighter, River Strike (haroldo-ok) | MIT | Juegos de 32 KB sin mapper: para el recompilador | Sí | Releases de GitHub, con su SHA-256. |
| Waternet (joyrider3774) | MIT | Un juego de 64 KB con mapper Sega, en SMS y en GG | Sí | Release de GitHub, con su SHA-256. |
| ROMs propias: `mkcontrato`, la mínima | MIT (de este repo) | El contrato común; `webtest` y `apktest` | Sí | Se arman en el build (fase 4). |
| VDPTEST 1.31 de FluBBa | Sin licencia | Puertos del VDP, búfer de lectura, contadores H/V, tiempos de las interrupciones, sprites | No | `EXTRA=1 make testroms`, de SMS Power!, con su SHA-256. |
| VCounterTest, ScrollTestProgram, 64ColorPaletteTest (Charles MacDonald) | Sin licencia | Contador V, bloqueo del scroll, paleta de 64 colores | No | Ídem. |
| PauseTest y SMSCycleCounter (FluBBa), SMS Sound Test | Sin licencia | La NMI de la Pausa, ciclos por línea, el PSG | No | Ídem. |
| GG High-Res Graphics Demo, Fade Test (GG) | Sin licencia | El VDP y la CRAM de 12 bits del GG | No | Ídem. |
| SMS Test Suite de sverx (v0.38) | Sin licencia | Patrones 240p, mandos, PSG | No | Ídem (release de GitHub). |

No se encontró ninguna prueba dedicada a los mappers ni a los puertos `$00` y `$06` del GG: eso lo cubren las ROMs
propias de `bustest` y `mkcontrato`. Las pruebas sin licencia se bajan para usarlas en tu máquina y nunca se
redistribuyen.

## Juegos para validar

Comerciales de EE. UU., del autor (en `roms/`, nunca en el repo). Candidatos por lo que prueban; se confirman con
las colecciones No-Intro del autor al llegar a la fase 2:

| Qué prueba | SMS | Game Gear |
|---|---|---|
| Sin mapper (32 KB) | Hang-On, Teddy Boy | Columns |
| Mapper Sega, 128-256 KB | Alex Kidd in Miracle World, Sonic the Hedgehog, Wonder Boy III | Sonic the Hedgehog, Shinobi |
| Mapper Sega, 512 KB y RAM del cartucho con batería | Phantasy Star | Sonic Chaos, Shining Force: The Sword of Hajya |
| Codemasters (224 líneas, su mapper) | Cosmic Spacehead, Micro Machines (de Europa: Codemasters casi no publicó para el SMS de EE. UU.; corren en NTSC) | Micro Machines, Cosmic Spacehead |
| Modo TMS9918 | F-16 Fighter | — |
| Interrupción de línea y efectos de trama | Sonic the Hedgehog (el agua), Space Harrier | Sonic Chaos |
| Cartucho de SMS en el GG | — | Los que la colección de GG traiga con región 3 o 4 en la cabecera (se cuentan en la fase 2) |

## Riesgos

1. **Temporización del VDP:** los contadores H y V, el momento exacto de las interrupciones de cuadro (msvdp se
   contradice: V = `$C1` en una tabla, «la primera línea tras el área activa» en otra) y de línea, y el ciclo
   del `IN`/`OUT` dentro de la instrucción. msvdp admite que no sabe en qué punto de la línea ocurre cada evento.
   Se resuelve con el VDPTEST de FluBBa (local), con los oráculos y con `Z80_IO_AT`.
2. **Recompilar el Z80 compartido:** un cambio en `shared/z80.c` toca GEN y NEO. Se valida con sus pruebas y sus
   líneas base exactas antes de fusionar, y sin cambiar `sizeof(Z80)`.
3. **Pocas semillas y muchas tablas:** el descubrimiento depende de las heurísticas (como en GBRecomp: sin ellas,
   Tetris se quedaba en 29 %). Se mide con `--coverage` y con los juegos de prueba.
4. **Una página en varias ranuras:** el mismo código compilado para más de una dirección; se mide cuánto pasa.
5. **Sin BIOS:** el estado inicial sale del desensamblado de la BIOS 1.3; algún juego puede depender de lo que
   deja otra versión. La BIOS opcional (fase 8) lo resuelve.
6. **Pruebas sin licencia:** las mejores pruebas del VDP no tienen licencia publicada; quedan fuera del CI. El CI
   se apoya en las SingleStepTests, ZEXALL, el homebrew MIT y las ROMs propias.
7. **Licencias:** Genesis Plus GX (no comercial), Gearsystem (GPL-3), MAME y SMS Plus solo se consultan para
   resolver dudas o se corren como programas aparte; no se copia su código.

## Decisiones

1. Precisión como en GENRecomp: el Z80 por instrucción con sus ciclos exactos, el VDP perezoso de a línea; lo que
   depende del momento dentro de la instrucción (los puertos) se cobra con `Z80_IO_AT`.
2. Las interrupciones las atiende el intérprete; el código generado sale apenas hay una pendiente (IM 1 por
   cuadro o por línea, la NMI de la Pausa).
3. Un repo para las dos consolas, con `--model sms|gg`; el SMS2 por omisión en el Master System.
4. Sin BIOS por omisión, con el estado que deja la de EE. UU.; BIOS opcional al final.
5. Orden: plan → Z80 → bus e interrupciones → VDP, sonido y ventana → herramientas y contrato → recompilador →
   web y Android → compatibilidad.
6. NTSC de EE. UU. primero; PAL, el FM, SG-1000/SC-3000, los mappers coreanos, la Light Phaser, la Paddle, el
   Sports Pad y el cable de enlace del GG, después o nunca.

## Fuentes

- SMS Power! (smspower.org/Development): VDP, contadores (`ScanlineCounter`), registros del VDP, `SMS1SMS2VDPs`,
  `GGVDP`, `GGLCD`, `Palette`, `IOPortMap`, `Port3E`, `PeripheralPorts`, `PauseButton`, `StartButton`, `Mappers`,
  `3DGlasses`, `ROMHeader`, `CodemastersHeader`, `SDSCHeader`, `BIOSes` (y los desensamblados `bios13.asm` y
  `majbios.asm`), `SN76489` (de Maxim), `ClockRate`, `GearToGearCable`.
- La documentación del VDP del SMS y del GG de Charles MacDonald (`msvdp-20021112.txt`) y `smstech-20021112.txt`.
- «The Undocumented Z80 Documented», de Sean Young.
- Para cotejar, sin copiar código (sus licencias no son MIT): Genesis Plus GX, Gearsystem, SMS Plus, MAME.
