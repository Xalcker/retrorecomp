# retrorecomp

Recompiladores estáticos de consolas clásicas, en un solo repositorio, con una interfaz local común:

| Carpeta | Sistema |
|---|---|
| [`gb/`](gb/README.md) | GBRecomp: Game Boy, Game Boy Color y Super Game Boy |
| [`nes/`](nes/README.md) | NESRecomp: NES |
| [`gen/`](gen/README.md) | GENRecomp: Sega Genesis |
| [`neo/`](neo/README.md) | NEORecomp: Neo Geo AES |
| [`sms/`](sms/README.md) | SMSRecomp: Master System y Game Gear |

El código común (núcleos del 68000 y del Z80, la ventana, menús, archivos, sonido, pruebas) está en
[`shared/`](shared/), y cada sistema lo toma por la ruta `../shared`. Hasta octubre de 2026 cada sistema era su
propio repositorio (Xalcker/GBRecomp, …, que quedan archivados) y `shared/` les llegaba como submódulo; la historia de los
cinco está aquí, en su carpeta. La interfaz detecta el sistema de cada ROM por su cabecera y llama a las herramientas
que corresponden.

```
python3 retrorecomp_gui.py            # abre el navegador; solo Python 3, sin dependencias
```

En Windows, con [uv](https://docs.astral.sh/uv/), desde PowerShell y dentro de la carpeta `retrorecomp`:

```
Set-Location "C:\ruta\a\retrorecomp"
uv run --python 3 .\retrorecomp_gui.py
```

uv elige un Python 3 y corre el script; como solo usa la biblioteca estándar, no hay que instalar dependencias
ni crear un entorno. El navegador se abre solo. Para detener el servidor, presiona `Ctrl+C`.

Encuentra los sistemas en sus carpetas (`gb/`, `nes/`, …); con `--nes DIR` / `--gb DIR` / `--gen DIR` / `--neo DIR` /
`--sms DIR`, o con `NESRECOMP_DIR` / `GBRECOMP_DIR` / `GENRECOMP_DIR` / `NEORECOMP_DIR` / `SMSRECOMP_DIR`, usa otra copia. Las ROMs salen
de la carpeta `roms/` de cada uno (en SMSRecomp, también de sus subcarpetas, como `roms/sms` y `roms/gg`) y de
las que se pasen con `--roms DIR`. Compila las herramientas desde la
propia interfaz («Compilar herramientas»), o con `make` en cada repositorio.

En Windows, `make` y `sh` (compilar, pruebas, ciclo completo, compilar el proyecto generado) corren dentro de MSYS2
UCRT64, como se compila a mano: la GUI los lanza con su `bash` (`MSYSTEM=UCRT64`) y pasa las rutas como `C:/...`.
Busca MSYS2 en `--msys DIR`, en `MSYS2_ROOT` o en `C:\msys64`, y al arrancar dice cuál usa. Los ejecutables de los
recompiladores y `git` se lanzan directo.

## Qué instalar en Windows

**Para compilar y jugar.** [MSYS2](https://www.msys2.org) en `C:\msys64` (`winget install --id MSYS2.MSYS2 -e`). Abre
la terminal «MSYS2 UCRT64» (no MSYS ni MINGW64), corre `pacman -Syu` (otra vez si la ventana se cierra) y luego:

```
pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf
pacman -S --needed mingw-w64-ucrt-x86_64-python diffutils
```

La primera línea son los paquetes del CI de Windows. La segunda la pide NEORecomp (su BIOS propia y sus pruebas); si
make encuentra un `python3` que no funciona, como el alias de Microsoft Store, el `make` entero de NEORecomp falla.
`make` es el de MSYS (no `mingw32-make`) y `gcc` el de UCRT64. curl, tar, sha256sum y cygpath ya vienen con MSYS2.
SDL2 se enlaza estático: el `.exe` no pide `SDL2.dll`.

Hay dos `git`. El de MSYS2 solo da la versión (`git describe`; sin él sale «dev»). La GUI usa el del PATH de Windows,
[Git for Windows](https://git-scm.com) (`winget install --id Git.Git -e`), para el estado de cada repo y para
«sincronizar»; sin él solo fallan esas dos cosas. Después de un `winget install`, abre otro PowerShell para que se vea el PATH nuevo.

**La GUI.** uv (`winget install --id astral-sh.uv -e`) y el comando de arriba, o cualquier Python 3 de Windows: solo
usa la biblioteca estándar. make no ve ese Python; por eso NEORecomp pide además el de pacman.

**Trampas de Windows.**

- La GUI pasa su entorno a cada trabajo: después de crear una variable, abre otro PowerShell (o reinicia VS Code)
  antes de `uv run`.
- El `bash -lc` de MSYS2 no hereda el PATH de Windows (`MSYS2_PATH_TYPE=minimal`), así que make no ve `java`,
  ni el Python de Windows.
- Rutas sin espacios: los Makefile usan `$(CURDIR)` sin comillas. Clona en algo corto, como `C:\dev\NESRecomp`; con
  el NDK, una ruta larga puede pasar el límite de 250 caracteres de CMake.
- `make testroms` de GENRecomp y NEORecomp baja las SingleStepTests del 68000 y del Z80 (unos 450 MB por repo; las
  del Z80 se recomprimen una por una), que en MSYS2 tardan mucho. `SKIP_SST=1 make testroms` las salta, como el CI de
  Windows.

## Modos

El interruptor de la barra superior elige entre dos modos (se recuerda en el navegador):

- **Experto**: todo lo de abajo, paso por paso. Abre en la pestaña **Ciclo**: el botón «Correr ciclo completo»
  (`tests/recomp_game.sh`) con el avance por pasos (traza guionada, cada ronda y cada semilla con su % recompilado) y
  el resultado final; recompilar, `difftest` y la velocidad sueltos quedan en «Pasos sueltos». Las otras pestañas son
  «Avance y mapa» y «Trazas y entradas». Si eliges unas «Entradas» (p. ej. una partida grabada) el ciclo las usa en
  lugar de las guionadas (`INPUT_TRACE` / `INPUT_DIFFTEST` de `recomp_game.sh`). Encima de las pestañas quedan siempre a la vista las fichas de estado de la
  ROM y sus ajustes comunes (cuadros de pruebas, cuadros de traza y velocidad, entradas y modelo). El registro muestra
  un ✓ o ✗ por cada paso del trabajo y se puede ocultar.

## Qué hace

- **Lista de ROMs:** búsqueda por nombre y filtro por consola; primero las que ya tienen su proyecto compilado
  (`build/out/NOMBRE`) y luego las demás.
- **Avance por ROM:** análisis estático, cobertura, trazas, proyecto generado, compilado, `difftest` y velocidad.
- **Mapa del descubrimiento** (como `--viz`): una fila por banco de 16 KB, celdas de 256 bytes coloreadas por cómo se
  halló el código; en rojo lo que el juego ejecutó y no se halló. Clic en una celda lista sus instrucciones y clic en
  una instrucción muestra la cadena de «por qué» (`--why`).
- **Trazas:** grabar sin ventana (`nestrace` / `gbtrace` / `gentrace` / `neotrace` / `smstrace`) con un guion de entradas o una semilla, jugar grabando, con
  `difftest --uncovered`, unir y borrar; las marcadas se suman al análisis. Captura PNG y audio WAV.
- **Entradas:** editor de guiones `--input` / `--play` / `--record` (`cuadro BOTONES`), con plantillas y las del
  repositorio (`tests/inputs`).
- **Recompilar y probar:** recompilar y compilar el proyecto generado, `difftest`, `bench` del intérprete y del
  recompilado, y el ciclo completo (`tests/recomp_game.sh`).
- **Repos:** sincronizar (`git fetch` y fast-forward) y compilar cada uno, y la salida de cada trabajo en vivo.

Escucha solo en `127.0.0.1` con un token en la URL, valida que las ROMs estén en las carpetas configuradas y arma
cada comando como una lista de argumentos (sin shell).

**El registro de sistemas (`shared/sistemas.json`).** Una entrada por consola con lo que antes se repetía en varios
lugares: nombre, sigla, carpeta del recompilador, extensiones, botones del guion de entradas, jugadores, modelos
(`--model`), si usa BIOS, cuadros por omisión del ciclo completo (`cuadros`, `cuadros_traza`), herramientas, la
leyenda y la nota del mapa, la nota de entradas y las plantillas de guiones. Lo leen la GUI (y su página, por
`/api/state`), `tests/contract.py`, `tests/compat_test.py` y `shared/recomp_game.sh`. El id de la entrada es la
carpeta (`gb/`, `nes/`...) y el prefijo de las herramientas (`gbrecomp`, `nesshot`...). `tests/contract.py` comprueba
que cada entrada tenga los mismos campos, que sus botones sean los que acepta el guion de entradas de su recompilador
(`INPUT_NAMES`) y que cada clave numérica vaya en su propia línea, porque `recomp_game.sh` la lee con awk. Un sistema
nuevo es una entrada más, más su clase en `retrorecomp_gui.py` con lo que solo él sabe hacer: leer la cabecera de la
ROM, el mapa y cómo graba trazas.

## El contrato

La interfaz no conoce el interior de los recompiladores, solo esta línea de comandos común:

| Herramienta (NES / GB / Genesis / Neo Geo / Master System) | Uso |
|---|---|
| `nestrace` / `gbtrace` / `gentrace` / `neotrace` / `smstrace` | `rom traza [--frames N] [--input archivo] [--seed S]` |
| `nesshot` / `gbshot` / `genshot` / `neoshot` / `smsshot` | `rom salida.png [--frames N] [--input archivo] [--seed S]`; si el CPU se detiene (opcode KIL / ilegal, doble falta) escribe `CPU detenido en el cuadro N (motivo)` |
| `nesaudio` / `gbaudio` / `genaudio` / `neoaudio` / `smsaudio` | `rom salida.wav [--frames N] [--input archivo] [--seed S]` |
| `nesbench` / `gbbench` / `genbench` / `neobench` / `smsbench` | `rom [--frames N] [--repeat N] [--input archivo] [--seed S]` (sin entradas: Start y luego la derecha). `genbench` y `neobench` dicen además cuánto del Z80 corrió nativo, interpretado o saltado (línea `Z80: ...`) y, con `--perfil`, en qué se fue el tiempo (Z80, sonido, video y lo demás) |
| `nesramdump` / `gbramdump` / `genramdump` / `neoramdump` / `smsramdump` | `rom [--frames N] [--input archivo] [--seed S]` (RAM de trabajo a la salida estándar) |
| `difftest` (cada proyecto) | `rom [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--screens carpeta [--every N]] [--bench] [--executed archivo] [--estricto]` (`--estricto` falla en la primera instrucción de la ROM que corre el intérprete porque no está recompilada, `ESTRICTO: código que falta en $PC ...`, en GB al final de ese cuadro; el código en RAM sí pasa; al final, `Intérprete por motivo: ...`. `tests/estricto.sh` de cada sistema lo prueba con las ROMs de prueba: trazadas y recompiladas con esa traza no les falta nada) (sin `--input` ni `--seed` no pulsa nada; el nombre es el mismo en todos porque va dentro del proyecto generado, que ya es de una consola). Sin diferencias termina con `difftest: N cuadros iguales (L llamadas, R a código recompilado)` (en GEN y NEO, antes, la línea `Z80: N ciclos T: X% nativo, Y% interpretado, Z% saltado (...)`) y la línea común de la métrica (`shared/metrica.h`, también en el `bench` del proyecto): `Métrica: X% de las instrucciones nativas (N de M); ciclos: A% nativo, B% intérprete en ROM, C% intérprete en RAM, D% entrada a interrupciones, E% espera (HALT/STOP), F% otros`, del CPU principal (el 68000 en GEN y NEO), y sale con 0; con diferencias imprime líneas `DIFERENCIA ...` y sale con 1 |
| `bench` (cada proyecto) | las opciones de `nesbench` / `gbbench` / `genbench` / `neobench` / `smsbench`, con el código recompilado |
| `nesrecomp` / `gbrecomp` / `genrecomp` / `neorecomp` / `smsrecomp` | `rom -o carpeta --name nombre [--trace archivo]...`, más `--viz` (`--color`), `--events`, `--why`, `--coverage`, `--html archivo`, `--codemap archivo` (sin `-o`, sin proyecto) |

**Salida para máquinas: la línea `RESUMEN {json}`.** `difftest`, `bench` (el de la consola y el del proyecto),
`*recomp --viz` y `recomp_game.sh` terminan con una línea `RESUMEN ` seguida de un objeto JSON en una sola línea,
con lo mismo que dicen las líneas para leer. La GUI lee solo esa línea, no el texto en español:

- `difftest`: `{"herramienta":"difftest","ok":true,"cuadros":N,"llamadas":L,"recompilado":R,"interpretadas":K,"metrica":{...}}`
  (`interpretadas` solo con `--uncovered`). Si falla: `{"herramienta":"difftest","ok":false,"cuadro":N,"motivo":"diferencia"|"estricto"}`.
- `bench`: `{"herramienta":"bench","ok":true,"cuadros":N,"segundos":S,"x_tiempo_real":X}`, más `hash_imagen` y
  `hash_audio` con `--hash`, y `metrica` en el del proyecto.
- `metrica` (`shared/metrica.h`): `{"nativas_pct","nativas","instrucciones","ciclos":{"nativo","rom","ram","irq","espera","otros"},"motivos":{"falta","ram","forzado","detenido"}}`.
- `--viz` (NES, Genesis, Neo Geo y Master System): `{"herramienta":"viz","tam":BYTES,"instrucciones","bytes","pct","celdas_codigo","celdas"}`,
  más `"cobertura":{"halladas","total"}` con `--coverage` y `mapper` en NES.
- `recomp_game.sh` deja una línea por evento, todas con `"herramienta":"recomp_game"`:
  - una al empezar cada ronda: `{"evento":"ronda","ronda":N}`, y en la segunda también `"direcciones":D`;
  - una por semilla: `{"evento":"semilla","ronda":N,"semilla":"S","difftest":{el RESUMEN de difftest}}`;
  - la última: `{"evento":"fin","ok":...}`.

Cada recompilador trae `sh tests/contract.sh`, la misma comprobación sobre sus propias herramientas, y su CI la corre
en cada PR. (Lo hacen por separado porque, con repos privados, un CI no puede leer el otro repositorio sin un
token.) Cuando la tabla de arriba cambie, actualiza `shared/contract.sh` y `tests/contract.py` de este repo.

**Código compartido (`shared/`).** Lo que antes se copiaba idéntico en los recompiladores vive aquí y cada uno lo toma por la ruta `../shared`: `random_pad.h` (el mando aleatorio de `--seed`), `trace_line.h` (el formato `BANCO:DIRECCION`), `sha1.c` y `sha1.h`, la interfaz de la ventana (`ui_menu`, `ui_font`), `png`, `files.h` y `files.c` (rutas en UTF-8 y el diálogo para elegir la ROM, con el filtro de cada consola como argumento de `ask_rom_path`), `export` (captura PNG y grabadora WAV) y `wav.h` (la cabecera WAV de las herramientas `*audio`, `*run` y la exportación), `console.h` (UTF-8 en la consola de Windows), `embed.c` (mete el runtime y la plataforma en el recompilador), **los núcleos del 68000 y del Z80 de GENRecomp y NEORecomp** (`m68k.h`, `m68k_ops.h`, `cpu68k.c`, `dis68k.c/h`, `z80.h`, `z80_ops.h`, `z80.c`, `disz80.c/h` (el desensamblador del Z80, como `dis68k`: `tests/disz80_test.c` comprueba en el CI de este repo que dé el largo de `z80_decode` en todas las instrucciones), con sus pruebas `m68ktest.c`, `z80test.c`, `json.h`, `sst68k.sh` y `sstz80.sh`: cada consola pone un `m68k_host.h` y un `z80_host.h` con su tipo (`Gen`, `Neo`, `Sms`), su cabecera y cómo anota la traza; el resto es igual, así que un arreglo, como el del HALT del Z80 de GENRecomp#15, llega a todos. Como `m68k_ops.h` para el 68000, `z80_ops.h` tiene la semántica de cada instrucción del Z80 para el intérprete y para el código generado de SMSRecomp, donde el Z80 es el CPU principal; `z80test --recomp` y `SST_OPTS=--recomp sh tests/sstz80.sh` corren las SingleStepTests por ese camino, y en los dos caminos comprueban también el decodificador `z80_decode` y, por el gancho opcional `Z80_IO_AT`, en qué ciclo de la instrucción se accede a cada puerto), `z80_recomp.h` (el recompilador del Z80, común a NEORecomp, GENRecomp y SMSRecomp: descubre el código de un tramo fijo desde sus semillas con `z80_decode` (y guarda desde qué instrucción se llegó a cada una, para `--why`) y lo emite en C, una etiqueta por instrucción que llama a la rama de `z80_exec` de su primer byte, con despacho por tabla directa; cada consola pone en su `z80_recomp_host.h` lo que hace su bucle antes y después de cada `z80_step`. Hoy lo usan NEORecomp, para los primeros 32 KB de la M1, y GENRecomp, para el driver que el 68000 sube a la RAM del Z80, aprendido con un sondeo en el intérprete y compilado con una guarda que compara los bytes de cada instrucción con la RAM; y SMSRecomp, donde el Z80 es el CPU principal, para todo el juego por vistas —una página del mapper en una ranura— y el código que el juego copia a la RAM, también con guarda; Xalcker/retrorecomp#59), `visor_html.h` (la página de `--html` de genrecomp, neorecomp y smsrecomp, la misma de nesrecomp; cada uno define `VISOR_NAME` antes de incluirlo), la estadística del Z80 en `z80.h` (`Z80_STAT`: ciclos nativos, interpretados y saltados, que `bench` y `difftest` imprimen en la línea `Z80: ...`) y `prof.h` (`--perfil` de `genbench`, `neobench` y su `bench`: un hilo que muestrea la zona que el runtime marca), `ui_golden.sh` (capturas de la interfaz contra una referencia), `player_util.h`, `player_common.h` y `player_rom.h` (funciones de la ventana iguales en los cinco; `player_rom.h` no lo usa GBRecomp), `player_audio.h` (la salida de sonido: una cola de SDL con pre-búfer, rampas al pausar y volver, y control de ritmo por remuestreo; `tests/player_audio_test.c` la prueba con una tarjeta simulada y corre en el CI de este repo), `player_touch.h` (el mando en pantalla de las pantallas táctiles: la cruz y los botones dibujados con el renderer, multitáctil, con una tabla de botones en cada consola; `tests/player_touch_test.c` lo prueba con toques simulados y corre en el CI de este repo),  `ram_code.h` (el código que el juego copia a RAM y ejecuta desde ahí: el intérprete lo anota en líneas `ram DIRECCION B0 B1 ...` del mismo archivo de traza, que `trace_line_read` ignora, y el recompilador lo une y lo arma en bloques que compila con una guarda; cada consola pone cuánto mide una instrucción y qué bytes la identifican. Lo usan GENRecomp, NESRecomp (la RAM interna y la del cartucho, que lee según el mapper) y NEORecomp; GBRecomp tiene el suyo, del que salió, y `tests/ram_code_test.c` lo prueba en el CI de este repo), `settings.c` (las preferencias de la ventana, `settings.ini`: cada consola pone en su `settings.h` la estructura y la lista de sus campos, `SETTINGS_FIELDS`, con su valor por omisión y su rango), `inputs.h` y `cli.h` (las entradas grabadas `cuadro BOTONES` y las opciones comunes de las herramientas sin ventana, de NESRecomp, GENRecomp, NEORecomp y SMSRecomp: cada uno pone un `input_host.h` con el tipo y los nombres de sus botones, cómo cae en ellos el mando aleatorio y, en NES, las opciones propias de `nesshot`; GBRecomp tiene los suyos, de un jugador y con el modelo en el encabezado), `audio.c` y `audio.h` (la mezcla a 44 100 Hz de GENRecomp, NEORecomp y SMSRecomp: cada consola pone un `audio_host.h` con su reloj maestro y cómo lleva sus chips al final del cuadro), `psg.c` y `psg.h` (el SN76489 de Sega, de GENRecomp y SMSRecomp, con el estéreo del Game Gear: cada consola pone un `psg_host.h` con su tipo, el campo, qué canales están silenciados, el estéreo —`$FF` en el Genesis— y el nivel), `retro_run.c` y `libretro_min.h` (el oráculo libretro de GENRecomp, NEORecomp y SMSRecomp: cada consola pone en `tools/oracles/oracle_host.h` las descripciones de botón de sus núcleos y qué RAM vuelca —`ORACLE_RAM_SIZE`, 64 KB si no se dice; 8 KB en el Master System— y, si hace falta, cómo traducir sus botones a los del núcleo, `ORACLE_PAD`; `oracle.py`, que baja los núcleos, sigue en cada repo), `tests/comun_test.c` prueba `settings.c`, `inputs.h`, `cli.h` y `audio.c` con consolas de mentira y corre en el CI de este repo, `contract.sh` y `recomp_game.sh`. Lo que sigue en cada repo es lo que depende de la consola: el puente de la ventana con el runtime (`main_sdl.c`, lo común ya está en `player_*.h`), los textos de `i18n`, `difftest.c` y `bench.c` (qué se compara), los estados guardados (cada formato valida su propia cabecera; unificarlos invalidaría las partidas guardadas) y `golden.sh`. Los repos compilan con `-Itools/retrorecomp/shared` y embeben esos headers en los proyectos que generan; `tests/contract.sh` y `tests/recomp_game.sh` son envoltorios idénticos que entran a la raíz del repo y llaman a `shared/`. Por eso compilar un recompilador exige el submódulo (`git clone --recursive`, o `git submodule update --init`); el ZIP de GitHub no lo trae. `git pull` mueve el commit al que apunta el submódulo pero no lo que hay en la carpeta (`git status` muestra ` M tools/retrorecomp`): después de un `pull` que lo mueva hay que repetir `git submodule update --init`, o configurar una vez `git config --global submodule.recurse true`. Si no, `make` falla con «No rule to make target `tools/retrorecomp/shared/...`». `tests/contract.py` comprueba que los envoltorios sean idénticos y que nadie conserve una copia vieja de los headers.

Los tres se compilan igual: mismos paquetes (MSYS2 UCRT64 en Windows), el mismo bloque común al principio del
`Makefile` y los mismos objetivos, `make`, `make testroms` (ROMs de prueba libres), `make test` y `make dist`. El
`.github/workflows/ci.yml` también es el mismo en los tres y corre esos pasos en Linux, macOS y Windows. Lo que cambia
entre consolas queda dentro de esos objetivos.

El mando aleatorio de `--seed` es el mismo en los tres: `src/platform/random_pad.h` es idéntico en todos los repositorios
(`tests/contract.py` lo comprueba), así que la misma semilla da la misma secuencia de botones.

Las trazas también: una línea `BANCO:DIRECCION` por instrucción ejecutada, escrita y leída por `src/runtime/trace_line.h`
(idéntico en los tres). El banco es el desplazamiento en la ROM entre una ventana fija de cada consola (tabla de abajo) y la
dirección es la del CPU. NES y Genesis siguen leyendo sus trazas viejas `DESPLAZAMIENTO:DIRECCION`, y `--why` acepta las
dos formas.

Una traza puede llevar además líneas de otra forma, que no empiezan con un número hexadecimal y dos puntos. Hoy son las
`ram DIRECCION BYTES` de GBRecomp (con `??` en los operandos que cambian), el código que el juego ejecutó desde WRAM o HRAM
y que `gbrecomp --trace` compila (ver «Código que el juego copia a WRAM o HRAM» en su `docs/ARQUITECTURA.md`, que también
explica cómo llevarlo a otra consola). Los lectores de `BANCO:DIRECCION` las ignoran, así que el contrato no cambia, pero
quien una, filtre o cuente trazas debe tratarlas por líneas enteras, no por palabras: `compat.py` las partía por espacios
y las deshacía.

`python3 tests/contract.py` comprueba (sin ROMs) que las herramientas compiladas de los cinco mencionan esas opciones y que el código compartido (`SHARED`) sea idéntico,
mirando todos los repositorios que encuentre. También que la prueba de compatibilidad sea gemela: `tools/compat/report.py`
igual en los cinco salvo el nombre del `Sistema`, ningún `run.py` que fije lo que es común (cuadros, métrica) y una sola
carpeta `docs/compat`, sin rutas locales en su `results.json` (lo de los datos publicados sale como «pendiente»: se arregla
al volver a correr la prueba). Si cambias una opción en un recompilador, cámbiala también en los otros y aquí.

## Prueba de compatibilidad

`compat.py` es el motor común de la prueba de compatibilidad de los cinco (solo la biblioteca estándar). Cada
recompilador lo llama desde su `tools/compat/run.py`, que define solo lo propio de su consola (una subclase de
`compat.Sistema`: leer la cabecera, la categoría de cada ROM —mapper, MBC o tipo de cartucho—, qué se soporta, los
botones), y desde `tools/compat/report.py`, que arma el informe HTML. Usa solo la línea de comandos de la tabla:

- **Etapa 1, todas las ROMs:** `*shot` con un guion de entradas, capturas a los cuadros 450, 900 y 1500.
- **Etapa 2, una selección:** `*recomp` sin traza, `make difftest`, `difftest` con las semillas 1 y 2, una segunda
  ronda con lo que `--uncovered` vio caer al intérprete y, al final y de una ROM a la vez, `difftest --bench`.

```
python3 tools/compat/run.py [CARPETA ...] [--solo etapa1|etapa2] [--etapa2 N] [--lista ARCHIVO] [--juego TEXTO]
                            [--categoria A,B] [--hilos N] [--salida DIR] [--rehacer] [--oraculos]
python3 tools/compat/report.py [--salida DIR]
```

Los resultados (`docs/compat/results.json` y las miniaturas) se guardan después de cada ROM de la etapa 2 (y cada 25
de la etapa 1); la misma orden sigue donde iba y salta las ROMs que ya tienen resultado del **mismo código** del
recompilador: un hash del contenido de `src/` y del `Makefile` (commiteado o no), no el commit. Así un commit que solo
toca `docs/` o `tests/`, o una fusión, no obliga a repetir una corrida a medias. Este motor y el guion no cuentan: si
cambian de forma que altere los resultados, `--rehacer` repite todo.

Lo mismo en los cinco, en el motor y no en cada `run.py`: las semillas (1 y 2), los cuadros de `difftest` por
semilla (1800), los de `--bench` (1800), los estados, las columnas y las tablas del informe, y la carpeta de salida,
`docs/compat` (un solo informe por repo: si hay más de una colección, van en el mismo `results.json` y las distingue el
campo `coleccion`). `results.json` se publica, así que guarda solo nombres de archivo: el motor quita las carpetas de la
corrida de los mensajes que guarda y el informe avisa si encuentra una ruta local.

**Lo que define la subclase de `compat.Sistema`** en `tools/compat/run.py` (un recompilador nuevo solo necesita esto):

| | Qué es | Ejemplos |
|---|---|---|
| `id`, `nombre`, `consola` | prefijo de las herramientas (`{id}shot`, `{id}recomp`), el nombre del repo y el de la consola | `"gb"`, `"GBRecomp"`, `"Game Boy"` |
| `exts` | extensiones de ROM, sueltas o dentro de los zips | `(".gb", ".gbc")` |
| `leer(rom)` | datos de la cabecera; `{"error": ...}` si no sirve | mapper, MBC, tamaños, batería |
| `soportado(r)` | si el recompilador soporta esa ROM | mapper o MBC conocido; NEO: no cifrado |
| `categoria(r)`, `categoria_nombre`, `orden_categoria(c)`, `nombre_categoria(c)` | la tabla «Por …» del informe y el filtro | `"Mapper"`, `4` → `"4 MMC3"` |
| `cupo`, `cupo_def` | cuántas ROMs de cada categoría pasan a la etapa 2 | `{"MBC5": 11}` |
| `botones` | los dos botones de acción del guion común | `("A", "B")`; GEN `("B", "C")` |
| `compara` | qué compara `difftest`, para el texto del informe | `"CPU, ciclos, RAM…"` |
| `escala_mini` | la miniatura es la captura dividida entre esto | 1 en GB, 2 en los demás |
| `combinacion(r)`, `tamano(r)`, `tamano_titulo()`, `caracteristicas(r)`, `subtitulo(r)`, `otros_resumenes()` | columnas y variedad de la selección | `"PRG/CHR KB"`, «batería» |
| opcional: `coleccion(carpeta)` | el nombre de la colección (por omisión, la carpeta) | GB: `"GB"`/`"GBC"`; NEO: la BIOS |
| opcional: `es_juego(p)`, `juego_zip`, `cuadros1`, `guiones()` | lo que el Neo Geo cambia por su BIOS | NEO salta `aes.zip` y captura más tarde |
| opcional: `oraculos(f, guion)`, `nombres_oraculos`, `columnas_oraculos(r)` | emuladores de referencia (`--oraculos`): una celda por nombre | GEN: Genesis Plus GX y BlastEm; NEO: Geolith y FBNeo; SMS: Genesis Plus GX y Gearsystem |
| opcional: `migrar(r)` | pasar filas de un `results.json` anterior al motor común | hasta regenerar los de NES y GB |

`python3 tests/compat_test.py` (en el CI) arma un `results.json` sintético para cada uno de los cinco sistemas y
genera su informe: comprueba las cabeceras de los oráculos, los estados y que no queden rutas locales. Corre antes de
publicar el primer informe de un recompilador nuevo, con su `Sistema` agregado a la prueba.

## Diferencias que la interfaz esconde

| | NES | Game Boy | Genesis | Neo Geo | Master System / Game Gear |
|---|---|---|---|---|---|
| El juego | `.nes` | `.gb`, `.gbc`, `.sgb` | `.md`, `.gen`, `.bin`, `.smd` | zip de MAME (entero: muchos chips) o `.neo`; necesita la BIOS del AES | `.sms`, `.gg` (TMR SEGA en `$7FF0`, `$3FF0` o `$1FF0`; los del GG no siempre la traen) |
| Filas del mapa | 16 KB de PRG (`--viz --color`) | un banco (`--events`) | 16 KB de ROM (`--viz --color`) | 64 celdas del programa P (`--viz --color`): de 256 bytes hasta 1 MB, más grandes después | una página de 16 KB (`--viz --color`) |
| Banco de la traza (`BB:AAAA`) | 8 KB del PRG | 16 KB (el banco del MBC) | 512 KB de la ROM (las ventanas de SSF2) | 1 MB de P (P1 y cada banco de P2); `FF` la BIOS | 16 KB (la página del mapper), con la dirección del Z80 |
| Jugar y grabar traza | `nes` (intérprete) con `NES_TRACE` | `gb` (intérprete) o el recompilado, con `--uncovered` | `gen` (intérprete) con `--trace` | `neo` (intérprete) con `--trace` | `sms` (intérprete) con `--trace` |
| Visor | `--html`: página sola con el mapa del PRG | `--html`: `tools/visor.html` con los eventos adentro | `--html`: página sola con el mapa de la ROM | `--html`: página sola con el mapa de P | `--html`: página sola con el mapa de la ROM |
| Extras | segunda columna (jugador 2), Zapper en `nesshot` | `--model dmg/cgb/sgb` | segunda columna (jugador 2), botones C X Y Z MODE (sin SELECT) | segunda columna (jugador 2), botones C y D; la BIOS tarda unos 20 s en dejar el juego en su demo | `--model sms/gg`, segunda columna (jugador 2), botones A B (1 y 2) START (el del GG; en el SMS, el botón 1) y PAUSE |

## Licencia

retrorecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp, GENRecomp, NEORecomp y SMSRecomp.
