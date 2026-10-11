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
| `src/runtime/` | La consola: `gen.h` (estado y interfaz), `gen.c` (carga, reset, cuadro, SRAM), `cart.c` (formatos, cabecera, vectores, mapeo), `bus.c` (mapa de memoria del 68000), `io.c` (versión y mandos), `eeprom.c` (EEPROM serie), `vdp.c` (puertos, DMA, líneas), `vdp_render.c` (dibujo), `z80.h`/`z80.c` (el Z80), `z80bus.c` (su mapa y cuándo corre), `psg.c`, `ym2612.c`, `audio.c` (mezcla), `m68k.h` (estado del 68000 y lo que pide del bus), `m68k_ops.h` (semántica de cada instrucción), `cpu68k.c` (decodificador, excepciones, intérprete); `sha1`, `console.h`. |
| `src/genrecomp/` | El recompilador. Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | SDL2 (`main_sdl.c`, menús y diálogos en `ui_menu`/`ui_font`/`i18n`, `settings`, `files`, `export`, `png`, `icono.h`), `random_pad.h`. |
| `src/tools/` | Sin ventana, con las opciones comunes (`cli.h`): `gentrace`, `genshot`, `genaudio`, `ramdump` (y `genbench`, de `src/platform/bench.c`). Pruebas: `carttest`, `m68ktest` y `z80test` (SingleStepTests, con `json.h`), `bustest`, `vdptest`, `audiotest`, `statetest`, `inputtest`. Diagnóstico: `genrun`. `embed`. |
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

## Temporización

Cada acceso del 68000 al bus son 4 ciclos (`m68k_fetch`, `m68k_rd*`, `m68k_wr*`); los ciclos
internos van con `m68k_idle`. Los totales por instrucción coinciden con las SingleStepTests en todos
los casos. El orden de los accesos dentro de una instrucción coincide en el 78 % de los casos
(`m68ktest --bus`); lo que falta es sobre todo si una escritura va antes o después del último prefetch
(4 ciclos dentro de la misma instrucción), que se afinará cuando el VDP lo haga visible (fase 3).
El VDP y el audio serán perezosos.

## Interrupciones y despachador

`gen_run_frame` corre hasta que el VDP termina el cuadro con `m68k_run`, que ejecuta instrucciones
bajo un solo `setjmp` (el error de dirección vuelve ahí; en macOS `setjmp` hace una llamada al sistema
y uno por instrucción costaba casi 10 veces el tiempo) y después de cada una llama a `vdp_sync`, que pone las líneas al día y fija el nivel de interrupción del 68000
(`cpu.ipl`): 6 si hay VINT pendiente y habilitada (registro 1, bit 5), 4 si hay HINT (registro 0, bit
4). El 68000 la atiende al empezar la instrucción siguiente si el nivel pasa su máscara (autovector,
como en la Mega Drive) y avisa con `m68k_int_ack`, que la borra en el VDP. El despachador del código
generado llega en la fase 6.

## Recompilador

```
genrecomp juego.bin [--info]
```

`--trace` (fase 6) leerá las trazas de `gentrace` o de `gen --trace`: una línea `OFFSET:DIRECCION` en
hexadecimal por instrucción de ROM ejecutada (el desplazamiento en la ROM normalizada y la dirección
del 68000 con la que se ejecutó; con SSF2 pueden no coincidir). Las marca el intérprete (`g->trace`).

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
dos últimos dejan el bit de DMA del estado mientras duran. Estado (FIFO vacía, F, desborde y colisión
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

Sin emular todavía: la FIFO (las escrituras del 68000 no esperan y el estado dice siempre "vacía"),
el orden exacto de los accesos del 68000 dentro de una instrucción (78 % con `m68ktest --bus`), el
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
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en NESRecomp y GBRecomp),
  en Linux, macOS y Windows: `make`, `make testroms`, `sh tests/contract.sh`, `make test` y `make dist`.
- **Contrato común** (`tests/contract.sh`, el mismo archivo en los tres repos): `gentrace`, `genshot`,
  `genaudio` y `genbench` aceptan `--frames`, `--input`, `--seed` (y `--repeat`); con la demo Overdrive,
  la misma entrada da la misma traza, captura y sonido, y `--input` da el mismo `genshot --hash` que la
  sintaxis `boton:desde-hasta`. Los botones del guion común (`START`, `DOWN+A`) existen en las tres
  consolas; `--seed` usa el mismo mando aleatorio (`random_pad.h`) traducido a la Mega Drive.
- **`make testroms`** (`tests/testroms.sh`): las pruebas libres en versiones fijas, en `testroms/` (no se
  versionan): las SingleStepTests del 68000 y del Z80 (comprimidas) y las demos Overdrive (Evoke 2013, la versión para hardware)
  y Overdrive 2 (Revision 2017: 8 MB con SSF2, solo PAL) de Titan, de scene.org, con su SHA-256. Las
  demos son la meta más exigente (Overdrive 1 corre pero avisa que necesita PAL; Overdrive 2 queda en
  negro). El 240p Test Suite (GPL) no se baja solo: su ROM compilada está en itch.io y compilarla pide
  SGDK; se pone a mano en `testroms/240p/`. La versión 1.21 corre: menú principal, submenús y detecta
  "NTSC VDP 320x224p".
- **`make test`:** `carttest` (la misma ROM sintética como `.bin`, `.smd` con y sin marca y `.bin`
  con cabecera de copiador debe dar la misma imagen, SHA1 y cabecera), `bustest` (RAM y espejos,
  ROM, versión, bus del Z80, mandos de 3 y 6 botones, SRAM y guardarla, EEPROM serie (escritura,
  lectura aleatoria con START repetido, ACK, página), TMSS (con y sin "SEGA"), SSF2, y un programa de 68000
  que cuenta VINT y HINT, también con la máscara), las SingleStepTests del 68000 por el intérprete y
  por el camino del código generado (~9 s cada una), `genrecomp --info` con cada ROM de `roms/` y, si
  están: Sonic 1 (USA/Europa), que con Start en el título el intérprete llegue a jugar el nivel; NBA
  Jam (USA/Europa), que guarde su EEPROM y la acepte al volver a arrancar. También `vdptest` (puertos,
  los tres DMA, estado, contador V, planos, ventana, sprites con su límite, máscara y desborde,
  sombra y resalte) y que `gen` dibuje cada menú y cada diálogo sin ventana (`--ui-shot`).

### Resultados con juegos

Con el intérprete, el bus y el VDP: Sonic 1 se ve bien en la pantalla de SEGA, "SONIC TEAM
PRESENTS", el título (con el emblema y Sonic) y Green Hill (HUD, scroll por capas, agua, enemigos);
con Start entra al nivel y juega (corriendo a la derecha pierde una
vida contra un enemigo), a ~60 veces el tiempo real. Super Street Fighter II corre con el mapper de
SSF2 (155 cambios de banco en 1200 cuadros) y muestra el logo de SEGA, la intro y el título. NBA Jam guarda su EEPROM y la acepta al volver a
arrancar. `genrun rom --watch DIR` muestra cuándo cambia un byte de la RAM (en Sonic 1, el modo de
juego está en `$FFF600`) y `--sram ARCHIVO` carga y guarda la SRAM o EEPROM.
