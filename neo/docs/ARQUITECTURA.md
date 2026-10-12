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
| `src/runtime/` | La consola: `neo.h` (estado e interfaz), `cart.c` (formatos, imagen normalizada, cabecera, vectores, identidad del código), `bios.c` (BIOS del AES y tabla L0), `zip.c` (zip, inflate y CRC32), `neodb.c`/`neodb.h` (base de datos de juegos), `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete), `z80.h`/`z80.c` (el Z80); `sha1`, `console.h`, `trace_line.h` (idéntico en los cuatro recompiladores). |
| `src/neorecomp/` | El recompilador. Por ahora, `--info` y `--neo`. |
| `src/platform/` | Lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (idéntico en los cuatro). La ventana llega en la fase 3. |
| `src/tools/` | `carttest` (con `inflate_vectors.h`), `m68ktest` y `z80test` (SingleStepTests, con `json.h`), `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh` y `sstz80.sh` (las SingleStepTests), `contract.sh` (contrato común, idéntico en los cuatro), `testroms.sh` (`make testroms`). |
| `tools/` | `retrorecomp/` (la GUI común, submódulo). |

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
modifica a sí mismo: en ROM no pasa y el código en RAM lo corre el intérprete).

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

Llega en la fase 2. Lo que ya está decidido: todo se cuenta en ciclos del reloj maestro. Un cuadro son
384 pixeles × 264 líneas × 4 = 405 504 ciclos maestros; el 68000 va a la mitad (202 752 por cuadro, 768
por línea), el Z80 a un sexto y el YM2610 a un tercio. El reloj del AES (24,167829 MHz, 59,60 cuadros por
segundo) solo cambia el ritmo de la ventana y del audio.

## Interrupciones y despachador

Llega en las fases 2 (interrupciones: nivel 1 VBLANK, 2 temporizador, 3 arranque; se reconocen
escribiendo `IRQACK`, no al atenderlas) y 6 (despachador).

## Recompilador

Llega en las fases 6 y 7.

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

## Bancos, mappers y guardado

### Mapa de memoria del 68000

Llega en la fase 2.

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
hay BIOS del AES de EE. UU. `bios_find` la busca en `$NEO_BIOS`, junto al juego y en `roms/`. En el AES no
hay SFIX ni SM1: la capa fija y el Z80 salen siempre del cartucho.

La **tabla L0** (`000-lo.lo`, un chip de la placa, ni de la BIOS ni del cartucho) dice qué línea de un
sprite se dibuja con cada reducción vertical: 256 tablas de 256 bytes (mosaico en el nibble alto, línea en
el bajo); el chip de 128 KB repite esa mitad. Se calcula (`l0_generate`): la tabla z tiene z + 1 líneas, las
de `invertir_bits(i) XOR $88` para i = 0..z, ordenadas, y el resto en `$FF`. La fórmula se dedujo del
contenido del chip y da su SHA1 (`5992277d...`); si el `aes.zip` trae el chip, `--info` dice si son iguales.

### Guardado

Llega en la fase 2: la tarjeta de memoria (el AES no tiene RAM de respaldo).

## Video y audio

Llegan en las fases 3 (LSPC) y 4 (YM2610).

## Variantes de la consola y accesorios

Solo el AES, NTSC. El MVS (DIP, RAM de respaldo, monedas, SFIX/SM1, varios slots) y PAL, después.

## Interfaz del ejecutable

Llega en la fase 3.

## Empaquetado y pruebas

- **`make dist`:** deja `dist/neorecomp-<versión>-<sistema>.tar.gz` con `neorecomp`, el README, la licencia
  y este documento. La versión sale de `git describe`.
- **Integración continua:** el mismo flujo que en los otros recompiladores (Linux, macOS, MSYS2 UCRT64),
  pendiente.
- **Contrato común** (`tests/contract.sh`, el mismo archivo en los cuatro repos; si cambia, se copia a los
  otros tres): ya tiene la rama `neo`; se cumple cuando existan `neotrace`, `neoshot`, `neoaudio` y
  `neobench` (fase 5).
- **`make testroms`** (`tests/testroms.sh`): las SingleStepTests del 68000 y del Z80, en los mismos commits
  fijos que GENRecomp (se quedan comprimidas en `testroms/`, que no se versiona).
- **`make test`:** las SingleStepTests del 68000 por el intérprete y por el camino del código generado
  (~8 s cada una) y las del Z80 (~24 s); `z80test --nmi`; `carttest` (un juego sintético con el P1 de 2 MB de mitades cambiadas da la misma imagen,
  SHA1, cabecera y vectores desde los chips, como `.neo`, como zip guardado dentro de una carpeta y como
  carpeta; el inflate con códigos fijos y dinámicos, cortado y con salida chica; un zip dañado; los sufijos
  con P2 y buses de V separados; los errores; un juego cifrado y uno sin `NEO-GEO`; la tabla L0 y su SHA1;
  una BIOS desconocida o de otro tamaño; la identidad del código en P1, en los bancos de un P de 5 MB y en
la BIOS con sus espejos, con la ida y vuelta por una línea de traza). Con los juegos de `roms/`: `neorecomp --info` y que `--neo` dé el
  mismo SHA1; con Metal Slug, la base de datos, el SHA1, el P1 y las entradas; con `aes.zip`, que se acepte
  `neo-epo.bin` y que su `000-lo.lo` sea la tabla calculada; `neogeo.zip` (MVS) no se acepta.

## Estado de la validación

- **CPU:** las 1 000 058 SingleStepTests del 68000 por el intérprete y por el camino del código generado
  (`m68ktest`, `--recomp`), y las 1 604 000 del Z80 (`z80test`), más la NMI (`z80test --nmi`).
- **Carga:** juegos (`.neo`, zip, carpeta) y BIOS, con `make test`. Probado con Metal Slug (`mslug.zip` de
  MAME) y las BIOS del `aes.zip` y del `neogeo.zip` de MAME.

### Línea base para optimizar el runtime

Llega en la fase 8 (`tests/golden.sh`).
