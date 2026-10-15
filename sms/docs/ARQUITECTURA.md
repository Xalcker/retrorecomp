# Arquitectura de SMSRecomp

SMSRecomp tomará una ROM de Sega Master System o Game Gear y generará un proyecto C99 que se compila en un
ejecutable nativo. El ejecutable cargará la ROM del usuario en tiempo de ejecución y validará su SHA1: nunca se
distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de NESRecomp, GBRecomp,
GENRecomp y NEORecomp: lo que cambia entre las consolas queda dentro de cada una. Las que todavía no tienen
contenido dicen en qué fase de [PLAN.md](PLAN.md) llegan. Hoy están hechas las fases 0 a 2: el cartucho, el Z80 común con
su semántica en una sola fuente de verdad, el bus con los mappers, la E/S, las interrupciones y el VDP sin el dibujo.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `sms.h` (estado y interfaz), `sms.c` (encendido, cuadro, RAM del cartucho, estados guardados), `cart.c` (formato, cabeceras, mapper, modelo), `smsdb.c` (base de datos de juegos), `bus.c` (mapa de memoria, mappers, reparto de los puertos), `io.c` (`$3E`, `$3F`, los mandos y los puertos del Game Gear), `vdp.c` (puertos, VRAM, CRAM, contadores e interrupciones), `vdp_render.c` (el dibujo), `z80_host.h` y `z80_host.c` (lo que el Z80 común toma de esta consola), `psg_host.h` y `audio_host.h` (lo que toman el PSG y la mezcla comunes). De `tools/retrorecomp/shared`, común con GENRecomp y NEORecomp: `z80.h`, `z80_ops.h` (la semántica de cada instrucción) y `z80.c` (el intérprete); con GENRecomp, `psg.c` y `psg.h` (el SN76489) y `audio.c` (la mezcla); también `sha1` y `console.h`. |
| `src/smsrecomp/` | El recompilador. Hoy, `--info`; la recompilación llega en la fase 5. |
| `src/platform/` | La ventana con SDL2 (`main_sdl.c`, con `i18n`, `settings.h`, `input_host.h` y el icono de `tools/make_icon.py`); el resto de la ventana es de `tools/retrorecomp/shared`. `difftest` y `bench` llegan en las fases 4 y 5. |
| `src/tools/` | Diagnóstico: `smsrun`. Pruebas: `carttest`, `bustest`, `vdptest`, `audiotest`, `statetest`; de `tools/retrorecomp/shared`, `z80test` (SingleStepTests, con `json.h`). Las herramientas sin ventana (`smstrace`, `smsshot`, `smsaudio`, `smsramdump`, `smsbench`) e `inputtest` llegan en la fase 4. |
| `tests/` | `run_tests.sh` (`make test`), `testroms.sh` (`make testroms`), `sstz80.sh` (las SingleStepTests del Z80), y los envoltorios comunes `contract.sh` y `recomp_game.sh` (el mismo archivo en los cinco repos; con efecto desde las fases 4 y 5). |
| `tools/` | `retrorecomp/` (la GUI común y `shared/`, submódulo). `compat/` y `oracles/` llegan en las fases 4 y 7. |

## Una sola fuente de verdad para el CPU

`z80_ops.h` (de `tools/retrorecomp/shared`, común con GENRecomp y NEORecomp, como `m68k_ops.h` para el 68000)
tiene la semántica de cada instrucción del Z80. Todo pasa por un contexto, `Z80X`, con el PC y, para el código
generado, los bytes de la instrucción:

- El **intérprete** (`z80_step`, en `z80.c`) lo arma con `ops = NULL`: el opcode y los operandos se leen del bus.
  `z80_step` atiende antes la NMI (la Pausa), la interrupción (IM 0/1/2, con el retraso de `EI`) y el `HALT`.
- El **código generado** (fase 5) lo arma con `ops` apuntando a los bytes de la instrucción, leídos de la ROM al
  recompilar, y el PC constante. `z80_exec` devuelve los ciclos y deja el PC siguiente; el compilador pliega los
  `switch`: `JR NZ,e` queda en unas 19 instrucciones de máquina con los dos destinos y sus ciclos (12 y 7) como
  constantes.

R sube con cada M1 igual por los dos caminos (los juegos hacen `LD A,R` para el azar), y WZ (MEMPTR), Q, P y el
retraso de `EI` quedan en el estado del Z80, cuyo tamaño no cambió (los estados guardados de GENRecomp y NEORecomp
lo validan). `z80_decode` da, para el descubrimiento, el largo, el tipo de salto, la condición, el destino
constante y los ciclos con y sin salto. `Z80_IO_AT(g, t)`, un gancho opcional, dice en qué ciclo de la instrucción
se accede a cada puerto: 9 en `IN`/`OUT (n)` (13 con un prefijo `DD`/`FD` delante), 10 en `IN`/`OUT (C)`, 11 en
`INI`, `IND`, `INIR` e `INDR`, y 14 en `OUTI`, `OUTD`, `OTIR` y `OTDR`; lo usarán los contadores y las
interrupciones del VDP (fase 2).

**Validación:** las 1 604 000 [SingleStepTests del Z80](https://github.com/SingleStepTests/z80) (registros, con
WZ, Q y P; RAM; puertos; ciclos) **por los dos caminos**: el intérprete (`z80test`) y el del código generado
(`z80test --recomp`; `SST_OPTS=--recomp sh tests/sstz80.sh`), en `make test`. En los dos se comprueba además el
decodificador contra cada prueba (el PC final con o sin salto y los ciclos) y, por el camino del código generado,
el ciclo del acceso al puerto contra el que marcan las pruebas. Más las pruebas propias de interrupciones
(`z80test --int`: la INT desde un `HALT` en los modos 1 y 2; `--nmi`: la NMI, que aquí es la Pausa).

## Temporización

El reloj es el del Genesis: maestro de 53,693175 MHz, el Z80 a 1/15 (3,579545 MHz), líneas de 342 pixeles de 10
ciclos maestros (3420, 228 del Z80) y 262 líneas: 59 736 ciclos del Z80 por cuadro, 59,92 cuadros por segundo.
`Sms.cyc` cuenta los ciclos del Z80 desde el reset; cada instrucción suma los suyos (los de `z80_step`, que dan
las SingleStepTests).

**El VDP es perezoso de a línea**, como el de GENRecomp: después de cada instrucción, `sms_after_step` lo pone al
día solo si empezó una línea nueva (`vdp_sync`), y antes de cada acceso a sus puertos se lleva hasta el ciclo de
ese acceso. Ese ciclo no es el del comienzo de la instrucción: el Z80 común avisa con `Z80_IO_AT` (`z80_host.h`,
`z80_host.c`) en qué ciclo de la instrucción toca el puerto (9 en `IN`/`OUT (n)`, 10 en `(C)`, 11 en `INI` y los
suyos, 14 en `OUTI` y los suyos, medidos en las SingleStepTests), y los puertos usan `Sms.io_cyc`. Así un
`IN A,($7E)` lee el contador V de la línea en que de verdad lee, y uno de `$BF` ve la bandera de cuadro si subió
antes del acceso.

Al empezar cada línea sube el contador V, y el contador de la interrupción de línea baja (líneas 0 a 192, o al alto
de la imagen) o se recarga con el registro 10 (las demás). La bandera de cuadro sube en la línea `$C1` (`$E1` con
224 líneas). msvdp no dice en qué punto de la línea pasa cada cosa: aquí, al empezarla, que es también el pixel 0
del contador H. Se calibra con el VDPTEST de FluBBa (local, `EXTRA=1 make testroms`) y con los oráculos (fase 4).

## Interrupciones y despachador

**Interrupciones.** La línea IRQ del Z80 (`z80.irq`) es la del VDP: la bandera de cuadro con el registro 1, bit 5,
o la interrupción de línea pendiente con el registro 0, bit 4 (`update_irq`, cada vez que cambia una de ellas o un
registro). Se borran al leer el estado (`$BF`). El Z80 la atiende al empezar la instrucción siguiente si IFF1 está
encendido y la anterior no fue `EI`: en IM 1 salta a `$0038`; en IM 0 y IM 2 el bus lee `$FF` (`RST $38`, o el
vector en `I`:`$FF`), como en el SMS2 y el GG. **La Pausa es la NMI** (`$0066`), por flanco: al empezar el cuadro
en que se aprieta (`sms_frame_begin`); en el Game Gear, solo en su modo SMS y con Start. `sms_run_frame` corre
instrucciones (`sms_step`) hasta que el VDP termina el cuadro.

El despachador del código recompilado llega en la fase 5.

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

`bus.c`: tres ranuras de 16 KB del cartucho en `$0000`, `$4000` y `$8000`, y la RAM de 8 KB en `$C000` con su
espejo en `$E000`. Las ranuras leen de la imagen en páginas enteras (`Cart.image`: una ROM de menos de 16 KB,
repetida), así que una página más grande que la ROM da la vuelta. La ROM no se escribe. Los puertos se deciden por
A7, A6 y A0: `$00-$3F` escriben el control de memoria (pares, `$3E`) o el de E/S (impares, `$3F`) y leen `$FF`;
`$40-$7F` escriben el PSG y leen los contadores V (pares) y H (impares); `$80-$BF` son el VDP (datos y control);
`$C0-$FF` leen los mandos (`$DC` y `$DD`; en el GG solo `$C0`, `$C1`, `$DC` y `$DD`). Un `OUT` a `$FD` va a la
consola de depuración SDSC si hay quien la escuche (`smsrun --sdsc`; no existe en la consola).

### Mappers

`cart_detect_mapper` (`cart.c`) elige el mapper al cargar: el de la base de datos (`smsdb.c`, por el SHA1 de la
imagen; empieza vacía y solo lleva juegos comprobados con un archivo real); si no, **Codemasters** si está su
cabecera en `$7FE0` (la suma y su inversa suman `$10000`); si no, **sin mapper** hasta 48 KB (las tres ranuras
fijas) y **Sega** (315-5235) desde ahí. Una cabecera SDSC en el mismo lugar no se confunde con la de Codemasters.
Cómo cambian las ranuras (SMS Power!, Mappers):

- **Sega:** `$FFFD`, `$FFFE` y `$FFFF` eligen la página de cada ranura (al encender, 0, 1 y 2: el 315-5235); el
  primer KB (`$0000-$03FF`) es siempre la página 0. `$FFFC`: el bit 3 pone la RAM del cartucho en la ranura 2 (el
  bit 2 elige el banco de 16 KB) y el bit 4 en `$C000-$FFFF`; el bit 7 y el desplazamiento de bancos (bits 0-1)
  no los usa ningún juego conocido y no se emulan. Los registros son de solo escritura, pero la escritura también
  llega a la RAM: se leen en `$DFFC-$DFFF`.
- **Codemasters:** una escritura en una ranura (los juegos escriben en `$0000`, `$4000` y `$8000`) elige su página
  (al encender, 0, 1 y 0), sin el KB fijo; el bit 7 de lo escrito en `$4000` pone 8 KB de RAM en `$A000-$BFFF`
  (Ernie Els Golf). Lo escrito no se refleja en la RAM.
- **Sin mapper:** las páginas 0, 1 y 2; `$FFFC-$FFFF` es solo RAM.

Las gafas 3-D (`$FFF8-$FFFB`) no se emulan: es solo RAM. Cada cambio sube `Mapper.map_ver`, que el código
generado vigilará (`MAPCHK`, fase 5).

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

- **RAM del cartucho:** 32 KB (los dos bancos de 16 KB del mapper Sega; Codemasters usa 8 KB). Ninguna cabecera la
  declara: tiene partida que guardar si el juego la mapeó alguna vez (`Cart.ram_used`). `sms_sram_load`,
  `sms_sram_save` y `sms_sram_dirty` la guardan entera, como archivo de 32 KB (`smsrun --sram`; la ventana, al salir y
  cada 5 segundos si cambió).
- **Estados guardados** (`sms_state_save`/`sms_state_load`): una cabecera (marca, versión, `sizeof(Sms)` y el SHA1
  de la ROM), la estructura `Sms` entera (Z80, RAM, VDP, mapeo, E/S) y la RAM del cartucho. Al cargar se
  conservan los punteros de la corrida actual. Solo los lee la misma versión del runtime con la misma ROM.
  `statetest` lo prueba: tras cargar, los mismos cuadros dan exactamente lo mismo.
- **Encendido** (`sms_reset`): sin BIOS, con lo que deja la BIOS 1.3 de EE. UU. al saltar al cartucho (su
  desensamblado, `bios13.asm`): interrupciones apagadas, IM 1, SP en `$DFF0`, la RAM en cero salvo `$C000`, con lo
  último escrito en `$3E` (`$AB`; la del GG escribe `$A8`), los registros del VDP como los deja (R0 `$36`, R1 `$A0`,
  R2-R5 `$FF`, R6 `$FB`, R7-R9 0, R10 `$FF`) y el mapper como al encenderse (la BIOS 1.3 no lo toca).

## Video y audio

### VDP

`vdp.c`, escrito desde msvdp (Charles MacDonald):

- **Control** (`$BF`, y sus espejos impares): un comando de dos bytes. El primero pone los 8 bits bajos de la
  dirección; el segundo, los 6 altos y el código: 0 lee la VRAM (carga el búfer y sube la dirección), 1 escribe la
  VRAM, 2 escribe el registro de los bits 3-0 con el primer byte (del 0 al 10; los otros no hacen nada), 3 escribe la
  CRAM. Leerlo da el estado (INT, OVR y COL en los bits 7-5; los bits 4-0 no los pone el VDP: aquí 0) y lo borra,
  junto con la interrupción de línea pendiente y el primer byte suelto.
- **Datos** (`$BE`, y sus espejos pares): escribir va a la VRAM o a la CRAM y carga el búfer de lectura; leer da
  el búfer y lo vuelve a llenar de la VRAM. Los dos suben la dirección (14 bits) y borran el primer byte suelto.
  La CRAM del SMS son 32 bytes `--BBGGRR`; la del Game Gear en modo GG, 32 palabras `----BBBBGGGGRRRR`: la dirección
  da la vuelta en `$3F` y el byte par queda en un latch que se escribe junto con el impar.
- **Contadores** (`$7E`, `$7F`): el V es lineal hasta `$DA` (192 líneas) o `$EA` (224) y salta a `$D5` o `$E5`
  para terminar en `$FF` en la línea 261. El H se congela cuando cambia TH en `$3F` (io.c): 342 pixeles por línea,
  1,5 por ciclo del Z80; el contador interno de 9 bits va de 0 a `$127`, salta a `$1D2` y llega a `$1FF`, y se leen
  sus 8 bits altos (`$00-$93`, `$E9-$FF`).
- El alto de la imagen (192, 224 o 240 líneas, con el modo 4 y los bits M1 y M3) se fija al empezar el cuadro.

**Cuándo pasa cada cosa.** msvdp no lo dice; lo dicen la guía y la tabla del VDPTEST de FluBBa (medido en un
SMS), cuyas tres páginas dan todo Ok. Con c = ciclos desde que cambia el contador V (cuando el H vale `$F4`): en c = 0
se dibuja la línea, baja el contador de la interrupción de línea y se ven las banderas de cuadro y de desborde; la
línea INT del Z80 sube en c + 2 por la de cuadro y en c + 3 por la de línea (el Z80 la mira al empezar la siguiente
instrucción); la colisión se ve en el ciclo en que se dibuja su pixel, 9 ciclos después. `vdp_sync` lleva todo de
evento en evento (`next_event`), y `sms_after_step` solo lo llama cuando toca. En las líneas del borde los sprites
colisionan pero no desbordan; con la pantalla apagada, al revés.

**El dibujo** (`vdp_render.c`), de a línea en `fb` (12 bits `----BBBBGGGGRRRR`; los 2 bits por color del SMS se
escalan ×5):

- **Modo 4:** el fondo de mosaicos de 8×8 de 4 bits con volteo, prioridad y paleta, scroll horizontal (con el
  bloqueo de las dos primeras filas, bit 6 del registro 0) y vertical (con el de las 8 columnas de la derecha, bit
  7; el vertical se fija al empezar el cuadro); la columna izquierda en blanco (bit 5); 64 sprites, 8 por línea (el
  9.º enciende el desborde), de 8×8 u 8×16, con zoom, el desplazamiento de 8 pixeles (bit 3) y la colisión; 224 y
  240 líneas con la tabla de nombres de 32×32.
- **Modos TMS9918:** gráficos I y II con la paleta fija del SMS2 y sus 32 sprites (4 por línea); el texto y el
  multicolor, sin juegos de EE. UU. que los usen, quedan con el color de fondo.
- **Game Gear:** se ve la ventana de 160×144 en (48, 24); en su modo GG el alto queda en 192 aunque el juego pida el
  modo extendido (lo usa Micro Machines para la tabla de nombres).

`vdptest` arma pantallas en la VRAM y la CRAM y comprueba pixeles: las dos paletas, el volteo, la prioridad, el
scroll y sus bloqueos, los sprites (límite, zoom, desborde y colisión con su ciclo), los modos TMS y la ventana del
GG.

### Z80

El núcleo común de `tools/retrorecomp/shared` (ver «Una sola fuente de verdad para el CPU»). Aquí es el CPU
principal: su bus es `bus.c` (memoria y puertos), y `Z80_IO_AT` le da a los puertos el ciclo de cada acceso.
ZEXDOC y ZEXALL (el ejercitador de Frank Cringle portado al SMS) pasan sus 79 pruebas, también con las banderas no
documentadas.

### Audio

El SN76489 de `tools/retrorecomp/shared/psg.c`, el mismo de GENRecomp, que pasó a `shared/` en esta fase. Lo que
cambia entre las consolas va en `psg_host.h`: `PSG_STEREO` (el Game Gear en su modo GG manda cada canal a la
izquierda con los bits 4-7 del puerto `$06` y a la derecha con los 0-3; el SMS y el Genesis, a los dos lados),
`PSG_MUTED` (el menú Audio: bits 0-3 de `audio.mute`) y `PSG_LEVEL` (el volumen al lado de la FM del Genesis; aquí
suena solo). Se escribe con el ciclo del acceso al puerto (`Z80_IO_AT`), y el GG lleva el PSG al ciclo del cambio
antes de guardar un `$06` nuevo. La mezcla común (`audio.c`, con `audio_host.h`) promedia en el tiempo a 44 100 Hz
estéreo. `audiotest` mide la afinación (un La de 440 Hz por los cruces por cero), los pasos de 2 dB, el silencio,
el ruido, el silencio por canal y el estéreo del GG.

## Variantes de la consola y accesorios

`--model sms` es el Master System II (VDP 315-5246) y `--model gg` el Game Gear; la razón de no emular el SMS1 por
ahora está en PLAN.md («Modelos»). `sms_load` elige el modelo: el que se pide; si no, el de la base de datos; si
no, la extensión (`.gg` → gg, `.sms` → sms); si no, la región de la cabecera (5, 6 y 7 son del Game Gear). En el
Game Gear, un cartucho de SMS corre en su modo SMS: el que dice la base de datos, el de región 3 o 4 o, sin
cabecera, el que no es `.gg`. `carttest` prueba cada combinación y `smsrecomp --info` dice con qué modelo y en qué
modo correría el juego.

**Mandos** (`io.c`): `$DC` lee arriba, abajo, izquierda, derecha, TL (botón 1) y TR (botón 2) del mando A, y
arriba y abajo del B; `$DD`, izquierda, derecha, TL y TR del B, el botón de reset (no hay en el SMS2 ni en el GG: 1),
un 1 y TH de los dos puertos. En 0 lo apretado. En el Master System, START aprieta el botón 1. `$3F` pone TR y TH de
cada puerto como entradas o salidas y su nivel: es una consola de exportación, así que `$DD` lee en TH el nivel
escrito (así detectan los juegos que no es japonesa), y TR de cada puerto, si es salida, también. Con el bit 2 de
`$3E` (la E/S apagada), `$C0-$FF` leen `$FF`.

**Game Gear** en modo GG: `$00` tiene Start (bit 7, en 0 apretado) y la región (bit 6 en 1: fuera de Japón; bit 5
en 0: NTSC); `$01-$05` son el puerto de enlace (sin cable: se guarda lo escrito) y `$06` el estéreo del PSG (se
suena: ver «Audio»); al encender, C0 7F FF 00 FF 00 FF. Start no da la NMI. En su modo SMS no existen
`$00-$06` y Start es la Pausa.

Los accesorios (Light Phaser, gafas 3-D, cable de enlace) llegan en la fase 8.

## Interfaz del ejecutable

La ventana `sms` (`src/platform/main_sdl.c`) es la de GENRecomp con lo que cambia para esta consola: la barra de
menús y los diálogos comunes (`ui_menu`, `ui_font`, `player_*.h` de `tools/retrorecomp/shared`), en español e inglés
(`i18n.c`). Menús: Juego (abrir ROM, reiniciar, pausa, estados guardados en 4 ranuras con F5 y F8), Video (tamaño,
pantalla completa, ocultar la barra, escala entera y pixeles de 8:7 en el Master System; la pantalla del Game Gear
queda con pixeles cuadrados), Audio (sonido, volumen y los cuatro canales del PSG), Entrada (la consola: la que pide
el juego, Master System o Game Gear; reinicia), Exportar (captura PNG), Idioma y Ayuda. Teclado: flechas, Z X =
botones 1 y 2, Enter = Start del GG, Espacio = Pausa; el jugador 2 en I J K L y V B. El mando táctil tiene los
botones 1 y 2 y, arriba, la Pausa (SMS) o Start (GG). La ventana toma el tamaño de lo que se ve: 256×192 (×8/7 con
el aspecto) o 160×144, por la escala (3 por omisión). `--record` y `--play` usan los guiones comunes
(`inputs.h`, con `input_host.h`). El icono es el mando del Master System de la serie TriMui de
KyleBing/retro-game-console-icons (CC BY 4.0), que `tools/make_icon.py` lleva a `icono.h` y `icono.ico`.

`tests/ui_golden.txt` y `tests/ui_golden_gg.txt` guardan el cksum de cada menú y diálogo dibujado con `--ui-shot`
(sin ventana, ajustes de fábrica y en español) con una ROM mínima de 32 KB de NOP, como `.sms` y como `.gg`.

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
- **`make test`:** `carttest`, `z80test --int` y `--nmi`, `bustest`, `vdptest`, `audiotest`, `statetest` con el
  homebrew, las
  SingleStepTests del Z80 por los dos caminos, ZEXDOC y ZEXALL completos (unos 330 000 cuadros cada uno; corren a la
  vez, ~30 s), el homebrew con `smsrun` (pantalla encendida y una interrupción por cuadro), `smsrecomp` con un
  archivo que no es ROM, sin argumentos y con un modelo desconocido, `smsrecomp --info` con las ROMs de prueba
  (SHA1, suma, mapper y modelo) y con cada ROM de `roms/` y de sus subcarpetas (también 1200 cuadros con `smsrun`),
  y la interfaz de `sms` contra `tests/ui_golden.txt` y `tests/ui_golden_gg.txt` (los 7 menús, 3 diálogos, la barra
  oculta, `--no-menu`, `--fullscreen` y el Game Gear; `UI_GOLDEN_UPDATE=1` las reescribe).
- **`smsrun rom [--frames N] [--model sms|gg] [--watch DIR] [--sram ARCHIVO] [--sram-text] [--sdsc]
  [--shot ARCHIVO.png] [--shots CARPETA [--every N]] [--wav ARCHIVO.wav] [boton:desde-hasta]`:** el diagnóstico, como `genrun`: corre sin ventana y resume ciclos, interrupciones, el
  mapeo, el VDP y los PC más frecuentes del último cuadro (los bucles de espera); `--watch` avisa cuando cambia un
  byte de la RAM, `--sram-text` imprime el texto de la RAM del cartucho (ahí deja ZEXALL su resultado) y `--sdsc`
  lo que el juego escribe en la consola de depuración SDSC; `--shot` y `--shots` guardan la imagen (lo que se ve) y
  `--wav` el sonido.

## Estado de la validación

`make test` (`tests/run_tests.sh`), en Linux, macOS y Windows en cada PR:

- **CPU:** las 1 604 000 SingleStepTests del Z80 por el intérprete y por el camino del código generado, con el
  decodificador y el ciclo de los puertos (`z80test` y `--recomp`, en Linux), y las interrupciones (`--int`, `--nmi`).
- **Cartucho:** `carttest` (cabeceras TMR SEGA en sus tres lugares, Codemasters y SDSC, la suma de cada código de
  tamaño, la cabecera de copiador, el mapper, el modelo y el modo, los archivos que no pueden ser una ROM, el
  CRC32) y `smsrecomp --info` con las ROMs de prueba.
- **Bus y VDP sin el dibujo:** `bustest`, con ROMs hechas en la prueba: la RAM y su espejo; sin mapper (32 y 48
  KB); el mapper Sega (ranuras al encender, el KB fijo, los registros en la RAM, páginas que dan la vuelta, la RAM
  del cartucho en la ranura 2 con sus dos bancos y en `$C000`); el de Codemasters (0, 1, 0; sin KB fijo; la RAM en
  `$A000`); los puertos del SMS (mandos y espejos, la detección de exportación por TH, START como botón 1, la E/S
  apagada) y del GG (`$00`, `$06`, los espejos que no tiene, el modo SMS); el VDP (comando, búfer, registros, la
  CRAM del SMS y la del GG con su latch, el estado); los contadores V (192 y 224 líneas) y H (congelado por TH);
  la bandera de cuadro en `$C1` y `$E1`; la interrupción de línea cada R10 + 1 líneas; la línea IRQ; y programas de
  Z80 con una interrupción IM 1 por cuadro y la NMI de la Pausa, una por cada vez que se aprieta (y no en el GG en
  modo GG).
- **Z80 con el bus:** ZEXDOC y ZEXALL, sus 79 pruebas en OK.
- **Estados guardados:** `statetest` con Waternet (SMS y GG) y Dragon Blaster.
- **Juegos:** los cinco homebrew de `make testroms` encienden la pantalla y atienden una interrupción por cuadro
  (y las de línea los que las usan). Con los juegos de `roms/` (y sus subcarpetas, si están): `smsrecomp --info` y
  1200 cuadros con Start en los que encienden la pantalla y atienden al menos una interrupción cada dos cuadros.
  Probado con 15 de EE. UU.: Alex Kidd in Miracle World, Ghost House, OutRun, Phantasy Star, Sonic the Hedgehog,
  Space Harrier, Teddy Boy, TransBot y Wonder Boy III (SMS); Columns, Micro Machines (Codemasters), Shining Force:
  The Sword of Hajya, Shinobi, Sonic Chaos y Sonic the Hedgehog (GG). Phantasy Star y Shining Force guardan su
  partida en la RAM del cartucho y la vuelven a cargar (`smsrun --sram`).

**Todavía no:** todo lo demás (ver [PLAN.md](PLAN.md)).

### Línea base para optimizar el runtime

Llega en la fase 4 (`tests/golden.sh`: hash de todos los cuadros y de todo el audio de cada ROM).
