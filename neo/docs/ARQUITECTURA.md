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
| `src/runtime/` | La consola: `neo.h` (estado e interfaz), `cart.c` (formatos, imagen normalizada, cabecera, vectores), `bios.c` (BIOS del AES y tabla L0), `zip.c` (zip, inflate y CRC32), `neodb.c`/`neodb.h` (base de datos de juegos); `sha1`, `console.h`, `trace_line.h` (idéntico en los cuatro recompiladores). |
| `src/neorecomp/` | El recompilador. Por ahora, `--info` y `--neo`. |
| `src/platform/` | Lo que no depende de SDL: `png`, `files`, `export`, `random_pad.h` (idéntico en los cuatro). La ventana llega en la fase 3. |
| `src/tools/` | `carttest` (con `inflate_vectors.h`), `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `contract.sh` (contrato común, idéntico en los cuatro), `testroms.sh`. |
| `tools/` | `retrorecomp/` (la GUI común, submódulo). |

## Una sola fuente de verdad para el CPU

Llega en la fase 1: `m68k_ops.h` y `cpu68k.c` de GENRecomp sin cambios de semántica, validados con las
SingleStepTests del 68000 por el intérprete y por el camino del código generado, y el Z80 con las suyas.

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
el primer MB es `$000000-$0FFFFF` y el MB n + 1 es el banco n de la ventana `$200000-$2FFFFF`. Las de la
BIOS, por su desplazamiento dentro de la BIOS, en un espacio aparte. En las trazas (`trace_line.h`, el
formato común `BANCO:DIRECCION`) el banco es el desplazamiento dividido entre 1 MB, y la BIOS va en el banco
`FF`: `FF:C00402` es la instrucción de `$00402` de la BIOS.

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
- **`make test`:** `carttest` (un juego sintético con el P1 de 2 MB de mitades cambiadas da la misma imagen,
  SHA1, cabecera y vectores desde los chips, como `.neo`, como zip guardado dentro de una carpeta y como
  carpeta; el inflate con códigos fijos y dinámicos, cortado y con salida chica; un zip dañado; los sufijos
  con P2 y buses de V separados; los errores; un juego cifrado y uno sin `NEO-GEO`; la tabla L0 y su SHA1;
  una BIOS desconocida o de otro tamaño). Con los juegos de `roms/`: `neorecomp --info` y que `--neo` dé el
  mismo SHA1; con Metal Slug, la base de datos, el SHA1, el P1 y las entradas; con `aes.zip`, que se acepte
  `neo-epo.bin` y que su `000-lo.lo` sea la tabla calculada; `neogeo.zip` (MVS) no se acepta.

## Estado de la validación

Fase 0: la carga de juegos (`.neo`, zip, carpeta) y de la BIOS, con `make test`. Probado con Metal Slug
(`mslug.zip` de MAME) y las BIOS del `aes.zip` y del `neogeo.zip` de MAME.

### Línea base para optimizar el runtime

Llega en la fase 8 (`tests/golden.sh`).
