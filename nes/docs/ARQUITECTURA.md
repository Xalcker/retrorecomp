# Arquitectura de NESRecomp

NESRecomp toma una ROM de NES (iNES) y genera un proyecto C99 que se compila en un ejecutable
nativo. El ejecutable carga la ROM del usuario en tiempo de ejecución y valida su SHA1: nunca se
distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de GBRecomp: lo
que cambia entre las dos consolas queda dentro de cada una.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `nes.c` (cartucho iNES y NES 2.0, bus, reloj, DMA, joypads, RAM con batería, estados, traza), `cpu6502.c` (intérprete), `cpu_ops.h` (tabla de opcodes y semántica de cada operación), `ppu.c`, `apu.c`, `mapper.c`, `vrc7.c` (el OPLL del VRC7), `recomp.h` (macros del código generado). |
| `src/nesrecomp/` | El recompilador: descubrimiento, partición en funciones, emisión de C y del proyecto. Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | SDL2 (`main_sdl.c`, menús y diálogos en `ui_menu`/`ui_font`/`i18n`, `settings`, `files`, `export`, `png`, `inputs.h`), `difftest.c`, `bench.c`. Se copian a cada proyecto generado. |
| `src/tools/` | `nestest`, `nesshot` (captura sin ventana), `nesaudio` (WAV), `nestrace` (traza), `nesramdump`, `sramtest`, `statetest`, `mappertest`, `inputtest`, `embed`. |
| `tests/` | `run_tests.sh` (la batería de `make test`: nestest, Blargg, recompilador, interfaz), `recomp_game.sh` (recompila un juego, prueba con varias semillas y recompila con lo que cayó al intérprete), `golden.sh` y `golden.txt` (línea base exacta), `coverage.sh`, `contract.sh`, `accuracycoin.py`. |
| `tools/` | `retrorecomp/` (la GUI común con GBRecomp, submódulo), `oracles/` (emuladores de referencia que se descargan al usarlos y dan la misma salida que `nesramdump`: `tricnes.py`, la referencia ciclo por ciclo, y `mesen.py`, Mesen2 sin ventana) y `compat/` (prueba de compatibilidad con una colección de ROMs: `scan.py`, `run.py`, `report.py`; con `uv`). Resultados en `docs/compat/` (ver [COMPATIBILIDAD.md](COMPATIBILIDAD.md)). |
| `testroms/` | ROMs de prueba de código abierto (`make testroms`; no se versionan). |

## Una sola fuente de verdad para el CPU

`cpu_ops.h` define la tabla de opcodes y la semántica de cada operación (`op_read`, `op_rmw`,
`op_store`, `op_implied`, `op_branch_taken`). El intérprete las llama con valores de tiempo de
ejecución y el código generado con constantes, que el compilador pliega. La secuencia de accesos
al bus de cada modo de direccionamiento sí está escrita dos veces (`cpu6502.c` y el emisor); el
`difftest` la vigila.

## Temporización

Cada acceso al bus del CPU, incluidas las lecturas y escrituras falsas, avanza un ciclo de la consola:
3 puntos de PPU y 1 de APU. Los ciclos de cada instrucción salen de los accesos, no de una tabla.
NMI e IRQ se muestrean en el penúltimo ciclo de cada instrucción (el NMI es un borde, el IRQ un
nivel) y un NMI que llega mientras se empujan los registros secuestra el vector. Verificado con
`nestest` (incluida la posición de la PPU), las pruebas de temporización de Blargg y `ppu_vbl_nmi`.

**DMA** (`dma_run` en `nes.c`, mismo modelo que TriCNES). Una DMA detiene al CPU en su próxima lectura
y avanza ciclo a ciclo alternando get (ciclos pares, lecturas) y put (impares). Recién pedida empieza
detenida hasta que pasa un get; esos ciclos repiten la lectura del CPU. La del DMC lee su byte en el
primer get libre (3 o 4 ciclos); la de OAM lee en los get y escribe en los put (513 o 514). Juntas, el
DMC tiene prioridad en los get y la OAM en los put. Del lado del APU (`apu.c`): la DMA de carga sale dos
ciclos put después de encender el DMC con `$4015`; el bit del DMC se aplica 3-4 ciclos después de
escribirlo (5-6 si la DMA está por salir); si se apaga mientras su DMA está detenida, la DMA se aborta
pero queda pendiente (`dmc_left`) y sigue al volver a encenderlo; y encenderlo justo antes de un reloj
del DMC produce una DMA de un ciclo (aborto implícito). Los registros del APU solo están activos con el bus del
CPU en `$4000-$401F` (`dma_read`): una DMA que lee la página `$40` con el CPU en otra parte ve bus abierto. Si el
CPU quedó detenido ahí, responden en cualquier dirección: leer `$xx15` lee también `$4015` (borra el IRQ del frame
counter; la OAM recibe el estado) y `$xx16`/`$xx17` avanza el control, que deja su bit en el bus (el fallo de la
DPCM con los controles). El strobe de `$4016` lo toma el APU en los ciclos get: un 1 escrito en un put y quitado
en el ciclo siguiente no latchea el control.

### Rendimiento: ejecución perezosa

La PPU y el APU no avanzan en cada ciclo de CPU sino cuando hace falta; el resultado es idéntico bit a bit
(`tests/golden.sh` compara el hash de la imagen y del audio de 16 casos con una línea base).

- **PPU** (`ppu_sync`): avanza cuando el CPU toca sus registros, un registro del mapper o el DMA de OAM, y
  cuando va a pasar algo visible desde fuera (bandera de vblank, fin de cuadro: `event_dot`). En las líneas
  visibles va de 8 en 8 puntos (`ppu_tile`, y `ppu_tile_bg` si no hay sprites: cada pixel sale del bit que le
  toca de los registros, sin desplazarlos punto a punto), salta los puntos 258-320 y avanza de golpe el
  vblank. Con el MMC3 (su IRQ depende de las lecturas de la PPU) va ciclo a ciclo solo a partir del punto 256 de las líneas
  con dibujo y en la pre-render (`set_event`); las baldosas reproducen los eventos de A12 y no se usan si una subida
  que cuenta podría quedar dentro de una. Durante el DMA de OAM avanza en cada ciclo.
- **APU** (`apu.c`): entre dos eventos (un temporizador que vence, un paso del frame counter, una muestra
  de salida) nada cambia y esos ciclos se saltan de golpe. Solo se pone al día al acceder a sus registros,
  al leer el audio y cuando algo visible puede pasar (IRQ, DMA del DMC; `deadline`). Los canales inaudibles
  (triángulo con periodo 0, ruido sin volumen...) también se adelantan de golpe: solo se actualiza su estado interno.
- **Bus**: lectura de ROM y RAM por tablas (`prg_map`, `chr_page`); en el código recompilado los ciclos sin
  acceso (`TICKS(k)`) se agrupan.

Super Mario Bros. corre a ~55x tiempo real en el código recompilado y ~42x en el intérprete (antes 13x y 11x).

## Interrupciones y despachador

`nes_run_frame` corre hasta que la PPU termina el cuadro. En cada vuelta busca la función
recompilada que contiene el PC (`recomp_lookup`); si no hay (código en RAM o que el descubrimiento
no encontró, o `--interp`), ejecuta una instrucción con el intérprete (`cpu_step`) y vuelve a
buscar. Así el código recompilado y el intérprete se alternan sin perder un ciclo.

`recomp_lookup` (generado en `gen/dispatch.c`) traduce el PC a desplazamiento de PRG con el mapeo
actual (`mapper_prg_offset`) y busca en la tabla de funciones ordenada por desplazamiento, empezando
por la primera función de esa página de 256 bytes (`page_first`). Ver "Identidad de las
instrucciones".

Dentro de una función, las macros de `recomp.h` deciden cuándo volver al despachador:

- `FRAME(pc)`, al empezar cada instrucción: si la PPU terminó el cuadro, guarda el PC y sale.
- `ICHK(pc)`, al terminar cada instrucción: si hay un NMI o un IRQ listo (`prev_need_nmi`,
  `prev_run_irq`, muestreados en el penúltimo ciclo como en el intérprete), guarda el PC y atiende
  la interrupción (`nes_interrupt`) antes de salir.
- `LEAVE(pc)`, en un salto fuera de la función (`jsr`, `rts`, `rti`, `jmp` fuera): igual, pero
  siempre sale.
- `MAPCHK(pc)`, tras una escritura que pudo cambiar el mapeo de PRG (ver "Código generado").

Los IRQ por ciclos de algunos mappers (FME-7, Jaleco, Namco 163) guardan en `cart.irq_cycle` el
ciclo en que se levantan, y `nes.c` lo compara en cada acceso al bus.

## Recompilador

```
nesrecomp juego.nes -o salida/ [--trace traza.txt] [--name nombre]
```

### Identidad de las instrucciones

Una instrucción se identifica por su **desplazamiento en la ROM de PRG**, no por su dirección del
CPU. Así el mismo código sirve con cualquier banco: `recomp_lookup(n, pc)` traduce el PC con el
mapeo actual (`mapper_prg_offset`) y busca la función que contiene ese desplazamiento. La función
empieza con `switch (c->pc)`; si el PC no coincide con la dirección con la que se decodificó
(otro mapeo, un espejo de NROM-128), ejecuta una instrucción en el intérprete.

### Descubrimiento

Recorrido recursivo desde los vectores NMI/RESET/IRQ, siguiendo saltos, `jsr` y bifurcaciones con
destino constante (las llamadas se asumen con regreso). Un destino en la misma ventana de banco se
resuelve por aritmética; en otra ventana, solo si es fija en el mapeo del reset
(`mapper_prg_window`). Los opcodes no oficiales no se siguen por flujo (casi siempre son datos). Lo
siguiente cubre lo que el flujo constante no alcanza, sin necesitar una traza:

- **Descubrimiento especulativo multibanco.** Un destino en una ventana intercambiable al que se
  llega desde otra ventana (típicamente un `jsr $8xxx` desde el banco fijo) no dice qué banco habrá.
  Se prueba en todos los bancos y se conservan los que `plausible()` acepta: una secuencia de opcodes
  oficiales, con destinos de salto dentro de la ROM y operandos de E/S coherentes, que termina en
  RTS/RTI/JMP o dura lo bastante. Los datos al azar casi nunca pasan.
- **Despachadores con tabla en línea** (el `JumpEngine` de Super Mario Bros.): una rutina que saca la
  dirección de retorno de la pila (dos `pla`), la usa de puntero (`lda (zp),y`) y salta con
  `jmp (ind)`. Tras cada `jsr` a ella va una tabla de direcciones, no código: se lee la tabla y no se
  sigue de largo.
- **Tablas indexadas**: `lda tabla,x / sta ptr / lda tabla+1,x / sta ptr+1 / jmp (ptr)` (palabras
  seguidas o arreglos de bajos y altos), y el truco `lda alto,x / pha / lda bajo,x / pha / rts` (las
  direcciones van menos uno, también con constantes). Se mira el historial de las últimas
  instrucciones del recorrido.
- **Pares de cargas inmediatas** (`lda #<f / sta ptr / lda #>f / sta ptr+1`) cuando `ptr` es el
  operando de algún `jmp (ptr)`.
- **Tablas sueltas**: cuatro o más palabras seguidas que apuntan, todas, a algo que parece rutina.

`--coverage traza` mide cuántas de las instrucciones que ejecutó un juego encuentra esto sin ayuda
(`tests/coverage.sh` lo hace para los juegos de `roms/`): 94-100 % en los ocho, frente a 3-100 % con
solo el flujo constante. `--no-tables` y `--no-spec` apagan las heurísticas para comparar.

`--why`, `--viz`, `--html` (visor interactivo de una sola página) y `--events` explican el resultado: cada instrucción guarda de
qué instrucción se llegó (`parent`), cómo (`how`) y su origen (flujo, traza, tabla, especulativo).
`--codemap` escribe qué bytes del PRG se tomaron como código, una letra por byte (`C`), 64 por línea:
el mismo formato que `difftest --executed` y que `gbrecomp --codemap`, para comparar lo descubierto
con lo que el juego ejecutó.

`--trace` agrega las instrucciones que ejecutó el juego (`OFFSET:DIRECCION`, que escribe `nestrace`, la
ventana con `--trace`/`--uncovered`, o `nes` con `NES_TRACE`) como puntos de entrada: es lo que falta
cuando alguna heurística no alcanza.

Descubrir de más no rompe nada, solo agrega código que nunca se ejecuta (SMB3 pasa de 5 mil a 69 mil
instrucciones, 13 s de compilación con `make -j8`); de menos tampoco: lo que falta, o está en RAM, lo
ejecuta el intérprete (`nes_run_frame` alterna entre código recompilado e intérprete).

### Código generado

```c
/* PRG $000000-$0000FF (60 instrucciones) */
void f_000000(NES *n)
{
    Cpu *c = &n->cpu;
    u32 ver0 = n->cart.map_ver; (void)ver0;
    switch (c->pc) { case 0x8000: goto L_8000; ... default: cpu_step(n); return; }
L_8000: /* lda #$10 */
    FRAME(0x8000);                  /* ¿terminó el cuadro? */
    { TICK(); TICK(); op_read(c, LDA, 0x10); }
    ICHK(0x8002);                   /* ¿interrupción lista? */
    ...
}
```

- `TICK()` es un ciclo sin acceso al bus: sustituye a las lecturas de ROM y a las lecturas falsas
  sin efecto (RAM, ROM). Las lecturas falsas que pueden tocar registros de E/S (al cruzar página
  con índices) se mantienen como `RD`.
- El PC solo se escribe al salir de la función o al atender una interrupción. Un salto con destino
  constante dentro de la función es un `goto`; los demás (`jsr`, `rts`, `rti`, `jmp` fuera) salen al
  despachador. Pila, `jsr`/`rts` y `rti` usan la pila emulada.
- En funciones de ventanas intercambiables, tras una escritura que puede llegar a `$8000-$FFFF`,
  `MAPCHK` revisa `cart.map_ver` (sube con cada escritura al mapper) y sale al despachador si el
  mapeo cambió.
- Las funciones no cruzan un múltiplo del tamaño mínimo de ventana (`mapper_prg_granule`).

### Proyecto generado

`gen/` (código y despachador), `runtime/` (copia del runtime), `platform/`, `Makefile`, `LEEME.txt`.
`make` produce `nombre` (el juego), `difftest` y `bench`.

- `./nombre [rom.nes]`: sin argumentos busca en la carpeta un `.nes` con el SHA1 esperado.
- `./difftest rom.nes [--frames N] [--input f] [--seed S] [--uncovered f] [--screens d] [--executed f] [--bench]`: corre el
  código recompilado y el intérprete lado a lado (`--seed` suma un mando aleatorio; `--uncovered`
  guarda las instrucciones de PRG que tuvo que interpretar, como traza para `nesrecomp --trace`); tras cada función, el intérprete avanza hasta el mismo ciclo y se
  comparan CPU, RAM, PPU, APU, mapper y, al final de cada cuadro, imagen y audio.
- `./bench rom.nes [cuadros]`: velocidad sin ventana.

## Bancos, mappers y guardado

### Mappers

El proyecto generado se compila con `-DNES_MAPPER=<n>`: `MAPPER(c)` y las macros `CART_MMC3/MMC5/CHRHOOK/LOWREGS/EXGFX` (nes.h) pasan
a ser constantes y el compilador descarta el resto. `nes_load` rechaza una ROM con otro mapper.

0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3 (IRQ por flanco de A12 con filtro), 7 AxROM, 11, 34, 66, 71, 79, 87, 140, 180, 206, 9, 10, 69, 118, 119, 5,
13 CPROM, 18 Jaleco SS88006, 19 Namco 163 (con su audio), 21, 22, 23 y 25 VRC2/VRC4, 24 y 26 VRC6 (con su audio), 85 VRC7 (con su audio FM), 75 VRC1, 95, 115 Kasheng, 184 Sunsoft 1 y 210 Namco 175/340.

Un mapper nuevo es un caso en `mapper_supported`, `mapper_reset`, `prg_offset_slow` (de donde sale `prg_map`), `mapper_cpu_write` (y `low_write`
si tiene registros por debajo de `$8000`, con `low_regs` y `CART_LOWREGS`), `chr_off` y las consultas del recompilador (`mapper_prg_window`,
`mapper_prg_granule`), más una prueba sintética en `src/tools/mappertest.c`.

**VRC2/VRC4** (21, 22, 23, 25): las dos líneas de registro llegan por patas distintas según el cartucho; con el
submapper de NES 2.0 se decodifica la variante exacta y sin él se juntan las dos del mapper con un OR. El IRQ del
VRC4 (`vrc_sync`/`vrc_arm`, con `irq_cycle` como el FME-7) cuenta por ciclos o por líneas (prescaler de 341/3) y
empieza en el ciclo siguiente a la escritura de `$F002`. Coincide ciclo a ciclo con Mesen2 en una ROM sintética
con las seis variantes de cableado, y Gradius II, TMNT y Parodius Da! (Japan) dan la misma imagen que Mesen2.

**VRC6** (24, 26; el 26 cruza A0 y A1): 16 KB de PRG en `$8000`, 8 KB en `$C000`, CHR de 1 KB (solo el modo de
bancos 0, el habitual), espejado en `$B003` y el mismo IRQ del VRC4 con el latch entero en `$F000`. Su audio (dos
pulsos de 8 anchos y un diente de sierra, `apu.c`) va en `NES.vrc6` y se suma a la mezcla (un pulso a volumen 15
suena ~1.5 veces un pulso del 2A03 a 15, como en Mesen2). `advance` no deja que un salto en bloque pase un vencimiento de sus divisores.

### Cartucho

`nes_load` lee la cabecera iNES y NES 2.0 (tamaños con MSB o en notación exponente-multiplicador y los bits altos del mapper; no el submapper).
Un PRG de 8 KB se repite hasta 16 KB. La RAM con batería sin estrenar se inicializa a `$FF`: con ceros, Kid Dracula interpreta la RAM como una
partida válida y se cuelga.

### Guardado

- **RAM con batería:** `<juego>.sav` en la carpeta de datos del juego; se guarda al salir y cada 5 s
  si cambió. Sin partida previa se lee como `$FF` (ver arriba).
- **Estados** (`nes_state_save` / `nes_state_load`, cuatro ranuras: `<juego>.estado1-4`): la
  estructura `NES` completa, más la CHR RAM si la hay, con una cabecera que identifica la ROM
  (mapper, tamaños y un hash del PRG). Al cargar se rehacen los punteros del mapeo
  (`mapper_remap`), porque los guardados eran de otra ejecución. `statetest` lo prueba en
  `make test`.

### La ROM se pide una sola vez

Un juego recompilado busca la ROM, en orden: el argumento, la copia guardada en la carpeta de datos,
`rom.nes` junto al ejecutable o en la carpeta actual, y un `.nes` de la carpeta actual con el SHA1
correcto. Si no está, la pide con el diálogo de abrir archivo (Windows) o arrastrándola a la ventana.
Una ROM válida se copia a la carpeta de datos para las siguientes veces (`--forget-rom` la borra).

Carpeta de datos (la da SDL): en Windows `%APPDATA%\NESRecomp\<juego>\`, en Linux
`~/.local/share/NESRecomp/<juego>/` y en macOS `~/Library/Application Support/NESRecomp/<juego>/`.
Ahí van también las preferencias (`settings.ini`), la paleta propia (`paleta.pal`) y lo exportado.

## Video y audio

### PPU

2C02 punto por punto: fondo, sprites con su evaluación por línea, scroll, sprite 0 hit y los eventos
de A12 que necesita el IRQ del MMC3. Pasa las 11 `sprite_hit_tests` y las 10 `ppu_vbl_nmi`. Avanza
de forma perezosa (ver "Rendimiento: ejecución perezosa"). Los colores salen de una paleta de 64
entradas, con énfasis: la predeterminada, grises, saturada o un `paleta.pal` propio
(`ppu_load_palette`). El MMC5 agrega su ExGrafix (`CART_EXGFX`); falta su pantalla dividida.

### APU

Dos pulsos, triángulo, ruido, DMC y frame counter con IRQ (`src/runtime/apu.c`). La mezcla es la no
lineal del hardware (tablas `pulse_mix` y `tnd_mix`) y la salida pasa por los filtros de la consola:
paso alto de 90 y 440 Hz y paso bajo de 14 kHz. Sale en mono a 44 100 Hz. Pasa las 8 `apu_test` y las
pruebas viejas del frame counter. El MMC5 suma sus dos pulsos y su PCM; el VRC6, sus dos pulsos y su diente de
sierra; el 5B del FME-7, sus tres tonos, ruido y envolvente (como un YM2149 a la mitad del reloj del CPU, con
volumen de 32 niveles de 1.5 dB); el Namco 163, de 1 a 8 canales con ondas de 4 bits en su RAM de 128 bytes, uno
actualizado cada 15 ciclos, y la salida es la media de los canales (el chip los muestra de a uno). Los niveles
de los tres están calibrados como Mesen2 (`VRC6_UNIT`, `S5B_UNIT`, `N163_UNIT`). El VRC7 (`vrc7.c`) es un OPLL
propio: 6 canales FM de dos operadores, 15 instrumentos de fábrica (volcado de Nuke.YKT) y uno programable, una
muestra cada 36 ciclos. Las constantes que la documentación deja abiertas (velocidad de la envolvente, ataque,
realimentación) están ajustadas contra emu2413 con los 15 instrumentos en tres notas, y el nivel también
(`VRC7_UNIT`). Su estado va al final de `NES`;
`advance` no deja que un salto en bloque pase un evento del VRC6 o del 5B, y el Namco 163 se suma por tramos
dentro del salto. Avanza de forma perezosa (ver "Rendimiento: ejecución perezosa").

## Variantes de la consola y accesorios

Solo se emula la NES NTSC (60.0988 cuadros por segundo); no hay modo PAL. Lo que se conecta a la
consola:

- **Dos mandos** (teclado y mandos de juego) y el **Four Score** (cuatro jugadores, con mandos).
- **Zapper:** el ratón apunta y el clic izquierdo dispara; la pistola lee la luz de la pantalla en
  ese punto (`ppu_light_at`). Sin ventana, `nesshot` lo simula con `zapper:X,Y,desde-hasta`.
  Verificado con Duck Hunt.

## Interfaz del ejecutable

La misma para el intérprete (`nes`) y para los juegos recompilados (`main_sdl.c`, con
`-DNES_RECOMP` en el proyecto generado): ventana con barra de menús, controles, audio y ritmo de
60.0988 cuadros por segundo. La barra de menús y los diálogos vienen de Moskva 89 y GBRecomp
(`ui_menu.c`, `ui_font.c`): se dibujan dentro de la ventana para verse igual en todos los sistemas,
en español o inglés (`i18n.c`). Con un menú o un diálogo abierto, el juego se pausa.

- **Juego:** estados guardados (4 ranuras), pausa y avance rápido (Tab).
- **Video:** tamaños 1x-6x, pantalla completa, escala entera, aspecto 8:7, overscan y paletas.
- **Audio:** volumen y silenciar cada canal del APU (tecla M = silencio).
- **Controles:** 2 jugadores, Four Score y Zapper (ver "Variantes de la consola y accesorios").
  Jugador 1: flechas, Z = B, X = A, Enter = Start, Mayús/Retroceso = Select. Jugador 2: WASD,
  F = B, G = A, C = Start, V = Select.
- **Exportar:** captura PNG y grabación de audio WAV.

Opciones: `juego [rom.nes] [--scale N] [--interp] [--force] [--forget-rom] [--record archivo |
--play archivo] [--uncovered archivo]`. `--interp` ejecuta todo con el intérprete (para comparar);
`--force` acepta una ROM con otro SHA1 (solo con el intérprete); `--record` y `--play` graban y
reproducen los botones (`inputs.h`, el mismo formato que `--input` de las herramientas sin ventana);
`--uncovered` agrega al salir las instrucciones que tuvo que ejecutar el intérprete, como traza para
`nesrecomp --trace`. Para pruebas sin ventana: `--ui-shot archivo.png` con `--ui-frames N`,
`--ui-menu N` y `--ui-action N`.

## Empaquetado y pruebas

- **`make dist`:** deja `dist/nesrecomp-<versión>-<sistema>.tar.gz` con `nesrecomp`, el README, la
  licencia y este documento. La versión sale de `git describe`.
- **Proyecto generado:** compila con el mismo Makefile en Windows (MSYS2, SDL2 enlazado de forma
  estática e icono del juego: no necesita DLLs), Linux y macOS (SDL2 por pkg-config). `make dist` en
  el proyecto deja el ejecutable en `dist/`.
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en GBRecomp), en Linux,
  macOS y Windows:
  - `make` (compila todo) y `sh tests/contract.sh` (contrato común).
  - `make testroms` (christopherpow/nes-test-roms en un commit fijo) y `make test`, que también
    recompila tres ROMs de prueba y las compara con `difftest`.
  - `make dist` y publica el paquete como artefacto; con una etiqueta `vX.Y.Z`, además crea el
    release con los paquetes de los tres sistemas.

### Prueba diferencial

`difftest` (en cada proyecto generado; ver "Proyecto generado") corre el código recompilado y el
intérprete lado a lado y compara todo el estado de la consola, la imagen y el audio. `--seed S` suma
un mando aleatorio reproducible y `--uncovered` guarda las instrucciones de PRG que tuvo que
interpretar, como traza para `nesrecomp --trace`. Como en GBRecomp, `--screens carpeta --every N`
guarda capturas PNG, `--executed archivo` escribe el mapa de bytes de PRG ejecutados (lo registra la
traza de la máquina que solo usa el intérprete; cada instrucción marca su opcode y sus operandos) y
`--bench` mide la velocidad del recompilado y del intérprete por separado.

`tests/recomp_game.sh rom nombre [cuadros] [semillas...]` (los mismos argumentos que en GBRecomp) hace el
ciclo completo: traza guionada (`TRACE_FRAMES`), recompila, compila, corre
`difftest` con varias semillas, junta lo que cayó al intérprete, recompila con esa traza y vuelve a
probar.

### Resultados con juegos

Con una traza, Super Mario Bros., Donkey Kong, Arkanoid, Contra, Castlevania, Mega Man, Super Mario
Bros. 3 y Zelda corren 1500 cuadros con 100 % de los ciclos en código recompilado (Zelda 99.7 %) e
idénticos al intérprete (CPU, RAM, PPU, APU, mapper, imagen y audio). Sin traza, solo con el análisis
estático, los mismos juegos corren con 97-100 % de los ciclos recompilados e idénticos al intérprete.
La prueba de compatibilidad con 732 ROMs de No-Intro está en [COMPATIBILIDAD.md](COMPATIBILIDAD.md).

## Estado de la validación

`make test` (`tests/run_tests.sh`):

- CPU: `nestest` (incluida la posición de la PPU), las 16 `instr_test-v5`, `instr_misc`,
  `instr_timing` y `cpu_interrupts_v2` 1, 2, 3, 4 y 5.
- Mappers: `mmc3_test_2` (con la revisión vieja del MMC3 en `NES_MMC3_ALT=1`) y `mappertest`
  (sintética).
- APU: las 8 `apu_test`. PPU: las 10 `ppu_vbl_nmi`.
- RAM con batería, estados y entradas grabadas (`sramtest`, `statetest`, `inputtest`).
- Recompilador: nestest, instr_test 01-basics y ppu_vbl_nmi 01-vbl_basics recompiladas y comparadas
  con `difftest`; `--why`, `--viz`, `--events` y `--html`.
- Interfaz: cada menú y cada diálogo se dibuja sin fallar (`--ui-shot`).

AccuracyCoin (`tests/accuracycoin.py`, opcional): 128 de 149. Faltan, sobre todo, el comportamiento
por punto de la PPU (lecturas de `$2004`/`$2007` con el dibujo activo, activar o desactivar el dibujo
en puntos concretos, la OAM2), y detalles del bus
abierto de la PPU y del APU. `tests/accuracycoin.py --compara` lista las diferencias con TriCNES (que pasa todas).

### Línea base para optimizar el runtime

`tests/golden.sh` corre varios juegos y ROMs de prueba con entradas fijas y compara el hash de la
imagen final y de todo el audio con `tests/golden.txt`: una optimización no debe cambiar ni un byte
(`--update` reescribe la línea base). Es lo que respalda la ejecución perezosa de la PPU y el APU
(ver "Rendimiento: ejecución perezosa").

`nesshot --hash` imprime un hash de todos los cuadros y de todo el audio, como `gbtest --hash`, que
es lo que usa el `golden.sh` de GBRecomp. El de NES todavía compara el md5 de la captura final y del
WAV (`nesshot` y `nesaudio`); pasarlo a `--hash` cambia `tests/golden.txt`, así que hay que
regenerarlo con todos los juegos de `roms/` a la mano.
