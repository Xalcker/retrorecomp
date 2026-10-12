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
| `src/runtime/` | La consola: `neo.h` (estado e interfaz), `neo.c` (carga, encendido, cuadro, tarjeta), `cart.c` (formatos, imagen normalizada, cabecera, vectores, identidad del código), `bios.c` (BIOS del AES y tabla L0), `bus.c` (mapa de memoria del 68000), `lspc.c` (registros del LSPC, VRAM, líneas, temporizador e interrupciones), `lspc_render.c` (el dibujo), `z80bus.c` (mapa y puertos del Z80, cuándo corre), `ym2610.c` (el YM2610: FM, SSG, ADPCM-A y ADPCM-B, temporizadores), `audio.c` (la mezcla), `zip.c` (zip, inflate y CRC32), `neodb.c`/`neodb.h` (base de datos de juegos), `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete), `z80.h`/`z80.c` (el Z80); `sha1`, `console.h`, `trace_line.h` (idéntico en los cuatro recompiladores). |
| `src/neorecomp/` | El recompilador. Por ahora, `--info` y `--neo`. |
| `src/platform/` | SDL2 (`main_sdl.c`, menús y diálogos en `ui_menu`/`ui_font`/`i18n`, `settings`, `icono.h`) y lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (idéntico en los cuatro). |
| `src/tools/` | `carttest` (con `inflate_vectors.h`), `bustest`, `lspctest`, `audiotest`, `m68ktest` y `z80test` (SingleStepTests, con `json.h`); diagnóstico: `neorun`; `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `sst68k.sh` y `sstz80.sh` (las SingleStepTests), `contract.sh` (contrato común, idéntico en los cuatro), `testroms.sh` (`make testroms`). |
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

Todo se cuenta en ciclos: `g->cyc` (del 68000) y los maestros. Un cuadro son 384 pixeles × 264 líneas ×
4 = 405 504 ciclos maestros; el 68000 va a la mitad (202 752 por cuadro, 768 por línea), el Z80 a un sexto
(exactamente un ciclo suyo cada 3 del 68000) y el YM2610 a un tercio (una muestra del FM cada 432
maestros). El reloj del AES (24,167829 MHz según la wiki de NeoGeoDev, 59,60 cuadros por segundo; el MVS y
MAME usan 24 MHz, 59,19) solo cambia el ritmo de la ventana y del audio, no la emulación.

Cada acceso del 68000 al bus son 4 ciclos; la tarjeta de memoria tiene 2 de espera. No hay refresco de
DRAM (las memorias son SRAM) ni esperas de la ROM (los cartuchos van sin estados de espera; no se emulan las
patas ROMWAIT/PWAIT). El Z80 corre detrás del 68000 (`z80_sync`): se pone al día tras cada instrucción del
68000 y antes de que el 68000 le mande un comando o lea su respuesta.

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
próximo evento (`lspc.next_event`: fin de línea, VBLANK, recarga o vencimiento del temporizador) y el Z80
solo si quedó atrás. `lspc_sync` fija el nivel de interrupción del 68000 (`cpu.ipl`) con lo pendiente:
3 la de arranque, 2 el temporizador, 1 el VBLANK (autovector). **No se borran al atenderlas**
(`m68k_int_ack` no hace nada): el programa las reconoce escribiendo `IRQACK` (`$3C000C`: bit 0 la de
arranque, 1 el temporizador, 2 el VBLANK). Si no, al volver con `RTE` la misma interrupción entra otra
vez. La de arranque queda pendiente al encender (no con el botón de reset).

El despachador del código recompilado llega en la fase 6.

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
hay BIOS del AES de EE. UU. `bios_find` la busca en `$NEO_BIOS`, junto al juego y en `roms/`. En el AES no
hay SFIX ni SM1: la capa fija y el Z80 salen siempre del cartucho.

La **tabla L0** (`000-lo.lo`, un chip de la placa, ni de la BIOS ni del cartucho) dice qué línea de un
sprite se dibuja con cada reducción vertical: 256 tablas de 256 bytes (mosaico en el nibble alto, línea en
el bajo); el chip de 128 KB repite esa mitad. Se calcula (`l0_generate`): la tabla z tiene z + 1 líneas, las
de `invertir_bits(i) XOR $88` para i = 0..z, ordenadas, y el resto en `$FF`. La fórmula se dedujo del
contenido del chip y da su SHA1 (`5992277d...`); si el `aes.zip` trae el chip, `--info` dice si son iguales.

### Guardado

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
se salta de una vez hasta el destino o hasta que venza un temporizador.

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
  consola. El tiempo real sale del reloj del AES (740 muestras por cuadro). Los niveles relativos de FM,
  SSG, ADPCM-A y ADPCM-B son provisionales (los picos de los juegos de validación llegan a 10 000-12 000,
  sin recortes) y se ajustarán con los oráculos.
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
- **BIOS:** `--bios archivo`, o la que encuentre el runtime (`NEO_BIOS`, junto al juego, `roms/`), o la copia
  de la carpeta de datos (`bios.bin`), o se pide (diálogo o arrastrarla). La que no venía de la copia se
  guarda ahí. Carpeta de datos: `NEORecomp/interprete/` (con las preferencias y lo exportado).

Opciones: `neo [juego] [--bios archivo] [--scale N]`. Para pruebas sin ventana: `--ui-shot archivo.png` con
`--ui-frames N`, `--ui-menu N` y `--ui-action N`. Los estados guardados, la grabación de entradas y la traza
llegan en la fase 5; el modo recompilado, en la 6.

Icono: `NEOGEO.png` (el mando del AES, serie TriMui) de
[KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons), CC BY 4.0; la
ventana lo recorta al dibujo y `icono.ico` (Windows) es ese recorte llevado a 256x256 (`tools/make_icon.py`).

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
- **`neorun juego [--frames N] [--bios archivo] [--card archivo] [--watch DIR] [--fix] [--shot/--shots] [boton:desde-hasta]`:**
  corre sin ventana con el intérprete y resume instrucciones, ciclos, excepciones, el estado del Z80, las
  variables de la BIOS en la RAM (`SYSTEM_MODE`, `USER_REQUEST`, `USER_MODE`...) y los PC más frecuentes del
  último cuadro. `--fix` muestra la capa fija como texto (el byte bajo de cada mosaico como ASCII: muchos
  juegos ponen su fuente así, Metal Slug en `$300` + ASCII). `--shot archivo.png` guarda la imagen del
  último cuadro y `--shots carpeta --every N` una cada N cuadros; dice cuántos colores distintos tiene.
- **`make test`:** las SingleStepTests del 68000 por el intérprete y por el camino del código generado
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
- **Audio:** `audiotest`. Los tres juegos suenan: Metal Slug con FM y ADPCM-A, Samurai Shodown II también con
  ADPCM-B y Neo Turf Masters además con el SSG. Con el sonido, 14-18 veces el tiempo real.
- **Sin comprobar todavía con un oráculo:** la posición del VBLANK y de la recarga del temporizador en la
  línea, lo que leen `$300001` y `$320001` en el AES, el perro guardián (MAME no lo emula en el AES) y la
  dirección exacta del latch de bancos de P2.

### Línea base para optimizar el runtime

Llega en la fase 8 (`tests/golden.sh`).
