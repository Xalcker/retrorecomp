# Arquitectura de GENRecomp

GENRecomp toma una ROM de Mega Drive / Genesis y generará un proyecto C99 que se compila en un
ejecutable nativo. El ejecutable cargará la ROM del usuario en tiempo de ejecución y validará su
SHA1: nunca se distribuyen datos de la ROM.

Las secciones son las mismas, y en el mismo orden, que en `docs/ARQUITECTURA.md` de NESRecomp y
GBRecomp: lo que cambia entre las consolas queda dentro de cada una. Las que todavía no tienen
contenido dicen en qué fase de [PLAN.md](PLAN.md) llegan.

## Piezas

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `gen.h` (estado y interfaz), `gen.c` (carga, reset, cuadro, SRAM), `cart.c` (formatos, cabecera, vectores, mapeo), `bus.c` (mapa de memoria del 68000), `io.c` (versión y mandos), `vdp.c` (por ahora el esqueleto), `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete); `sha1`, `console.h`. |
| `src/genrecomp/` | El recompilador. Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | Por ahora las partes sin SDL (`png`, `files`, `export`, `random_pad.h`). Se copian a cada proyecto generado. |
| `src/tools/` | `carttest`, `m68ktest` (SingleStepTests del 68000), `bustest` (bus, mandos, SRAM, SSF2, interrupciones), `genrun` (corre una ROM sin ventana y resume), `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh` (las SingleStepTests por familia), `contract.sh` (contrato común). |
| `tools/` | `retrorecomp/` (la GUI común, submódulo). |

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
modifica a sí mismo: en ROM no pasa y el código en RAM lo corre el intérprete).

Comportamientos poco documentados que siguen a las pruebas: banderas N/V de ABCD/SBCD/NBCD; CHK
compara primero con el límite (38 ciclos, N = signo) y luego el signo (40 ciclos, N = 1), y si no
atrapa N no cambia; DIVU/DIVS con desborde no tocan N ni Z, y DIVS termina antes (16/18 ciclos); la
división entre cero apila la dirección de la instrucción (solo un caso lo cubre); ASR con más pasos que
bits deja C = X = 0; ADDQ.l a An tarda 6 ciclos. El error de dirección apila el IR, el estado (R/W,
I/N, código de función), la dirección y como PC la última palabra de programa leída menos 2 (al saltar a
una dirección impar, el destino menos 4); MOVE a `-(An)` hace el prefetch antes de escribir y un long
se escribe en dos pasos (la palabra baja primero), igual que MOVEM a `-(An)` y ADDX/SUBX con `-(An)`.

## Temporización

Cada acceso del 68000 al bus son 4 ciclos (`m68k_fetch`, `m68k_rd*`, `m68k_wr*`); los ciclos
internos van con `m68k_idle`. Los totales por instrucción coinciden con las SingleStepTests en todos
los casos. El orden de los accesos dentro de una instrucción coincide en el 78 % de los casos
(`m68ktest --bus`); lo que falta es sobre todo si una escritura va antes o después del último prefetch
(4 ciclos dentro de la misma instrucción), que se afinará cuando el VDP lo haga visible (fase 3).
El VDP y el audio serán perezosos.

## Interrupciones y despachador

`gen_run_frame` corre hasta que el VDP termina el cuadro: una instrucción del intérprete
(`m68k_step`) y `vdp_sync`, que pone las líneas al día y fija el nivel de interrupción del 68000
(`cpu.ipl`): 6 si hay VINT pendiente y habilitada (registro 1, bit 5), 4 si hay HINT (registro 0, bit
4). El 68000 la atiende al empezar la instrucción siguiente si el nivel pasa su máscara (autovector,
como en la Mega Drive) y avisa con `m68k_int_ack`, que la borra en el VDP. El despachador del código
generado llega en la fase 6.

## Recompilador

```
genrecomp juego.bin [--info]
```

### Identidad de las instrucciones

Una instrucción se identificará por su desplazamiento en la ROM normalizada. Sin mapper coincide
con su dirección (`$000000-$3FFFFF`); con el de SSF2, la dirección se traduce con los 8 bancos de 512 KB.

### Descubrimiento

Fase 6. Las semillas son los 64 vectores de `$000000` (`cart_vector`), que `genrecomp --info` ya
muestra.

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

Sin emular todavía: los ciclos de espera al tocar el bus del Z80 y el refresco de la DRAM.

### Mappers

Sin mapper, la dirección es el desplazamiento en la ROM. Con más de 4 MB (o "SEGA SSF" en la
cabecera), el de SSF2: ocho ventanas de 512 KB; la 0 fija y las otras siete eligen banco en
`$A130F3-$A130FF` (`cart.bank`, `cart_rom_offset`). Cada cambio sube `cart.map_ver`, que el código
generado vigilará como `MAPCHK` en NES. Probado con Super Street Fighter II (5 MB). Los mappers sin
licencia y la EEPROM serie (NBA Jam, etc., necesitan una base de datos por juego) quedan pendientes.

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
- **Estados guardados:** fase 5.

## Video y audio

### VDP

Fase 2 (`vdp.c`): el esqueleto que necesita el bus. Puerto de control (registros y comandos de
dirección de dos palabras), puerto de datos a VRAM/CRAM/VSRAM con el incremento del registro 15,
estado (VBLANK, F, HBLANK aproximado), contador HV (la V exacta; la H aproximada) y el reloj de líneas
con VINT al empezar la línea 224 y HINT por el contador del registro 10 (baja en las líneas 0-224 y se
recarga en las demás). Va al día de forma perezosa (`vdp_sync`). En la fase 3: DMA, FIFO, la H
exacta y el dibujo.

### Audio

Fase 4.

## Variantes de la consola y accesorios

Solo NTSC (262 líneas, 3420 ciclos maestros por línea; el 68000 a 1/7 del maestro). El registro de
versión (`$A10001`) sale de la región de la cabecera: se prefiere EE. UU. (`$A0`); si el juego solo
dice Japón, `$20`; si solo Europa, `$E0` (PAL, aunque el ritmo siga siendo NTSC por ahora). TMSS
opcional (`gen.tmss`): solo el bit de versión y el registro `$A14000`; no se bloquea el VDP.

- **Mandos** (`io.c`): 3 botones (con TH = 1 lee C B Der Izq Abajo Arriba; con TH = 0, Start A 0 0 Abajo
  Arriba) y 6 botones, si la cabecera dice `6` en los periféricos: cuenta las bajadas de TH seguidas
  (se reinicia tras ~1.5 ms sin una); en la tercera, con TH = 0 los bits 3-0 leen 0 y con TH = 1, Mode X
  Y Z; en la cuarta, con TH = 0, 1111. El puerto 3 y la línea serie no tienen nada conectado.

## Interfaz del ejecutable

Fase 3.

## Empaquetado y pruebas

- **`make dist`:** deja `dist/genrecomp-<versión>-<sistema>.tar.gz` con `genrecomp`, el README, la
  licencia y este documento. La versión sale de `git describe`.
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en NESRecomp y GBRecomp),
  en Linux, macOS y Windows: `make`, `make testroms`, `sh tests/contract.sh`, `make test` y `make dist`.
- **`make test`:** `carttest` (la misma ROM sintética como `.bin`, `.smd` con y sin marca y `.bin`
  con cabecera de copiador debe dar la misma imagen, SHA1 y cabecera), `bustest` (RAM y espejos,
  ROM, versión, bus del Z80, mandos de 3 y 6 botones, SRAM y guardarla, SSF2, y un programa de 68000
  que cuenta VINT y HINT, también con la máscara), las SingleStepTests del 68000 por el intérprete y
  por el camino del código generado (~9 s cada una), `genrecomp --info` con cada ROM de `roms/` y, si
  está Sonic 1 (USA/Europa), que con Start en el título el intérprete llegue a jugar el nivel.

### Resultados con juegos

Con el intérprete, el bus y el esqueleto del VDP (sin imagen todavía): Sonic 1 pasa por la pantalla
de SEGA, el título y el demo, y con Start entra al nivel y juega (corriendo a la derecha pierde una
vida contra un enemigo), a ~6.7 veces el tiempo real. Super Street Fighter II corre con el mapper de
SSF2 (155 cambios de banco en 1200 cuadros). `genrun rom --watch DIR` muestra cuándo cambia un byte
de la RAM (en Sonic 1, el modo de juego está en `$FFF600`).
