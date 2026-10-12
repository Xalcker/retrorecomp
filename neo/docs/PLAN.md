# Plan de NEORecomp

Recompilador estático de juegos de Neo Geo (68000 principal, Z80 de sonido) con la misma estructura que
[GENRecomp](https://github.com/Xalcker/GENRecomp), [NESRecomp](https://github.com/Xalcker/NESrecomp) y
[GBRecomp](https://github.com/Xalcker/GBRecomp): runtime con intérprete de respaldo, proyecto C99 generado
sin datos de la ROM ni de la BIOS (valida sus SHA1), `difftest` contra el intérprete y el mismo contrato de
línea de comandos. Herramientas: `neorecomp`, `neotrace`, `neoshot`, `neoaudio`, `neobench`, `neoramdump`,
`neorun` y la ventana `neo`.

El modelo es GENRecomp: el mismo 68000 y el mismo Z80, validados con las SingleStepTests por los dos
caminos. Se copian y se adaptan (el código es del mismo autor); la semántica del CPU no cambia.

**Estado: fases 0 a 5 hechas.** Cada fase empieza con la aprobación del autor.

## Qué cambia respecto al Genesis

| | Genesis (GENRecomp) | Neo Geo AES (NEORecomp) | Consecuencia |
|---|---|---|---|
| Reloj | Maestro 53,69 MHz; 68000 a /7, Z80 a /15; 262 líneas, 59,92 Hz | Maestro de 24 MHz (MVS) o 24,167829 MHz (AES, sin medir con osciloscopio); 68000 a /2, Z80 a /6, YM2610 a /3, pixel a /4. 384 pixeles × 264 líneas = 405 504 ciclos maestros por cuadro: 202 752 del 68000, 768 por línea. AES: 59,5995 Hz; MVS: 59,1856 Hz | Todo se cuenta en ciclos maestros: el cuadro tiene los mismos ciclos con cualquier reloj, que solo cambia el ritmo de la ventana y del audio. El Z80 va exactamente a 1/3 del 68000 (en el Genesis era 7/15). |
| Esperas del bus | Refresco de la DRAM (2 de cada 128 ciclos), FIFO del VDP, DMA | Memorias SRAM: sin refresco. ROM y zona PORT con 0-2 esperas según patas del cartucho (casi todos a toda velocidad), tarjeta de memoria con 2 | Más simple. Se agregan esperas solo si los oráculos lo piden. |
| Programa | ROM lineal hasta 4 MB (más el mapper de SSF2) | P1 en `$000000-$0FFFFF`; P2 en la ventana `$200000-$2FFFFF`: fija si P mide hasta 2 MB (un P1 de 2 MB tiene sus mitades cambiadas), en bancos de 1 MB si mide más (latch en `$2FFFF0`, se cuenta toda escritura en la ventana) | Identidad por desplazamiento en la imagen P normalizada, con ventanas de 1 MB: el banco P2 hace el papel del mapper de SSF2. |
| Vectores | 64 en `$000000` del cartucho | Al reset, `$000000-$00007F` muestra los de la BIOS; `REG_SWPROM` pone los del juego. Los juegos además exponen una tabla de entradas en su cabecera (`$122` USER, `$128` PLAYER_START, `$12E` DEMO_END, `$134` COIN_SOUND) | Semillas del juego: sus vectores (VBLANK en `$64`, temporizador en `$68`) y las entradas de la cabecera. Semillas de la BIOS: sus vectores y su tabla de saltos `$C00402-$C004DA`. |
| BIOS | No hay (TMSS opcional, sin ROM) | 128 KB en `$C00000` (espejos hasta `$CFFFFF`), obligatoria: arranca el juego, lo llama por la cabecera y el juego la llama a ella (`SYSTEM_INT1`, `SYSTEM_IO`, `FIX_CLEAR`, `LSP_1st`, `MESS_OUT`, `CARD`...); variables en `$10FD80-$10FFFF` | Segundo espacio de identidad: la BIOS se recompila aparte, una vez por versión, y la comparten todos los juegos. |
| RAM | 64 KB en `$FF0000` (espejos desde `$E00000`) | 64 KB en `$100000` (espejos hasta `$1FFFFF`); la BIOS se reserva `$10F300-$10FFFF`. TAS escribe de verdad | El bus decide sobre la escritura de TAS (`cpu.tas`), sin tocar `m68k_ops.h`. |
| E/S | `$A10000` (versión, mandos), `$A11100` (bus del Z80) | `$300000` P1, `$340000` P2, `$380000` Start/Select y tarjeta, `$320000` comando al Z80 y su respuesta, `$3A0001-$3A001F` latch del sistema (sombra, vectores, tarjeta, paleta; en el AES no hay BRDFIX/CRTFIX), `$3C0000` LSPC, `$400000` paleta (dos bancos), `$800000` tarjeta de memoria | El 68000 nunca toca el bus del Z80: solo un byte de comando y uno de respuesta. |
| Video | VDP: 2 planos, ventana, 80 sprites, scroll por línea o celda, 3 DMA | LSPC: **sin planos**. 381 sprites de 16 pixeles de ancho, columnas de hasta 32 mosaicos de 16×16 encadenadas (bit pegajoso), reducción horizontal (tabla fija del LSPC) y vertical (tabla L0, chip de la placa), 96 sprites por línea, autoanimación; capa fija de 8×8 (40×32, de la S ROM); paleta de 2×256×16 colores con bit de oscurecido, modo sombra; VRAM de 34 K palabras por un puerto con incremento con signo. Gráficos en las C ROM (hasta 64 MB en los juegos sin cifrar) | El dibujo es de a línea (efectos de trama) y de a sprite: se evalúa qué sprites tocan la línea y se dibuja su tramo de 16 pixeles de una vez. Las C ROM se convierten al cargar a un formato de 4 bits por pixel listo para copiar. |
| Interrupciones | VINT (6) y HINT (4) del VDP, reconocidas por el CPU | Nivel 1 VBLANK (línea 224 activa, `$1F0` del contador), 2 temporizador de 32 bits a 6 MHz (recarga al llegar a 0, al empezar el cuadro o al escribir; pone efectos a mitad de cuadro), 3 tras el arranque en frío. Autovector, pero **no se borran al atenderlas**: el programa escribe `IRQACK` (`$3C000C`) | `m68k_int_ack` no borra nada; el nivel lo fija el LSPC según lo pendiente y no reconocido. |
| Segundo CPU | Z80 con código en su RAM (lo sube el 68000) | Z80 con código en la **M1 ROM** del cartucho (32 KB fijos más ventanas de 16/8/4/2 KB por puertos `IN $08-$0B`), 2 KB de RAM. NMI por cada comando del 68000; INT (modo 1) por los temporizadores del YM2610 | El Z80 sigue interpretado. Como su código está en ROM, podría recompilarse (fase 8, solo si el perfil lo pide). |
| Sonido | YM2612 (6 FM + DAC) y SN76489 | YM2610: 4 canales FM (los 2, 3, 5 y 6 del OPNA), SSG (3 tonos, ruido, envolvente), ADPCM-A (6 canales a 18 518 Hz) y ADPCM-B (uno, frecuencia variable), desde las V ROM (buses A y B separados en los juegos viejos) | El FM sale del YM2612 de GENRecomp. SSG, ADPCM-A y ADPCM-B son nuevos, escritos desde la documentación. FM a 55 555 Hz; ADPCM-A cada 3 muestras del FM. |
| Mandos | 3 y 6 botones | Cruz, A B C D, Start y Select (en el AES no hay monedas) | 10 botones en el guion de entradas. |
| Guardado | SRAM y EEPROM serie en el cartucho | AES: solo la **tarjeta de memoria** (2 KB, la maneja la BIOS con `CARD`). MVS: RAM de respaldo (después) | La tarjeta se emula como un archivo, igual que la SRAM. |
| Formatos | `.bin`/`.md`/`.gen`/`.smd` | Varios chips por juego: P, S, M1, V (A y B), C en pares intercalados. `.neo` (un archivo) o zip al estilo MAME | Ver abajo. |
| Protección | Ninguna (salvo sin licencia) | Desde 1999: NEO-CMC (C y S), SMA (P), PCM2 (V), PVC; antes, unos pocos con protección del P | Primero solo juegos sin cifrar; los cifrados se rechazan con un mensaje claro. |

## Formatos de ROM

Propuesta: **los dos**, normalizados a la misma imagen.

- **`.neo`** (cabecera de 4096 bytes con `NEO\1` y los tamaños de P, S, M, V1, V2 y C en little-endian, más
  año, género, NGH, nombre y fabricante; los datos en ese orden). Es lo que usa Geolith, el oráculo
  principal, así que hace falta de todos modos. No es una especificación oficial (la describe el código de
  `bodgit/terraonion` y de Geolith): se escribe un lector propio y se comprueba con los archivos del usuario.
- **Zip al estilo MAME** (o una carpeta con los chips sueltos), con una base de datos pequeña por juego
  (`neodb.c`): nombre del juego, NGH, y por cada chip su CRC32, tamaño, región y forma de cargarlo (P con
  palabras invertidas, P1 de 2 MB con las mitades cambiadas, P2/SP2, V del bus A o del B, C en pares). Los
  chips se reconocen por el CRC32 del directorio del zip, no por el nombre. Un zip que no está en la base
  se intenta por los sufijos (`.p1`, `.s1`, `.m1`, `.v1`/`.v11`/`.v21`, `.c1`...) con un aviso.
- **Exportar**: `neorecomp juego.zip --neo salida.neo` escribe el `.neo` (para Geolith y para quien lo
  prefiera).

**Imagen normalizada:** las regiones en el orden del `.neo` (P, S, M1, V-A, V-B, C) y con su forma (P
en el orden de bytes de los chips, palabras con los bytes invertidos, en orden lineal y con las mitades
del P1 de 2 MB ya en su lugar; V-B vacía si el juego usa un solo bus; C intercalada por pares). La identidad es el **SHA1 de la imagen normalizada
completa**, igual con cualquier formato; también se calcula el SHA1 de cada región para decir cuál no
coincide. El código generado depende solo de P, pero se valida todo para no correr un juego con los
gráficos o el sonido equivocados. Comprobado en la fase 0 contra el código de los convertidores y de
Geolith (que invierte los bytes de P al cargar): es la forma del `.neo`, así que el SHA1 de la imagen
normalizada es el de la parte de datos de un `.neo`.

`neorecomp juego --info` muestra el formato, las regiones y sus tamaños, los SHA1, la cabecera de `$100`
(`NEO-GEO`, NGH, tamaño de P, RAM de respaldo, entradas), los vectores y si el juego está en la base de
datos. Si el NGH es de un juego cifrado o protegido, lo dice y no sigue.

## BIOS

**Etapa 1: la original, obligatoria, la pone el usuario.** Nunca va al repositorio. Se valida por SHA1.
Propuesta de versiones aceptadas para el AES:

| Archivo (MAME) | Versión | SHA1 | |
|---|---|---|---|
| `neo-epo.bin` | AES de exportación ("Asia" en MAME; no hay una de EE. UU.) | `1b3b22092f30c4d1b2c15f04d1670eb1e9fbea07` | La de referencia: textos en inglés. |
| `neo-po.bin` | AES de Japón | `4e4a440cae46f3889d20234aebd7f8d5f522e22c` | Aceptada (los juegos salen en japonés). |

Los SHA1 son los de MAME y coinciden con los del `aes.zip` del autor. El byte `$401` de la BIOS es la
región que ven los juegos: `neo-epo.bin` dice **Europa** (2) y `neo-po.bin` Japón (0). No hay BIOS del AES
de EE. UU.; los juegos salen en inglés con la de exportación. La BIOS de desarrollo y la Universe
BIOS (de uso personal, prohibido distribuirla o modificarla) no se aceptan. Las de MVS llegan con el MVS.

- **En el AES no hay SFIX ni SM1:** la capa fija sale siempre de la S ROM del cartucho y el Z80 de su M1
  (las escrituras a BRDFIX/CRTFIX no hacen nada). SFIX y SM1 son de la placa del MVS.
- **La tabla L0** (reducción vertical, `000-lo.lo`) es un chip de la placa, ni de la BIOS ni del cartucho.
  Se calcula exacta (hecho en la fase 0): la tabla z son las líneas `invertir_bits(i) XOR $88` para
  i = 0..z, ordenadas y rellenadas con `$FF`, y la segunda mitad repite la primera. La fórmula se dedujo
  del contenido del chip, no de otro código, y da su SHA1 (`5992277debadeb64d1c1c64b0a92d9293eaf7e4a`).
  Así no se le pide al usuario; si la pone, se compara con la calculada.
- **Se recompila también**, con su propio espacio de identidad: el desplazamiento dentro de la imagen de
  la BIOS. En las trazas va como el banco `FF`: `FF:C00402` es la instrucción de `$00402` de la BIOS. Al
  principio (fases 2-5) todo corre en el intérprete, también la BIOS. Desde la fase 6, `neorecomp` genera el
  código de cada versión **una vez**, en una caché (`build/bios/<sha1>/`), y lo copia a cada proyecto
  (`bios/`), que puede llevar varias versiones. En tiempo de ejecución, el despacho de `$C00000-$CFFFFF` usa
  el código de la BIOS cargada si su SHA1 coincide; si no, el intérprete. Los datos de la BIOS (tablas,
  textos) se leen del archivo en tiempo de ejecución.
- Por qué recompilarla: la BIOS corre en cada cuadro (`SYSTEM_INT1`, `SYSTEM_IO`, `MESS_OUT`); sin ella no se
  llega al 100 % de los ciclos en código recompilado.

**Etapa 2 (opcional, al final): una BIOS de reemplazo propia.** Lo que ya existe: **nullbios** (de
ngdevkit, LGPL-3) arranca homebrew y deja vacíos `MESS_OUT`, `CARD`, `HOW_TO_PLAY`, el calendario y la
pantalla de SNK, así que no basta para juegos comerciales, y su licencia no es MIT. La Universe BIOS no es
libre. El diagnóstico de smkdan no arranca juegos y no tiene licencia. Así que se escribe una propia (fase
10), a partir de la interfaz documentada: puntos de entrada, arranque y comandos (`USER_REQUEST` 0/1/2,
`SYSTEM_RETURN`), variables de la BIOS en la RAM, mandos, Start/Select y créditos, la tarjeta, `MESS_OUT`;
la tabla de zoom calculada y una fuente propia. Nunca se traduce ni se copia el código de SNK (ni el de
nullbios). `tools/compat` mide juego por juego qué cambia contra la original.

## Fases

Cada fase dice qué se copia de GENRecomp. "Copiar" es tomar el archivo y adaptarlo (nombres `neo`, la
estructura `Neo` en lugar de `Gen`), sin cambiar la semántica de lo que ya está validado.

- [x] **0. Esqueleto y carga.** Hecho: `neorecomp --info` y `--neo` con `.neo`, zip de MAME y carpeta; la
  imagen normalizada es la del `.neo` (su SHA1 es el de la parte de datos de un `.neo`; Metal Slug
  `582233c4...`, comprobado aparte); la BIOS del AES por SHA1 (suelta o en `aes.zip`); la tabla L0
  calculada (fórmula deducida del chip, da su SHA1); `carttest` y `make test`. La integración continua
  queda para más adelante (decisión del autor).
  - Copia: el bloque común del `Makefile` (con `.DEFAULT_GOAL := all`), la integración continua (Linux,
    macOS, MSYS2 UCRT64, con la caché de `testroms`; pendiente), la licencia MIT, el submódulo `tools/retrorecomp`,
    `png`, `files`, `export`, `random_pad.h` (idéntico, sin tocar), `trace_line.h` (idéntico salvo el
    comentario, que agrega Neo Geo: hay que copiarlo a los otros tres repos), `sha1`, `console.h`, `embed`.
  - `tests/contract.sh` con la rama `neo` (nota en el archivo: copiarlo a NESRecomp, GBRecomp y GENRecomp;
    los clones locales de NESRecomp y GBRecomp están atrasados respecto a GitHub, se parte del de GENRecomp).
  - Nuevo: lectura de `.neo`, zip (un lector propio de zip con inflate, o `unzip -p` si no) y carpeta;
    `neodb.c`; imagen normalizada y sus SHA1; validación de la BIOS; generador de la tabla L0; `--info`;
    `--neo`. `carttest`: el mismo juego sintético como `.neo`, zip y carpeta da la misma imagen y el mismo
    SHA1; la L0 calculada da su SHA1.
  - ROMs del autor: `mslug.zip`, `aes.zip` y `neogeo.zip` de MAME.
- [x] **1. CPU.** Hecho: el 68000 y el Z80 de GENRecomp sin cambios de semántica (solo los nombres y la
  traza); pasan las 1 000 058 SingleStepTests del 68000 por los dos caminos y las 1 604 000 del Z80. Al Z80
  se le agregó la NMI (`z80test --nmi`), y esa prueba encontró un error que viene de GENRecomp: al
  atender una interrupción en un `HALT` se apilaba una dirección de más (se volvía una instrucción más
  adelante). Corregido aquí; **falta corregirlo en GENRecomp** (Xalcker/GENRecomp#15). La identidad del código
  (`neo_code_offset`: P1, bancos de P2, la BIOS en el banco `FF`) con su prueba en `carttest`.
  - Copia: `m68k.h`, `m68k_ops.h`, `cpu68k.c`, `z80.h`, `z80.c`, `m68ktest`, `z80test`, `json.h`,
    `sst68k.sh`, `sstz80.sh`, y la parte de `tests/testroms.sh` que baja las SingleStepTests (los mismos
    commits fijos). Sin cambios de semántica: las 1 000 058 del 68000 por los dos caminos (con las mismas
    excepciones de DIVU/DIVS) y las 1 604 000 del Z80.
  - Se adelanta el Z80 (en GENRecomp llegó en la fase 4) porque en el Neo Geo la BIOS y los juegos
    esperan las respuestas del Z80 para arrancar.
- [x] **2. Bus, BIOS e interrupciones.** Hecho: mapa de memoria, vectores de la BIOS y del juego, bancos
  de P2, latch del sistema, mandos, paleta, tarjeta de memoria (2 KB, con sus llaves), registros del LSPC
  y VRAM, contador de líneas, temporizador y las tres interrupciones con `IRQACK`; el Z80 con los bancos de
  la M1, la NMI por comando y la respuesta; los temporizadores y la IRQ del YM2610. `neorun` (con `--fix` y
  las variables de la BIOS) y `bustest`. **Meta cumplida:** con la BIOS original, Metal Slug pasa la
  pantalla de SNK, llega a la demo ("CREDITS 04") y con Start empieza la partida. Pendiente de los
  oráculos: la posición del VBLANK en la línea, `$300001`/`$320001` en el AES, el perro guardián.
  - Copia la forma de `bus.c` (cada acceso 4 ciclos, bus abierto con el último dato) y de `gen.c`
    (cargar, reset, cuadro). `m68k_run` con un solo `setjmp` por cuadro.
  - Nuevo: mapa de memoria del 68000; banco P2; cambio de vectores; latch del sistema; mandos del AES;
    tarjeta de memoria (archivo); registros del LSPC sin imagen (VRAM, módulo, contador de líneas,
    temporizador con sus tres modos de recarga, `IRQACK`); niveles 1, 2 y 3; el mapa del Z80, sus puertos
    y bancos, el comando con NMI y la respuesta; los temporizadores, las banderas y el IRQ del YM2610 (sin
    sonido todavía: los drivers del Z80 dependen de ellos).
  - `neorun` (como `genrun`: `--watch`, `--card`, resumen de instrucciones, excepciones y dónde se va el
    tiempo) y `bustest` (con programas de 68000 hechos en la prueba: VBLANK y temporizador, `IRQACK`,
    banco P2, comando y respuesta del Z80).
  - Meta: con la BIOS original, Metal Slug pasa el arranque y llega a la demo (se ve en la RAM).
- [x] **3. LSPC (video) y la ventana.** Hecho: el dibujo de a una línea (sprites con cadenas, las dos
  reducciones, volteos, autoanimación, 96 por línea; capa fija; paleta con el bit de oscurecido y la
  sombra), con la C y la S convertidas al cargar; la ventana `neo` (menús de Moskva 89 en español e
  inglés, tamaños, pantalla completa, aspecto, captura, tarjeta de memoria, la BIOS buscada o pedida y
  guardada en la carpeta de datos, `--ui-shot`); el icono `NEOGEO.png` de KyleBing (CC BY 4.0); `lspctest`;
  `neorun --shot/--shots`. **Meta cumplida:** Metal Slug, Samurai Shodown II y Neo Turf Masters se ven bien
  (también los efectos de trama del hoyo de Neo Turf Masters). Sin comprobar: cuánto oscurece la sombra.
  - Copia: la ventana (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`; la barra de menús de
    Moskva 89, en español e inglés) y el modo perezoso del VDP de GENRecomp (dibujar una línea al terminar o
    cuando el CPU va a cambiar algo que la afecta).
  - Nuevo: sprites (SCB1-4, cadenas, tamaño 32/33, reducción horizontal y vertical, volteo, autoanimación,
    96 por línea), capa fija, paleta con el bit de oscurecido y la sombra, color de fondo, efectos de
    trama por el temporizador. Diseñado para la velocidad desde el principio: de a sprite y de a fila de
    mosaico, con las C ROM ya convertidas.
  - Icono de la consola de KyleBing/retro-game-console-icons (CC BY 4.0, con su crédito); se busca el
    dibujo del Neo Geo en el repositorio.
  - `lspctest`, `neorun --shot/--shots`. Meta: Metal Slug, Samurai Shodown II y Neo Turf Masters se ven bien.
- [x] **4. Audio.** Hecho: el YM2610 completo (el FM del YM2612 de GENRecomp con sus 4 canales; SSG con
  ruido y envolvente; 6 canales de ADPCM-A; ADPCM-B con interpolación y repetición; banderas de fin con su
  máscara; ocupado), la mezcla de GENRecomp a 44 100 Hz con el reloj del AES, el menú Audio y el sonido en
  la ventana, `neorun --wav/--mute`, `audiotest`. Los tres juegos suenan. Pendiente de los oráculos: los
  niveles relativos de FM, SSG y ADPCM y la duración de "ocupado".
  - Copia: `ym2612.c` como base del FM (4 canales, sin DAC), `audio.c` (mezcla a 44 100 Hz con
    pasa-altos) y el menú Audio.
  - Nuevo: SSG, ADPCM-A (tabla de pasos documentada) y ADPCM-B, las V ROM con sus dos buses. Desde el
    principio: canales callados sin sintetizar, ADPCM decodificado de a bloque, frecuencias guardadas.
  - `audiotest` (afinación de cada parte, fin de muestra y sus banderas, temporizadores), `neorun --wav`.
- [x] **5. Herramientas sin ventana.** Hecho: `cli.h` e `inputs.h` con los 10 botones, `neotrace`,
  `neoshot` (`--expect`, `--hash`), `neoaudio`, `neobench`, `neoramdump`, `statetest`, `inputtest`; estados
  guardados sin la BIOS (F5/F8, 4 ranuras), `--record`/`--play` y `--trace` en `neo`; el contrato común sin
  fallas con un juego de prueba propio (`mkcontrato`, MIT, en `make testroms`), que encontró una errata del
  código de seguridad en la wiki de NeoGeoDev. Falta copiar `contract.sh` y `trace_line.h` a los otros tres
  repos.
  - Copia: `cli.h`, `inputs.h` (10 botones: `UP DOWN LEFT RIGHT A B C D START SELECT`, el mismo formato de
    texto), `neotrace`, `neoshot` (`--expect`, `--hash`), `neoaudio`, `neobench`, `neoramdump`,
    `statetest`, `inputtest`, estados guardados (F5/F8, 4 ranuras), `--record`/`--play`, `--trace` y
    `--ui-shot` en `neo`.
  - El mando aleatorio: `random_pad.h` sin cambios; en el Neo Geo A es A, B es B y Start es Start. C, D y
    Select no los pulsa (como en el Genesis no pulsa C, X, Y, Z ni Mode), pero existen en los guiones y en
    `boton:desde-hasta`.
  - `contract.sh` completo con una prueba libre: el juego de prueba propio `testroms/neo/contrato.neo`.
- [ ] **6. Recompilador v1.**
  - Copia: de `genrecomp.c`, el recorrido, la partición en funciones, la emisión (`AFTER`, `LEAVE`,
    `MAPCHK`), el despacho (`recomp_lookup`), el proyecto generado (`embed`, `Makefile`, `LEEME.txt`),
    `difftest.c`, `bench.c`, `recomp.h`; `dis68k.c` tal cual.
  - Nuevo: el código se identifica por su desplazamiento en la imagen P (bancos de 1 MB) o en la BIOS
    (banco `FF`). Semillas del juego: vectores, entradas de la cabecera; de la BIOS: vectores y su tabla de
    saltos. Los saltos de un espacio al otro (`JSR $C0044A`, la BIOS llamando a `$122`) no se siguen:
    salen al despachador. En los juegos de más de 2 MB, un salto a la ventana P2 desde fuera se prueba en
    todos los bancos y se queda lo que tenga pinta de código (como en NESRecomp); `MAPCHK` vigila el banco.
  - `difftest`: 68000, ciclos, RAM, VRAM, paleta, LSPC, banco P2, latch, tarjeta, Z80 y su RAM, YM2610,
    mezcla de audio y bus abierto tras cada función; imagen y audio al final de cada cuadro.
    `--uncovered`, `--screens`, `--bench`, `--executed`.
  - Meta: los juegos de validación dan 1800 cuadros idénticos al intérprete (con traza).
- [ ] **7. Heurísticas de cobertura.**
  - Copia: todas las de GENRecomp (tablas de desplazamientos, de instrucciones, de punteros de 32 bits,
    bases constantes; especulativas aparte: punteros inmediatos, tablas sueltas, tras un final) y su
    plausibilidad, ajustando las direcciones razonables al mapa del Neo Geo. `--coverage`, `--why`,
    `--viz [--color]`, `--html`, `--events`, `--codemap`, `--dis`, `--no-tables`, `--no-spec`;
    `tests/coverage.sh`.
  - Nuevo, si los juegos lo piden: los modismos de SNK (tablas de estados de sus motores).
  - Meta: sin traza, cerca del 100 % de lo que ejecutan los juegos, y sin diferencias con el intérprete.
- [ ] **8. Rendimiento.** Copia `tests/golden.sh`. Perfilar primero; cada cambio sin mover un bit de la
  imagen ni del sonido. Candidatos según la lección de GENRecomp (el 68000 era ~13 %): el LSPC, el Z80 (el
  `HALT` de una vez; recompilar la M1 solo si pesa) y el YM2610.
- [ ] **9. Compatibilidad, oráculos y empaquetado.**
  - Copia: `tools/oracles/` (`retro_run.c`, `libretro_min.h`, `oracle.py`: los núcleos bajan a
    `build/oracles/` y los botones se asignan por las descripciones que publica cada núcleo, que también
    escriben en la salida estándar) y `tools/compat/` (`run.py`, `report.py`).
  - Oráculos: **Geolith** (BSD-3; `.neo`; `aes.zip` con `neo-epo.bin` y `000-lo.lo` armado en
    `build/oracles/sys/` con la BIOS del usuario y la L0 calculada; opción `geolith_system_type=aes`) y
    **FBNeo** (licencia no comercial, solo como programa aparte; zips de MAME y `neogeo.zip`;
    `fbneo-neogeo-mode=AES_EUR`). El MAME actual como tercero si hace falta. El buildbot solo tiene
    `latest/`: se guarda el SHA-256 de lo que se bajó.
  - `docs/COMPATIBILIDAD.md` y `docs/compat/`; `make dist`; el README y `ARQUITECTURA.md` completos.
  - Agregar Neo Geo a la GUI de retrorecomp: **es un cambio en ese repositorio, se pregunta antes.**
- [ ] **10. (Opcional) BIOS de reemplazo propia.** Ver "BIOS". Se mide con `tools/compat` contra la original.

Después: **MVS** (DIP, RAM de respaldo, monedas, SFIX/SM1, varios slots, el reloj de 24 MHz), **juegos
cifrados** (NEO-CMC, SMA, PCM2, PVC), **PAL**. Fuera de alcance: Neo Geo CD.

**¿AES o MVS primero?** AES primero, como estaba decidido: su BIOS no tiene monedas, DIP, RAM de
respaldo, calendario ni SFIX/SM1, es lo que Geolith usa por omisión y es la consola de NTSC. Lo que se
pierde: algunos juegos salieron solo en MVS (Shock Troopers, Neo Drift Out). Casi todos corren con la BIOS
del AES, pero alguno puede fallar; eso se verá en la fase 9.

## Pruebas libres (`make testroms`)

Hay pocas, y ninguna corre sin BIOS salvo las de ngdevkit:

| Prueba | Licencia | ¿Pide la BIOS de SNK? | Cómo se obtiene |
|---|---|---|---|
| SingleStepTests del 68000 y del Z80 | MIT | No | Los mismos commits que GENRecomp. |
| Ejemplos de ngdevkit (`dciabrin/ngdevkit-examples`: temporizador por línea, P de 1 y 2 MB y con bancos, tarjeta, ADPCM-A/B, música) | GPL-3 (los programas; se usan como datos, no van en el repo) | No: arrancan con nullbios (LGPL-3), que el mismo ngdevkit construye | Compilarlos con ngdevkit en una etiqueta fija (`brew tap dciabrin/ngdevkit` en macOS ARM, PPA en Ubuntu, paquete de MSYS2 UCRT64). |
| 240p Test Suite para Neo Geo (Artemio Urbina) | GPL (v2 o v3: el repo y la página de itch.io no coinciden) | Sí | A mano desde itch.io (`.neo` o zip), en `testroms/240p/`, como en GENRecomp. |
| BIOS de diagnóstico de smkdan (`jwestfall69/neogeo-diag-bios`) | Sin licencia | Es una BIOS de diagnóstico | Solo a mano, opcional. |
| Homebrew (Neo Thunder, Neo Pong, Blut Engel...) | Sin licencia explícita | Sí | No se usa. |

Y como en GENRecomp, `make test` arma en C sus propios programas mínimos de 68000 (un juego sintético
con su cabecera) para el recompilador y las pruebas unitarias. Sin la BIOS de SNK en el CI, lo que necesita
una BIOS se prueba ahí con nullbios (mientras no exista la propia) o se salta.

## Juegos para validar (sin cifrar)

Comprobado en las tablas de MAME y FBNeo: los cinco candidatos son cartuchos normales, sin NEO-CMC, SMA,
PCM2, PVC ni protección del P.

| Juego (MAME) | NGH | P | C | V | AES | Qué prueba |
|---|---|---|---|---|---|---|
| Metal Slug (`mslug`) | 201 | 2 MB (P1 con mitades cambiadas) | 16 MB | 8 MB | Sí | El primero: sprites, ADPCM, el P1 de 2 MB. |
| Samurai Shodown II (`samsho2`) | 063 | 2 MB | 16 MB | 7 MB | Sí | Zoom de cámara. |
| Fatal Fury Special (`fatfursp`) | 058 | 1 MB + 512 KB | 12 MB | 5 MB | Sí | P2 fijo y chico. |
| Neo Turf Masters (`turfmast`) | 200 | 2 MB | 8 MB | 8 MB | Sí | Efectos de trama a mitad de cuadro (temporizador). |
| Shock Troopers (`shocktro`) | 238 | 1 MB + 4 MB | 32 MB | 6 MB | **No, solo MVS** | Bancos P2 de verdad; probar si corre con la BIOS del AES. |

Para completar, todos sin cifrar: **Super Sidekicks 2** (`ssideki2`, temporizador), **Spin Master**
(`spinmast`, trama en el agua), **Samurai Shodown IV** (`samsho4`, P de 5 MB) o **The King of Fighters
'97 del AES** (`kof97h`), **The King of Fighters '98 del AES** (`kof98h`: P de 5 MB, C de 64 MB, M1 de 256
KB con bancos del Z80; el `kof98` de MVS está cifrado), **Art of Fighting** (`aof`, zoom). Evitar por
ahora: Fatal Fury 2 y Super Sidekicks (protección del P), los juegos con cable de enlace (Riding Hero,
League Bowling, Thrash Rally), Metal Slug X (protección), los de mahjong, trackball o paleta. Desde 1999
(NGH `$251`, KOF '99) todo está cifrado.

## Riesgos

1. **Sin BIOS en el CI.** La BIOS es del usuario y nunca va al repositorio: en el CI solo corren las
   SingleStepTests, los programas sintéticos y lo de ngdevkit con nullbios. Instalar ngdevkit en los tres
   sistemas del CI puede salir caro o frágil; si es así, se hace opcional y se depende más de los
   programas sintéticos. La validación con juegos es local (como con `roms/` en GENRecomp).
2. **Dos espacios de identidad.** Juego y BIOS se llaman entre sí en cada cuadro: cada cruce sale al
   despachador. Hay que medir el costo y que `difftest` cubra el cruce. Las versiones de la BIOS que no se
   recompilaron (o una no aceptada con `--force`) caen al intérprete.
3. **Temporizador y efectos de trama.** La posición exacta de la interrupción del temporizador dentro de
   la línea (la wiki y MAME difieren en uno), los 4 ciclos entre que se dispara y que el 68000 la atiende,
   y qué cambia a mitad de línea. Neo Turf Masters, Super Sidekicks 2 y Spin Master lo delatan. Se empieza
   dibujando de a línea y se afina con los oráculos.
4. **El reloj del AES.** 24,167829 MHz no está medido con osciloscopio y MAME usa 24 MHz. No cambia la
   emulación (todo va en ciclos maestros), solo el ritmo de la ventana y del audio, y la comparación con
   oráculos que usen otro ritmo.
5. **Detalles inciertos de la documentación:** el sprite 0 (¿se dibuja?), el perro guardián en el AES
   (MAME no lo emula), la interrupción de nivel 3 en el reset, la dirección exacta del latch de bancos
   (MAME: `$2FFFF0`; la wiki: toda la ventana). Se resuelven con los oráculos y con las pruebas de ngdevkit.
6. **El YM2610 es más que el YM2612:** SSG, ADPCM-A y ADPCM-B son nuevos. Y el audio pesó mucho en
   GENRecomp: se diseña rápido desde el principio.
7. **Memoria y carga:** las C ROM llegan a 64 MB (KOF '98) y la imagen completa a ~100 MB; leer un zip y
   calcular el SHA1 cada vez que se abre el juego tarda algo (decenas o cientos de milisegundos).
8. **El formato `.neo` no es oficial:** el orden de bytes de P y la división de V1/V2 pueden variar entre
   convertidores. Por eso la identidad es la de la imagen normalizada, no la del archivo.
9. **Juegos solo de MVS con la BIOS del AES:** Geolith avisa que algunos fallan. Se anotan en la prueba de
   compatibilidad.
10. **Licencias:** MAME, FBNeo, Geolith (aunque es BSD), nullbios y el script de la L0 de ngdevkit (LGPL)
    no se copian; solo se leen sus documentos o se corren como programas aparte. FBNeo prohíbe además pedir
    donaciones en proyectos que usen su código: aquí solo se corre, no se usa su código.
11. **Archivos compartidos entre los cuatro repos** (`contract.sh`, `trace_line.h`): hay que copiarlos a
    los otros tres al cambiarlos, y `retrorecomp/tests/contract.py` los compara.

## Decisiones

1. Precisión como en GENRecomp: accesos al bus del 68000 contados, el LSPC perezoso de a línea; no todo
   ciclo a ciclo. Lo que el código generado no hace igual (leer los operandos) se cobra igual, y lo que
   depende del tiempo se cobra en el límite de instrucción.
2. Las interrupciones las atiende siempre el intérprete; el código generado sale apenas hay una pendiente.
3. El Z80 interpretado al principio; recompilar la M1 solo si el perfil lo justifica.
4. La BIOS en el intérprete hasta la fase 5; recompilada aparte, una vez por versión, desde la fase 6.
5. Orden: CPU → bus y BIOS → LSPC → audio → herramientas → recompilador → heurísticas → rendimiento →
   compatibilidad. El audio puede ir en paralelo al LSPC (`difftest` funciona sin él).
6. AES, NTSC y juegos sin cifrar primero; MVS, cifrados y PAL después.

## Fuentes

- NeoGeo Development Wiki (wiki.neogeodev.org): mapa de memoria, registros, LSPC, sprites, L0, paletas,
  temporización, interrupciones, Z80, YM2610, BIOS (llamadas, variables, cabecera, arranque), formatos de
  las ROM.
- MAME `src/mame/snk/neogeo.cpp` (sus comentarios y tablas de ROMs, para cotejar; no se copia código).
- FBNeo `d_neogeo.cpp` (tablas de ROMs y banderas de protección, para cotejar).
- Geolith (`libretro/geolith-libretro`): formato `.neo`, opciones del núcleo y descripciones de los botones.
- ngdevkit (`dciabrin/ngdevkit`, `dciabrin/ngdevkit-examples`): nullbios y ejemplos.
