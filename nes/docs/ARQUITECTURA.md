# Arquitectura de NESRecomp

NESRecomp toma una ROM de NES (iNES) y genera un proyecto C99 que se compila en un ejecutable
nativo. El ejecutable carga la ROM del usuario en tiempo de ejecución y valida su SHA1: nunca se
distribuyen datos de la ROM. Sigue el diseño de GBRecomp.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `nes.c` (cartucho iNES, bus, reloj, DMA, joypads, traza), `cpu6502.c` (intérprete), `cpu_ops.h` (tabla de opcodes y semántica de cada operación), `ppu.c`, `apu.c`, `mapper.c`, `recomp.h` (macros del código generado). |
| `src/nesrecomp/` | El recompilador: descubrimiento, partición en funciones, emisión de C y del proyecto. |
| `src/platform/` | SDL2 (`main_sdl.c`), `difftest.c`, `bench.c`. Se copian a cada proyecto generado. |
| `src/tools/` | `nestest`, `nesshot` (captura sin ventana), `nesaudio` (WAV), `nestrace` (traza), `sramtest`, `embed`. |
| `tests/` | `run_tests.sh` (nestest, Blargg, recompilador), `recomp_game.sh` (recompila un juego y lo compara). |

## Temporización

Cada acceso al bus del CPU, incluidas las lecturas y escrituras falsas, avanza un ciclo de la consola:
3 puntos de PPU y 1 de APU. Los ciclos de cada instrucción salen de los accesos, no de una tabla.
NMI e IRQ se muestrean en el penúltimo ciclo de cada instrucción (el NMI es un borde, el IRQ un
nivel) y un NMI que llega mientras se empujan los registros secuestra el vector. El OAM DMA corre
como 513/514 ciclos reales. Verificado con `nestest` (incluida la posición de la PPU), las
pruebas de temporización de Blargg y `ppu_vbl_nmi`.

## Rendimiento: ejecución perezosa

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

## Una sola fuente de verdad para el CPU

`cpu_ops.h` define la tabla de opcodes y la semántica de cada operación (`op_read`, `op_rmw`,
`op_store`, `op_implied`, `op_branch_taken`). El intérprete las llama con valores de tiempo de
ejecución y el código generado con constantes, que el compilador pliega. La secuencia de accesos
al bus de cada modo de direccionamiento sí está escrita dos veces (`cpu6502.c` y el emisor); el
`difftest` la vigila.

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
- `./difftest rom.nes [cuadros] [boton:desde-hasta ...]`: corre el código recompilado y el
  intérprete lado a lado; tras cada función, el intérprete avanza hasta el mismo ciclo y se
  comparan CPU, RAM, PPU, APU, mapper y, al final de cada cuadro, imagen y audio.
- `./bench rom.nes [cuadros]`: velocidad sin ventana.

## Mappers

0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3 (IRQ por flanco de A12 con filtro), 7 AxROM, 11, 34, 66, 71, 79, 87, 140, 180, 206, 9, 10, 69, 118, 119 y 5. Un mapper
nuevo es un caso en `mapper_prg_offset`, `mapper_cpu_write`, `chr_off` y las consultas del
recompilador (`mapper_prg_window`, `mapper_prg_granule`).
