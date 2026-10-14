# Arquitectura de NEORecomp

NEORecomp toma un juego de Neo Geo y genera un proyecto C99 que se compila en un ejecutable nativo.
El ejecutable carga el juego y la BIOS del usuario en tiempo de ejecución y valida sus SHA1: nunca se
distribuyen datos del juego ni de la BIOS.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de GENRecomp, NESRecomp
y GBRecomp: lo que cambia entre las consolas queda dentro de cada una. Las que todavía no tienen contenido
dicen en qué fase de [PLAN.md](PLAN.md) llegan.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `neo.h` (estado e interfaz), `neo.c` (carga, encendido, cuadro, tarjeta), `cart.c` (formatos, imagen normalizada, cabecera, vectores, identidad del código), `bios.c` (BIOS del AES y tabla L0), `bus.c` (mapa de memoria del 68000), `lspc.c` (registros del LSPC, VRAM, líneas, temporizador e interrupciones), `lspc_render.c` (el dibujo), `z80bus.c` (mapa y puertos del Z80, cuándo corre), `ym2610.c` (el YM2610: FM, SSG, ADPCM-A y ADPCM-B, temporizadores), `audio.c` (la mezcla), `zip.c` (zip, inflate y CRC32), `neodb.c`/`neodb.h` (base de datos de juegos), `recomp.h` (la interfaz con el código generado), `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete), `z80.h`/`z80.c` (el Z80); `sha1`, `console.h`, `trace_line.h` (idéntico en los cuatro recompiladores). |
| `src/neorecomp/` | El recompilador (`neorecomp.c`, `dis68k.c`, `visor_html.h`). Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | SDL2 (`main_sdl.c`, menús y diálogos en `ui_menu`/`ui_font`/`i18n`, `settings`, `icono.h`), `difftest.c` y `bench.c` (también para los proyectos generados), `cli.h` e `inputs.h` (las opciones comunes) y lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (idéntico en los cuatro). |
| `src/tools/` | Sin ventana, con las opciones comunes (`cli.h`): `neotrace`, `neoshot`, `neoaudio`, `neoramdump` (y `neobench`, de `src/platform/bench.c`), con `romload.h`. `mkcontrato` (el juego de prueba del contrato). Pruebas: `carttest` (con `inflate_vectors.h`), `bustest`, `lspctest`, `audiotest`, `statetest`, `inputtest`, `m68ktest` y `z80test` (SingleStepTests, con `json.h`); diagnóstico: `neorun`; `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `recomp_game.sh` (ciclo completo de un juego), `sst68k.sh` y `sstz80.sh` (las SingleStepTests), `contract.sh` (contrato común, idéntico en los cuatro), `testroms.sh` (`make testroms`). |
| `tools/` | `retrorecomp/` (la GUI común, submódulo), `make_icon.py` (los iconos a partir de `NEOGEO.png`). |

## Una sola fuente de verdad para el CPU

El 68000 y el Z80 son los de GENRecomp (`m68k.h`, `m68k_ops.h`, `cpu68k.c`, `z80.h`, `z80.c`), sin
cambios de semántica: solo cambian los nombres (`struct Neo`), la traza (`neo_trace_mark`, ver
Recompilador) y, en el Z80, la NMI y la vuelta de un `HALT` (abajo).

`m68k_ops.h` tiene la semántica de cada familia de instrucciones como una función
`k_NOMBRE(g, op, ext, pc)` que ejecuta la instrucción `op` que está en `pc` y devuelve la dirección
de la siguiente (la que sigue, el destino de un salto o el manejador de una excepción):

- El **intérprete** (`cpu68k.c`) las llama con `ext = NULL`: las palabras de extensión salen de la
  cola de prefetch (`ir`, `irc`) y cada una que se consume pide la siguiente al bus, como el 68000.
- El **código generado** las llamará con `op`, `pc` y `ext` constantes (las palabras de extensión,
  leídas de la ROM al recompilar). Las lecturas de programa se vuelven ciclos sin acceso al bus y
  `ir`/`irc` se rehacen de la memoria al salir del código generado. El compilador pliega todo: `ADD.w
  #$1234,D0` queda en unas 20 instrucciones de máquina (las banderas en línea y el siguiente PC como
  constante).

`m68k_decode` dice a qué familia pertenece cada opcode (o `K_ILLEGAL`, validando los modos de
direccionamiento) y `m68k_ext_words` cuántas palabras de extensión lleva: es lo que usará el
descubrimiento para recorrer el código y rechazar datos.

**Validación:** las [SingleStepTests del 68000](https://github.com/SingleStepTests/680x0) (`make
testroms`, un commit fijo; ~1 millón de casos, uno por instrucción, con el estado del CPU, la cola de
prefetch, la RAM y los ciclos antes y después). `m68ktest` las corre **por los dos caminos**: el
intérprete y el del código generado (`--recomp`). Pasan las 1 000 058 por los dos; se omiten 2 de
ASL.b con datos malos conocidos ([issue #4](https://github.com/SingleStepTests/680x0/issues/4)). En
`--recomp` no se compara la cola de prefetch cuando la instrucción escribe sobre ella (código que se
modifica a sí mismo: en ROM no pasa, y el código de RAM que se compila (ver «Código que el juego copia a
RAM») es el que no se reescribe así; `difftest` lo delataría).

Comportamientos poco documentados que siguen a las pruebas: banderas N/V de ABCD/SBCD/NBCD; CHK
compara primero con el límite (38 ciclos, N = signo) y luego el signo (40 ciclos, N = 1), y si no
atrapa N no cambia; DIVS con desborde termina antes (16/18 ciclos); la
división entre cero apila la dirección de la instrucción (solo un caso lo cubre); ASR con más pasos que
bits deja C = X = 0; ADDQ.l a An tarda 6 ciclos. El error de dirección apila el IR, el estado (R/W,
I/N, código de función), la dirección y como PC la última palabra de programa leída menos 2 (al saltar a
una dirección impar, el destino menos 4); MOVE a `-(An)` hace el prefetch antes de escribir y un long
se escribe en dos pasos (la palabra baja primero), igual que MOVEM a `-(An)` y ADDX/SUBX con `-(An)`.

**Donde no seguimos a las pruebas:** DIVU/DIVS con desborde dejan N = 1 y Z = 0 (las SingleStepTests
dejan N y Z sin cambiar). NBA Jam Tournament Edition lo demuestra: tras un `divu.w #4` que se desborda
decide con `beq` cómo escribir su EEPROM, y solo con Z = 0 escribe páginas de 4 bytes y reconoce su
partida al volver a arrancar; Genesis Plus GX hace lo mismo. `m68ktest` compara esos casos sin N ni Z
y los cuenta aparte (1854 de DIVU y 2839 de DIVS).

La escritura de TAS (`cpu.tas`) la decide el bus: el Genesis la ignora en la RAM; el Neo Geo la hace.

**El Z80** (`z80.c`): completo, con lo no documentado (banderas X/Y, MEMPTR/WZ, Q para SCF/CCF, mitades
de IX/IY, SLL, la copia del resultado de DDCB/FDCB a un registro, las banderas de las instrucciones de
bloque al repetir) y las interrupciones IM 0/1/2. Pasa las 1 604 000 SingleStepTests del Z80 (`z80test`,
`tests/sstz80.sh`). Dos cosas nuevas respecto a GENRecomp, que las SingleStepTests no cubren (no tienen
interrupciones) y prueba `z80test --nmi`:

- **NMI** (`z80.nmi`, por flanco): el Neo Geo la usa para avisar de cada comando del 68000. Se atiende
  tras cualquier instrucción (también la que sigue a EI), sale del `HALT`, borra IFF1 y deja IFF2 (RETN lo
  devuelve a IFF1), apila el PC y salta a `$0066`; 11 ciclos T.
- **Vuelta de un `HALT`:** después de ejecutar `HALT` el PC ya apunta a la instrucción siguiente (así lo
  piden las SingleStepTests), y esa es la que se apila al atender una interrupción. En GENRecomp la
  interrupción le sumaba 1 otra vez (se volvía una instrucción más adelante); aquí está corregido.

## Temporización

Todo se cuenta en ciclos: `g->cyc` (del 68000) y los maestros. Un cuadro son 384 pixeles × 264 líneas ×
4 = 405 504 ciclos maestros; el 68000 va a la mitad (202 752 por cuadro, 768 por línea), el Z80 a un sexto
(exactamente un ciclo suyo cada 3 del 68000) y el YM2610 a un tercio (una muestra del FM cada 432
maestros). El reloj del AES (24,167829 MHz según la wiki de NeoGeoDev, 59,60 cuadros por segundo; el MVS y
MAME usan 24 MHz, 59,19) solo cambia el ritmo de la ventana y del audio, no la emulación.

Cada acceso del 68000 al bus son 4 ciclos; la tarjeta de memoria tiene 2 de espera. No hay refresco de
DRAM (las memorias son SRAM) ni esperas de la ROM (los cartuchos van sin estados de espera; no se emulan las
patas ROMWAIT/PWAIT). El Z80 corre detrás del 68000 (`z80_sync`): solo se comunican por `$320000`, así que
se pone al día antes de que el 68000 le mande un comando o lea su respuesta, y al final de cada cuadro
(`audio_end_frame`). `z80_run_until` termina en el mismo límite de instrucción en un tramo que en muchos,
así que correrlo de a ratos no cambia nada (fase 8; antes se ponía al día tras cada instrucción del 68000).

**Las líneas del cuadro se cuentan desde el VBLANK** (el contador del LSPC vale `$1F0`, la primera línea
del borde de abajo): 0-15 borde de abajo, 16-23 sincronía vertical (`$0F8-$0FF`), 24-39 borde de arriba
(`$100-$10F`) y 40-263 las 224 visibles (`$110-$1EF`). Así un cuadro termina con la última línea visible y
las entradas de un cuadro las lee el juego en su VBLANK, al principio. El VBLANK se pide 85 ciclos después
de empezar la línea 0 (168 + 2 ciclos maestros desde el comienzo de la sincronía horizontal, según los
comentarios de MAME) y la recarga del temporizador "al empezar el cuadro", 573 ciclos después; los dos
números se ajustarán con los oráculos.

## Interrupciones y despachador

`neo_run_frame` corre hasta que el LSPC termina el cuadro con `m68k_run` (un solo `setjmp` por cuadro,
como en GENRecomp) y después de cada instrucción `neo_after_step_inline`: el LSPC solo si llegó su
próximo evento (`lspc.next_event`: fin de línea, VBLANK, recarga o vencimiento del temporizador); el Z80
va aparte (arriba). `lspc_sync` fija el nivel de interrupción del 68000 (`cpu.ipl`) con lo pendiente:
3 la de arranque, 2 el temporizador, 1 el VBLANK (autovector). **No se borran al atenderlas**
(`m68k_int_ack` no hace nada): el programa las reconoce escribiendo `IRQACK` (`$3C000C`: bit 0 la de
arranque, 1 el temporizador, 2 el VBLANK). Si no, al volver con `RTE` la misma interrupción entra otra
vez. La de arranque queda pendiente al encender (no con el botón de reset).

En los proyectos generados (`-DNEO_RECOMP`), `neo_run_frame` usa `m68k_run_hybrid`, como en GENRecomp:
antes de cada instrucción, si el 68000 puede seguir sin el intérprete (`m68k_native_ok`: sin
interrupción pendiente, sin traza, sin STOP, sin estar detenido), busca código recompilado para el PC
(`recomp_lookup`); si no lo hay, ejecuta una instrucción con el intérprete. Las interrupciones las
atiende siempre el intérprete, y el código recompilado sale apenas hay una que atender.

## Recompilador

```
neorecomp juego -o carpeta [--name nombre] [--bios archivo ...] [--trace traza.txt ...] [--coverage traza.txt]
neorecomp juego [--info]
```

`--trace` lee las trazas de `neotrace`, de la ventana con `--trace` y de `difftest --uncovered`: una línea
`BANCO:DIRECCION` por instrucción de ROM ejecutada, el formato común (`trace_line.h`). Las del juego se
agregan como puntos de entrada; las de la BIOS (banco `FF`) van a la caché de la BIOS.

### Identidad de las instrucciones

Una instrucción del juego se identifica por su desplazamiento en la imagen P normalizada (ver Cartucho):
el primer MB es `$000000-$0FFFFF` (con espejos si P1 es más chico) y el MB n + 1 es el banco n de la
ventana `$200000-$2FFFFF` (un P2 más chico que 1 MB se repite). Las de la BIOS, por su desplazamiento
dentro de la BIOS, en un espacio aparte: `BIOS_SPACE` (`$FF00000`) más el desplazamiento, con los espejos
de `$C00000-$CFFFFF`. `neo_code_offset` hace esa traducción con el banco actual (`p2_bank`) y devuelve -1
donde no hay ROM. En las trazas (`trace_line.h`, el formato común `BANCO:DIRECCION`) el banco es el
desplazamiento dividido entre 1 MB: `03:212344` es el banco 2 de P2 y la BIOS va en el banco `FF`
(`FF:C00402`). La traza del intérprete (`g->trace`) tiene una entrada por palabra de P y luego las de la
BIOS (`neo_trace_index`).

**Dos espacios, el mismo análisis.** El descubrimiento, la partición y la emisión corren una vez para el
juego y otra para la BIOS (`space_begin`), con el mismo código. Lo que cambia es el espacio:
`space_offset` traduce una dirección del CPU al desplazamiento (el juego: P1 en `$000000` y P2 en
`$200000`; la BIOS: `$C00000-$CFFFFF`) o dice que no es de este espacio. Los saltos de uno al otro (el
juego llamando a `$C0044A`, la BIOS saltando a `$000122`) no se siguen: en el código generado, salen al
despachador, que busca en el otro espacio. Las funciones no cruzan una ventana de 1 MB (en la BIOS, sus
128 KB).

### Descubrimiento

Es el de GENRecomp. Las semillas del juego son sus vectores que apuntan a P (desde `$200`) y los `JMP` de
la cabecera a los que salta la BIOS (`$122` USER, `$128` PLAYER_START, `$12E` DEMO_END, `$134` COIN_SOUND);
las de la BIOS, sus vectores y su tabla de saltos (`$C00402-$C004DA`, de a 6 bytes: lo que llaman los
juegos). Desde cada una se siguen las instrucciones hasta un salto sin vuelta, con los destinos
constantes de `Bcc`, `BRA`, `BSR`, `DBcc` y `JMP`/`JSR`. Un opcode inválido al que se llega siguiendo el
flujo corta el recorrido (son datos), salvo `ILLEGAL` o en una traza.

**Bancos de P2.** En los juegos de más de 2 MB, un salto a la ventana P2 desde P1 puede ir a cualquier
banco: se prueba en todos y se toma lo que tenga pinta de código (como en NESRecomp con los bancos
cambiables). Desde una función de P2, la ventana es la de su mismo banco. Esas funciones vigilan el banco
(`MAPCHK`, con `map_ver`) y salen si cambió.

**Heurísticas** (las de GENRecomp, con el mapa de memoria del Neo Geo en `sane_abs` y `code_addr`,
ajustadas en la fase 7 con lo que hacen los juegos de SNK). Seguras (`--no-tables` las quita):

- Tablas de desplazamientos de 16 bits (`move.w tbl(pc,d0.w),d1 / jmp tbl(pc,d1.w)`, y también
  `lea tbl,a0 / move.w 0(a0,d0.w),d0 / lea 0(a0,d0.w),a0 / jsr (a0)`), de instrucciones (`bra.w`/`jmp`,
  aceptando un `jmp` a la BIOS), de punteros de 32 bits y bases constantes.
- Dos niveles: `movea.l tbls(pc,d0.w),a0 / move.w 0(a0,d0.w),d1 / jmp 0(a0,d1.w)`: cada puntero de
  `tbls` es una tabla de desplazamientos relativos a ella misma.
- Un `jmp blk(pc,d4.w)` sin tabla entra a la mitad de un bloque desenrollado (`move.l d7,(a6) / add.l
  d3,d7 ...`): el bloque es código desde `blk`, y se puede entrar en cualquiera de sus instrucciones.
- Lo especulativo no corta una tabla: "tras un final" puede haber tomado por código la tabla que sigue a
  un `bra`.

Especulativas (`--no-spec`):

- Punteros inmediatos (`move.l #r,...`, `lea r,An`, `pea r`) y tablas sueltas: cuatro o más punteros con
  el mismo paso de 4, 6 u 8 bytes (tablas de registros con un puntero, como las de objetos de SNK), en P1
  en el juego y en toda la BIOS (donde la dirección dice el desplazamiento).
- Anclas de máquinas de estados: `move.l #X,(An)` o `move.l #X,d16(An)` donde X es la instrucción que
  sigue, o la que sigue a un `rts` inmediato (con hasta 16 bytes de datos en medio): el objeto guarda su
  próximo estado. Que unos datos valgan justo su propia dirección más 6 u 8 es casi imposible, así que se
  buscan en todo el programa, en cada banco.
- Lo que sigue a un final de rutina.

"Con pinta de código" (`plausible`): opcodes válidos, direcciones absolutas razonables (también la RAM de
respaldo del MVS en `$D00000`, que mira la BIOS) y destinos de saltos dentro del espacio, hasta un final.
Para los punteros sueltos (estricto) el final tiene que llegar en menos de 64 instrucciones, o la rutina
tiene que dar señales de código: llamadas a rutinas ya descubiertas, saltos que caen justo en una de las
instrucciones decodificadas y accesos absolutos a la RAM, la E/S, la paleta o la BIOS (un final antes de
160 instrucciones con algo de esa evidencia, o 160 instrucciones con mucha): así entran las rutinas largas
de inicialización y de estados, que usan `a5`/`a6` y no tienen un `rts` cerca.

**Cobertura** (`tests/coverage.sh`, con trazas de 6000 cuadros que llegan a jugar): sin traza el análisis
encuentra el 99,0 % de las instrucciones que ejecutan los 8 juegos (97,5-100 % cada uno) y el 100 % de las
de la BIOS. Lo que falta se cubre con `--trace` o con la segunda ronda de `difftest --uncovered`.

### La BIOS: una vez por versión

El código de cada versión de la BIOS se genera en una caché del usuario (`~/.neorecomp/bios/<sha1>/`, o
`$NEO_BIOS_CACHE`) y cada proyecto lleva una copia en `bios/`. La caché guarda la versión de `neorecomp`
y las instrucciones de la BIOS que vieron las trazas de todos los juegos (`semillas.txt`): se regenera solo
si cambió `neorecomp` o si una traza trae instrucciones nuevas, así crece con cada juego y la comparten
todos. `bios/bios.c` dice qué versiones hay; en tiempo de ejecución, `recomp_bios_lookup` busca la de la
BIOS cargada por su SHA1 (una vez: `bios.code`). Con una versión aceptada que no se recompiló, la BIOS corre
en el intérprete.

### Herramientas de análisis

Las mismas que en GENRecomp, con los mismos formatos, sobre el espacio del juego: `--coverage traza`,
`--why BB:DIRECCION` (o el desplazamiento en P), `--viz [--color]`, `--html archivo`, `--events archivo`,
`--codemap archivo` y `--dis OFFSET:DIR:N` (`dis68k.c`, que también pone el desensamblado en los
comentarios del código generado).

### Funciones y emisión

Como en GENRecomp: las instrucciones se agrupan en funciones (tramos contiguos con huecos de hasta 32 bytes,
de hasta 400 instrucciones, sin cruzar una ventana); cada función empieza con un `switch` sobre el PC y tiene
una etiqueta por instrucción, que llama a su `k_*` de `m68k_ops.h` con el opcode, las palabras de extensión
y el PC constantes. `AFTER` pone al día el LSPC y el Z80 y sale si terminó el cuadro o hay que volver al
intérprete; después se sigue con un `goto` si el destino es de la misma función, y si no, `LEAVE` rehace el
PC y la cola de prefetch y vuelve al despachador. Las funciones del juego se llaman `f_DESPLAZAMIENTO` y las
de la BIOS `b<sha1 corto>_DESPLAZAMIENTO`.

`gen/dispatch.c` tiene las funciones del juego ordenadas por desplazamiento, con una tabla por KB, y
`recomp_lookup`: traduce el PC con `neo_code_offset` y busca en el juego o, si el desplazamiento es de la
BIOS, en `recomp_bios_lookup`. Si el PC está en la RAM, pregunta a `ram_lookup` (siguiente sección).

### Código que el juego copia a RAM

Algunos juegos copian código a la RAM del 68000 y lo ejecutan desde ahí (KOF '98 espera en un bucle en RAM).
Pesa poco en Neo Geo (en los juegos medidos, a lo sumo un 1 % de los ciclos), pero el mecanismo es el de
GENRecomp, con el que comparte el 68000, y el módulo común de retrorecomp, `shared/ram_code.h` (salió de lo que
hizo GBRecomp):

1. Con la traza activa (`difftest --uncovered`, el ejecutable con `--uncovered`, `neotrace`), el intérprete anota
   con `neo_ram_note` (por el puntero `m68k_ram_trace` del CPU, que pone `neo_trace_start`) cada instrucción que
   ejecuta en `$100000-$10FFFF`: hasta 4 versiones por dirección y qué bytes cambiaron entre ejecuciones. La tabla
   va aparte de `Neo`, porque los estados guardados la copian y comprueban su tamaño. Salen como líneas
   `ram DIRECCION B0 B1 ...` (`??` en los bytes que cambiaron) en el mismo archivo de la traza, que
   `trace_line_read` ignora; el ejecutable, que carga el archivo y lo vuelve a escribir, conserva las que traía.
   Los espejos de la RAM (hasta `$1FFFFF`) siguen en el intérprete.
2. **Qué identifica una instrucción: su opcode.** La longitud depende solo de él, y las palabras de extensión las
   lee el código generado de RAM al ejecutar (`ram_ext`), como el intérprete: el código que parcha sus inmediatos o
   destinos sigue funcionando.
3. `neorecomp --trace` une lo visto en varias corridas, descarta las direcciones con más de 8 versiones y arma
   **bloques** (cadenas sin ambigüedad), que van a `gen/ram.c` como las funciones de ROM.
4. **La guarda:** `ram_lookup` elige el primer bloque cuyas palabras de opcode siguen en RAM; si no, el intérprete.
   Se comprueba al entrar al bloque: un bloque que reescribe un opcode propio mientras corre sigue con lo
   aprendido hasta la siguiente entrada; `difftest` lo delataría.

`tests/run_tests.sh` lo prueba con la BIOS propia y una variante del juego de prueba (`mkcontrato --ram`) que
copia una rutina a `$100100`, la llama en cada cuadro y le parcha el inmediato: la segunda ronda compila 6
instrucciones (1 con un operando variable) y no deja nada en el intérprete, sin diferencias; `neotrace` y el
ejecutable anotan las mismas líneas que `difftest`. `difftest` dice qué parte del tiempo del intérprete fue ROM
(P y BIOS) o RAM («Intérprete por zona»).

### El proyecto generado

`neorecomp -o carpeta` escribe `runtime/` (el runtime de este repo, que va dentro de `neorecomp`),
`platform/` (la ventana, `difftest`, `bench`), `gen/` (`code_NNN.c` de unas 3000 instrucciones cada uno,
para compilar en paralelo, `dispatch.c`, `ram.c` con el código aprendido de RAM, `info.c` con el SHA1 y el nombre, `gen.h`), `bios/` (el código de
la BIOS de la caché y `bios.c`), un `Makefile` (con `-DNEO_RECOMP`) y `LEEME.txt`. No lleva datos del juego
ni de la BIOS: el ejecutable busca el juego (el argumento, la copia que guarda la primera vez en su carpeta
de datos como `juego.neo`, o un `.neo` o `.zip` de la carpeta actual con el SHA1 correcto) o lo pide, y
comprueba el SHA1 de la imagen normalizada; la BIOS, como el intérprete. `--interp` corre solo el
intérprete, `--force` acepta otro juego (con el intérprete) y `--uncovered archivo` guarda lo que el
intérprete tuvo que ejecutar. Ayuda → Este juego dice cuántas instrucciones se recompilaron y qué parte de
los ciclos corrió en código recompilado.

### difftest

`difftest juego [cuadros] [--seed S] [--input archivo] [--uncovered archivo] [--screens carpeta] [--bench]
[--executed archivo]`, las mismas opciones que en los otros recompiladores. La máquina B corre función por
función (recompilada, o el intérprete donde no hay código); tras cada función la A (solo intérprete) avanza
hasta el mismo ciclo y se comparan el 68000, los ciclos, la RAM, la VRAM, los registros del LSPC (dirección,
modo, temporizador, interrupciones, líneas), la paleta, el latch del sistema y el banco de P2, la tarjeta,
el Z80 con su RAM, sus bancos y la comunicación con el 68000, el YM2610, la mezcla de audio, el bus abierto
y las excepciones atendidas; al final de cada cuadro, también la imagen y el audio listo. `--uncovered`
escribe en formato de traza lo que B tuvo que interpretar (del juego y de la BIOS), para volver a
recompilar con `--trace`.

`tests/recomp_game.sh JUEGO NOMBRE [CUADROS] [SEMILLAS...]` hace el ciclo completo de un juego, como en los
otros recompiladores (y lo usa la GUI): una traza guionada, recompilar, compilar, `difftest` con cada
semilla, juntar lo que cayó al intérprete y repetir una vez con esa traza.

## Bancos, mappers y guardado

### Mapa de memoria del 68000

`bus.c`:

| Dirección | Qué hay |
|---|---|
| `$000000-$0FFFFF` | P1: el primer MB del programa (con espejos si mide menos). `$000000-$00007F` muestra los vectores de la BIOS hasta que se escribe `REG_SWPROM` (`$3A0013`); `REG_SWPBIOS` (`$3A0003`) los vuelve a poner. |
| `$100000-$1FFFFF` | RAM de 64 KB con espejos. TAS escribe (en el Genesis no). |
| `$200000-$2FFFFF` | P2: fijo si P mide hasta 2 MB; si mide más, el banco de 1 MB que eligió la última escritura en `$2FFFF0-$2FFFFF` (los 3 bits bajos, con espejos si hay menos bancos; cada cambio sube `map_ver`). La wiki dice que cualquier escritura en la ventana cambia el banco; MAME, solo en `$2FFFF0`. Se sigue a MAME. |
| `$300000` | `REG_P1CNT`: cruz y A B C D del mando 1 (activos en 0). `$300001` es el DIP del MVS (lee `$FF`) y escribirlo es el perro guardián del MVS (el AES no tiene). |
| `$320000` | `REG_SOUND`: escribir manda un comando al Z80; leer, su respuesta. `$320001` (monedas y reloj del MVS) lee `$FF`. |
| `$340000` | `REG_P2CNT`. |
| `$380000` | `REG_STATUS_B`: bit 7 en 0 (AES), bit 6 tarjeta protegida, bits 5-4 en 0 con tarjeta, Select y Start de los dos mandos. `$380001` salidas de los mandos, `$380011` banco de la tarjeta. |
| `$3A0001-$3A001F` | Latch del sistema (bytes impares; el dato es el bit 4 de la dirección): sombra, vectores, bloqueo de la tarjeta (dos llaves), banco de la paleta. BRDFIX/CRTFIX y la RAM de respaldo no hacen nada en el AES. |
| `$3C0000-$3DFFFF` | LSPC (`lspc.c`). |
| `$400000-$7FFFFF` | Paleta: 8 KB del banco elegido, con espejos. |
| `$800000-$BFFFFF` | Tarjeta de memoria: bytes impares (los pares leen `$FF`), 2 ciclos de espera. |
| `$C00000-$CFFFFF` | BIOS de 128 KB con espejos. `$D00000-$FFFFFF`: nada (el AES no tiene RAM de respaldo). |

Lo que no responde devuelve el último valor del bus de datos (`open_bus`). Los registros de 16 bits (LSPC,
paleta) reciben un byte escrito en las dos mitades, porque el 68000 lo pone en las dos.

### Cartucho

`cart_load_path` acepta tres formatos y los normaliza a la misma imagen:

- **`.neo`**: cabecera de 4096 bytes (`NEO\1`; en little-endian los tamaños de P, S, M, V1, V2 y C en
  `$04-$1B`, año, género, pantalla y NGH en `$1C-$2B`; nombre de 33 bytes en `$2C` y fabricante de 17 en
  `$4D`) y las regiones en ese orden.
- **Zip al estilo MAME** (`zip.c`: directorio central, archivos guardados o con deflate, CRC32 comprobado;
  sin zip64): un archivo por chip. La base de datos (`neodb.c`) dice qué es cada chip por su CRC32 y su
  tamaño; si el juego no está, se deduce de los sufijos: `.p1`, `.p2`/`.sp2`, `.s1`, `.m1`, `.v1`-`.v9` (bus
  A, o los dos), `.v11`-`.v19` (bus A) y `.v21`-`.v29` (bus B), `.c1`, `.c2`... (también `juego_p1.rom`: lo
  que sigue al último `-` o `_`). Un juego que se arma por los sufijos lo dice en `--info`.
- **Carpeta** con los mismos archivos sueltos.

La **imagen normalizada** son las regiones en el orden del `.neo` y con su forma:

| Región | Contenido |
|---|---|
| P | El programa en el orden de bytes de los chips: palabras con los bytes invertidos (el byte de la dirección `a` del 68000 está en `a ^ 1`), en orden lineal. Un P1 de 2 MB viene con las mitades cambiadas (su segundo MB es `$000000`) y se acomoda; se reconoce por la base de datos o porque así aparece `NEO-GEO` en `$100`. Un P1 de menos de 1 MB con P2 se repite hasta 1 MB. |
| S | La capa fija. |
| M1 | El programa del Z80. |
| V-A | Las muestras del bus A (ADPCM-A, y también ADPCM-B si el juego usa un solo bus), seguidas. |
| V-B | Las del bus B, solo en los juegos viejos con los buses separados (`v21`...). |
| C | Cada par de chips intercalado byte a byte: C1 en los bytes pares y C2 en los impares, luego C3/C4... |

Es la forma del `.neo` (comprobado contra el código de los convertidores y de Geolith, que invierte los bytes
de P al cargarlo), así que **el SHA1 de la imagen normalizada es el de la parte de datos de un `.neo`**: el
mismo juego da el mismo SHA1 como zip, como carpeta o como `.neo`. También se calcula el SHA1 de cada región,
para decir cuál no coincide. Con Metal Slug (`mslug` de MAME): `582233c4d95e4a2d80dd78a9de29b92e0f60d40f`,
comprobado con un cálculo aparte.

`finish` lee la cabecera de `$100` (`NEO-GEO`, NGH en BCD, tamaño declarado, RAM de respaldo, logo de SNK,
DIP de cada región, los `JMP` de `$122` USER, `$128` PLAYER_START, `$12E` DEMO_END y `$134` COIN_SOUND, el
puntero de seguridad de `$182`) y los 64 vectores. `cart_unsupported` rechaza lo que todavía no se puede
correr: sin `NEO-GEO` en `$100` (no es de Neo Geo, o está cifrado), los NGH de los juegos cifrados (`$251`
a `$275` y unos pocos de antes) y los de protección del programa (Fatal Fury 2, Super Sidekicks, Metal Slug
X); avisa de los que piden un periférico (enlace, mahjong, trackball) y del set cifrado de KOF '98.

### BIOS

`bios.c`. La BIOS es de SNK, la pone el usuario y se acepta por su SHA1: `neo-epo.bin` (AES de
exportación, `1b3b2209...`) y `neo-po.bin` (AES de Japón, `4e4a440c...`), sueltas o en el `aes.zip` de MAME
(la primera aceptada, en ese orden). Mide 128 KB y sus palabras vienen con los bytes invertidos, como P. Su
byte `$401` es la región que ven los juegos: Japón (0) en `neo-po.bin` y **Europa (2)** en `neo-epo.bin`: no
hay BIOS del AES de EE. UU. `bios_find` la busca en `$NEO_BIOS`, junto al juego y en `roms/`, después de la
propia. En el AES no
hay SFIX ni SM1: la capa fija y el Z80 salen siempre del cartucho.

**La BIOS propia** (fase 10, `tools/neobios/`, MIT): `neobios.s`, escrita desde cero con la interfaz de la wiki
de NeoGeoDev y lo que se ve desde afuera con la original (qué escribe en los registros y cuándo, qué variables
de la RAM lee o escribe cada juego), sin traducir ni copiar el código de SNK. La ensambla `asm68k.py`, un
ensamblador del 68000 hecho aquí (Python sin dependencias; `make neobios` deja `build/neobios.bin` en el orden
de los chips, como `neo-epo.bin`), que se prueba contra el desensamblador (`tests/asm68k/`). El runtime la
acepta por su firma (`"NEORECOMP BIOS"` en `$100`), no por el SHA1; `bios_find` la toma antes que la de SNK
(`neobios.bin` junto al juego, en `roms/` o en `build/`, donde la deja `make`), y también va con
`--bios`/`NEO_BIOS`. Las pruebas cuyas referencias se hicieron con la de SNK (`tests/golden.sh`,
`tests/run_tests.sh`, `tools/compat`, `tools/oracles`) fijan `NEO_BIOS=roms/aes.zip` si está. Se recompila como la original.
Lo que hace:

- **Arranque:** RAM en cero, paletas en negro, `FIX_CLEAR` y `LSP_1st`, las variables (`BIOS_COUNTRY_CODE` 2
  como `neo-epo.bin`, `BIOS_MESS_POINT`...), los DIP por software con los valores por omisión de la tabla de su
  región en la cabecera (los 6 bytes de los ajustes especiales y el nibble alto de cada ajuste simple: lo que
  deja la original en `BIOS_GAME_DIP`), el comando `$03` al Z80 (como la original), y `USER` con
  `USER_REQUEST` 0, 1 (si el juego hace su pantalla de arranque, `$114` = 1) y 2 para siempre, con los
  vectores del cartucho y `BIOS_SYSTEM_MODE` en `$80` (el VBLANK del juego).
- **Llamadas:** `SYSTEM_RETURN`, `SYSTEM_IO` (los dos mandos con flanco y repetición, Start y Select, y
  `PLAYER_START` cuando un jugador que no está jugando aprieta Start), `SYSTEM_INT1` (el VBLANK de la BIOS:
  `SYSTEM_IO` y `MESS_OUT`), `SYSTEM_INT2`, `MESS_OUT` (los 14 comandos, también el texto de 8 × 16, con la
  mitad de abajo en la página siguiente de la fuente, y el japonés), `FIX_CLEAR`, `LSP_1st`,
  `CONTROLLER_SETUP`. Las del MVS (créditos, calendario, HOW TO PLAY) vuelven sin hacer nada.
- **Lo que piden los juegos y no está documentado:** una **tabla de azar** de 256 bytes en `$C04200` (KOF '98
  saca números de ahí; es una permutación propia de 0-255, no la de SNK) y la bandera **`$10FE8C`**, que quien
  quiere esperar un cuadro pone en 1 y el VBLANK de la BIOS vuelve a 0 (KOF '98, en su pantalla de arranque).
  Las variables propias van en `$10FE90-$10FEBE`: ningún juego de `roms/` escribe ahí, y sí, por ejemplo, en
  `$10FC00-$10FC7B` (KOF '98, como RAM propia).
- **La tarjeta de memoria** (`CARD`, los comandos 0-7, y `CARD_ERROR`): con el formato que deja la original,
  medido desde afuera con `neocard` (abajo). Bloques de 64 bytes:
  - **Bloque 0, la cabecera:**
    - `$0A`: el tamaño en el bus, el doble de los bytes (`$1000` en la de 2 KB);
    - `$0D`/`$0E`: las sumas de las dos FAT;
    - `$0F`: el nombre puesto (0: no; cualquier otro valor: sí), y desde `$10` los 16 bytes del nombre;
    - `$20`: la marca `"NEO-GEO",$80` en los bytes pares (los impares no se miran);
    - `$30`: la región.
  - **Desde el bloque 1, el directorio:** una entrada de 4 bytes por bloque de la tarjeta (sub, `$FF` libre;
    el NGH; el primer bloque).
  - **Las dos FAT:** cada una ocupa bloques enteros, con un byte por bloque (0 libre, 1 el último, 2
    reservado; si no, el siguiente). Después vienen los datos; en la de 2 KB, desde el bloque 5. Todo eso
    sale del tamaño de la cabecera.

  La **suma de una FAT** es la de todos sus bytes, también los de más allá de los bloques que hay. Al
  cargar, guardar, borrar o leer un título, una copia que no da su suma se rehace con la otra; si las dos
  están mal, `$83`. Si las dos están bien y son distintas, gana la FAT 1. Buscar no mira la FAT.

  **El orden de los errores:**
  - no hay tarjeta: `$80`;
  - formatear, guardar o borrar con la tarjeta protegida: `$85`, antes de mirar el formato;
  - sin formato: `$81` (buscar deja antes `BIOS_CARD_SUB` en 0);
  - guardar el nombre con la tarjeta protegida: `$85`, después de mirar el formato;
  - la FAT mal: `$83`.

  **Guardar** escribe la cadena nueva en los bloques libres más bajos antes de soltar la vieja. Por eso hace
  falta lugar para la cadena entera, también al reemplazar una partida (si no, `$84`). El byte `sub` de una
  entrada nueva se escribe al final.

  **Formatear** mide el tamaño por dónde se repite la tarjeta (2, 4 u 8 KB; si no, 16 KB). Borra la cabecera
  primero y escribe la marca al final.

  Un comando que no existe cuelga a la original; la propia vuelve sin tocar nada.

  **`CARD_ERROR`**: la del AES solo atiende `$85`. Muestra el aviso con la letra de 8 × 16 de la ROM S del
  juego (mosaicos `$100` + ASCII arriba y `$200` + ASCII abajo, la paleta 0) y espera un botón:
  - guarda la capa fija y los colores que cambia en la zona de la BIOS (`$10F300`);
  - esconde los sprites (los vuelve a poner el juego);
  - deja todo lo demás como estaba.

  El texto es propio.
- **Todavía no:** la animación de la pantalla de SNK (con `$114` en 0 se salta: el juego empieza unos 470
  cuadros antes que con la original).

**`neocard`** usa la tarjeta por la interfaz de una BIOS, desde afuera, para ver qué hace cada una sin leer su
código:
- corre el juego hasta un cuadro;
- pone las variables de `CARD` y llama a `$C00468` con las interrupciones enmascaradas;
- muestra la respuesta y lo cargado.

Tiene órdenes para dañar la tarjeta a propósito, sacarla o protegerla, y `error:RESP`, que llama a
`CARD_ERROR` cuadro por cuadro y dice qué cambió en la VRAM, las paletas y la RAM. `tools/neobios/tarjeta.py`
muestra lo que importa de un archivo de tarjeta: la cabecera, las partidas con su cadena y las FAT, sin los
bytes libres, que cada BIOS deja a su manera. Así se comparan dos tarjetas.

`neorun --cardlog` imprime las llamadas de un juego a `CARD` y a `CARD_ERROR`. Los 7 juegos de `roms/` que usan
la tarjeta la leen al empezar la partida: Shock Troopers no la usa, Metal Slug 2 y The Last Blade buscan y los
otros cargan. Ninguno guardó en las corridas guionadas: 30 000 cuadros cada uno, casi una hora de juego en total, quietos y
sin llegar a perder del todo.

**Prueba:** `tests/neobios/prueba_tarjeta.sh` (en `make test`) corre `tarjeta.guion` en el juego del contrato
y lo compara con lo que dio la original:
- 60 llamadas: formatear, guardar, reemplazar, sin lugar, protegida, sin tarjeta, FAT y cabeceras dañadas;
- las respuestas, lo cargado y la estructura que queda.

Las dos BIOS dan lo mismo también cruzadas: una tarjeta escrita por una y leída o modificada por la otra.

La **tabla L0** (`000-lo.lo`, un chip de la placa, ni de la BIOS ni del cartucho) dice qué línea de un
sprite se dibuja con cada reducción vertical: 256 tablas de 256 bytes (mosaico en el nibble alto, línea en
el bajo); el chip de 128 KB repite esa mitad. Se calcula (`l0_generate`): la tabla z tiene z + 1 líneas, las
de `invertir_bits(i) XOR $88` para i = 0..z, ordenadas, y el resto en `$FF`. La fórmula se dedujo del
contenido del chip y da su SHA1 (`5992277d...`); si el `aes.zip` trae el chip, `--info` dice si son iguales.

### Guardado

- **Estados guardados** (`neo_state_save`/`neo_state_load`, cuatro ranuras en `neo`: `<juego>.estado1-4`):
  una cabecera (magia, versión, tamaño de `Neo`, SHA1 del juego, tamaño de la tarjeta), la estructura `Neo`
  entera (68000, Z80, LSPC con la VRAM y la imagen, paleta, YM2610, mezcla, RAM) y el contenido de la
  tarjeta. **Sin la BIOS ni la tabla L0**: se guardan en cero y al cargar se ponen las de esta corrida (un
  estado nunca lleva la BIOS de SNK), igual que los punteros. Solo los lee la misma versión con el mismo
  juego. `statetest` lo prueba: tras cargar, los mismos cuadros dan exactamente lo mismo (imagen, sonido,
  RAM, VRAM, paleta, CPU, YM2610), un estado de otro juego no se carga y la BIOS no aparece en el estado.

El AES no tiene RAM de respaldo: las partidas van a la **tarjeta de memoria**, que maneja la BIOS
(`CARD`). Se emula una de 2 KB (`neo_card_insert`/`neo_card_save`, `neorun --card archivo`): sin tarjeta,
`REG_STATUS_B` dice que no hay; con tarjeta, solo se escribe con las dos llaves abiertas (`CRDUNLOCK1`, que
es la salida en 0, y `CRDUNLOCK2`) y si no está protegida. Por omisión no hay tarjeta puesta.

## Video y audio

### LSPC

`lspc.c`. Registros de `$3C0000` (de 16 bits; con espejos cada 16 bytes): `VRAMADDR`, `VRAMRW` (escribir
avanza la dirección `VRAMMOD`, con signo, sin pasar de la VRAM lenta `$0000-$7FFF` a la rápida
`$8000-$87FF` ni al revés; leer `$3C0000` o `$3C0002` da la palabra de la dirección), `VRAMMOD`, `LSPCMODE`
(al leer, el contador de líneas en los bits 15-7 y el de autoanimación en 2-0), `TIMERHIGH`/`TIMERLOW`,
`IRQACK` y `TIMERSTOP` (solo PAL). La autoanimación avanza en cada VBLANK cada (velocidad + 1) cuadros.

**El temporizador** baja con el reloj de pixel (un paso cada 2 ciclos del 68000) y no se lleva paso a paso:
se guarda el ciclo en que llegará a 0 (`timer_at`). Desde que se carga con N tarda N + 1 pixeles (el
ejemplo de la wiki con Neo Turf Masters y la cuenta de MAME coinciden). Se carga al escribir `TIMERLOW` (si
`LSPCMODE` tiene el bit 5), al empezar el cuadro (bit 6) y al llegar a 0 (bit 7); al llegar a 0 pide la
interrupción de nivel 2 si el bit 4 lo permite.

**El dibujo** (`lspc_render.c`) es de a una línea, al empezar cada línea visible (como los búferes de
línea de la consola, que llenan la línea siguiente mientras se muestra la actual): así lo que cambia una
interrupción del temporizador sale desde la línea que sigue. La imagen queda en `fb` (320 × 224 en ARGB).

- **Gráficos convertidos al cargar** (`gfx_init`): la C ROM a 16 filas de 64 bits por mosaico (el pixel x
  en los bits 4x a 4x+3) y la S ROM a 8 filas de 32 bits. En la C cada mosaico de 128 bytes son cuatro
  bloques de 8 × 8 (arriba a la derecha, abajo a la derecha, arriba a la izquierda, abajo a la izquierda);
  cada fila, dos bytes por chip (planos 0 y 1 en el impar, 2 y 3 en el par) con el pixel de la izquierda
  en el bit 0. En la S, columnas de dos pixeles (el de la izquierda en el nibble bajo): los bytes
  `$00-$07` son los pixeles 4-5, `$08-$0F` los 6-7, `$10-$17` los 0-1 y `$18-$1F` los 2-3. Los dos
  formatos son los de la wiki de NeoGeoDev y se comprobaron con los juegos (los textos se leen).
- **Sprites:** se recorren los 381 (del 1 al 381; el último queda encima) con sus SCB. Un sprite pegajoso
  (bit 6 de SCB3) toma Y, alto y reducción vertical del anterior y va a su derecha (la X del anterior más
  su ancho reducido). La línea dentro del sprite es `(línea + 16 - (512 - Y)) mod 512` (arriba de la
  pantalla está en 496 - Y). Se dibujan los primeros 96 que tocan la línea. Qué mosaico (de los 32) y qué
  fila sale de la tabla L0 con la reducción vertical: la mitad de arriba directo y la de abajo al revés; con
  alto 33 el sprite se repite cada 2 × (reducción + 1) líneas. Después: autoanimación (el contador
  reemplaza los 3 o 2 bits bajos del mosaico), volteo vertical, la máscara de la reducción horizontal
  (qué pixeles de los 16 se dibujan, la tabla de la wiki) y el volteo horizontal.
- **Capa fija:** encima de los sprites; la fila de mosaicos es línea / 8 + 2.
- **Color:** el 0 de cada paleta es transparente; el fondo es el color `$0FFF` del banco actual. Cada
  componente tiene 5 bits y el bit de oscurecido es un sexto, común y al revés (con él, más oscuro): 6
  bits que se llevan a 8 repitiendo los altos. `pal_argb` guarda la paleta ya en ARGB y se pone al día en
  cada escritura. El modo sombra (`REG_SHADOW`) oscurece toda la imagen a la mitad (la wiki no dice
  cuánto: sin comprobar).

### Z80

`z80bus.c`. Su mapa: `$0000-$7FFF` los primeros 32 KB de la M1 del cartucho; ventanas de 16, 8, 4 y 2 KB
en `$8000`, `$C000`, `$E000` y `$F000`, que se eligen leyendo los puertos `$0B`, `$0A`, `$09` y `$08` con el
número de banco en la mitad alta de la dirección (al encender, como si la M1 fuera lineal); `$F800-$FFFF`,
2 KB de RAM. Puertos: `$00` el comando del 68000 (leerlo da por atendida la NMI), `$04-$07` el YM2610,
escribir `$08`/`$18` habilita o deshabilita la NMI, `$0C` la respuesta. Cada comando del 68000 deja la NMI
pedida; la línea de la NMI es "pedida y habilitada" y el Z80 la atiende en el flanco (si ya estaba pedida, un
segundo comando no da otra). La INT viene de los temporizadores del YM2610. Un `HALT` sin nada que atender
se salta de una vez hasta el destino o hasta que venza un temporizador; lo mismo los **bucles de espera**
(`ld a,(hl) / and a / jp z,bucle`): si son puros (sin puertos; cada registro que leen lo escriben antes en
la vuelta o no lo tocan; a lo sumo un contador en la RAM, `ld a,(nn) / inc a / ld (nn),a`, que suma k), cada
vuelta deja el Z80 igual salvo el tiempo y R, y se saltan las vueltas enteras que caben. Lo que dura una
vuelta se mide entre dos llegadas seguidas al comienzo.

### Audio

- **YM2610** (`ym2610.c`): su reloj es un tercio del maestro (8 MHz). Una muestra del FM cada 144 ciclos
  suyos (432 maestros: 55 944 Hz con el reloj del AES); en cada una se calculan el FM, 9 pasos del SSG, el
  ADPCM-B y, cada 3, el ADPCM-A, y todo sale a la mezcla como un tramo de 432 ciclos maestros. Se pone
  al día (`ym_run`) antes de cada escritura del Z80 y al terminar el cuadro.
  - **FM:** el YM2612 de GENRecomp sin cambios (fase con detune, múltiplo y PM; envolvente con la tabla
    del chip, escala por tecla, TL, AM y SSG-EG; seno logarítmico y exponencial; los 8 algoritmos y la
    realimentación; panorama; LFO; canal 3 especial), sin DAC y con solo los 4 canales que existen (los 2,
    3, 5 y 6 del OPNA; la tecla de los otros dos no hace nada). Los canales callados no se calculan.
  - **SSG** (como el YM2149; wiki de NeoGeoDev, "SSG"): va a 500 kHz (8 MHz / 16, 9 pasos por muestra del
    FM, promediados). El tono cambia cada TP pasos (TP = 568 da 440 Hz), el ruido (LFSR de 17 bits, bits
    0 y 3) avanza cada 2 × NP y la envolvente da 32 pasos por periodo, uno cada EP, con las formas de
    `$0D` (seguir, subir, alternar, quedarse). Volumen fijo de 4 bits (nivel 2v + 1) o el de la envolvente,
    de 5 bits, en pasos de 1,5 dB. Sin signo: el pasa-altos de la mezcla le quita la continua.
  - **ADPCM-A** (wiki, "ADPCM"): 6 canales a 18 518 Hz desde la V ROM del bus A, 4 bits por muestra con el
    nibble alto primero; tabla de 49 pasos (16 a 1552), el índice cambia -1 -1 -1 -1 2 5 7 9 con los 3
    bits bajos del nibble y el acumulador es de 12 bits (da la vuelta). Atenuación = (63 - volumen maestro)
    + (31 - volumen del canal), en pasos de 0,75 dB. Al pasar su dirección de fin se calla y levanta su
    bandera.
  - **ADPCM-B:** un canal desde la V ROM del bus B (o la del A si el juego usa un solo bus); avanza Delta-N /
    65 536 muestras por muestra del FM e interpola entre la anterior y la nueva; acumulador de 16 bits y
    paso adaptativo (127-24 576, por 57, 57, 57, 57, 77, 102, 128 o 153 sesenta y cuatroavos). Volumen
    lineal (`$1B`), repetición (`$10`, bit 4), bandera al terminar.
  - **Banderas de fin** (puerto 6 del Z80: bits 0-5 ADPCM-A, bit 7 ADPCM-B): `$1C` con 1 las borra y las
    enmascara; con 0, las desenmascara. **Ocupado** (bit 7 del estado) durante 32 ciclos internos (576
    maestros) después de escribir un dato.
  - **Temporizadores:** A (10 bits) de a una muestra del FM y B (8 bits) de a 16, sin llevarlos muestra a
    muestra (se guarda cuándo vence cada uno: el Z80 los necesita para saber cuándo despertar de un
    `HALT`); sus banderas y la IRQ.
- **Mezcla** (`audio.c`, la de GENRecomp): cada muestra de salida (44 100 Hz, estéreo de 16 bits) es el
  promedio de lo que sonó durante ella; después, un pasa-altos de ~10 Hz como el acople de la salida de la
  consola. El tiempo real sale del reloj del AES (740 muestras por cuadro). **Niveles** (fase 9, medidos
  con `tools/oracles/oracle.py mix` contra Geolith y FBNeo en Metal Slug, Neo Turf Masters y Samurai
  Shodown II): la FM en la escala de 16 bits (9 bits por canal × 16) y el resto en proporción, con el
  promedio de las seis mediciones: ADPCM-A ×4,9 y ADPCM-B ×1,3 respecto de la escala de bits de antes, el
  SSG ×5,5 (Geolith lo pone unos 4 dB más fuerte que FBNeo: queda en el medio). Antes el ADPCM-A (voces,
  gritos, disparos) sonaba unos 13 dB más bajo. Todo por 0,87, el nivel general de los oráculos (que no
  recortan). Después del ajuste, cada parte queda a menos de unos 3 dB de los dos.
- **Pruebas:** `audiotest` maneja el chip como el Z80 y mide la afinación (FM y SSG a 440 Hz), el
  panorama, silenciar, los canales que no existen, el volumen y el ruido del SSG, dos formas de la
  envolvente, los primeros valores de una muestra de ADPCM-A (calculados a mano), el fin y su bandera con
  la máscara, el ADPCM-B a dos velocidades y con repetición, los temporizadores y la bandera de ocupado.
  `neorun --wav` guarda el sonido y su nivel por segundo (`--mute` silencia partes).

## Variantes de la consola y accesorios

Solo el AES, NTSC. El MVS (DIP, RAM de respaldo, monedas, SFIX/SM1, varios slots) y PAL, después.

- **Mandos** (`pad[0]`, `pad[1]`, bits `NEO_*`): cruz y A B C D en `REG_P1CNT`/`REG_P2CNT`, Start y Select en
  `REG_STATUS_B`. En el AES no hay monedas.
- **BIOS:** la de exportación declara la región Europa (`COUNTRY_CODE` 2 en la RAM de la BIOS).

## Interfaz del ejecutable

`neo` (`main_sdl.c`): ventana con barra de menús, controles y ritmo de 59,60 cuadros por segundo (el del
AES). La barra de menús y los diálogos vienen de Moskva 89, GBRecomp, NESRecomp y GENRecomp (`ui_menu.c`,
`ui_font.c`): se dibujan dentro de la ventana para verse igual en todos los sistemas, en español o inglés
(`i18n.c`). Con un menú o un diálogo abierto, el juego se pausa.

- **Juego:** abrir (o arrastrar a la ventana un `.neo`, un zip o una carpeta), reiniciar (el botón de reset
  de la consola), pausa, tarjeta de memoria (ponerla o sacarla; se guarda en `<juego>.tarjeta` al salir, al
  sacarla y cada 5 segundos si cambió).
- **Video:** tamaños 1x-6x, pantalla completa, escala entera y aspecto 4:3.
- **Audio:** sonido (Ctrl+M), volumen y silenciar cada parte (FM 1-4, SSG, ADPCM-A, ADPCM-B); en pausa, con
  un menú abierto o en avance rápido, no suena.
- **Exportar:** captura PNG (F12). **Idioma** y **Ayuda** (controles, este juego, acerca de).
- **Controles:** jugador 1 con flechas, Z X C V = A B C D, Enter = Start, Mayús = Select; jugador 2 con
  I J K L, B N M , = A B C D, H = Start, G = Select; mandos con A B X Y = A B C D (el arco del mando del
  AES, de abajo hacia arriba), Start y Back = Select.
- **BIOS:** `--bios archivo`, o la que encuentre el runtime (`NEO_BIOS`; la propia y luego la de SNK, junto al
  juego, `roms/`), o la copia
  de la carpeta de datos (`bios.bin`), o se pide (diálogo o arrastrarla). La que no venía de la copia se
  guarda ahí. Carpeta de datos: `NEORecomp/interprete/` (con las preferencias y lo exportado).

- **Estados:** guardar (F5) y cargar (F8) en cuatro ranuras.

Opciones: `neo [juego] [--bios archivo] [--scale N] [--record archivo | --play archivo] [--trace archivo]`.
`--record` y `--play` graban y reproducen los botones (`inputs.h`, el mismo formato que `--input` de las
herramientas sin ventana); mientras tanto no se puede reiniciar, cargar estados ni cambiar de juego.
`--trace` agrega al salir las instrucciones de ROM que se ejecutaron (del juego y de la BIOS), como traza
para `neorecomp`. Para pruebas sin ventana: `--ui-shot archivo.png` con `--ui-frames N`, `--ui-menu N` y
`--ui-action N`. El modo recompilado llega en la fase 6.

Icono: `NEOGEO.png` (el mando del AES, serie TriMui) de
[KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), CC BY 4.0; la
ventana lo recorta al dibujo y `icono.ico` (Windows) es ese recorte llevado a 256x256 (`tools/make_icon.py`).

## Empaquetado y pruebas

- **`make dist`:** deja `dist/neorecomp-<versión>-<sistema>.tar.gz` con `neorecomp`, el README, la licencia
  y este documento. La versión sale de `git describe`.
- **Integración continua:** el mismo flujo que en los otros recompiladores (Linux, macOS, MSYS2 UCRT64),
  planeada pero todavía no (decisión del usuario): sin la BIOS del AES en el CI solo correrían las pruebas que
  no la necesitan, hasta la BIOS propia de la fase 10.
- **Herramientas sin ventana** (las mismas opciones que en los otros recompiladores, `cli.h`: `--frames N`,
  `--input archivo` con el guion de `inputs.h`, `--seed S` con el mando aleatorio común de `random_pad.h`
  traducido al Neo Geo, `--repeat N` en `neobench`, el número de cuadros suelto y `boton:desde-hasta` con
  `up down left right a b c d start select`): `neotrace` (traza `BANCO:DIRECCION`), `neoshot` (captura;
  `--expect` y `--hash` de todos los cuadros y todo el audio), `neoaudio` (WAV y nivel por segundo),
  `neobench` (velocidad), `neoramdump` (la RAM de `$100000` a `$10FFFF`). La BIOS la buscan como el runtime.
  El guion de entradas: `cuadro BOTONES [BOTONES]` con `UP DOWN LEFT RIGHT A B C D START SELECT` unidos con
  `+`, `-` o `$hex` (los bits `NEO_*`). El mando aleatorio pulsa A, B, Start y la cruz (C, D y Select solo en
  los guiones).
- **Contrato común** (`tests/contract.sh`, el mismo archivo en los cuatro repos; si cambia, se copia a los
  otros tres): la rama `neo` con el juego de prueba `testroms/neo/contrato.neo`. Se cumple: las cuatro
  herramientas aceptan las opciones, la misma entrada da la misma traza, captura y sonido, y `--input` da el
  mismo `neoshot --hash` que `boton:desde-hasta`.
- **El juego de prueba del contrato** (`mkcontrato`, en `make testroms`): un programa propio (MIT) hecho en C,
  que no se baja de ningún lado. Sigue el protocolo de la BIOS (cabecera, USER con `SYSTEM_RETURN`, el
  VBLANK propio con `SYSTEM_IO`, el código de seguridad de SNK que compara la BIOS) y depende de las entradas
  en las tres salidas: un mosaico blanco por cada botón apretado y, al cambiar los botones, un comando al
  Z80, que toca un tono del SSG. La BIOS le da el control en el cuadro 8. Necesita la BIOS del AES (la de la
  fase 10 servirá para el CI). La wiki de NeoGeoDev tiene una errata en el código de seguridad (la palabra
  25 es `$6606`, no `$0606`): la delató la comparación de la BIOS contra Metal Slug.
- **`make testroms`** (`tests/testroms.sh`): las SingleStepTests del 68000 y del Z80, en los mismos commits
  fijos que GENRecomp (se quedan comprimidas en `testroms/`, que no se versiona), y el juego de prueba del
  contrato (`testroms/neo/contrato.neo`).
- **`neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR] [--fix] [--shot/--shots] [boton:desde-hasta]`:**
  corre sin ventana con el intérprete y resume instrucciones, ciclos, excepciones, el estado del Z80, las
  variables de la BIOS en la RAM (`SYSTEM_MODE`, `USER_REQUEST`, `USER_MODE`...) y los PC más frecuentes del
  último cuadro. `--fix` muestra la capa fija como texto (el byte bajo de cada mosaico como ASCII: muchos
  juegos ponen su fuente así, Metal Slug en `$300` + ASCII). `--shot archivo.png` guarda la imagen del
  último cuadro y `--shots carpeta --every N` una cada N cuadros; dice cuántos colores distintos tiene.
- **Oráculos** (`tools/oracles/`, de GENRecomp): `retro_run` corre un núcleo libretro sin ventana con las
  entradas de las herramientas (los botones se asignan por las descripciones que publica cada núcleo) y da
  la RAM del 68000 como `neoramdump`, una captura o el sonido (`--wav`, con el nivel por segundo de
  `neorun`). `oracle.py` baja los núcleos a `build/oracles/` (con su SHA-256) y arma lo que necesita cada
  uno con los archivos del usuario: **Geolith** (BSD-3) carga el `.neo` que escribe `neorecomp --neo` y busca
  `aes.zip` en `build/oracles/sys/`; **FBNeo** (licencia no comercial, solo como programa aparte) carga el zip
  de MAME por el nombre del set desde `build/oracles/fbneo_roms/` (enlaces a `roms/`; un clon de un zip
  merged apunta al mismo zip) con un `neogeo.zip` que lleva las BIOS del AES. Los dos en modo AES; Geolith sin
  recortar los bordes (da 320 × 240) y FBNeo recortado (304 × 224): `compare` mide la imagen en el área común.
  FBNeo corre a 59,18 cuadros por segundo (el reloj del MVS), así que su RAM se va corriendo; Geolith, al
  mismo ritmo que NEORecomp, coincide en Metal Slug un 99,9 % en la RAM y un 100 % en la imagen. `mix`
  estima los niveles de la mezcla (ver Audio).
- **Prueba de compatibilidad** (`tools/compat/run.py`, `report.py`): el motor es el común de los cuatro
  recompiladores (`tools/retrorecomp/compat.py`); aquí va lo propio del Neo Geo (el zip entero es el juego,
  los zips de BIOS se saltan, los datos de `neorecomp --info`, categorías por el programa, capturas y guiones
  más tarde porque la BIOS tarda unos 20 s, y los oráculos). Resultado en `docs/COMPATIBILIDAD.md` y
  `docs/compat/`.
- **`make test`:** `inputtest`, `statetest` (con el juego de prueba y con Metal Slug), el contrato común; el
  recompilador (generar, compilar `difftest` y compararlo contra el intérprete con el juego de prueba, 600
  cuadros, y con Metal Slug sin traza, 1200; las herramientas de análisis con el juego de prueba; la caché
  de la BIOS de las pruebas va en `build/bios-cache`); las SingleStepTests del 68000 por el intérprete y por el camino del código generado
  (~8 s cada una) y las del Z80 (~24 s); `z80test --nmi`; `carttest` (un juego sintético con el P1 de 2 MB de mitades cambiadas da la misma imagen,
  SHA1, cabecera y vectores desde los chips, como `.neo`, como zip guardado dentro de una carpeta y como
  carpeta; el inflate con códigos fijos y dinámicos, cortado y con salida chica; un zip dañado; los sufijos
  con P2 y buses de V separados; los errores; un juego cifrado y uno sin `NEO-GEO`; la tabla L0 y su SHA1;
  una BIOS desconocida o de otro tamaño; la identidad del código en P1, en los bancos de un P de 5 MB y en
la BIOS con sus espejos, con la ida y vuelta por una línea de traza). `bustest`, con una BIOS de prueba
hecha en la prueba y juegos sintéticos: el mapa de memoria (vectores de la BIOS y del juego, espejos, bus
abierto, esperas de la tarjeta), los mandos, la paleta, la tarjeta y sus llaves, la VRAM y su incremento, el
contador de líneas, las interrupciones con un programa de 68000 (la de arranque una vez, 10 VBLANK en 10
cuadros, el temporizador cada 10 líneas, un VBLANK sin reconocer que vuelve a entrar), los bancos de P2 y un
programa de Z80 (banco de la M1, INT del temporizador A, NMI con el comando y su respuesta). `lspctest`, con
mosaicos sintéticos y la VRAM a mano: colores y bit de oscurecido, fondo, capa fija y transparencia,
sprite entero con volteos, reducción horizontal y pegajosos, prioridad, capa fija encima, reducción vertical
con la tabla L0, autoanimación, el límite de 96 por línea y que cada línea se dibuje con la VRAM de ese
momento. Con los juegos de `roms/`: `neorecomp --info` y que `--neo` dé el
  mismo SHA1; con Metal Slug, la base de datos, el SHA1, el P1 y las entradas; con `aes.zip`, que se acepte
  `neo-epo.bin` y que su `000-lo.lo` sea la tabla calculada; `neogeo.zip` (MVS) no se acepta; y que Metal
Slug arranque con la BIOS, llegue a la demo ("CREDITS 04" en la capa fija) y con Start empiece la partida;
que los tres juegos de validación dibujen (40 colores o más), y que `neo` dibuje cada menú y cada diálogo sin
ventana (`--ui-shot`); y que suenen (`neorun --wav`).

## Estado de la validación

- **CPU:** las 1 000 058 SingleStepTests del 68000 por el intérprete y por el camino del código generado
  (`m68ktest`, `--recomp`), y las 1 604 000 del Z80 (`z80test`), más la NMI (`z80test --nmi`).
- **Carga:** juegos (`.neo`, zip, carpeta) y BIOS, con `make test`. Probado con Metal Slug (`mslug.zip` de
  MAME) y las BIOS del `aes.zip` y del `neogeo.zip` de MAME.
- **Bus, interrupciones y Z80:** `bustest`. Con la BIOS original, Metal Slug pasa la pantalla de SNK, llega
  a la demo y empieza la partida con Start (`neorun`).
- **Video:** `lspctest`. Metal Slug (pantalla de SNK, demo, título, menú), Samurai Shodown II (intro,
  selección de personaje, pelea con la cámara reducida) y Neo Turf Masters (título, menús, el hoyo con el
  campo en perspectiva, que usa el temporizador: unas 13 700 interrupciones en 4200 cuadros) se ven bien.
  Con el intérprete y el dibujo, 16-20 veces el tiempo real.
- **Recompilador** (`tests/recomp_game.sh` con 1800 cuadros y las semillas 1 y 2): Metal Slug, Samurai
  Shodown II y Neo Turf Masters dan cuadros idénticos al intérprete. En la primera ronda (análisis más una
  traza guionada) quedan 52 a 109 instrucciones de ROM en el intérprete; en la segunda, ninguna. Los ciclos en
  código recompilado (del juego y de la BIOS): 100 % en Metal Slug y Samurai Shodown II, 99,7-99,8 % en Neo
  Turf Masters (la entrada a sus miles de interrupciones del temporizador la hace siempre el intérprete). Sin
  traza, Metal Slug ya da 99,9 %. Velocidad (`difftest --bench`, 3600 cuadros): recompilado 23-31 veces el
  tiempo real, el intérprete 17-20. Compilar un proyecto (`make -j8`): ~55 s (Metal Slug: 95 000
  instrucciones del juego; la BIOS, 5500).
- **Herramientas, estados y contrato:** `inputtest`, `statetest`, `tests/contract.sh` sin fallas.
- **Audio:** `audiotest`. Los tres juegos suenan: Metal Slug con FM y ADPCM-A, Samurai Shodown II también con
  ADPCM-B y Neo Turf Masters además con el SSG. Con el sonido, 14-18 veces el tiempo real.
- **Contra los oráculos** (fase 9, `docs/COMPATIBILIDAD.md`): con los 8 juegos, la RAM del 68000 coincide con
  la de Geolith entre el 99,6 % y el 100 % tras 1200 y 2400 cuadros (con entradas), y la imagen es idéntica o
  casi; FBNeo, que corre a otro ritmo de cuadros, coincide en la imagen. Eso respalda la temporización (la
  posición del VBLANK y de la recarga del temporizador, el reloj de cada chip) y los bancos de P2, aunque no
  la prueba línea por línea. Sigue sin comprobar por separado: lo que leen `$300001` y `$320001` en el AES y el
  perro guardián (MAME no lo emula en el AES).

### Línea base para optimizar el runtime

`tests/golden.sh` corre cada juego de `roms/` y el juego de prueba del contrato 3600 cuadros (de la demo a la
partida, con las entradas de `tests/coverage.sh`) y guarda el hash de todos los cuadros y de todo el audio
(`neoshot --hash`) en `tests/golden.txt`, nombrados por el SHA1 de la imagen. Una optimización no debe
cambiar ni un bit (`--update` reescribe la línea base). En la fase 8, con `sample` de macOS sobre `bench`:

| | Antes | Después |
|---|---|---|
| Metal Slug, recompilado | 22,4× | 43,3× |
| Samurai Shodown II, recompilado | 24,0× | 55,3× |
| KOF '98, recompilado | 22,9× | 51,7× |
| Los 8 juegos, intérprete | 14,8-17,9× | 23,8-32,3× |

Lo que se hizo, todo sin mover un bit:

1. **El Z80 de a ratos** (arriba): se ponía al día tras cada instrucción del 68000 y se pasaba el tiempo
   entrando y saliendo de `z80_run_until`. Ahora solo en `$320000` y al final del cuadro.
2. **Bucles de espera del Z80** (arriba): el programa de sonido pasaba del 38 % (Metal Slug) al 90 % (Samurai
   Shodown II) de su tiempo esperando.
3. **Los sprites ya recorridos** (`SprCache`): las cadenas pegajosas y la lista de sprites con alto se arman
   una vez y se rehacen solo si cambiaron SCB2-4 (cada línea compara la copia con la VRAM: también sirve si
   alguien escribe la VRAM directo, como `lspctest`). Y el dibujo de un mosaico sin ramas (máscaras en lugar
   de un `if` por pixel; la reducción horizontal en una tabla de desplazamientos, volteada o no).
4. **El SSG callado**: con los tres canales en volumen fijo 0 suena 0 exacto, y sus contadores (tonos, ruido,
   envolvente) avanzan los 9 pasos de una muestra con aritmética.
5. **Bucles de espera del 68000 en el código generado** (`IDLE68`, `m68k_idle_skip`): `neorecomp` reconoce
   los bucles puros (de la cabecera al `Bcc`, sin saltos ni escrituras: `TST`, `CMP`, `CMPI`, `BTST`, `MOVE` a
   Dn) y el código mide una vuelta entre dos llegadas seguidas por el `Bcc` (una interrupción sale de la
   función y un `Bcc` no tomado borra la medida); el runtime mira que las direcciones sean de RAM o ROM y
   salta las vueltas que terminan antes del próximo evento del LSPC.

Queda (perfil de KOF '98 recompilado): el dibujo del LSPC, la FM del YM2610 y, en Metal Slug, el programa de
sonido, que trabaja de verdad (unos 42 000 ciclos del Z80 por cuadro: recompilar la M1 si alguna vez pesa) y
un bucle de espera del 68000 con un contador de 16 bits en la RAM que no se reconoce.
