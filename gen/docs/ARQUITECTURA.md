# Arquitectura de GENRecomp

GENRecomp toma una ROM de Sega Genesis y genera un proyecto C99 que se compila en un
ejecutable nativo. El ejecutable carga la ROM del usuario en tiempo de ejecución y valida su
SHA1: nunca se distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de NESRecomp,
GBRecomp y NEORecomp: lo que cambia entre las consolas queda dentro de cada una. Las que todavía no tienen
contenido dicen en qué fase de [PLAN.md](PLAN.md) llegan.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `gen.h` (estado y interfaz), `gen.c` (carga, reset, cuadro, SRAM), `cart.c` (formatos, cabecera, vectores, mapeo), `bus.c` (mapa de memoria del 68000), `io.c` (versión y mandos), `eeprom.c` (EEPROM serie), `vdp.c` (puertos, DMA, líneas), `vdp_render.c` (dibujo), `z80bus.c` (el mapa del Z80 y cuándo corre), `psg.c`, `ym2612.c`, `m68k_host.h`, `z80_host.h` y `audio_host.h` (lo que los núcleos y la mezcla comunes toman de esta consola). De `tools/retrorecomp/shared`, comunes con NEORecomp: `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete), `z80.h`/`z80.c` (el Z80), `audio.c`/`audio.h` (la mezcla), `dis68k.c`; también `sha1`, `console.h`. |
| `src/genrecomp/` | El recompilador. Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | SDL2 (`main_sdl.c`, menús y diálogos en `ui_menu`/`ui_font`/`i18n`, `settings.h` (los campos; `settings.c` es común), `icono.h`), `input_host.h` (los botones para `inputs.h` y `cli.h` comunes). De `tools/retrorecomp/shared`: `files`, `export`, `png`, `random_pad.h`, `inputs.h`, `cli.h`, `settings.c`. |
| `src/tools/` | Sin ventana, con las opciones comunes (`cli.h`): `gentrace`, `genshot`, `genaudio`, `genramdump` (y `genbench`, de `src/platform/bench.c`). Pruebas: `carttest`, `m68ktest` y `z80test` (SingleStepTests, con `json.h`; los tres de `tools/retrorecomp/shared`), `bustest`, `vdptest`, `audiotest`, `statetest`, `inputtest`. Diagnóstico: `genrun`. |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh` (las SingleStepTests por familia), `contract.sh` (contrato común). |
| `tools/` | `retrorecomp/` (la GUI común, submódulo), `make_icon.py` (los iconos a partir de `MD.png`). |

## Una sola fuente de verdad para el CPU

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

## Temporización

Cada acceso del 68000 al bus son 4 ciclos (`m68k_fetch`, `m68k_rd*`, `m68k_wr*`); los ciclos
internos van con `m68k_idle`. Los totales por instrucción coinciden con las SingleStepTests en todos
los casos. El orden de los accesos dentro de una instrucción coincide en el 78 % de los casos
(`m68ktest --bus`); lo que falta es sobre todo si una escritura va antes o después del último prefetch
(4 ciclos dentro de la misma instrucción).

El 68000 además espera al VDP (la FIFO llena, ver VDP) y al refresco de la DRAM: 2 ciclos cada 128,
cobrados en el límite de instrucción (`gen_after_step_inline`), para que el intérprete y el código
generado cuenten igual. Las dos cosas se ajustaron contra los oráculos (`tools/oracles/`): en los
23 juegos de `roms/` y Overdrive, cuadros 600 y 1200, el parecido medio con Genesis Plus GX pasó de
98,84 a 99,75 % de la RAM y de 89,6 a 95,6 % de la imagen; con BlastEm, de 98,89 a 99,69 % y de 85,3 a
90,4 % (los dos emuladores entre sí: 99,86 % y 92,7 %). La proporción del refresco se eligió entre 1/256
y 3/128 por ese mismo número; lo que importa es la proporción (64:1 da lo mismo que 128:2).

## Interrupciones y despachador

`gen_run_frame` corre hasta que el VDP termina el cuadro con `m68k_run`, que ejecuta instrucciones
bajo un solo `setjmp` (el error de dirección vuelve ahí; en macOS `setjmp` hace una llamada al sistema
y uno por instrucción costaba casi 10 veces el tiempo) y después de cada una llama a `vdp_sync`, que pone las líneas al día y fija el nivel de interrupción del 68000
(`cpu.ipl`): 6 si hay VINT pendiente y habilitada (registro 1, bit 5), 4 si hay HINT (registro 0, bit
4). El 68000 la atiende al empezar la instrucción siguiente si el nivel pasa su máscara (autovector,
como en el Genesis) y avisa con `m68k_int_ack`, que la borra en el VDP.

En los proyectos generados (`-DGEN_RECOMP`), `gen_run_frame` usa `m68k_run_hybrid`: antes de cada
instrucción, si el 68000 puede seguir sin el intérprete (`m68k_native_ok`: sin interrupción pendiente,
sin traza, sin STOP, sin estar detenido), busca código recompilado para el PC (`recomp_lookup`); si no
lo hay, ejecuta una instrucción con el intérprete. Las interrupciones las atiende siempre el
intérprete, y el código recompilado sale apenas hay una que atender.

## Recompilador

```
genrecomp juego.bin -o carpeta [--name nombre] [--trace traza.txt ...] [--coverage traza.txt]
genrecomp juego.bin [--info]
```

`--trace` lee las trazas de `gentrace`, de la ventana con `--trace` y de `difftest --uncovered`: una
línea `BANCO:DIRECCION` en hexadecimal por instrucción de ROM ejecutada, el mismo formato que NESRecomp,
GBRecomp y NEORecomp (`src/runtime/trace_line.h`, idéntico en los cuatro). El banco es el desplazamiento en la ROM
normalizada dividido entre 512 KB (las ventanas del mapper de SSF2) y la dirección es la del 68000 con la
que se ejecutó: `01:0A1234` es el desplazamiento `01 × $80000 + ($0A1234 & $7FFFF)` = `$0A1234` (sin SSF2
los dos coinciden). Se siguen leyendo las trazas viejas, `DESPLAZAMIENTO:DIRECCION` con 6 dígitos. Las marca
el intérprete (`g->trace`). `--coverage` dice cuántas instrucciones de una traza se encontraron.

### Identidad de las instrucciones

Una instrucción se identifica por su desplazamiento en la ROM normalizada. Sin mapper coincide con su
dirección (`$000000-$3FFFFF`, con los espejos de las ROM chicas); con el de SSF2, la dirección se
traduce con los 8 bancos de 512 KB. `gen_code_offset` hace esa traducción en tiempo de ejecución y
devuelve -1 si en esa dirección no se lee la ROM (la SRAM encima de la ROM, la EEPROM, la RAM).

### Descubrimiento

Las semillas son los vectores de `$000000` (`cart_vector`) que apuntan a la ROM, desde `$200` (los
que valen 0 son de los que no se usan). Desde cada una se siguen las instrucciones hasta un salto sin
vuelta (`BRA`, `JMP`, `RTS`, `RTE`, `RTR`, `ILLEGAL`, línea A/F), con los destinos constantes de
`Bcc`, `BRA`, `BSR`, `DBcc` y `JMP`/`JSR` absolutos o relativos al PC. Dentro de un banco de 512 KB el
destino está en el mismo banco; fuera, se usa el mapeo del arranque. Un opcode inválido al que se
llega siguiendo el flujo corta el recorrido (son datos), salvo `ILLEGAL` (`$4AFC`) o en una traza.

Los saltos indirectos se resuelven con heurísticas (`--no-tables` las apaga), mirando las últimas
16 instrucciones del recorrido:

- **Tabla de desplazamientos de 16 bits:** `move.w tbl(pc,d0.w),d1` / `jmp tbl(pc,d1.w)` (también con
  `lea tbl,a0` y `(a0,d0.w)`): cada entrada es un desplazamiento desde la base; la tabla termina donde
  empieza la primera rutina o en la primera entrada que no apunta a algo con pinta de código. Es la de
  los objetos de Sonic.
- **Tabla de instrucciones:** `jmp tbl(pc,d0.w)` sin una carga desde tabla: cada entrada es un
  `bra.w`, `bra.s` o `jmp` del mismo largo (los modos de juego de Sonic 1).
- **Tabla de punteros de 32 bits:** `movea.l tbl(pc,d0.w),a0` / `jmp (a0)` (Sonic 3).
- **Base constante:** `lea rutina,a0` / `jmp (a0)`.

Y otras especulativas (`--no-spec` las apaga), que se recorren solo cuando ya no queda nada seguro:

- **Punteros inmediatos:** `move.l #rutina,...` (no a un `Dn`), `movea.l #rutina,An`, `pea rutina` y
  `lea rutina,An`: punteros que se guardan para llamarlos después (los objetos de Sonic 2 y 3, las
  tareas de Comix Zone, los trampolines `JMP` en la RAM).
- **Tablas sueltas:** cuatro o más largos seguidos que apuntan, todos, a algo con pinta de rutina.
- **Tras un final:** después de un `RTS`, `RTE`, `RTR`, `BRA` o `JMP` suele empezar otra rutina, aunque
  solo se llegue a ella por un salto calculado (los trozos desenrollados de Mortal Kombat 3 que
  terminan en `jmp (a0)`).

"Con pinta de código" (`plausible`) es decodificar en línea recta sin guardar nada: opcodes válidos
(ni `ILLEGAL`, ni línea A/F, ni `ori.b #0,d0`, que son ceros), direcciones absolutas razonables
(cartucho, Z80, E/S, VDP, RAM) y destinos de saltos dentro de la ROM, hasta un final de rutina. Para lo
especulativo ese final tiene que llegar en menos de 64 instrucciones; para las tablas que el código
delata también valen 64 instrucciones válidas seguidas (las rutinas de inicialización son largas).

Descubrir de más no rompe nada: lo que se recompila es lo que hay en esa dirección, y si el juego
nunca llega ahí, nunca se ejecuta. Solo cuesta tiempo de compilación. Las instrucciones de las trazas
(`--trace`) se agregan como semillas para lo que las heurísticas no encuentran.

**Cobertura sin traza** (`sh tests/coverage.sh`: de lo que el juego ejecuta en 1800 cuadros con un
mando aleatorio, cuánto encuentra el análisis solo): 97,6 % en total con los 23 juegos de `roms/`;
100 % en Sonic 1 y 2, Comix Zone, Contra Hard Corps, Phantasy Star IV, MK1, Road Rash II, SF2CE y
SSF2; entre 93 y 99,7 % en el resto (lo más bajo: Wonder Boy in Monster World, 92,8 %, y Wily Wars,
93,6 %). Sin las heurísticas (`--no-tables`), Sonic 1 se queda en 25 %.

### Herramientas de análisis

Las mismas que `nesrecomp`, con los mismos formatos:

- `--coverage traza`: cuántas instrucciones de la traza encontró el análisis sin ella, y las que
  faltan (las primeras 12; todas con `--verbose`).
- `--why BB:DIRECCION` (o el desplazamiento en la ROM): la cadena desde un punto de entrada (vector, traza, tabla...) hasta esa instrucción.
- `--viz [--color]`: mapa de la ROM en la terminal, coloreado por origen (flujo, traza, tabla,
  especulativo; en rojo, con `--coverage`, lo ejecutado y no descubierto).
- `--html archivo`: el mismo mapa en una página sola, con las instrucciones de cada celda y el "por
  qué" al hacer clic.
- `--events archivo`: cada instrucción con su desplazamiento, dirección, cómo se llegó, desde dónde,
  origen y desensamblado.
- `--codemap archivo`: una letra por byte de la ROM (`C` = código), 64 por línea, como `difftest
  --executed`.
- `--dis OFFSET:DIR:N`: desensambla N instrucciones (`dis68k.c`, que también pone el desensamblado en
  los comentarios del código generado).

### Funciones y emisión

Las instrucciones descubiertas se agrupan en funciones: tramos contiguos de ROM (con huecos de hasta
32 bytes) que no cruzan un banco de 512 KB y de hasta 400 instrucciones. Cada función empieza con un
`switch` sobre el PC (se puede entrar en cualquiera de sus instrucciones; si el PC no es suyo devuelve
0) y tiene una etiqueta por instrucción, que llama a su `k_*` de `m68k_ops.h` con el opcode, las
palabras de extensión (un `static const` por instrucción) y el PC constantes:

```c
L_000214: /* $000214: 4A79 00C0 0004 */
    { static const u16 e[2] = { 0x00C0, 0x0004 };
      npc = k_TST(g, 0x4A79, e, 0x000214); }
    AFTER(npc);
    if (npc == 0x00021A) goto L_00021A;
    LEAVE(npc);
```

`AFTER` pone al día el VDP y el Z80 (como el intérprete tras cada instrucción) y sale si terminó el
cuadro o si hay que volver al intérprete (interrupción, traza, STOP). Después se sigue con un `goto`
si el destino (la siguiente, el de un salto o un `DBcc`) es de la misma función con el mismo mapeo;
si no, `LEAVE` rehace el PC y la cola de prefetch (`ir`, `irc`, leídas sin efectos con
`m68k_peek16`) y vuelve al despachador. En los cartuchos que cambian de banco (SSF2, o SRAM encima de
la ROM) `MAPCHK` sale también si cambió el mapeo (`cart.map_ver`). El error de dirección sale con
`longjmp`, igual que en el intérprete. El compilador pliega los `switch` de `k_*` sobre el opcode
constante: queda la instrucción sola.

`gen/dispatch.c` tiene las funciones ordenadas por desplazamiento y una tabla por KB de ROM con la
primera candidata; `recomp_lookup` traduce el PC con `gen_code_offset` y busca la función que lo
contiene. Si el PC está en la RAM, pregunta a `ram_lookup` (siguiente sección).

### Código que el juego copia a RAM

Muchos juegos copian rutinas de la ROM a la RAM del 68000 y las ejecutan desde ahí: manejadores de
interrupción (el de VBlank de Gunstar Heroes está en `$FFFFEE00`), trampolines `JMP` por vectores,
rutinas que corren mientras se cambia el banco o el DMA. No están en la ROM donde corren, así que el
descubrimiento no llega. Se aprenden de la traza con el módulo común de retrorecomp,
`shared/ram_code.h`, que salió de lo que hizo GBRecomp (su `ARQUITECTURA.md` explica por qué de la traza
y no del análisis):

1. Con la traza activa (`difftest --uncovered`, el ejecutable con `--uncovered`, `gentrace`), el
   intérprete anota, por cada instrucción que ejecuta desde la RAM, sus bytes: hasta 4 versiones por
   dirección y qué bytes cambiaron entre ejecuciones. `gen_ram_note` (`gen.c`) lo hace por el puntero
   `m68k_ram_trace` del CPU, que pone `gen_trace_start`; la tabla va aparte de `Gen`, porque los
   estados guardados la copian entera y comprueban su tamaño. Salen como líneas
   `ram DIRECCION B0 B1 ...` (`??` en los bytes que cambiaron) en el mismo archivo de la traza, que
   `trace_line_read` ignora. El ejecutable carga el archivo y lo vuelve a escribir: conserva las líneas
   `ram` que traía.
2. **La dirección lleva los 32 bits del PC.** El 68000 no los recorta: `JSR ($8000).w` deja el PC en
   `$FFFF8000` y `JMP $FF0000` en `$00FF0000`, y esos bits llegan a la pila con la dirección de retorno.
   Son dos lugares distintos para el código (`$00FFxxxx` y `$FFFFxxxx`); los espejos de `$E00000` siguen
   en el intérprete.
3. **Qué identifica una instrucción: su opcode.** La longitud depende solo de él (`m68k_ext_words`), y las
   palabras de extensión (inmediatos, direcciones, desplazamientos, la palabra de los modos indexados) las
   lee el código generado de RAM al ejecutar, con `ram_ext`, como el intérprete. Así el código que parcha
   sus propios inmediatos o destinos sigue funcionando sin contarlo como otra instrucción.
4. `genrecomp --trace` une lo visto en varias corridas, descarta las direcciones con más de 8 versiones
   (código que el juego genera con estructuras distintas) y arma **bloques**: cadenas de instrucciones que
   van una detrás de otra sin ambigüedad, de hasta 1024. Cada bloque es una función de `gen/ram.c` como las
   de ROM (un `switch` de entrada, una etiqueta por instrucción, `AFTER` y `goto` a los sucesores que no
   dependen de una palabra variable).
5. **La guarda.** `ram_lookup` elige, para el PC, el primer bloque cuyas palabras de opcode siguen en RAM
   (`m_N`); si ninguno coincide, o el juego cambió el código, cae al intérprete. Se comprueba al entrar al
   bloque, no por instrucción: un bloque que reescribe un opcode de sí mismo mientras corre sigue con lo
   aprendido hasta la siguiente entrada, y si escribe sobre la cola de prefetch el 68000 real ejecuta lo
   viejo. `difftest` lo delata si ocurre en lo que probó.

`genrecomp` imprime «código en RAM aprendido de la traza: N instrucciones (M con operandos variables) en K
bloques» y `difftest --uncovered`, «Código en RAM: ...» con lo que todavía interpretó. `difftest` también
dice qué parte del tiempo del intérprete fue ROM o RAM («Intérprete por zona»). Sin líneas `ram`,
`gen/ram.c` es un `ram_lookup` que devuelve 0.

`tests/run_tests.sh` lo prueba con una ROM hecha ahí mismo que copia una rutina a `$00FF0000` y a
`$FFFF8000`, la llama en un bucle y parcha un inmediato de la primera: la primera ronda la interpreta (con
`??` en el byte parchado), la segunda compila 12 instrucciones en 2 bloques y corre el 100 % de los ciclos
recompilado, sin diferencias; `gentrace` y el ejecutable anotan las mismas líneas que `difftest`.

### El proyecto generado

`genrecomp -o carpeta` escribe `runtime/` (el runtime de este repo, que va dentro de `genrecomp`),
`platform/` (la ventana, `difftest`, `bench`), `gen/` (`code_NNN.c` de unas 3000 instrucciones cada
uno, para compilar en paralelo, `dispatch.c`, `ram.c` con el código aprendido de RAM, `info.c` con el SHA1 y el nombre, `gen.h`), un
`Makefile` (con `-DGEN_RECOMP`) y `LEEME.txt`. No lleva datos de la ROM: el ejecutable la busca (el
argumento, la copia que guarda la primera vez en su carpeta de datos, `rom.bin` junto al ejecutable o
en la carpeta actual, o cualquier `.bin/.md/.gen/.smd` de la carpeta actual con el SHA1 correcto) o la
pide, y comprueba el SHA1 de la imagen normalizada. `--interp` corre solo el intérprete, `--force`
acepta otra ROM (con el intérprete) y `--uncovered archivo` guarda lo que el intérprete tuvo que
ejecutar. Ayuda → Este juego dice cuántas instrucciones se recompilaron y qué parte de los ciclos
corrió en código recompilado.

### difftest

`difftest rom.bin [cuadros] [--seed S] [--input archivo] [--uncovered archivo] [--screens carpeta]
[--bench] [--executed archivo]`, las mismas opciones que en NESRecomp, GBRecomp y NEORecomp. La máquina B corre
función por función (recompilada, o el intérprete donde no hay código); tras cada función la A (solo
intérprete) avanza hasta el mismo ciclo y se comparan el 68000, los ciclos, la RAM, el VDP (sin la
imagen), la E/S, la EEPROM, los bancos y la SRAM, el Z80 y su RAM, el PSG, el YM2612, la mezcla de
audio, el bus abierto y las excepciones atendidas; al final de cada cuadro, también la imagen y el
audio listo. `--uncovered` escribe en formato de traza lo que B tuvo que interpretar, para volver a
recompilar con `--trace`.

### Rendimiento

Medido con `bench`/`genbench` (3600 cuadros, sin ventana) en un Mac con Apple Silicon:

| Juego | Intérprete | Recompilado |
|---|---|---|
| Sonic 1 | 20.3x | 28.2x |
| Sonic 2 | 21.8x | 30.8x |
| Streets of Rage 2 | 21.2x | 29.4x |
| Gunstar Heroes | 23.1x | 30.6x |
| Mortal Kombat 3 | 20.9x | 29.1x |
| Ecco the Dolphin | 19.9x | 27.6x |

(x = veces el tiempo real.) Antes de la fase 8, Sonic 1 iba a 11.6x y 14.9x. El 68000 recompilado
ya es una parte chica del tiempo (~13 % en Sonic 1); el resto se va en los periféricos, que hacen
trabajo real en cada línea o muestra: dibujar el VDP, el Z80 (que corre detrás del 68000 y se pone al
día tras cada instrucción) y el YM2612. Lo que se hizo, sin cambiar un bit de la imagen ni del sonido
(`tests/golden.sh`):

- VDP: los planos y la ventana de a un mosaico (la fila de 32 bits de una vez, las transparentes de
  un golpe); la paleta en color de 9 bits una vez por línea; la mezcla de capas sin sombra ni
  resalte pintando de atrás hacia adelante, sin ramas.
- Bus: las lecturas de 16 bits de la ROM sin pasar byte por byte por la SRAM y la EEPROM.
- YM2612: los canales callados (teclas sueltas, envolvente al mínimo) no se sintetizan; el código de
  tecla y el incremento de fase se guardan y se recalculan solo al escribir un registro.
- Audio: los tramos en silencio no se suman a la mezcla.
- Z80: un `HALT` sin interrupción que atender se salta de una vez hasta el destino o hasta que
  empieza la interrupción (sube R como lo haría paso a paso).
- Tras cada instrucción (`gen_after_step_inline`, en línea en el intérprete y en el código
  generado): el VDP solo si empezó una línea y el Z80 solo si quedó atrás; `m68k_idle` en línea.

Lo que se probó y no convino: compilar el código generado con `-O1` (tarda más, ocupa más y corre
más lento que con `-O2`). Lo que no se hizo, porque el 68000 ya no es lo que pesa: llevar las
banderas del CCR en variables locales con análisis de vida, encadenar funciones sin volver al
despachador en cada `RTS`, y saltar los bucles de espera del VBLANK (con el Z80 detrás, solo sería
exacto si el Z80 no puede escribir la RAM del 68000).

Tiempo de compilación del proyecto generado (`make -j8`): de 10 a 68 s según cuánto código se
descubra (Sonic 1, 12 s; Sonic & Knuckles, 68 s).

## Bancos, mappers y guardado

### Mapa de memoria del 68000

`bus.c`: cartucho en `$000000-$3FFFFF` (ROM con espejos si mide menos que una potencia de 2, SRAM),
nada en `$400000-$9FFFFF`, bus del Z80 en `$A00000` (solo con el bus pedido o el Z80 en reset; es de 8
bits: una palabra escrita deja solo el byte alto y una leída repite el byte en las dos mitades), E/S
en `$A10000`, pedido y reset del Z80 en `$A11100`/`$A11200` (BUSACK lee 0 con el bus pedido y el Z80
fuera de reset), SRAM y bancos en `$A130F1-$A130FF`, TMSS en `$A14000`, VDP en `$C00000` (con sus
espejos; un byte escrito va en las dos mitades) y RAM en `$E00000-$FFFFFF`. Cada acceso son 4 ciclos.
Lo que no responde devuelve el último valor del bus de **datos**: no el del prefetch, para que el
intérprete y el código generado (que no lee programa del bus) lean lo mismo.

El refresco de la DRAM le quita al 68000 2 ciclos cada 128 (ver Temporización). Sin emular todavía:
los ciclos de espera al tocar el bus del Z80.

### Mappers

Sin mapper, la dirección es el desplazamiento en la ROM. Con más de 4 MB (o "SEGA SSF" en la
cabecera), el de SSF2: ocho ventanas de 512 KB; la 0 fija y las otras siete eligen banco en
`$A130F3-$A130FF` (`cart.bank`, `cart_rom_offset`). Cada cambio sube `cart.map_ver`, que el código
generado vigilará como `MAPCHK` en NES. Probado con Super Street Fighter II (5 MB). Los mappers sin
licencia quedan pendientes.

### Cartucho

`gen_load` acepta `.bin`, `.md` y `.gen` (la imagen tal como la ve el 68000) y `.smd` (512 bytes de
cabecera del copiador y cada bloque de 16 KB con los bytes impares primero y los pares después). Un
`.smd` se reconoce por la marca `$AA $BB` de su cabecera o porque tras deshacer el entrelazado
aparece `SEGA` en `$100`; también se acepta un `.bin` con cabecera de copiador. Todo se normaliza a
la imagen del 68000 (tamaño par), y el **SHA1 se calcula sobre la imagen normalizada**: la misma ROM
en `.bin` o en `.smd` da el mismo SHA1.

`cart_parse_header` lee la cabecera de `$100-$1FF` (sistema, nombres, serie, suma, periféricos,
rangos, región) y la RAM del cartucho declarada con `RA` en `$1B0`. `cart_checksum` suma las
palabras de `$200` al fin de ROM que declara la cabecera, como la rutina de comprobación de los
juegos.

### Guardado

- **SRAM:** la que declara la cabecera (`RA`, tipo en `$1B2`: de bytes pares, impares o de 16 bits),
  en su rango. Si queda encima de la ROM (ROM de más de 2 MB), solo se ve con `$A130F1` bit 0; el bit
  1 la protege. Sin partida se lee `$FF`. `gen_sram_load`/`gen_sram_save`/`gen_sram_dirty` guardan el
  rango completo (formato de Genesis Plus GX).
- **EEPROM serie** (`eeprom.c`): el juego mueve SCL y SDA a mano (I2C) en una dirección del
  cartucho. La cabecera no dice dónde ni qué chip, así que sale de una base de datos por número de
  serie; el contenido vive en `sram_data` y se guarda igual que la SRAM. Máquina de estados de los
  X24C01 (dirección en el primer byte), 24C01-24C16 (byte de dispositivo 1010xxx y dirección) y 24C32
  o más (dos bytes de dirección), con lectura secuencial y escritura por página. En la base de datos
  por ahora solo NBA Jam (EE. UU./Europa y Japón: 24C02 de 256 bytes, SDA y SCL en los bits 0 y 1 de
  `$200001`, SDA se lee en el bit 1, página de 4) y NBA Jam Tournament Edition (X24C02: SDA en el
  bit 0 de `$200001` para escribir y leer, SCL en el bit 0 de `$200000`), sacados de su propio código
  y comprobados: el primer arranque la inicializa y el segundo la acepta sin reescribirla. En los
  chips de hasta 256 bytes, los bits 1-3 del byte de dispositivo son patas de selección conectadas a
  tierra: con otro valor no hay ACK (NBA Jam TE escribe en `A2`, `A4` y `A6` y cuenta con que no
  respondan). Y Mega Man: The Wily Wars (EE. UU. del Genesis Mini y Europa: X24C01 de 128 bytes, la
  dirección en el primer byte; SDA en el bit 0 y SCL en el bit 1 de `$200001`, SDA se lee en el bit 0),
  comprobado igual. Los demás juegos con EEPROM (NFL Quarterback Club, los de EA y Codemasters...) se
  agregan cuando se puedan probar.
- **Estados guardados** (`gen_state_save`/`gen_state_load`, cuatro ranuras en `gen`:
  `<rom>.estado1-4`): una cabecera (magia, versión, tamaño de `Gen`, tamaño y SHA1 de la ROM, tamaño de
  la SRAM), la estructura `Gen` entera (68000, Z80, VDP con su imagen, YM2612, PSG, mezcla, RAM) y el
  contenido de la SRAM/EEPROM. Al cargar se conservan los punteros de la corrida actual. Solo los lee
  la misma versión con la misma ROM. `statetest` lo prueba: tras cargar, los mismos cuadros dan
  exactamente lo mismo (imagen, sonido, RAM, CPU, chips).

## Video y audio

### VDP

`vdp.c`: puerto de control (registros y comandos de dirección de dos palabras; en modo 4 solo se
escriben los registros 0-10) y de datos (VRAM, donde una dirección impar cambia el orden de los
bytes; CRAM; VSRAM; lecturas con los bits sin usar del bus) con el incremento del registro 15. Los tres
DMA: desde el bus del 68000 (la fuente da la vuelta en 128 KB; el 68000 queda detenido lo que dura,
según los accesos por línea: 18/205 en H40 y 16/167 en H32, con imagen o en el VBLANK), relleno (tras la
escritura en el puerto de datos, el byte alto en VRAM o la palabra en CRAM/VSRAM) y copia de VRAM; los
dos últimos dejan el bit de DMA del estado mientras duran. FIFO de 4 escrituras: el VDP las atiende en
sus ranuras de acceso (las mismas 18/205 y 16/167 por línea; una escritura en VRAM ocupa dos, va de a
byte); con las 4 ocupadas el 68000 espera, y antes de leer el puerto de datos espera a que se vacíe (la
memoria se escribe en el momento: solo se emula la espera). Estado (FIFO vacía y llena, F, desborde y colisión
de sprites, cuadro impar, VBLANK, HBLANK, DMA, PAL), contador HV (la V salta de `$EA` a `$E5`; la H
recorre los valores de H40 o H32 repartidos parejo en la línea) y el reloj de líneas: VINT al empezar la
línea 224 y HINT por el contador del registro 10 (baja en las líneas 0-224 y se recarga en las demás).
Cada línea con imagen se dibuja al terminar, así lo que cambia una HINT sale en su línea. El modo (H32
o H40, 224 o 240 líneas) se fija al empezar el cuadro.

`vdp_render.c`: planos A y B (tamaño del registro 16; scroll horizontal de pantalla, por celda o por
línea; vertical de pantalla o por columna de 16 pixeles), ventana (franja horizontal o vertical, sin
scroll), sprites (lista enlazada; 20/16 por línea y 320/256 pixeles, con desborde; un sprite en X = 0
tapa a los que siguen si ya hubo otro; colisión), prioridades (sprite, A, B con prioridad; luego sin
ella; luego el fondo), sombra y resalte (operadores con los colores 14 y 15 de la paleta 3), columna
izquierda en blanco y pantalla apagada. `fb` guarda por pixel el color de 9 bits y el modo (normal,
sombra, resalte); `vdp_argb` lo pasa a ARGB con los niveles no lineales de la consola.

Sin emular todavía: el orden exacto de los accesos del 68000 dentro de una instrucción (78 % con `m68ktest --bus`), el
modo 2 de entrelazado (doble resolución, como el 2 jugadores de Sonic 2), el modo 4 (SMS) y la
posición exacta de HINT/VINT y del HBLANK dentro de la línea. Las orillas del DMA de relleno (qué pasa
con el primer y el último byte) no están confirmadas con hardware.

### Z80

`z80.c`: el Z80 completo, con lo no documentado (banderas X/Y, MEMPTR/WZ, Q para SCF/CCF, mitades de
IX/IY, SLL, la copia del resultado de DDCB/FDCB a un registro, las banderas de las instrucciones de
bloque al repetir) y las interrupciones IM 0/1/2. Pasa las 1 604 000 SingleStepTests del Z80
(`z80test`, `tests/sstz80.sh`, en `make test`). Siempre interpretado.

`z80bus.c`: su mapa (RAM de 8 KB con espejo, YM2612, registro de banco de 9 bits, PSG en `$7F11` y la
ventana de 32 KB al bus del 68000 en `$8000`). Va a 1/15 del reloj maestro y corre detrás del 68000:
se pone al día hasta el mismo ciclo después de cada instrucción del 68000 y antes de que el 68000 toque
su bus. No corre con el reset puesto (`$A11200`, que también reinicia el YM2612) ni con el bus pedido.
Su INT dura una línea desde el VBLANK. Cada acceso del Z80 al bus del 68000 le cuesta unos 3 ciclos
más (espera a que el 68000 le ceda el bus); sin emular todavía: lo que pierde el 68000 en esa cesión.

### Audio

- **PSG** (`psg.c`): el SN76489 de Sega: tres tonos (un periodo de 0 o 1 deja la salida fija, para
  las muestras por volumen) y el ruido con LFSR de 16 bits (blanco: bits 0 y 3), al reloj del Z80 /
  16; volumen en pasos de 2 dB. Desde el 68000 en `$C00011` y desde el Z80 en `$7F11`.
- **YM2612** (`ym2612.c`, escrito desde la documentación del chip): una muestra cada 144 ciclos del
  68000 (53 267 Hz). Fase de 20 bits con detune, múltiplo y PM; envolvente de 10 bits con la tabla de
  incrementos del chip, escala por tecla, TL y AM; operador con seno logarítmico y exponencial; los 8
  algoritmos y la realimentación; panorama; LFO; temporizadores A y B con sus banderas; DAC del canal 6;
  modo especial del canal 3; bandera de ocupado tras escribir; SSG-EG (la envolvente 4 veces más
  rápida hasta `$200` y, según el modo, repetir con la fase a 0, alternar invertida o quedarse quieta;
  invertida suena `$200` menos la atenuación). Sin emular todavía: CSM, los retardos entre operadores
  y la distorsión de la DAC del YM2612.
- **Mezcla** (`audio.c`): los chips entregan tramos de tiempo y cada muestra de salida (44 100 Hz,
  estéreo de 16 bits) es el promedio de lo que sonó durante ella; después, un pasa-altos de ~10 Hz como
  el acople de la salida de la consola (quita la componente continua de la DAC quieta).
- **Pruebas:** `audiotest` programa los chips y mide la afinación (La de 440 Hz en cada uno), el
  panorama, el ruido, el escalón de la DAC, la relajación, el temporizador A, la bandera de ocupado y
  los modos de SSG-EG (repetir y alternar siguen sonando, una vez queda en silencio, una vez invertida
  queda sonando).
  Con juegos: la voz "SEGA" de Sonic 1 (por la DAC del Z80) y la música; ningún juego de `roms/` satura.
  `genrun --wav` guarda el sonido y su nivel por segundo.

## Variantes de la consola y accesorios

Solo NTSC (262 líneas, 3420 ciclos maestros por línea; el 68000 a 1/7 del maestro). El registro de
versión (`$A10001`) sale de la región de la cabecera: se prefiere EE. UU. (`$A0`); si el juego solo
dice Japón, `$20`; si solo Europa, `$E0` (PAL, aunque el ritmo siga siendo NTSC por ahora).

**TMSS** (apagado por omisión; `gen.tmss`, `genrun --tmss`): la versión dice 1 y el VDP solo
responde después de escribir "SEGA" en `$A14000`. Si el 68000 lo toca antes, se cuelga como en la
consola (`cpu.halted` y `tmss_locked`). No hay ROM de arranque de TMSS (es de Sega y no se incluye),
así que no aparece su pantalla de licencia. Sonic 1, NBA Jam y Super Street Fighter II escriben
"SEGA" y corren igual con él.

- **Mandos** (`io.c`): 3 botones (con TH = 1 lee C B Der Izq Abajo Arriba; con TH = 0, Start A 0 0 Abajo
  Arriba) y 6 botones, si la cabecera dice `6` en los periféricos: cuenta las bajadas de TH seguidas
  (se reinicia tras ~1.5 ms sin una); en la tercera, con TH = 0 los bits 3-0 leen 0 y con TH = 1, Mode X
  Y Z; en la cuarta, con TH = 0, 1111. El puerto 3 y la línea serie no tienen nada conectado.

## Interfaz del ejecutable

`gen` (`main_sdl.c`): ventana con barra de menús, controles y ritmo de 59.92 cuadros por segundo. La
barra de menús y los diálogos vienen de Moskva 89, GBRecomp y NESRecomp (`ui_menu.c`, `ui_font.c`): se
dibujan dentro de la ventana para verse igual en todos los sistemas, en español o inglés (`i18n.c`).
Con un menú o un diálogo abierto, el juego se pausa.

- **Juego:** abrir ROM (o arrastrarla a la ventana), reiniciar, pausa.
- **Video:** tamaños 1x-6x, pantalla completa, escala entera y aspecto 4:3 (los pixeles de la Mega
  Drive no son cuadrados; la ventana se ajusta al cambiar entre H32 y H40).
- **Entrada:** el mando que pide la cabecera, o forzar el de 3 o el de 6 botones.
- **Controles:** jugador 1 con flechas, Z X C = A B C, Enter = Start, A S D = X Y Z, Mayús = Mode;
  jugador 2 con I J K L, V B N = A B C, H = Start; mandos con X A B = A B C y LB Y RB = X Y Z.
- **Exportar:** captura PNG (F12).
- La SRAM o EEPROM se guarda al salir y cada 5 segundos si cambió, en la carpeta de datos
  (`GENRecomp/interprete/`, junto con `settings.ini` y lo exportado).

- **Audio:** sonido (Ctrl+M), volumen y silenciar cada canal (FM 1-6 y PSG); en pausa, con un menú
  abierto o en avance rápido, no suena.
- **Estados:** guardar (F5) y cargar (F8) en cuatro ranuras.

Opciones: `gen [rom.bin] [--scale N] [--tmss] [--record archivo | --play archivo] [--trace archivo]`.
`--record` y `--play` graban y reproducen los botones (`inputs.h`, el mismo formato que `--input` de
las herramientas sin ventana); mientras tanto no se puede reiniciar, cargar estados ni cambiar de ROM.
`--trace` agrega al salir las instrucciones de ROM que se ejecutaron, como traza para `genrecomp`. Para
pruebas sin ventana: `--ui-shot archivo.png` con `--ui-frames N`, `--ui-menu N` y `--ui-action N`. El
modo de juego recompilado llega en la fase 6.

Icono: `MD.png` (serie TriMui) de
[KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), CC BY 4.0;
la ventana lo recorta al dibujo y `icono.ico` (Windows) es ese recorte llevado a 256x256
(`tools/make_icon.py`).

## Empaquetado y pruebas

- **`make dist`:** deja `dist/genrecomp-<versión>-<sistema>.tar.gz` con `genrecomp`, el README, la
  licencia y este documento. La versión sale de `git describe`.
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en NESRecomp, GBRecomp y NEORecomp),
  en Linux, macOS y Windows: `make`, `make testroms`, `sh tests/contract.sh`, `make test` y `make dist`.
- **Contrato común** (`tests/contract.sh`, el mismo archivo en los tres repos): `gentrace`, `genshot`,
  `genaudio` y `genbench` aceptan `--frames`, `--input`, `--seed` (y `--repeat`); con la demo Overdrive,
  la misma entrada da la misma traza, captura y sonido, y `--input` da el mismo `genshot --hash` que la
  sintaxis `boton:desde-hasta`. Los botones del guion común (`START`, `DOWN+A`) existen en las tres
  consolas; `--seed` usa el mismo mando aleatorio (`random_pad.h`) traducido al Genesis.
- **`make testroms`** (`tests/testroms.sh`): las pruebas libres en versiones fijas, en `testroms/` (no se
  versionan): las SingleStepTests del 68000 y del Z80 (comprimidas) y las demos Overdrive (Evoke 2013, la versión para hardware)
  y Overdrive 2 (Revision 2017: 8 MB con SSF2, solo PAL) de Titan, de scene.org, con su SHA-256. Las
  demos son la meta más exigente (Overdrive 1 corre pero avisa que necesita PAL; Overdrive 2 queda en
  negro). El 240p Test Suite (GPL) no se baja solo: su ROM compilada está en itch.io y compilarla pide
  SGDK; se pone a mano en `testroms/240p/`. La versión 1.21 corre: menú principal, submenús y detecta
  "NTSC VDP 320x224p".
- **`tests/golden.sh`:** hash de todos los cuadros y de todo el audio (`genshot --hash`, 1800 cuadros
  con un mando aleatorio) de cada ROM de `roms/` y de las pruebas libres, identificadas por su SHA1;
  `--update` reescribe `tests/golden.txt`. Una optimización no debe cambiar ni un bit. No va en
  `make test` (tarda ~80 s y depende de tus ROMs).
- **`make test`:** `carttest` (la misma ROM sintética como `.bin`, `.smd` con y sin marca y `.bin`
  con cabecera de copiador debe dar la misma imagen, SHA1 y cabecera), `bustest` (RAM y espejos,
  ROM, versión, bus del Z80, mandos de 3 y 6 botones, SRAM y guardarla, EEPROM serie (escritura,
  lectura aleatoria con START repetido, ACK, página), TMSS (con y sin "SEGA"), SSF2, y un programa de 68000
  que cuenta VINT y HINT, también con la máscara), las SingleStepTests del 68000 por el intérprete y
  por el camino del código generado (~9 s cada una), `genrecomp --info` con cada ROM de `roms/` y, si
  están: Sonic 1 (USA/Europa), que con Start en el título el intérprete llegue a jugar el nivel; NBA
  Jam (USA/Europa), que guarde su EEPROM y la acepte al volver a arrancar. También `vdptest` (puertos,
  los tres DMA, estado, contador V, planos, ventana, sprites con su límite, máscara y desborde,
  sombra y resalte) y que `gen` dibuje cada menú y cada diálogo sin ventana (`--ui-shot`). Y el
recompilador: generar, compilar (`make difftest`) y pasar `difftest` con una ROM mínima hecha en la
prueba, con la demo Overdrive (600 cuadros) y con Sonic 1 si está (1200 cuadros, recompilado sin
traza: 100 % de los ciclos en código recompilado, y la cobertura sin traza de lo que ejecuta debe ser
de 99 % o más). Y las herramientas de análisis de `genrecomp` con la ROM mínima (`--why`, `--viz`,
`--codemap`, `--events`, `--html`, `--dis`).

### Oráculos y compatibilidad

`tools/oracles/` compara contra dos emuladores de referencia, Genesis Plus GX y BlastEm, como núcleos
libretro. No van en el repositorio (Genesis Plus GX tiene una licencia no comercial; BlastEm es GPL-3):
`oracle.py` los baja la primera vez de la compilación nocturna de buildbot.libretro.com a
`build/oracles/` y anota el SHA-256 de lo que bajó. `retro_run` (C, `make build/retro_run`) es un
frontend libretro mínimo sin ventana: declara solo la parte pública de la API que usa
(`libretro_min.h`; los dos, `retro_run.c` y `libretro_min.h`, son de `tools/retrorecomp/shared`, comunes con NEORecomp, y lo propio del Genesis va en `tools/oracles/oracle_host.h`), toma las entradas de `cli.h` (`--frames`, `--input`, `--seed`, `boton:desde-hasta`)
y asigna los botones por las descripciones que publica cada núcleo ("A", "Start", "Mode"...: cada uno
los acomoda distinto en el mando de libretro). Da la misma salida que `genramdump` (con `--swap16`: los dos
guardan la RAM en palabras de 16 bits de la máquina) o una captura (`--shot`).

```
python3 tools/oracles/oracle.py compare rom.bin --frames 1200 start:120-125 [--png carpeta]
python3 tools/oracles/oracle.py ramdump gpgx rom.bin --frames 600
```

`compare` da el parecido de la RAM del 68000 y de la imagen del último cuadro entre GENRecomp y cada
oráculo, y entre ellos (con tolerancia por canal en la imagen: cada emulador convierte el color de 9 bits
a 8 bits a su manera). No se espera igualdad exacta: los dos emuladores difieren entre sí en la
temporización fina, y en un juego con entradas en cuadros fijos eso cambia en qué momento cae cada
botón. Sirven para medir si un cambio de temporización acerca o aleja (así se ajustaron la FIFO del VDP y
el refresco de la DRAM) y para encontrar diferencias grandes que no son de tiempos.

`tools/compat/run.py [carpeta] [--oraculos]` corre la prueba de compatibilidad (etapa 1 con el
intérprete y capturas; etapa 2 recompilando sin traza con `difftest` y `difftest --bench`; los oráculos) y
`tools/compat/report.py` arma `docs/compat/index.html`. El resumen está en `docs/COMPATIBILIDAD.md`.

### Resultados con juegos

**Recompilado (fase 6).** Con una traza de `gentrace` de 1800 cuadros (semilla 1, Start a los 2 y a
los 5 s), cada juego de `roms/` se recompila, compila en 5-13 s (`make -j8`) y da 1800 cuadros
idénticos al intérprete en `difftest` con otra semilla: los 23 (Comix Zone, Contra Hard Corps,
Earthworm Jim, Ecco, Gunstar Heroes, Wily Wars, Mickey Mania, MK1, MK3, NBA Jam y TE, Phantasy Star IV,
Ristar, Road Rash II, Sonic 1, 2, 3 y 3 & Knuckles, SF2CE, Streets of Rage 2, SSF2, Vectorman y
Wonder Boy in Monster World). La parte de los ciclos que corre en código recompilado va del 93 al
100 %, salvo Road Rash II (75 %: corre código desde la RAM). Sonic 1 con una traza de 3600 cuadros:
100 % y 14.9 veces el tiempo real contra 11.7 del intérprete (el costo está en poner al día el VDP y
el Z80 tras cada instrucción y en volver al despachador en cada `RTS`: fase 8).

**Sin traza (fase 7).** Recompilados solo con el análisis, los 23 juegos dan 1800 cuadros idénticos
al intérprete (mando aleatorio con semilla 2) y compilan en 10-54 s con 8 núcleos (Sonic & Knuckles,
el más grande: 130 000 instrucciones). La parte de los ciclos en código recompilado: 100 % en
Sonic 1, 2 y 3, MK1, PSIV, SF2CE, SSF2, Streets of Rage 2 y Wonder Boy; 99-99,9 % en Earthworm Jim,
Ecco, Wily Wars, MK3, NBA Jam y TE, Ristar, Road Rash II y Sonic & Knuckles; 98,8 % Mickey Mania,
97,2 % Comix Zone, 96,1 % Vectorman, 95,6 % Contra Hard Corps y 92,9 % Gunstar Heroes (lo que falta
es sobre todo código que corre desde la RAM).

Con el intérprete, el bus y el VDP: Sonic 1 se ve bien en la pantalla de SEGA, "SONIC TEAM
PRESENTS", el título (con el emblema y Sonic) y Green Hill (HUD, scroll por capas, agua, enemigos);
con Start entra al nivel y juega (corriendo a la derecha pierde una
vida contra un enemigo), a ~60 veces el tiempo real. Super Street Fighter II corre con el mapper de
SSF2 (155 cambios de banco en 1200 cuadros) y muestra el logo de SEGA, la intro y el título. NBA Jam guarda su EEPROM y la acepta al volver a
arrancar. `genrun rom --watch DIR` muestra cuándo cambia un byte de la RAM (en Sonic 1, el modo de
juego está en `$FFF600`) y `--sram ARCHIVO` carga y guarda la SRAM o EEPROM.

## Estado de la validación

`make test` (`tests/run_tests.sh`; con las ROMs de prueba de `make testroms`, y algunas más si están los
juegos en `roms/`), en Linux, macOS y Windows en cada PR:

- **CPU:** las 1 000 058 SingleStepTests del 68000 por el intérprete y por el camino del código generado
  (`m68ktest`, `--recomp`), y las 1 604 000 del Z80 (`z80test`).
- **Pruebas unitarias:** `carttest` (formatos `.bin`/`.smd`, cabecera, suma, SRAM, vectores), `bustest`
  (RAM, ROM, Z80, mandos de 3 y 6 botones, SRAM, EEPROM, TMSS, mapper de SSF2, VINT/HINT), `vdptest`
  (puertos, DMA, estado, planos, ventana, sprites, sombra y resalte), `audiotest` (PSG y YM2612) e
  `inputtest` (entradas grabadas, 12 botones, `--seed`, `boton:desde-hasta`).
- **Estados guardados:** `statetest` con la demo Overdrive (y con Sonic 1 si está).
- **Recompilador:** el proyecto generado de una ROM mínima y de Overdrive (y de Sonic 1 sin traza, con
  su cobertura del 99 % o más) se compila y `difftest` lo compara contra el intérprete; `--why`,
  `--viz`, `--codemap`, `--events`, `--html` y `--dis`; `genrecomp` rechaza lo que no es una ROM.
- **Interfaz:** cada menú y cada diálogo de `gen` se dibuja sin ventana (`--ui-shot`).
- **Contrato común** con NESRecomp y GBRecomp (`tests/contract.sh`, el mismo archivo en los tres).
- Con los juegos de `roms/`: `genrecomp --info` con cada uno, Sonic 1 llega al nivel con el intérprete
  y NBA Jam, NBA Jam TE y Mega Man: The Wily Wars guardan su EEPROM y la aceptan al volver a arrancar.

`make test` imprime cuánto tarda cada sección; en el CI, lo más largo es compilar el proyecto generado
de Overdrive y las SingleStepTests del Z80.

**Todavía no:** PAL (312 líneas, 50 Hz; lo piden Overdrive 2 y los juegos solo europeos), el orden
exacto de los accesos del 68000 dentro de una instrucción (78 % con `m68ktest --bus`), el entrelazado
doble del VDP, la posición exacta de HINT/VINT/HBLANK en la línea, y el CSM del YM2612. La lista de
cada fase está en [PLAN.md](PLAN.md).

### Línea base para optimizar el runtime

`tests/golden.sh` corre cada ROM de `roms/` y de las pruebas libres 1800 cuadros con entradas fijas y
una semilla, y compara el hash de **todos los cuadros y de todo el audio** (`genshot --hash`) con
`tests/golden.txt`: una optimización no debe cambiar ni un bit (`--update` reescribe la línea base). Los
casos se nombran por el SHA1 de la ROM. `GOLDEN_SHOT=ruta` usa otro `genshot`, por ejemplo el de un
proyecto generado, para comparar el código recompilado con la misma línea base.
