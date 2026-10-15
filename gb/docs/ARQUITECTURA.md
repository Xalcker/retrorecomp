# Arquitectura de GBRecomp

GBRecomp toma una ROM de Game Boy (DMG, Game Boy Color o con Super Game Boy) y
genera un proyecto C99 que se compila en un ejecutable nativo. El ejecutable carga la ROM del usuario en tiempo de
ejecución (validando su SHA1): nunca se distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md`
de NESRecomp, GENRecomp y NEORecomp: lo que cambia entre las consolas queda dentro de cada una.

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
| `src/runtime/` | El hardware: `gb.c` (bus, sincronización, interrupciones, despachador, DMA, serial, joypad), `timer.c`, `ppu.c`, `apu.c`, `mbc.c`, `sgb.c` (Super Game Boy), `compat.c` (paletas del GBC para juegos de DMG), `interp.c`. |
| `src/gbrecomp/` | El recompilador: descubrimiento, partición en funciones, emisión del proyecto (`gbrecomp.c`) y el mapa animado de `--viz` (`viz.c`). |
| `src/platform/` | Se copia a cada proyecto generado: `main_sdl.c` (ventana, controles, audio, cable link), `difftest.c` (prueba diferencial), `ui_menu.c` y `ui_font.c` (menús y diálogos), `i18n.c` (español e inglés), `settings.c`, `files.c` (carpeta de datos), `export.c` (música y gráficos), `png.c`, `iconos.c` e `inputs.h` (entradas grabadas). |
| `src/tools/` | `gen_interp` (genera el intérprete con el emisor), `embed` (mete el runtime en `gbrecomp`), `gbtest` (corre ROMs sin ventana; `--hash` para golden), `gbbench` (velocidad), `gbtrace`, `gbshot`, `gbaudio` y `gbramdump` (traza, captura, audio y RAM sin ventana, con las opciones de NESRecomp), `linktest` (dos Game Boy por cable) y `vgmcheck` (compara la música exportada). |
| `tests/` | `run_tests.sh` (la batería de `make test`), `run_mealybug.sh`, `golden.sh` y `golden.txt` (línea base exacta), `recomp_game.sh` y `batch_roms.sh` (recompilar y comparar juegos), pruebas de cable link y SGB, `sgb_controles.c`. `inputs/`: guiones de entradas. |
| `tools/` | `compat/` (lista de compatibilidad: `run.py` y `report.py`, con el motor común `retrorecomp/compat.py`), `visor.html` (descubrimiento en el navegador), `truth/` (mapa de código y datos desde un desensamblado), `gen_inputs.pl` (jugadas reproducibles), `iconos.py`. |
| `docs/` | Este documento y `compat/` (la lista de compatibilidad generada). |
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
- Cada acceso a memoria ocurre al **inicio** de su M-ciclo (el hardware se ve
  como estaba antes de esos 4 ciclos) y luego suma 4 ciclos. Es la convención
  del hardware real y la que exige la PPU; con ella pasan las pruebas de
  temporización de CPU, timer y PPU a la vez.
- **Hardware perezoso:** timer, PPU, APU, serial y DMA solo avanzan en
  `gb_sync`, que se llama:
  - antes de cualquier acceso a I/O, VRAM, OAM, RAM de cartucho o durante DMA
    (la ruta lenta del bus);
  - cuando el CPU alcanza `g->next_event`, el primer ciclo en que el hardware
    podría cambiar IF por su cuenta (desbordamiento de TIMA, cambio de modo o
    línea de la PPU, fin de transferencia serial).
- Las lecturas de ROM y WRAM van por un mapa de páginas (`rpage`/`wpage`) sin
  sincronizar. Durante un DMA de OAM el mapa se vacía y todo pasa por la ruta
  lenta, que reproduce los choques de bus. El código recompilado no lee sus
  instrucciones del bus, así que una instrucción que choca con el DMA (por
  ejemplo, el vector de una interrupción que llega mientras el juego espera
  el DMA en HRAM) la ejecuta el intérprete: el despachador y `gb_check` lo
  revisan con `dma_conflict`, y mientras el DMA está activo `next_event` se
  revisa en cada M-ciclo.
- El resultado no debe depender de dónde se corta la ejecución (`gb_run`
  hasta un ciclo cualquiera). `next_event` nunca puede quedar después de un
  evento que cambie IF: la PPU tiene que avisar del inicio del HBlank si su
  interrupción está activa. Antes, con `p->dot == 89` (el modo 3 todavía no
  empezaba a dibujar) caía al fin de la línea; Warlocked, corrido por cuadros
  enteros, perdía esa interrupción y se quedaba en blanco (#24).

## Interrupciones y despachador

En cada límite de instrucción, si `g->cyc >= g->next_event`, el despachador
(`gb_run`) o el código recompilado (`gb_check`) sincroniza el hardware y
revisa IME/IE/IF. Las escrituras lentas, `EI` y `RETI` ponen `next_event = 0`
para forzar la revisión. El despacho de interrupción toma 5 M-ciclos y decide
el vector después de empujar el byte alto de PC (si esa escritura cambió IE,
salta a `$0000`).

`EI` activa IME después de la instrucción siguiente. El despachador lo hace al reanudar tras el
`EI`; si entrara directo a la función recompilada de esa instrucción, su primer `CHECK` vería IME
activo con una interrupción pendiente y la atendería antes de ejecutarla, una instrucción antes
que el hardware (con `EI` seguido de `DI`, una interrupción que nunca debe atenderse). Por eso
esa instrucción corre en el intérprete (`after_ei` en `gb_run`).

`HALT` adelanta el reloj directo al siguiente evento. Detenido, el CPU muestrea
IF 2 ciclos dentro de cada M-ciclo (el primero tras `HALT`, al inicio) y
despierta al terminar ese M-ciclo. El bug de HALT (IME=0 con
interrupción pendiente) hace que la siguiente instrucción se ejecute en el
intérprete con el PC sin avanzar.

## Recompilador

### Descubrimiento

`gbrecomp` decodifica recursivamente desde `$0100` y los vectores de
interrupción, siguiendo saltos, llamadas y `rst` con destino constante (las
llamadas se asumen con regreso). Con `--trace` agrega como puntos de entrada las
direcciones de una traza de ejecución (formato `BB:AAAA`); así se resuelven los
saltos indirectos (`jp hl`, tablas de `rst $28`).

Con `--events` o `--viz`, el descubrimiento anota cada paso en memoria
(`src/gbrecomp/viz.c`): las fases, cada recorrido lineal con su origen
(arranque, interrupción, tabla o traza, heredado por todo lo que descubre) y
la instrucción o entrada de tabla que lo agregó, cada instrucción, las tablas
y sus entradas, los saltos al área de bancos sin banco conocido y los `jp hl`
sin tabla. `--events` escribe ese registro como texto y `--viz` lo reproduce
como un mapa animado de la ROM. El registro no cambia el resultado.
`tools/visor.html` lee el mismo registro en el navegador (mapa, línea de
tiempo, desensamblado y la cadena de `--why`); su desensamblador y su
explicación dan lo mismo que `gbrecomp --dis` y `--why`. `gbrecomp --html` lo
escribe con el registro adentro (el visor va dentro de `gbrecomp`, como el
runtime; no se copia al proyecto), así que la página abre ya cargada.

Cada recorrido guarda qué lo agregó (una instrucción o una entrada de tabla) y
cada tabla, qué instrucción la encontró. `--why BB:AAAA` usa eso para ir hacia
atrás hasta un punto de entrada. Cada paso apunta a eventos anteriores del
registro, así que la cadena siempre termina. `--coverage` compara contra una
lista de direcciones ejecutadas (formato de `--uncovered`).

Las instrucciones de cada banco se agrupan en funciones por tramos contiguos de
direcciones: se corta cuando hay más de 32 bytes sin código o al llegar a 1,500
instrucciones. Cada instrucción pertenece a una sola función. Si dos
decodificaciones se traslapan (saltos a mitad de instrucción), ambas existen,
cada una con su etiqueta.

### Heurísticas genéricas

Además de seguir saltos y llamadas con destino constante, `gbrecomp` reconoce
estas formas comunes de salto indirecto. Ninguna depende de un juego en
particular:

1. **Despachador con tabla en línea** (el `rst $28` / `TableJump` de muchos
   juegos). Una rutina que, sin ramificarse, saca de la pila su propia
   dirección de retorno (`pop` antes de cualquier `push`) y termina en
   `jp hl` es un despachador. En cada llamada a ella, los bytes siguientes se
   leen como tabla de punteros a código, no como instrucciones. Puede llamar
   a una rutina auxiliar del banco 0 que solo hace cuentas (sin tocar la pila
   ni saltar, terminada en `ret`), como el `rst $00` de Street Fighter Alpha,
   que suma el índice a HL con `rst $18`.
2. **Tabla fuera de línea**: `ld hl, Tabla` seguido, a lo más 8 instrucciones
   después, de `jp hl` (normalmente con una llamada intermedia que lee la
   entrada).
3. **Argumentos en línea**: una rutina del banco 0 que saca su dirección de
   regreso (`pop hl`), lee N bytes con `ld r, [hl+]` / `inc hl` y vuelve a
   empujar HL (`push hl`). Quien la llama pone N bytes de datos después del
   `call`, y su código sigue en `call + 3 + N`. Se reconoce por ejecución
   simbólica con valores "byte k de los argumentos". Si la rutina termina en
   `jp hl` con la dirección y el banco sacados de los argumentos, es una
   llamada a otro banco y el destino se explora. Si salta así sin haber vuelto
   a empujar HL, es un salto lejano que no regresa. En Shantae: `$0540`
   (llamada lejana, 3 bytes, 3141 llamadas), `$056B` (salto lejano), `$0803`
   (8 bytes) y dos de 1 byte.
4. **Filas de `jp`**: tres o más `jp nn` seguidos son una tabla de vectores
   (las 83 de `$0150` en Shantae). Al descubrir uno se exploran sus vecinos.
5. **Interfaz común de varios bancos**: si un salto a `$4000-$7FFF` no tiene
   banco conocido (el banco sale de una variable de RAM), pero cae dentro de
   la fila de `jp` con la que empiezan algunos bancos, se explora en cada uno
   de ellos. En Shantae, los bancos `$2C-$3F` empiezan con la misma tabla de 9
   saltos.
6. **Despachador con el destino en una tabla de la ROM**: una rutina del banco
   0 que, sin ramificarse, calcula HL y el banco a partir de lo que trae quien
   la llama y termina en `jp hl`. Se simula con los registros conocidos de
   cada llamador, leyendo los bytes reales de la ROM cuando la dirección es
   constante y llevando el acarreo de `add` + `adc` (índices de 16 bits). Las
   llamadas internas se saltan conservando la pila y la RAM conocida. Si el
   destino depende de A, A es un índice y se recorre la tabla entera hasta la
   primera entrada inválida, porque a algunos destinos solo se llega desde
   otros que todavía no se conocen. En Warlocked, `ld a, N` + `jp $0554`
   cambia de estado con una tabla de registros de 3 bytes (dirección y banco)
   en `$0521`.
7. **Punteros a código guardados en RAM**: el juego guarda la dirección de su
   estado actual en una variable (dos bytes) y en otro lado la carga y salta
   a ella (`ld hl, $FFE4` / `ld a, [hl+]` / `ld h, [hl]` / `ld l, a` /
   `jp hl`). El seguimiento de registros marca los bytes de RAM de valor
   desconocido como "el byte de RAM en `$X`" (`V_RAM`), así que reconoce un HL
   leído de una variable. Se juntan, en cualquier orden, las constantes de
   ROM que el código escribe en cada variable (dos bytes seguidos) y las
   variables que se usan como puntero; cuando coinciden se explora el
   destino. Las constantes de variables que nunca se usan así no se
   exploran. Una llamada a una rutina que solo hace `jp hl` cuenta como
   `jp hl` (también para las tablas fuera de línea).

Además, un `ld rr, nn` + `push rr` cuenta como dirección de regreso si, a lo
más 12 instrucciones después, viene un `jp hl`, `ret` o `jp` sin que en medio
se saque de la pila (el `ld hl, $0564` + `push hl` de `$0540`).

El largo de una tabla no está escrito en ningún lado. La lectura se detiene al
llegar a código ya conocido o a un destino de salto, cuando la entrada no
apunta a ROM (o apunta a la cabecera, $0104-$014F) y, solo en tablas fuera de
línea, al llegar a una dirección que el código carga como constante, porque
ahí empieza otro objeto. Cada entrada se explora antes de leer la siguiente,
así el código que sigue a la tabla se conoce a tiempo para detenerse.

La entrada `$0100` empieza con el banco 1 como banco activo conocido, porque
todos los MBC lo dejan así al encender. Sin eso, un `jp $5C45` en `$0101` (como
en Project S-11) quedaba sin resolver y el juego entero corría en el
intérprete. Con eso, Project S-11 pasó de 0.6 % a 92.5 % recompilado sin traza.

**Patrones que todavía no se reconocen**, encontrados con `tests/batch_roms.sh`.
Todos corren bien (en el intérprete) y la traza los resuelve:

| Patrón | Ejemplo | Sin traza → con traza |
|---|---|---|
| Bucle desenrollado con entrada calculada: se salta a la mitad de una fila de copias según un índice leído con `ld a, [bc]` | Shantae (`$23E0-$26F4`) | 87-90 % → 99.6 % |
| Puntero a código guardado en una variable de RAM: el bucle principal hace `ld hl, [estado]` + `jp hl` | Mary-Kate and Ashley | 1.9 % → 99.4 % |
| Trampolín con el banco en una variable de RAM en lugar de un registro | Duke Nukem | 8.8 % → 99.9 % |
| Código copiado a RAM (WRAM y HRAM) | International Superstar Soccer | 37 % → 72 % (estáticamente no se puede; la traza lo aprende: ver «Código que el juego copia a WRAM o HRAM») |

El seguimiento de registros recuerda hasta 16 celdas de RAM; cuando se llena,
olvida la más vieja (antes eran 8 y las nuevas se ignoraban: la rutina de
Warlocked escribe en 9 antes de leer el banco de `[$C600]`).

Shantae estaba en esta tabla por las llamadas con argumentos en línea (4.9 %
sin traza). Con las heurísticas 3 a 5 llega a 86-90 % sin traza (3 semillas de
6000 cuadros, sin diferencias), y el estático encuentra 41,916 instrucciones
en lugar de 925. Lo que le falta lo mostró `--coverage`: el bucle desenrollado
de arriba y código al que solo se llega por punteros en datos.

Warlocked (sin la tabla de arriba, no estaba en la prueba en lote): con la
heurística 6 el estático encuentra 40,432 instrucciones en lugar de 2,815.
Con el juego corriendo, recompila de 97.6 a 99.4 % sin traza (3 semillas de
6000 cuadros, sin diferencias); con la heurística 7, de 99.5 a 99.7 % (3000
cuadros). La cifra de 99.9 % que se midió al agregar la
heurística era falsa: el juego estaba atorado en la pantalla blanca de #24 y
solo se medía ese bucle.

`difftest` dice en qué zona ejecuta el intérprete (ROM, RAM del cartucho, WRAM,
HRAM), para distinguir código no descubierto de código en RAM.

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
gbrecomp tetris.gb -o build/out/tetris --codemap static.map
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
cursores de Luigi, `Handshake`), que necesita un segundo Game Boy por cable.
Ya se puede correr con `linktest` (ver "Cable link"):
`tests/link_tetris.sh` juega una partida de dos jugadores hasta la pantalla
de victoria. El código no ejecutado se generó con el mismo emisor que
el intérprete, así que sus instrucciones son confiables; lo que no se ha
probado es su flujo de control (etiquetas, cortes de función).

### Código generado

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
  cambió su propio banco, regresa al despachador. En el MBC1 de más de 512 KB,
  el código de `$0000-$3FFF` comprueba lo mismo con el banco de `$0000` (ver
  «Otro banco en `$0000-$3FFF`»).
- `gen/dispatch.c` tiene tablas ranura → página de 256 bytes → función. El
  código del banco 0 solo se usa si está mapeado en `$0000`, el de los demás
  bancos en `$4000` y sus vistas (`f10v_0150`, el banco `$10` compilado para
  `$0000-$3FFF`) cuando el modo 1 del MBC1 los pone en `$0000`. Cualquier otra
  combinación va al intérprete.

### Rutinas que el juego copia a HRAM

Muchos juegos copian a HRAM la rutina que lanza el DMA de OAM y la ejecutan desde ahí, porque
mientras corre el DMA el CPU solo puede leer HRAM. Ese código no está en la ROM en la dirección
donde corre, así que el descubrimiento no lo alcanza y pasaba siempre por el intérprete (unas
84 instrucciones por cuadro).

`gbrecomp` reconoce la rutina en la ROM (`match_dma`) con esta forma:

```
[di] [ld a,N]  ldh ($46),a | ld [c],a    ; dispara el DMA
ld a,M / dec a | dec r                   ; espera
jr nz,-3 / [nop hasta 4] / ret | reti
```

y genera `gen/hram.c` con **una función por variante**, válida para cualquier dirección de
HRAM: calcula en ejecución dónde está la copia (`hram_base`), las etiquetas son desplazamientos y
`CHECK(base + n)` usa esa base. `hram_lookup`, que el despachador consulta para `$FF80-$FFFE`,
solo devuelve la función si los bytes de HRAM coinciden con los de la variante. Los operandos de
`ld a,N` (el byte alto del origen y el contador del bucle) no se comparan, porque algunos juegos
los cambian después de copiar la rutina; la función los lee de HRAM cuando la instrucción corre,
como el intérprete (`Emit.imm8_expr`). Si la copia es otra, cae al intérprete.

Cada juego compila solo las variantes que tiene su ROM; sin rutina, `hram.c` queda en 10 líneas.
`ld [c],a` escribe en cualquier puerto, así que esa forma se descarta cuando sus bytes son código
que el análisis alcanza (en Rayman, las rutinas de infrarrojos tienen la misma forma). Esta forma no
cubre el código que se copia a WRAM, las rutinas de HRAM con otra forma ni un `nop` previo a la
copia: eso lo aprende la traza (siguiente sección).

### Código que el juego copia a WRAM o HRAM

Los manejadores de interrupción (STAT, HBlank), los trampolines de cambio de banco y los saltos por
vectores en RAM también se copian de la ROM y se ejecutan desde WRAM o HRAM. No están en la ROM donde
corren, así que el descubrimiento estático no los alcanza; en la muestra de 47 ROMs, varios juegos
pasaban millones de instrucciones por el intérprete solo por esto (Gex: 6.7 millones, el 90 % de lo
que interpretaba). `gbrecomp` no puede deducir ese código, pero el intérprete lo ve ejecutarse. El
ciclo de trazas lo aprende:

1. `difftest --uncovered archivo`, el ejecutable del juego con `--uncovered archivo` y `gbtrace` (los
   tres con el mismo módulo, `src/platform/ramtrace.h`) anotan, por cada dirección de WRAM o HRAM que
   ejecutó el intérprete, los bytes de la instrucción: hasta 4 versiones por dirección (las distingue
   el opcode; en `jr` y los `CB`, también el segundo byte) y qué bytes de operando cambiaron entre una
   ejecución y otra. Salen como líneas `ram DIRECCIÓN B0 [B1 B2]`, con `??` en los operandos
   variables. Las demás líneas del archivo siguen siendo `BB:AAAA`, y los lectores anteriores ignoran
   las de RAM porque no empiezan con un dígito hexadecimal.
2. `gbrecomp --trace` (se puede repetir: las lee de todas las trazas que se den) las lee, une las que
   son la misma instrucción vista en corridas distintas (el operando que difiere pasa a ser variable)
   y descarta las direcciones con más de 8 versiones, que son código que el juego genera con
   estructuras distintas cada vez.
3. Arma **bloques**: cadenas de instrucciones que van una detrás de otra sin ambigüedad (hasta 1024; no
   cruzan `$D000`, porque los bancos de WRAM no son contiguos). Cada bloque se compila como el código
   de ROM, con sus direcciones reales: un salto constante dentro del bloque es `goto`, `CHECK(dirección)`
   va antes de cada instrucción y se puede entrar por cualquiera.
4. `gen/ram.c` trae `ram_lookup`, que el despachador consulta para `$C000-$DFFF` y, si `hram_lookup`
   no sabe, para `$FF80-$FFFE`. Por cada dirección hay una lista de bloques candidatos, y corre el
   primero cuyos bytes siguen en RAM (`ram_match`, con una máscara que deja fuera los operandos
   variables). Si ninguno coincide, o el juego cambió el código, cae al intérprete.
5. Un operando variable se lee de RAM cuando la instrucción corre (`ram_peek`, por
   `Emit.imm8_expr` e `Emit.imm16_expr` en el emisor), igual que el intérprete; un `jp` o `call` cuyo
   destino es variable se vuelve un salto dinámico. Así sigue funcionando el código que se parcha a
   sí mismo para cambiar un valor o un destino.

`gbrecomp` imprime «código en WRAM/HRAM aprendido de la traza: N instrucciones (M con operandos
variables) en K bloques» y `difftest`, «Código en WRAM/HRAM: N instrucciones fijas, M con operandos
variables, K con varias versiones, J que cambian demasiado». Una dirección que cambia más de lo que
las versiones admiten sigue en el intérprete, por diseño.

Límites que quedan: la guarda se comprueba al entrar al bloque, no instrucción por instrucción, así que
un bloque que reescribe un opcode de sí mismo mientras corre (no un operando) sigue con lo aprendido
hasta la siguiente entrada; `difftest` lo delata si ocurre en lo que probó. Y el código que el juego
genera en ejecución con estructuras distintas (Rayman, Donkey Kong Land 2, las ranuras de instrucción
que parchan las ROMs de Blargg como `instr_timing` o `cpu_instrs`) se queda en el intérprete.

Resultados, con el ciclo de dos rondas de 3600 cuadros (la primera sin traza; la segunda con la que
dejó `difftest`) en las 47 ROMs de la muestra: **sin diferencias en las 47** con las semillas 1 y 2.
Instrucciones que pasaban por el intérprete en WRAM o HRAM en la primera ronda (que ya compila la
rutina de DMA de OAM) y en la segunda, con la semilla 1:

| Juego | Primera ronda | Segunda |
|---|---|---|
| Gex: Enter the Gecko | 6.7 M | 0 |
| Rayman | 2.0 M | 6 464 (código generado en ejecución) |
| Perfect Dark | 1.3 M en WRAM, 0.8 M en HRAM | 0 |
| Harry Potter y la Cámara Secreta | 0.32 M | 0 |
| Wario Land II | 0.31 M | 0 |
| Kirby's Dream Land | 125 k | 0 |
| Final Fantasy Legend II | 47 k | 8 |
| Donkey Kong Land 2 | 33 k | 738 (código generado en ejecución) |

`interrupt_time`, de Blargg (900 cuadros), pasa de 10.4 millones de instrucciones en WRAM a ninguna.
Cada juego compila solo el código de RAM que su traza trae; sin líneas `ram`, `gen/ram.c` es un
`ram_lookup` que devuelve `NULL`.

Con el ejecutable, la traza sale de lo que se juegue: Donkey Kong Land 2 con una sesión guionada
(`--play`, 3000 cuadros) deja 177 instrucciones de WRAM en 9 bloques, y el proyecto recompilado con
ella pasa, en `difftest` (1800 cuadros, semilla 1), de 65 255 a 44 739 instrucciones interpretadas,
sin diferencias; lo que falta es lo que esa sesión no ejecutó.

#### Por qué se aprende de la traza, por qué también del ejecutable y cómo llevarlo a otra consola

**Por qué de la traza y no del análisis.** Lo que corre desde RAM no existe en la ROM en esa
dirección: el descubrimiento sigue saltos desde la ROM, y deducir qué habrá en RAM exigiría emular la
copia (ejecutar el bucle que la hace, con sus registros y bancos). Las rutinas con una forma conocida
sí se reconocen (`match_dma`), pero cada juego trae otras: un manejador de STAT, un trampolín de banco,
un `jp` por un vector. El intérprete ya está ejecutando esos bytes y los lee de RAM, así que anotarlos
es exacto, barato y no necesita reconocer ningún patrón.

**Por qué también en el ejecutable.** Las entradas aleatorias de `difftest` no llegan a todo el juego,
y lo que falta se completa jugando con `--uncovered` y recompilando con esa traza. Si el ejecutable
solo anotara la ROM, jugar completaría la ROM y perdería justo lo único que no se puede deducir, el
código de RAM. Con el mismo módulo en las dos herramientas, cualquier sesión (aleatoria, guionada,
grabada con `--record` o jugada a mano) alimenta el mismo ciclo y escribe las mismas líneas; una
prueba lo comprueba con la ROM mínima. También lo anota `gbtrace`, la traza guionada con la que
empieza `tests/recomp_game.sh`: el ciclo aprende el código de RAM desde la primera ronda.

**Qué hay que replicar en otra consola** (la idea no depende del SM83):

1. *Medir si hace falta.* Un contador de instrucciones ejecutadas desde RAM, sobre el gancho del
   intérprete, en una muestra de juegos. En Game Boy, 6 de 47 pasaban más de 100 mil instrucciones por
   RAM y uno, 6.7 millones.
2. *Un gancho por instrucción interpretada* que dé la dirección y deje leer los bytes sin efectos
   secundarios (aquí `g->trace` y `gb_peek`): el mismo con el que ya se anota la ROM.
3. *Qué regiones son «RAM»* en esa máquina: ejecutables y fuera de la ROM. Hay que confirmarlo con la
   medida del punto 1; candidatas: la RAM interna y la del cartucho en NES, la RAM de trabajo del 68000
   y la RAM del Z80 en Mega Drive y Neo Geo.
4. *Qué identifica una instrucción*: el opcode más los bytes que cambian lo que hace (aquí, el
   desplazamiento de un salto relativo y el segundo byte de `CB`). El resto son operandos que pueden
   variar sin ser otra instrucción, y es lo que evita tratar un valor parchado como código nuevo. En un
   conjunto de instrucciones con longitud variable o palabras de extensión esta parte es la delicada.
5. *Un tope de versiones por dirección* (aquí 4 al anotar y 8 al compilar): con más es código generado
   en ejecución y se queda en el intérprete.
6. *Formato aditivo.* Líneas que no empiecen como las de la traza común (`BB:AAAA`), para no tocar el
   contrato entre consolas (`trace_line.h`, retrorecomp): los lectores anteriores las ignoran.
7. *Generación.* Bloques de instrucciones sin ambigüedad compilados con sus direcciones reales
   (`CHECK(dirección)` por instrucción, `goto` dentro del bloque), una guarda que compara los bytes de
   RAM con una máscara al entrar y caída al intérprete si no coinciden. Los operandos variables se
   leen de RAM al ejecutar (en el emisor, una expresión para el inmediato). Hay que respetar el mapa de
   memoria: aquí los bancos de WRAM no son contiguos y un bloque no cruza `$D000`.
8. *Pruebas.* Una ROM mínima armada en el propio script, sin descargas, que copia una rutina a RAM y
   parcha uno de sus operandos; el ciclo de dos rondas, exigiendo que la primera interprete RAM y la
   segunda no; y que el ejecutable anote lo mismo que `difftest`. El `make` de macOS compara fechas con
   precisión de un segundo: entre las dos rondas hay que esperar uno, o no recompila el objeto.

### Proyecto generado

```
gbrecomp juego.gb -o salida/ [--trace traza.txt]... [--name nombre] [--viz] [--events archivo]
         [--coverage archivo] [--why BB:AAAA]
cd salida && make
./nombre rom.gb          # el juego (SDL2); valida el SHA1 de la ROM
./difftest rom.gb        # prueba diferencial
```

`salida/` trae `gen/` (código generado), `runtime/` (copia del runtime),
`platform/` (SDL2 y difftest), un `Makefile` y `LEEME.txt` (cómo compilar en
cada sistema). No contiene datos de la ROM.

## Bancos, mappers y guardado

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
que queda es código en RAM (la rutina de DMA en HRAM se compila sola, y el resto
lo aprende la traza). Las sesiones
aleatorias de Pokémon apenas pasan de la introducción, así que esos porcentajes
valen para lo jugado, no para el juego completo. Ningún juego mostró
diferencias en la prueba diferencial.

`tests/recomp_game.sh ROM NOMBRE` hace el ciclo completo para un juego:
recompila, compila, prueba con varias semillas, junta la traza, recompila con
ella y vuelve a probar.

### Otro banco en `$0000-$3FFF` (modo 1 del MBC1)

En el modo 1 del MBC1, `$0000-$3FFF` no muestra el banco 0 sino el de los bits
altos: `$20`, `$40` o `$60` en las ROMs de más de 512 KB, y `$10`, `$20` o `$30`
en los cartuchos de varios juegos (MBC1M), donde así corre cada juego: el menú
del banco 0 escribe el modo y los bits altos, y desde la instrucción siguiente
el CPU lee el juego. Ese código usa direcciones de `$0000-$3FFF`, así que
`gbrecomp` lo compila aparte, en una **vista** del banco: la ranura
`nbanks + B` es el banco B visto en `$0000-$3FFF` (`gen/bank10v_*.c`), y el
despachador la usa cuando `g->mbc.bank0` es B.

- **De dónde salen.** En los MBC1M, de la cabecera de cada juego (`$0100` y los
  vectores de interrupción de los bancos `$10`, `$20` y `$30` que repiten el
  logo); el banco activo de cada juego tiene sus mismos bits altos
  (`mbc_bank`). En los demás MBC1 grandes, de la traza.
- **La traza anota la dirección real.** Las líneas `BB:AAAA` llevan la
  dirección con que corrió la instrucción (`trace_line.h`): `10:015A`, no
  `10:415A`. `difftest`, `gbtrace` y el ejecutable cuentan por ranura
  (`src/platform/romtrace.h`); antes escribían `0x4000 + desplazamiento` y
  `gbrecomp` compilaba ese código para `$4000`, donde nunca corre.
- **El banco puede cambiar debajo del código.** Después de una escritura que
  puede llegar al MBC, el código de `$0000-$3FFF` comprueba que el banco de
  `$0000` siga siendo el suyo; si no, regresa al despachador. Antes el código
  compilado del banco 0 seguía con sus propios bytes después de que el menú
  ponía el banco del juego, y `difftest` lo delata con la ROM mínima de
  `tests/run_tests.sh` (Bomberman Collection no lo mostraba porque salta al
  juego desde un trampolín en WRAM).
- Las rutinas especiales del banco 0 (argumentos en línea, trampolines,
  despachadores con tabla) se reconocen solo en el banco 0: desde una vista no
  se aplican, y lo que falte lo trae la traza.

Bomberman Collection (MBC1M, 1 MB), ciclo de dos rondas de 3000 cuadros con la
semilla 1: el código recompilado pasa de 18.8 % a **50.7 % de los ciclos** (el
resto es HALT), las instrucciones de ROM interpretadas de 3728 a **0**, sin
diferencias. Con las demás ROMs de la muestra (ningún otro MBC1 de más de
512 KB) `gbrecomp` genera el mismo código; solo cambia la tabla de
`gen/dispatch.c`.

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
  y los bytes del código de WRAM y HRAM que cayeron al intérprete durante la
  partida (para `gbrecomp --trace`).
- `gbtest --input` reproduce entradas grabadas (lo usa `rtc3test`).

## Video y audio

### PPU

`runtime/ppu.c` es una PPU con FIFO de píxeles que avanza punto por punto. La
línea de tiempo de cada línea (cuándo cambian LY, STAT y la comparación con
LYC, cuándo disparan las interrupciones, cuándo se bloquean OAM y VRAM para
leer o escribir) y la del modo 3 (fetcher de 6 puntos, 8 píxeles basura,
descarte por SCX, pausa por sprites, ventana) siguen al hardware tal como lo
miden las pruebas de Mooneye y Mealybug. Se usó el emulador SameBoy (licencia
MIT) como referencia de tiempos; la implementación es propia.

Cada registro se lee en el punto en que el hardware lo usa (SCX/SCY y los bits
de mapas y datos al leer cada mosaico; paletas y bits de fondo/sprites al
sacar cada píxel), y las escrituras del CPU surten efecto en su punto exacto
del M-ciclo (`ppu_write_timed`). Para poder aplicar las que entran antes del
M-ciclo, durante la sincronización la PPU se queda 2 puntos atrás y se pone al
corriente antes de cada lectura o revisión de interrupciones:

| Registro | Surte efecto (respecto al inicio del M-ciclo, S) |
|---|---|
| BGP, OBP0, OBP1 | S−2: (viejo \| nuevo); S−1: nuevo |
| LCDC | S−2: bits de mapas y datos; S−1: el resto (incluido encender/apagar) |
| SCX, SCY | S−2 |
| STAT | S: todas las fuentes habilitadas (falla del DMG); S+1: el valor |
| IF | S+1 |
| WX | S (y cuenta como "recién cambiado" ese punto) |
| resto | S |

Otros detalles del DMG que se reproducen:
- La interrupción de modo 2 dispara un punto antes de que STAT muestre el modo
  2, y la de HBlank un punto después del fin del modo 3.
- La línea 153 muestra LY = 153 solo unos puntos.
- La primera línea tras encender el LCD no tiene modo 2 y es más corta.
- WY se revisa al inicio de cada línea y, tras escribir LCDC o WY, en una
  rejilla de 8 puntos.
- Al arrancar, la VRAM tiene el logo de la cabecera y el ® como lo deja la boot
  ROM (algunos juegos y pruebas usan esos mosaicos).

Velocidad: con la PPU por punto, Tetris corre a unas 27 veces el tiempo real
(antes 47). Las optimizaciones posteriores, sin cambiar la salida, están en
"Línea base para optimizar el runtime".

### APU

Dos canales de pulso (el primero con sweep), uno de onda y uno de ruido
(`src/runtime/apu.c`). El secuenciador de 512 Hz lo marca el flanco de bajada
del bit 4 de DIV (`apu_div_event`), así que escribir en DIV lo afecta igual
que en el hardware. Sale en estéreo a 48 kHz con un filtro pasa-altos, como el
capacitor de salida de la consola. Avanza de forma perezosa (`apu_flush`; ver
"Línea base para optimizar el runtime").

## Variantes de la consola y accesorios

Lo que cambia según el modelo (Game Boy Color, Super Game Boy) y lo que se
conecta a la consola (cable link, en la sección del GBC).

### Game Boy Color

El modelo se elige según la cabecera (`$143`, bit 7): los juegos que admiten
color corren como CGB y los demás como DMG (`gb_init_model` permite forzarlo).
En el ejecutable generado, un juego que no admite color ni SGB corre en un GBC
en modo de compatibilidad (ver abajo); `--model dmg` lo regresa al verde.

Hay dos banderas: `g->cgb_hw` (el hardware es un GBC: temporización de la PPU,
fallas propias del DMG desactivadas, framebuffer en RGB555) y `g->cgb` (modo
CGB: bancos, atributos, HDMA, doble velocidad, registros de color).

En modo CGB el runtime tiene:
- **Memoria:** VRAM de 2 bancos (VBK) y WRAM de 8 bancos de 4 KB (SVBK, en
  `$D000`). El mapa de lectura rápida sigue el banco elegido.
- **Paletas de color:** 8 de fondo y 8 de sprites, de 4 colores RGB555, con
  índice y autoincremento (BCPS/BCPD, OCPS/OCPD). Durante el modo 3 se ignoran
  las escrituras, pero el índice sí avanza.
- **HDMA:** general (todo de una vez, con el CPU detenido 8 µs por bloque de
  16 bytes) y por HBlank (un bloque al empezar cada HBlank visible).
- **Cable link:** el serial con reloj interno recibe lo que devuelva
  `g->link_xfer` al completar el byte (sin cable, `$FF`), y
  `gb_serial_clock_in` hace de reloj externo para el otro lado: devuelve su
  SB y, si esperaba (SC = `$80`), recibe el byte y pide la interrupción. En el
  GBC, SC.1 elige el reloj rápido (262144 Hz). `GbLink` conecta dos máquinas
  en el mismo proceso: corren por tramos de tiempo real (una línea, o 32
  puntos mientras SC.7 esté encendido en alguna) y, al completar un byte, el
  otro lado se adelanta hasta ese instante antes del intercambio. `linktest`
  corre dos así, sin ventana, cada una con su guion de entradas. El cable por
  red podrá ser otra implementación de `link_xfer`.
  El ejecutable del juego también tiene el cable (menú Juego o `--link`):
  un segundo `GB` con la misma ROM y el mismo código recompilado (las
  funciones generadas solo usan el `GB` que reciben), con su propia partida.
- **Super Game Boy multijugador:** con MLT_REQ, cada subida de P15 pasa al
  siguiente control; los controles 2-4 devuelven `g->pad_sgb`
  (`gb_set_pad_player`). `tests/sgb_controles.c` lo comprueba leyendo P1 como
  un juego.
  `tests/sgb_bomberman.sh` juega una batalla de 4 en Bomberman GB (Bomberman
  Collection), cada jugador con su control (`gbtest --input2..4`).
- **Paquetes del SGB:** escribir en P1 el mismo valor que ya tenía no es un
  pulso. Bomberman Collection escribe `$00` dos veces al empezar un paquete, y
  tomarlo como pulso inválido cancelaba su `PAL_TRN`: el título de Bomberman
  GB quedaba en blanco.
- **Doble velocidad:** KEY1 + STOP. Hay dos relojes: `g->hw_cyc` en ciclos del
  CPU (timer, serial, DMA de OAM) y `g->rt` en tiempo real (PPU y APU). En doble
  velocidad cada M-ciclo del CPU dura 2 puntos en lugar de 4, y el secuenciador
  del APU usa el bit 13 de DIV para seguir a 512 Hz. Todo lo que mide tiempo
  real usa `g->rt`: un cuadro (`gb_run_frame`) dura `gb_frame_cycles()`, que
  son 70,224 ciclos del CPU o el doble en doble velocidad, y el RTC del MBC3
  cuenta `gb_realtime()`. Antes los dos contaban ciclos del CPU: en doble
  velocidad los juegos iban a la mitad de velocidad (la mitad de un cuadro por
  cada cuadro de la ventana, con el audio entrecortado) y el RTC al doble.
  difftest no lo notaba porque sus dos máquinas avanzan igual.
- **PPU:** atributos de fondo en el banco 1 del mapa (paleta, banco del
  mosaico, giros y prioridad), banco y paleta CGB de los sprites, prioridad de
  sprites por índice de OAM (OPRI) y LCDC.0 como prioridad maestra del fondo.
  Las fallas de ventana propias del DMG solo se aplican en DMG.
- **Arranque:** registros con A = `$11` (así detectan los juegos un Game Boy
  Color) y paletas en blanco.

El framebuffer guarda 16 bits por píxel: el tono (0-3) en DMG y el color RGB555
en CGB. La plataforma SDL convierte cada canal de 5 bits a 8 como
`(x << 3) | (x >> 2)`.

Validación:
- `cgb-acid2`: pantalla idéntica.
- `cgb-acid-hell`: difiere en un píxel en dos líneas.
- SameSuite: pasan `gbc_dma_cont`, `gdma_addr_mask` y `blocking_bgpi_increase`;
  fallan `hdma_lcd_off`, `hdma_mode0` (temporización fina de la HDMA) y
  `ei_delay_halt`.
- Pokémon Oro, Plata y Amarillo corren en color, sin diferencias en
  `difftest`.

#### Modo de compatibilidad

Un juego de DMG en un Game Boy Color (`cgb_hw` = 1, `cgb` = 0). La boot ROM
del GBC le elige colores (`src/runtime/compat.c`):

- **Paleta por título:** si el licenciatario es Nintendo, suma los 16 bytes del
  título y la busca en una tabla; las sumas repetidas se desempatan con la
  cuarta letra. Los demás juegos reciben la combinación por omisión: fondo
  verde y azul, sprites rojos.
- **Tres paletas:** fondo, sprites 0 y sprites 1. Cada tono que sale de BGP,
  OBP0 u OBP1 elige un color de su paleta. Las prioridades siguen las reglas del
  DMG, con OPRI = 1 (por X).
- **Combinaciones de botones:** las 12 del GBC (flecha, flecha + A, flecha + B),
  con `--palette up+a`, etc., en el ejecutable, `gbtest` y `difftest`.
- **Registros:** siguen accesibles VBK (fijo en el banco 0), BCPS/OCPS y los no
  documentados (menos `$FF74`); los demás registros del GBC dan `$FF`.
- **Arranque:** A = `$11`, E = `$08`, L = `$7C`; P1 en `$FF`; DIV interno en
  `$2678`. El mapa del logo solo queda para los juegos marcados en la tabla.
- **PPU:** la del GBC. Por ejemplo, la interrupción de modo 2 de la línea 144
  llega un M-ciclo antes que la de VBlank.

Las tablas vienen de la boot ROM libre de SameBoy (licencia MIT; el aviso está
en `compat.c`). La duración del arranque puede variar unos ciclos con la
búsqueda en la tabla. Solo se calibró con un juego que no es de Nintendo
(`boot_div-cgbABCDE`).

Las grabaciones de entradas guardan el modelo (`modelo dmg`) y la paleta, porque
un juego tarda distinto en cada modelo y la grabación se desfasaría.

Validación: pasan `boot_regs-cgb`, `boot_div-cgbABCDE`, `boot_hwio-C`,
`unused_hwio-C` y `vblank_stat_intr-C` de Mooneye. Tetris (amarillo y rojo) y
Super Mario Land (cielo azul claro) corren sin diferencias en `difftest`, en
DMG y en compatibilidad.

### Super Game Boy

Si el juego no admite color pero sí SGB (`$146` = 3 con el licenciatario viejo
`$33`), corre como Super Game Boy: un DMG más lo que hace la SNES
(`src/runtime/sgb.c`). Con `--model dmg|cgb|sgb` (en el ejecutable, `gbtest` y
`difftest`) se fuerza otro modelo.

- **Paquetes:** se reciben por P1. Un pulso de reinicio (P14 y P15 en bajo)
  abre el paquete; siguen 128 bits (P14 en bajo = 0, P15 en bajo = 1), del bit
  bajo al alto, y un bit 0 de fin. Un comando ocupa de 1 a 7 paquetes.
- **Colores:** cuatro paletas, con un color 0 común, y un mapa de atributos de
  20×18 mosaicos. Comandos implementados: PAL01/23/03/12, ATTR_BLK, ATTR_LIN,
  ATTR_DIV, ATTR_CHR, PAL_SET, ATTR_SET y MASK_EN (congelar, negro o color 0).
- **Transferencias de VRAM** (PAL_TRN, CHR_TRN, PCT_TRN, ATTR_TRN): la SNES lee
  la imagen que sale por la pantalla, no la memoria del Game Boy. Los 4 KB se
  reconstruyen con los tonos del cuadro completo que sigue al comando.
- **Borde:** 256 mosaicos de 4 bits por píxel en formato SNES y un mapa de
  32×28 con las paletas 4-7. `gb_sgb_screen` da la imagen final de 256×224 en
  RGB555, con el juego en (48,40); `gb_framebuffer` sigue dando los 160×144
  en tonos. El borde se decodifica solo cuando cambia (CHR_TRN, PCT_TRN o el
  color 0, que es el fondo); lo que cae sobre el juego (el borde se dibuja
  encima donde su color no es 0) se guarda aparte y se aplica en cada cuadro.
- **Multijugador (MLT_REQ):** con 2 o 4 jugadores, cada subida de P15 pasa al
  siguiente control, también a mitad de un paquete. Con ambas líneas en alto,
  P1 da `$F` menos el número de control. El modo 2, que no está documentado, se
  comporta como lo mide SameSuite.
- **Pulsos inválidos:** un pulso sin ambas líneas en alto antes cancela el
  paquete en curso. Es el caso de `$20` → `$10`, como al leer el control. Sin
  esa regla, International Superstar Soccer (que escribe `$00` en P1 al leer el
  control) armaba con la lectura normal del control un paquete de 128 bits en
  cero. Eso es un PAL01 que pinta todo de negro, y el juego se veía en negro
  dentro del borde. Es una deducción: SameBoy no tiene esa regla, pero con ella
  pasan todas las pruebas y los juegos.
- **Arranque:**
  - Registros del SGB (A = `$01`, C = `$14`, HL = `$C060`).
  - P1 en `$FF` y el canal 1 de sonido apagado, porque no hay sonido de arranque.
  - DIV depende de la cabecera: la boot ROM manda `$104-$14F` a la SNES y cada
    bit en 0 tarda un M-ciclo más. El contador interno es `$D308 + 4 × ceros`.

No se emulan el sonido de la SNES (SOUND, SOU_TRN), los programas que el juego
sube a la SNES (DATA_SND, DATA_TRN, JUMP) ni el borde de fábrica del SGB, que
viene en la ROM de la SNES. Un juego sin borde propio muestra el marco del
color 0. Tampoco se imita el reloj un 2.4 % más rápido del SGB original.

Validación:
- Pasan las pruebas de arranque de Mooneye (`boot_regs-sgb`, `boot_div-S`,
  `boot_div2-S`, `boot_hwio-S`) y las de SameSuite `command_mlt_req` y
  `command_mlt_req_1_incrementing`.
- Pokémon Rojo y Wario Land II muestran su borde y sus colores, sin diferencias
  en `difftest`.
- Las entradas grabadas de Pokémon Rojo (`pokered_save`, `pokered_continue`)
  se grabaron en DMG y lo dicen con una línea `modelo dmg`. En SGB el juego
  tarda distinto y la grabación se desfasaría.

## Interfaz del ejecutable

El intérprete con ventana (`gb juego.gb`, objetivo `gb` del Makefile) es este
mismo `main_sdl.c` compilado con `-DGB_INTERP`: en vez de `gen/info.c`, el
título, el nombre y el SHA1 salen de la ROM que se abre; no hay código
recompilado, no se valida el SHA1 ni se guarda copia de la ROM, y los datos
van a la carpeta `interprete/` con el nombre del archivo de la ROM. Es el
equivalente de `nes` en NESRecomp (que usa `NES_RECOMP` al revés).

`platform/main_sdl.c` usa la barra de menús y los diálogos de Moskva 89
(`ui_menu.c`, `ui_font.c`: fuente de mapa de bits propia, sin depender de las
del sistema), con textos en `i18n.c` (español e inglés) y preferencias en
`settings.c`. Los menús se arman según la consola encendida: la paleta solo
aparece en DMG y en compatibilidad GBC, y el borde solo en SGB. Un
desplegable que no cabe a la derecha de la ventana se corre a la izquierda
(`MenuBar.width`); con el tamaño ×1 los últimos títulos todavía no caben en la
barra (issue #54). Con un menú o
un diálogo abierto el juego se pausa, y al cerrarlo los botones se ignoran
hasta que se sueltan (si no, el Enter que cerró el diálogo llegaría como Start).

- **Estados guardados** (`gb_state_save` / `gb_state_load` en `save.c`): el
  struct GB completo más la RAM del cartucho, con un encabezado que lleva
  `sizeof(GB)` y la cabecera de la ROM. Solo los lee el mismo ejecutable con la
  misma ROM. Los punteros se reemplazan con los de la máquina actual al cargar
  (como en `gb_copy_state`).
- **Cambio de consola:** reinicia con otro modelo (guarda antes el `.sav`).
  Solo se ofrecen las que admite la cabecera.
- **Grabaciones:** mientras se graba o reproduce, se desactiva lo que
  desfasaría los botones (reiniciar, cargar estado, cambiar de consola).
- **Exportar** (`platform/export.c`):
  - **Música:** el runtime tiene un gancho, `g->apu_log`, que avisa de cada
    escritura del juego a `$FF10-$FF3F`. Se graba en VGM 1.71 con el tiempo de
    `g->rt` convertido a muestras de 44.1 kHz. Al empezar se escribe el estado
    actual del chip, sin disparar canales. Al mismo tiempo se graba un WAV con
    las muestras mezcladas.
  - **Verificación:** `vgmcheck` reproduce el VGM en el APU del runtime, con su
    propio reloj de 512 Hz, y compara la envolvente contra el WAV. Pokémon Rojo
    (60 s): correlación 0.94, sin desfase. Super Mario Land: 1.00.
  - **Gráficos:** después de cada cuadro se revisan los 384 mosaicos de VRAM
    (768 en modo color). Los nuevos se guardan, sin repetir, con una tabla
    hash. Al detener se escriben hojas PNG de 16 × 64 mosaicos en gris.
    También entran los mosaicos que el juego usa para mandar datos a la SNES
    en SGB (parecen ruido).
- **Audio:** la salida común de los cuatro recompiladores
  (`tools/retrorecomp/shared/player_audio.h`): una cola de SDL con silencio
  delante al empezar o al volver de una pausa, rampas de 5 ms para no
  chasquear y control de ritmo (remuestrea cada cuadro, ±1 % a lo más, para
  que la cola no crezca ni se vacíe). Con el cable, la mezcla de los dos
  Game Boy se hace antes.
- **Pruebas sin ventana:** `--ui-shot archivo.png [--ui-frames N] [--ui-menu N]
  [--ui-action N] [--ui-export]` corre N cuadros con la ventana oculta (con
  `--ui-export`, exportando música y gráficos), abre un menú o hace
  una acción y guarda la imagen.

## Empaquetado y pruebas

- **Un solo ejecutable:** `gbrecomp` lleva adentro el runtime, la plataforma y
  el intérprete generado. `src/tools/embed.c` los convierte en arreglos de C
  (`build/gen/embedded.c`) cuando se compila. La lista de archivos está en el
  `Makefile` (`EMBED_RUNTIME`, `EMBED_PLATFORM`).
- **Versión:** sale de `git describe` (`gbrecomp --version`).
- **`make dist`:** deja `dist/gbrecomp-<versión>-<sistema>.tar.gz` con `gbrecomp`, el
  README, la licencia y este documento. La versión sale de `git describe`.
- **Proyecto generado:** compila con el mismo Makefile en Windows (MSYS2,
  enlazado estático: no necesita DLLs), Linux y macOS (SDL2 por pkg-config).
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en
  NESRecomp), en Linux, macOS y Windows:
  - `make` (compila todo) y `sh tests/contract.sh` (contrato común).
  - `make testroms` (las ROMs de prueba libres, c-sp/game-boy-test-roms v7.0)
    y `make test`, que también recompila `cpu_instrs` de Blargg, compila el
    proyecto generado y corre `difftest`.
  - `make dist` y publica el paquete como artefacto; con una etiqueta `vX.Y.Z`,
    además crea el release con los paquetes de los tres sistemas.

Probado a mano en Windows (MSYS2) y Linux (Ubuntu en WSL): la batería daba 130
de 130 en ambos (hoy son 143, con `sgb_controles`, los proyectos generados, `--html`, `gbramdump` y la ventana de `gb`), y los proyectos de Tetris, Pokémon Rojo y Pokémon Oro
compilan sin advertencias y corren sin diferencias.

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
el intérprete y los bytes del código de WRAM y HRAM que ejecutó (para `gbrecomp --trace`),
`--screens carpeta --every N` guarda capturas y `--bench` mide la velocidad de cada máquina.

El porcentaje «de los ciclos» de la primera línea (el resumen común de los cuatro recompiladores)
cuenta todo el tiempo de la máquina, incluido el que el CPU pasa en HALT esperando una
interrupción, así que un juego sin nada interpretado puede quedar lejos del 100 %. La línea
siguiente reparte esos ciclos:

```
difftest: 3000 cuadros iguales (379355 llamadas, 379345 a código recompilado, 56.5% de los ciclos)
Ciclos: 56.5 % recompilado, 43.5 % en HALT, 0.1 % en interrupciones, 0.0 % en el intérprete
```

Es International Superstar Soccer; Rayman marca 20.8 % recompilado y 79.0 % en HALT. Los dos
interpretan menos del 0.1 % de los ciclos: no falta nada por compilar. Un porcentaje bajo
apunta a código sin compilar solo si lo que falta está en el intérprete. Los cuenta el
despachador (`gb_run`), y solo en la máquina que tiene `recomp_lookup`: HALT es lo que el CPU
pasa dormido más los 4 ciclos del despertar, las interrupciones son los 5 M-ciclos de cada
despacho y el intérprete son sus instrucciones. Van en la variable global `gb_cycles` y no en
`GB`, para no cambiar el tamaño de los estados guardados ni lo que compara `difftest`. Si lo que
no entra en ninguno de los cuatro (`STOP`, esperas de la DMA por HBlank atendidas por el
despachador) llega al 0.05 %, sale al final como «otros».

Al final imprime cuántas instrucciones corrieron recompiladas y cuántas en el intérprete (por
zona: ROM, RAM del cartucho, WRAM y HRAM), las 20 direcciones de ROM que más ejecutó el
intérprete y, aparte, las de RAM con cuántas veces y los 8 bytes que había la primera vez. Sirve
para decidir qué falta compilar: una rutina en HRAM se reconoce por esos bytes. También resume el
código de RAM que vio: instrucciones fijas, con operandos variables, con varias versiones y que
cambian demasiado.

### Resultados con Tetris (JUE) (V1.1)

| | Solo estático | Con trazas de 7 sesiones |
|---|---|---|
| Instrucciones recompiladas | 2,189 | 4,321 |
| Ejecutado en código recompilado | 96.6 % | 98.9 % |
| Direcciones de ROM interpretadas | 1,239 | 0 a 99 según la semilla |

- **Sin diferencias** en más de 160,000 cuadros (13 sesiones de 3,000 a 20,000
  cuadros con semillas distintas). Las capturas confirman partidas reales.
- El 1.1 % que quedaba en el intérprete era la rutina de DMA que Tetris copia a
  HRAM; hoy se compila (ver «Rutinas que el juego copia a HRAM»).
- Se comprobó el localizador inyectando un error a mano (`cp` → `sub`):
  reportó la instrucción y el registro exactos.
- Velocidad (sin audio): recompilado 47x tiempo real, intérprete 44x. Casi
  todo el tiempo se va en el hardware, que se simula punto por punto. Optimizar
  `gb_sync` es lo que más rendiría (se hizo después; ver "Línea base para
  optimizar el runtime").

## Estado de la validación

`sh tests/run_tests.sh`: **146 de 146** (DMG, más las de CGB, compatibilidad y SGB que ya pasan,
`sgb_controles`, que lee los controles 2 a 4 del SGB como un juego, el proyecto generado de `cpu_instrs`
comparado contra el intérprete con `difftest`, los de `firstwhite` y `strikethrough`, que copian la
rutina de DMA de OAM a HRAM y no deben pasar por el intérprete, `ei_di`, una ROM mínima que
repite `ei` / `di` y comprueba que el código recompilado no atiende una interrupción antes de la
instrucción que sigue a `ei`, y dos del código aprendido en WRAM: una ROM mínima que copia una
rutina a `$C000` y parcha un operando de ella, y `interrupt_time` de Blargg, que ejecuta unas 600
instrucciones distintas desde WRAM; en las dos, la primera ronda interpreta ese código y la
segunda, con la traza que deja, no interpreta ninguna; dos más que comprueban que el ejecutable `gb`
(al jugar con `--uncovered`) y `gbtrace` anotan las mismas líneas de RAM que `difftest` en la ROM
mínima; una de `gbrecomp` con dos `--trace`, una de código de RAM y otra de ROM, que comprueba que
se leen las dos; una del banco que el modo 1 del MBC1 pone en `$0000-$3FFF`, y una ROM mínima que
duerme en HALT y despierta en cada HBlank, que comprueba el reparto de los ciclos de `difftest`).

- Blargg: `cpu_instrs` (las 11 y la combinada), `instr_timing`, `mem_timing`,
  `mem_timing-2` y `halt_bug`. No hay un equivalente de `nestest` de NESRecomp
  (comparar instrucción por instrucción contra un log de CPU): ninguna de las
  ROMs de prueba libres trae ese log, y el CPU ya lo cubren estas y las de Mooneye.
- Mooneye: todas las de aceptación para DMG (CPU, interrupciones, HALT, timer,
  DMA, arranque, serial, PPU) y las de MBC1/2/5, incluido MBC1 multicart.
- `mbc3-tester` (incluye MBC30) y las tres partes de `rtc3test`.
- dmg-acid2 (pantalla idéntica).

`sh tests/run_mealybug.sh`: **18 de 24** pruebas de Mealybug con referencia DMG
(cambios de registros a mitad de línea). Las que faltan son fallas muy
específicas del DMG al cambiar LCDC o la ventana a mitad del dibujo:

| Prueba | Diferencia |
|---|---|
| `m3_lcdc_bg_en_change` | 4 píxeles (columna 0) |
| `m3_lcdc_obj_size_change`, `..._scx` | 15 y 30 píxeles |
| `m3_lcdc_win_en_change_multiple_wx` | 45 píxeles |
| `m3_lcdc_obj_en_change_variant` | 96 píxeles |
| `m3_window_timing_wx_0` | 126 píxeles |

Blargg `dmg_sound`: **9 de 12**. Fallan las tres de rarezas del canal de onda
del DMG (leer o escribir la RAM de onda mientras suena, y la corrupción al
dispararlo mientras toca), que los juegos casi no usan.

### Línea base para optimizar el runtime

`difftest` compara el código recompilado contra el intérprete, pero los dos
comparten la PPU, el APU y el timer: un error en una optimización de esas
piezas sale igual en ambas máquinas y pasa sin diferencias. Para eso está
`tests/golden.sh`, que guarda en `tests/golden.txt` un hash FNV-1a de cada
cuadro (pantalla del GB y, en el SGB, la de la SNES con el borde) y de cada
muestra de audio de 24 casos: juegos de DMG, SGB y GBC con entradas fijas
(`tests/inputs/golden.txt`) y pruebas de
la PPU y del APU. El hash se acumula cuadro a cuadro, así que también detecta
diferencias que no llegan a la imagen final; se comprobó cambiando un solo
píxel de un solo cuadro y un factor del mezclador de audio.

La medida de la velocidad es `gbbench`: corre el intérprete sin ventana, con
el APU generando muestras como en la ventana, y reporta la corrida más rápida
de `--repeat N` (la menos afectada por otros programas). Según un perfil por
muestreo con el APU apagado (en el mismo intérprete de `gbbench`), el CPU,
recompilado o interpretado, se lleva alrededor del 3 % del tiempo; la PPU,
entre 64 % y 79 % (`ppu_advance` y `fetcher_step`, punto a punto en el modo
3); el APU, entre 6 % y 12 %; el bucle por M-ciclo de `sync_hardware` y el
timer, cerca de 10 %; y en el SGB, `sgb_frame` 16 % (el borde se decodificaba
en cada cuadro). Por eso el recompilado corre casi a la misma velocidad que el
intérprete: lo que conviene optimizar es el hardware, no el CPU.

Después de las optimizaciones de abajo, el mismo perfil (SMB Deluxe
recompilado, con audio) queda así: PPU ~69 %, APU ~17 %, CPU ~8 % (código
recompilado ~3 %, el resto en el despacho, el intérprete para el código en
RAM y la sincronización). Lo que queda de la PPU son los casos que no pasan
por los caminos rápidos: líneas con sprites o ventana, el inicio de línea y
la búsqueda en OAM (issue #52).

Optimizaciones hechas con esta línea base (golden igual en todas):

| Cambio | Efecto (`gbbench`, 2400 cuadros, mejor de 3) |
|---|---|
| Borde del SGB en caché | Pokémon Rojo, DKL2 y Wario Land II en SGB: +15 % a +20 %; GBC sin cambio |
| APU perezoso (`apu_flush`) | Tetris y SML +12 % a +25 %; Tetris DX +25 %; SMB Deluxe (doble velocidad) +50 % |
| Sincronización en lote (`timer_skip`) | +23 % a +40 % en todos (Tetris 20x→27x, SML 23x→30x, SMB Deluxe 19x→26.5x, Harry Potter 15x→21x) |
| Modo 3 en régimen sin despacho (`mode3_fast`) | +3 % a +11 % (Tetris 27x→30x, SML 30.5x→33x, Harry Potter 21x→22x) |
| Un mosaico de 8 puntos de una vez (`mode3_tile`) | con el anterior, +38 % a +64 % (Tetris 27.5x→45x, SML 30.6x→50x, Pokémon Rojo 26.7x→40x, Tetris DX 28.2x→42x, SMB Deluxe 27.4x→44.6x, Harry Potter 21.3x→29.5x) |
| `bg_push_row` con tabla y búsqueda en OAM sin despacho (`mode2_fast`) | +5 % a +9 % (Tetris 42x→45.5x, SML 49x→53.6x, Tetris DX 39.8x→42.5x) |
| Muestras del APU sin sumar de a un M-ciclo (`sample_steps`) | con el anterior, +9 % a +15 % (Tetris 44.4x→48.5x, SML 49.5x→55x, SMB Deluxe 43.5x→49.7x, Harry Potter 28.5x→31x) |

**Sincronización en lote.** `sync_hardware` avanzaba el timer, la PPU y el
APU de a un M-ciclo. Sin DMA de OAM ni transferencia serial, ahora pregunta a
`timer_skip` cuántos M-ciclos faltan para que el timer haga algo más que
contar (el flanco del bit que elige TAC, el de DIV que marca el secuenciador
del APU, o un desborde en curso; el contador avanza de 4 en 4, así que los
flancos caen en múltiplos exactos) y los hace de una vez: suma al contador,
los acumula en el APU y pone la PPU al día al final del lote. Es equivalente
porque la PPU no lee el timer ni los relojes globales, y lo único que
comparten es IF, donde cada uno solo prende bits. Además, la PPU recibe
tramos largos y salta de golpe los puntos ociosos (`idle_dots`) en vez de que
se le pregunte en cada M-ciclo, que es de donde sale la mayor parte de la
ganancia.

**Modo 3 en régimen.** Mientras el dibujo va en régimen (ya pasó el descarte
inicial, no hay sprite por descartar ni por leer en ese punto, la ventana no
puede activarse, no hay píxel de la ventana por meter ni nada pendiente),
cada punto es solo `render_pixel` y `fetcher_step`. `mode3_fast` hace eso
mismo sin pasar por `ppu_dot`, `visible_line_dot` y `mode3_dot`, y le devuelve
el control a la ruta general en cuanto una condición deja de cumplirse. Cubre
entre 79 % y 93 % de los puntos del modo 3 en los juegos de golden.

Dentro de eso, cuando el fetcher acaba de empujar (estado 0 y 8 píxeles de un
mismo mosaico en la FIFO), la FIFO de sprites está vacía y en los 8 puntos
siguientes no cae ningún sprite, la activación de la ventana ni el fin de la
línea, `mode3_tile` hace los 8 puntos de una vez: escribe los 8 píxeles con
una tabla de 4 colores, lee el mosaico siguiente con la posición que tendría
el fetcher en su punto 1 y lo empuja como en el punto 8. Además de golden, se
comparó el hash de imagen y audio de 300 ROMs al azar de la colección (20 s
cada una) contra la versión sin estos dos cambios: 300 iguales.

La búsqueda en OAM (puntos 6 a 83) tiene el mismo tipo de camino rápido,
`mode2_fast`: una entrada cada 2 puntos y los bloqueos del punto 80 del DMG,
sin pasar por `ppu_dot`. `bg_push_row` reparte los bits de los dos bytes del
mosaico con una tabla de 256 entradas (`spread`) en vez de bit por bit.

**APU perezoso.** `apu_mcycles` ya no procesa los M-ciclos: solo los cuenta.
`apu_flush` los procesa cuando alguien va a mirar o cambiar el APU: al leer o
escribir sus registros, en cada paso del secuenciador (lo llama el timer antes
del `apu_mcycles` del mismo M-ciclo, como antes), al sacar audio y al terminar
`gb_run`. Dentro de un tramo pendiente busca el próximo M-ciclo en que algo
puede cambiar (vence el temporizador de un canal audible o sale una muestra) y
hace los anteriores de una vez: la salida de cada canal no cambia en ellos y
las sumas son enteros exactos en `double`. Los canales inaudibles (apagados o
con volumen 0) se adelantan de golpe; solo cambia su estado interno (paso,
posición de la onda, LFSR). `sample_acc` tiene parte fraccionaria, pero
empieza en 0, se le suman enteros y se le resta `CYCLES_PER_SAMPLE`, que es
múltiplo de 2^-46: siempre es múltiplo de 2^-46 y menor que 128, así que cabe
exacto en un `double` y ninguna suma redondea. `sample_steps` calcula con una
división exacta cuántos M-ciclos caben antes de la siguiente muestra en vez
de sumar de a uno (si el valor no tuviera esa forma, suma de a uno). Se
comparó contra el bucle original en 20 millones de casos al azar: iguales. El resultado es idéntico bit a bit (golden
igual) y `difftest` no cambia: el recompilado y el intérprete hacen
`apu_flush` en los mismos momentos del juego.
