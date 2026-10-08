# Arquitectura de GBRecomp

GBRecomp toma una ROM de Game Boy (DMG) y genera un proyecto C99 que se compila
en un ejecutable nativo. El ejecutable carga la ROM del usuario en tiempo de
ejecución (validando su SHA1): nunca se distribuyen datos de la ROM.

## Piezas

```
gbrecomp (herramienta)                proyecto generado
──────────────────────                ─────────────────
carga ROM → descubre código           gen/bankNN_*.c   código recompilado
 ├ recursivo desde $100, IRQ, RST     gen/dispatch.c   (banco, PC) → función
 ├ trazas de ejecución                runtime/         CPU, bus, MBC, PPU, APU,
 └ propagación de constantes                           timer, serial, joypad
emite C99 + Makefile                  platform/        SDL2
```

| Directorio | Contenido |
|---|---|
| `src/emit/` | Decodificador, desensamblador y **emisor** de C del SM83: la única fuente de verdad de la semántica y la temporización por M-ciclo. |
| `src/runtime/` | El hardware: `gb.c` (bus, sincronización, interrupciones, despachador, DMA, serial, joypad), `timer.c`, `ppu.c`, `apu.c`, `mbc.c`, `interp.c`. |
| `src/gbrecomp/` | El recompilador: descubrimiento, partición en funciones, emisión del proyecto. |
| `src/platform/` | Se copia a cada proyecto generado: `main_sdl.c` (ventana, controles, audio) y `difftest.c` (prueba diferencial). |
| `src/tools/` | `gen_interp` (genera el intérprete), `gbtest` (corre ROMs de prueba sin ventana), `png.c`. |
| `tests/` | `run_tests.sh`: Blargg, Mooneye y dmg-acid2. `inputs/`: guiones de entradas. |
| `tools/` | `truth/`: mapa de código y datos desde un desensamblado y comparación; `gen_inputs.pl`: jugadas reproducibles. |
| `testroms/` | ROMs de prueba de código abierto (no se versionan). |

## Una sola fuente de verdad para el CPU

`sm83_emit.c` decide, para cada opcode, qué operaciones hace y en qué orden
ocurre cada M-ciclo (lectura del opcode, operandos, accesos a memoria y ciclos
internos). Las operaciones en sí están en `cpu_ops.h`.

- `gen_interp` usa el emisor en **modo intérprete** (operandos leídos en tiempo
  de ejecución, PC dinámico) y produce `build/gen/interp_ops.inc`.
- `gbrecomp` usará el emisor en **modo recompilado** (operandos y PC
  constantes).

Así el intérprete y el código recompilado no pueden discrepar en semántica ni
en temporización, y la prueba diferencial aísla errores del *recompilador*.

## Temporización

- `g->cyc` cuenta T-ciclos del CPU y siempre es múltiplo de 4.
- Cada acceso a memoria suma 4 ciclos y luego accede: el hardware ya avanzó
  hasta el final del M-ciclo del acceso (validado con `mem_timing` de Blargg).
- **Hardware perezoso:** timer, PPU, APU, serial y DMA solo avanzan en
  `gb_sync`, que se llama:
  - antes de cualquier acceso a I/O, VRAM, OAM, RAM de cartucho o durante DMA
    (la ruta lenta del bus);
  - cuando el CPU alcanza `g->next_event`, el primer ciclo en que el hardware
    podría cambiar IF por su cuenta (desbordamiento de TIMA, cambio de modo o
    línea de la PPU, fin de transferencia serial).
- Las lecturas de ROM y WRAM van por un mapa de páginas (`rpage`/`wpage`) sin
  sincronizar. Durante un DMA de OAM el mapa se vacía y todo pasa por la ruta
  lenta, que reproduce los choques de bus.

## Interrupciones y despachador

En cada límite de instrucción, si `g->cyc >= g->next_event`, el despachador
(`gb_run`) o el código recompilado (`gb_check`) sincroniza el hardware y
revisa IME/IE/IF. Las escrituras lentas, `EI` y `RETI` ponen `next_event = 0`
para forzar la revisión. El despacho de interrupción toma 5 M-ciclos y decide
el vector después de empujar el byte alto de PC (si esa escritura cambió IE,
salta a `$0000`).

`HALT` adelanta el reloj directo al siguiente evento. El bug de HALT (IME=0 con
interrupción pendiente) hace que la siguiente instrucción se ejecute en el
intérprete con el PC sin avanzar.

## Código recompilado (fase 2)

### Descubrimiento y funciones

`gbrecomp` decodifica recursivamente desde `$0100` y los vectores de
interrupción, siguiendo saltos, llamadas y `rst` con destino constante (las
llamadas se asumen con regreso). Con `--trace` agrega como puntos de entrada las
direcciones de una traza de ejecución (formato `BB:AAAA`); así se resuelven los
saltos indirectos (`jp hl`, tablas de `rst $28`).

Las instrucciones de cada banco se agrupan en funciones por tramos contiguos de
direcciones: se corta cuando hay más de 32 bytes sin código o al llegar a 1,500
instrucciones. Cada instrucción pertenece a una sola función. Si dos
decodificaciones se traslapan (saltos a mitad de instrucción), ambas existen,
cada una con su etiqueta.

### Formato

```c
/* Banco 00, $14E0-$1530 (25 instrucciones) */
void f00_14E0(GB *g)
{
    Cpu *c = &g->cpu;
    switch (c->pc) {                 /* se puede entrar en cualquier instrucción */
    case 0x14E0: goto L_14E0;
    ...
    default: gb_interp_step(g); return;
    }
L_14F3: /* cp $1D */
    CHECK(0x14F3);                   /* ¿evento de hardware? → al despachador */
    {
        TICK();                      /* lectura del opcode */
        TICK();                      /* lectura del operando */
        op_cp(c, 0x1D);
    }
L_14F5: /* jr nz, $1500 */
    ...
}
```

- `CHECK` al inicio de cada instrucción revisa `g->cyc >= g->next_event`. Si
  hay que atender una interrupción o terminar la corrida, regresa con
  `c->pc` apuntando a esa instrucción, todavía sin ejecutar.
- Un salto con destino constante a una instrucción de la misma función se
  vuelve `goto`. Cualquier otro (`call`, `ret`, `jp hl`, `rst`, fuera de la
  función) asigna `c->pc` y regresa al despachador. CALL y RET usan la **pila
  emulada**, así que los trucos de pila funcionan.
- En funciones de bancos ≥ 1, después de cada instrucción que puede escribir en
  `$0000-$7FFF` se comprueba que el banco siga siendo el mismo. Si el código
  cambió su propio banco, regresa al despachador.
- `gen/dispatch.c` tiene tablas banco → página de 256 bytes → función. El
  código del banco 0 solo se usa si está mapeado en `$0000` y el de los demás
  bancos solo en `$4000`. Cualquier otra combinación va al intérprete.

### Proyecto generado

```
gbrecomp juego.gb -o salida/ [--trace traza.txt] [--name nombre]
cd salida && make
./nombre rom.gb          # el juego (SDL2); valida el SHA1 de la ROM
./difftest rom.gb        # prueba diferencial
```

`salida/` trae `gen/` (código generado), `runtime/` (copia del runtime),
`platform/` (SDL2 y difftest) y un `Makefile`. No contiene datos de la ROM.

### Prueba diferencial

`difftest` corre la misma ROM con código recompilado y con el intérprete, con
las mismas entradas pseudoaleatorias (`--seed`), y compara el estado completo al
final de cada cuadro: registros, ciclos, WRAM, HRAM, VRAM, OAM, I/O, timer,
PPU, banco y RAM del cartucho. Si hay diferencia, repite el cuadro instrucción
por instrucción y reporta la primera, con banco, dirección, opcode y el campo
que difiere:

```
Primera diferencia en el cuadro 401, instrucción 2943 del cuadro,
  tras ejecutar 00:14F3 (opcode $FE, recompilada)
  A: recompilado=FF intérprete=1C
```

Otras opciones: `--uncovered archivo` escribe las direcciones de ROM que ejecutó
el intérprete (para `gbrecomp --trace`), `--screens carpeta --every N` guarda
capturas y `--bench` mide la velocidad de cada máquina.

### Resultados con Tetris (JUE) (V1.1)

| | Solo estático | Con trazas de 7 sesiones |
|---|---|---|
| Instrucciones recompiladas | 2,189 | 4,321 |
| Ejecutado en código recompilado | 96.6 % | 98.9 % |
| Direcciones de ROM interpretadas | 1,239 | 0 a 99 según la semilla |

- **Sin diferencias** en más de 160,000 cuadros (13 sesiones de 3,000 a 20,000
  cuadros con semillas distintas). Las capturas confirman partidas reales.
- El 1.1 % que queda en el intérprete es la rutina de DMA que Tetris copia a
  HRAM: el código en RAM siempre va al intérprete.
- Se comprobó el localizador inyectando un error a mano (`cp` → `sub`):
  reportó la instrucción y el registro exactos.
- Velocidad (sin audio): recompilado 47x tiempo real, intérprete 44x. Casi
  todo el tiempo se va en el hardware, que se simula punto por punto. Optimizar
  `gb_sync` es lo que más rendiría.

## Descubrimiento de código (fase 3)

### Heurísticas genéricas

Además de seguir saltos y llamadas con destino constante, `gbrecomp` reconoce
dos formas comunes de salto indirecto. Ninguna depende de un juego en
particular:

1. **Despachador con tabla en línea** (el `rst $28` / `TableJump` de muchos
   juegos). Una rutina que, sin ramificarse, saca de la pila su propia
   dirección de retorno (`pop` antes de cualquier `push`) y termina en
   `jp hl` es un despachador. En cada llamada a ella, los bytes siguientes se
   leen como tabla de punteros a código, no como instrucciones.
2. **Tabla fuera de línea**: `ld hl, Tabla` seguido, a lo más 8 instrucciones
   después, de `jp hl` (normalmente con una llamada intermedia que lee la
   entrada).

El largo de una tabla no está escrito en ningún lado. La lectura se detiene al
llegar a código ya conocido o a un destino de salto, cuando la entrada no
apunta a ROM (o apunta a la cabecera, $0104-$014F) y, solo en tablas fuera de
línea, al llegar a una dirección que el código carga como constante, porque
ahí empieza otro objeto. Cada entrada se explora antes de leer la siguiente,
así el código que sigue a la tabla se conoce a tiempo para detenerse.

### Medición contra el desensamblado de Tetris

`tools/truth/` construye la "respuesta correcta" a partir de
[kaspermeerts/tetris](https://github.com/kaspermeerts/tetris):

1. `build_truth.sh` ensambla el desensamblado con RGBDS 0.6.1. Antes inserta
   una etiqueta marcadora delante de cada línea que emite bytes, clasificada
   como instrucción o dato (`instrument.pl`), y comprueba que la ROM
   resultante sea idéntica byte por byte a la original.
2. `make_truth.pl` convierte los marcadores del `.sym` en un mapa por byte
   (código, datos o relleno).
3. `compare.pl` compara contra `gbrecomp --codemap` o `difftest --executed` y
   lista los tramos faltantes con el nombre de la rutina que los contiene.

```
sh tools/truth/build_truth.sh build/ext/tetris-disasm build/ext/rgbds tetris.gb build/truth
gbrecomp tetris.gb -o out/tetris --codemap static.map
perl tools/truth/compare.pl build/truth/truth.txt static.map tetris-disasm/tetris.sym
```

Resultados (11,909 bytes de código según el desensamblado):

| Descubrimiento estático | Exhaustividad | Precisión |
|---|---|---|
| Solo saltos y llamadas | 29.1 % | 92.6 % |
| + tablas en línea (`rst $28`) | 94.7 % | 100 % |
| + tablas fuera de línea (efectos de sonido) | **99.0 %** | **100 %** |

Los 122 bytes que faltan son código muerto (`LookupTile`, `Unused_00D0`,
`StartRecordingDemo`, `Call_2A10` y `Call_2665` no tienen ninguna referencia),
ramas inalcanzables de pocos bytes, y la copia en ROM de `DMARoutine`, que solo
se ejecuta desde HRAM. Con esto, Tetris ya no necesita trazas: en las pruebas
diferenciales ninguna dirección de ROM cae al intérprete.

Una versión intermedia leía de más las tablas de efectos de sonido, hasta
meterse en los punteros a canciones: la precisión bajó a 93.1 %. La regla de
"constante cargada = otro objeto" lo corrigió.

### Entradas grabadas

`src/platform/inputs.h` define un formato de texto (`cuadro botones`). El
ejecutable graba con `--record` y reproduce con `--play`, y `difftest
--input` usa el mismo archivo. `tools/gen_inputs.pl` genera jugadas
pseudoaleatorias reproducibles para completar guiones escritos a mano. En
`tests/inputs/tetris_b.txt` hay un guion que entra al modo B y juega.

`sh tests/difftest_tetris.sh tetris.gb` regenera Tetris y corre la prueba
diferencial con todas las partidas de `tests/inputs/` y tres semillas.

### Cobertura de las pruebas

Lo que las pruebas ejecutan del código real (`difftest --executed`):

| Entradas | Código ejecutado |
|---|---|
| 3 sesiones aleatorias de 20,000 cuadros + guion del modo B | 53.2 % |
| + una partida real grabada (`tests/inputs/partida1.txt`, 29,963 cuadros) | **58.9 %** |

Casi todo lo que falta es el multijugador (pantallas de victoria y derrota,
cursores de Luigi, `Handshake`), que necesita un segundo Game Boy por cable:
habrá que conectar dos máquinas por un serial emulado. El código no ejecutado se generó con el mismo emisor que
el intérprete, así que sus instrucciones son confiables; lo que no se ha
probado es su flujo de control (etiquetas, cortes de función).

## Bancos, guardado y reloj (fase 4)

### Saltos entre bancos

Una llamada del banco 0 a `$4000-$7FFF` depende del banco activo, que el
análisis estático tiene que deducir. `gbrecomp` lleva, en cada recorrido
lineal, los **valores constantes de los registros** y el **último valor
escrito al registro de banco** (`$2000-$3FFF`). Ese banco conocido **viaja con
los saltos y llamadas** como contexto del destino; Wario Land II lo necesita,
porque fija el banco en un lugar y llama a `$4000` en otro.

Con eso se resuelven estas formas, todas genéricas:

1. **Cambio directo:** `ld a, N` → `ld [$2000], a` → `call $4xxx`.
2. **Comparación:** el banco sale de RAM (`ld a, [x]` → `ld [$2000], a`) y luego
   `cp N` + `jr nz`. Si el salto no se toma, A vale N, y como el banco se
   escribió desde A, también vale N.
3. **Rutinas especiales del banco 0**, reconocidas ejecutándolas con
   **registros simbólicos** ("lo que traía el registro al entrar"), siguiendo
   llamadas internas, la pila y celdas de RAM:
   - **Trampolín:** escribe en el registro de banco un valor que le llega en un
     registro y salta a `HL` (el `Bankswitch` de Pokémon Rojo, el `FarCall` de
     Oro, que además pasa el banco por RAM). En cada llamada con ese registro y
     `HL` constantes, el destino queda resuelto. También se descubre la
     dirección de regreso que empuja a la pila (`ld bc, nn` + `push bc` +
     `jp hl`).
   - **Fijador de banco:** escribe ese valor y regresa (el `rst $10` de Oro).
     Después de llamarla, el banco activo es ese valor.
   - **Saltador por tabla:** lee a través del `HL` con que la llamaron y termina
     en `jp hl` (el `rst $28` de Oro). `HL` apunta a una tabla de punteros.

Lo que se decide en tiempo de ejecución (p. ej., el motor de audio de Pokémon
Rojo se elige por descarte) lo cubren las trazas.

| Juego | Controlador | Solo estático | Con trazas de difftest |
|---|---|---|---|
| Super Mario Land | MBC1 | 92.7 % (todo lo de ROM) | — |
| Pokémon Rojo, sin trampolines | MBC3 | 20.0 % | — |
| Pokémon Rojo | MBC3 | 90.5 % | **97.6 %** |
| Pokémon Amarillo | MBC5 | 89.6-92.3 % | **96.6-97.4 %** |
| Pokémon Oro / Plata | MBC3 + reloj, 2 MB | 96.9 % | **98.2-98.3 %** |
| Wario Land II | MBC3 | 53.9 % | **93.8 %** |

En todos los juegos, con traza, ninguna dirección de ROM cae al intérprete: lo
que queda es código en RAM (la rutina de DMA en HRAM y similares). Las sesiones
aleatorias de Pokémon apenas pasan de la introducción, así que esos porcentajes
valen para lo jugado, no para el juego completo. Ningún juego mostró
diferencias en la prueba diferencial.

`tests/recomp_game.sh ROM NOMBRE` hace el ciclo completo para un juego:
recompila, compila, prueba con varias semillas, junta la traza, recompila con
ella y vuelve a probar.

### MBC

- **MBC1**, incluido **MBC1M** (multicart): se detecta porque la cabecera se
  repite en el banco `$10`.
- **MBC3**, incluido **MBC30** (más de 2 MB: registro de banco de 8 bits y 8
  bancos de RAM).
- **MBC2 y MBC5.**
- **Reloj del MBC3:** avanza con los ciclos emulados, no con el reloj de la PC,
  así la prueba diferencial sigue siendo determinista. Tiene latch, detenido,
  contador de 9 bits de días con acarreo pegajoso, y escribir los segundos
  reinicia el divisor. Pasa las tres partes de `rtc3test`.

### Guardado

`runtime/save.c` usa el formato de BGB y VBA-M: la RAM del cartucho tal cual y,
si hay reloj, 48 bytes con sus registros y la hora Unix. Al cargar, el reloj se
adelanta el tiempo real que pasó. El runtime solo serializa a memoria
(`gb_save_serialize` / `gb_save_deserialize`); el ejecutable escribe el archivo
a un temporal y lo renombra, para que un corte no dañe la partida anterior. Se
carga al iniciar y se guarda al salir y cada 5 segundos si cambió.

### La ROM se pide una sola vez

El ejecutable busca la ROM, en orden: el argumento, la copia guardada en la
carpeta de datos del juego, y `rom.gb` junto al ejecutable o en la carpeta
actual. Si no la encuentra, la pide con el diálogo de abrir archivo (Windows) o
arrastrándola a la ventana. Valida el SHA1 y, si es la correcta, guarda una
copia en la carpeta de datos; las siguientes veces ya no la pide.
`--forget-rom` borra esa copia.

La carpeta de datos la da SDL: `%APPDATA%\GBRecomp\<juego>\` en Windows,
`~/.local/share/GBRecomp/<juego>/` en Linux y
`~/Library/Application Support/GBRecomp/<juego>/` en macOS. Ahí van también las
partidas (`<juego>.sav`). Las rutas se manejan en UTF-8 (`platform/files.c`),
así que funcionan aunque lleven acentos.

No se extraen gráficos ni otros datos por separado: el código recompilado y el
intérprete de respaldo necesitan la ROM completa (los juegos leen datos de
cualquier parte de ella), y un recompilador genérico no sabe dónde empieza y
termina cada recurso de un juego en particular.

Para probar: `tests/inputs/pokered_save.txt` empieza una partida nueva en
Pokémon y guarda. `difftest --save-out` escribe el `.sav`, y
`tests/inputs/pokered_continue.txt` con `difftest --save` lo carga y elige
CONTINUAR: aparece PLAYER AAAAAAA.

### Herramientas nuevas

- `gbrecomp --dis BB:AAAA:N` desensambla N instrucciones.
- `gbrecomp --verbose` lista tablas y trampolines detectados.
- El ejecutable acepta `--uncovered archivo`, que agrega las direcciones de ROM
  que cayeron al intérprete durante la partida (para `gbrecomp --trace`).
- `gbtest --input` reproduce entradas grabadas (lo usa `rtc3test`).

## Estado de la validación del runtime

`sh tests/run_tests.sh`: **107 de 115** pruebas que aplican a DMG.

- Blargg: `cpu_instrs` (las 11 y la combinada), `instr_timing`, `mem_timing`,
  `mem_timing-2` y `halt_bug` pasan.
- Mooneye: pasan todas las de temporización de instrucciones, interrupciones,
  HALT, timer (salvo `rapid_toggle`), DMA, arranque, serial y MBC1/2/5,
  incluido MBC1 multicart.
- `mbc3-tester` (incluye MBC30) y las tres partes de `rtc3test` pasan.
- dmg-acid2 pasa (pantalla idéntica).

Pendientes conocidos:

| Prueba | Causa | Fase |
|---|---|---|
| `ppu/intr_2_*`, `lcdon_*`, `stat_lyc_onoff` | Temporización fina de modos de la PPU (duración del modo 3, primera línea tras encender el LCD) | 5 |
| `timer/rapid_toggle` | Interrupción una vuelta tarde; caso límite de encender/apagar TAC | 5 |
