
Hay que clonar con el submódulo de retrorecomp, que trae el código que comparten los cuatro recompiladores (`random_pad.h`, `trace_line.h`): `git clone --recursive`, o `git submodule update --init` en un clon ya hecho. El ZIP de GitHub no lo incluye.
# NESRecomp

Recompilador estático de juegos de NES, basado en la arquitectura de GBRecomp: toma una ROM
(`.nes`) y genera un proyecto C99 portable que se compila en un ejecutable nativo con SDL2. Híbrido
(código recompilado más el intérprete 6502 de respaldo), sin datos de la ROM en el proyecto
generado (valida su SHA1) y con `difftest` para comprobar el código recompilado contra el intérprete.

## Un caso completo: recompilar Super Mario Bros.

Se necesita un compilador C99, `make`, `pkg-config` y SDL2; `git` y `unzip` se usan para la versión y para
`make testroms`. Son los mismos paquetes que en GBRecomp, GENRecomp y NEORecomp. La ROM la pones tú en `roms/` (no se versiona ni se distribuye).

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

<details>
<summary><b>Windows 11 (MSYS2)</b></summary>

Con [MSYS2](https://www.msys2.org) instalado en `C:\msys64`, abre la terminal **MSYS2 UCRT64** (menú Inicio; no la
"MSYS2 MSYS" ni PowerShell) e instala los paquetes de la tabla una sola vez. Después se usan los mismos comandos de
abajo; los ejecutables salen con `.exe` y llevan el icono del control de NES. En esa terminal tus carpetas de Windows
están bajo `/c/Users/<tú>/...`:

```
cd /c/Users/<tú>/Documents/NESRecomp
make -j8
./nes.exe "roms/Super Mario Bros. (Japan, USA).nes"
./nesrecomp.exe "roms/Super Mario Bros. (Japan, USA).nes" -o juegos/smb --name smb
cd juegos/smb && make -j8 && ./smb.exe "../../roms/Super Mario Bros. (Japan, USA).nes"
```

En Windows SDL2 se enlaza de forma estática: el `.exe` no pide `SDL2.dll` ni otras DLL de MSYS2 y se puede copiar a otra carpeta o equipo.
(`make dist` en el proyecto generado lo copia a `dist/`). La carpeta de datos (partida, estados, preferencias)
es `%APPDATA%\NESRecomp\<juego>\`.

</details>

**1. Compilar el recompilador y sus herramientas**

```
make            # nesrecomp, nes (intérprete con ventana), las herramientas sin ventana y el runtime
make testroms   # una sola vez: baja las ROMs de prueba libres (nes-test-roms) a testroms/
make test       # opcional: nestest, Blargg, mappers, recompilado contra intérprete...
make dist       # dist/nesrecomp-<versión>-<sistema>.tar.gz (sistema: linux, macos o windows)
```

Son los mismos objetivos que en GBRecomp, GENRecomp y NEORecomp, y el CI (`.github/workflows/ci.yml`, el mismo flujo en los cuatro) corre exactamente
eso en Linux, macOS y Windows. AccuracyCoin es opcional (`tests/accuracycoin.py`; usa uv):

```
git clone --depth 1 https://github.com/100thCoin/AccuracyCoin testroms/accuracycoin
```

Para comparar contra una referencia, `tests/accuracycoin.py --compara` corre también AccuracyCoin en TriCNES (el
emulador del autor de AccuracyCoin, que pasa todas las pruebas) y lista las pruebas en que no coincidimos; con
`--mesen` compara contra Mesen2, y `--tricnes` o `--mesen` sin `--compara` dan solo el marcador del oráculo. Los
oráculos (`tools/oracles/`) se descargan la primera vez a `build/oracles/` y no van en el repo: Mesen2 2.1.1 (GPL-3,
comprobado con su SHA-256) y TriCNES en un commit fijo (MIT), compilado sin su ventana; si no hay SDK de .NET, se baja
uno portátil a `build/oracles/dotnet` sin tocar el sistema.

**Versiones.** La versión es la etiqueta de git más cercana (`git describe`: `v0.1.0`, o `v0.1.0-3-gabc1234` con
commits después de ella). Al subir una etiqueta `vX.Y.Z` de `main` (`git tag v0.2.0 && git push origin v0.2.0`), el CI
publica un release en GitHub con los paquetes de Linux, macOS y Windows. Igual que en los otros recompiladores.

**2. Probar la ROM con el intérprete** (ventana SDL2; teclas en Ayuda → Controles)

```
./nes "roms/Super Mario Bros. (Japan, USA).nes"
```

**3. Recompilar.** Con solo el análisis estático (vectores, bancos, tablas de saltos) casi todos los juegos ya salen
completos:

```
./nesrecomp "roms/Super Mario Bros. (Japan, USA).nes" -o juegos/smb --name smb
```
```
roms/Super Mario Bros. (Japan, USA).nes: mapper 0, PRG 32 KB, CHR 8 KB
código: 10754 instrucciones (22921 bytes, 69.95% del PRG)
26 funciones
proyecto escrito en juegos/smb/ (ejecutable: smb)
```

**4. Compilar el proyecto generado y jugarlo**

```
cd juegos/smb && make -j8
./smb "../../roms/Super Mario Bros. (Japan, USA).nes"    # valida el SHA1 de la ROM; sin argumentos busca un .nes con ese SHA1 en la carpeta
```

El proyecto no contiene la ROM. El ejecutable la pide una vez y guarda una copia en su carpeta de datos
(`~/Library/Application Support/NESRecomp/smb/` en macOS), junto con la partida, los estados, las preferencias y lo exportado.

**5. Comprobar que el código recompilado hace lo mismo que el intérprete.** `difftest` los corre lado a lado y compara
CPU, RAM, PPU, APU, mapper, y por cuadro la imagen y el audio:

```
./difftest "../../roms/Super Mario Bros. (Japan, USA).nes" 600 start:120-125 right:300-600
```
```
difftest: 600 cuadros iguales (106686 llamadas, 106686 a código recompilado, 100.0% de los ciclos)
```

Los argumentos `boton:desde-hasta` son los cuadros en que se mantiene cada botón (`a b select start up down left right`).
Usa entradas **distintas** de las de la traza (ver abajo) para ejercitar caminos nuevos. Más opciones (las mismas que el
`difftest` de GBRecomp): `--seed S` suma un mando aleatorio (reproducible con la semilla); `--uncovered archivo` escribe, en
formato de traza, las instrucciones de PRG que tuvo que interpretar (sirve como `--trace` de `nesrecomp`); `--screens carpeta`
guarda una captura PNG cada `--every N` cuadros (600 si no se dice); `--executed archivo` escribe qué bytes de PRG se ejecutaron
(una letra por byte, `C` = ejecutado); y `--bench` mide la velocidad del código recompilado y la del intérprete, sin comparar.

**6. Velocidad** (sin ventana ni límite de cuadros):

```
./bench "../../roms/Super Mario Bros. (Japan, USA).nes" 3000
```

**7. Si el análisis estático no alcanza** (el juego toca código que no encontró y corre más lento en el intérprete), se agrega una
traza de lo que el juego ejecuta. Se graba sin ventana con las mismas entradas programadas, o jugando:

```
./nestrace "roms/Super Mario Bros. (Japan, USA).nes" smb.trace 3000 start:120-125 right:300-3000 a:700-720 a:1100-1120
./nesrecomp "roms/Super Mario Bros. (Japan, USA).nes" -o juegos/smb --name smb --trace smb.trace
# o jugando en la ventana:  NES_TRACE=smb.trace ./nes juego.nes   (se puede repetir --trace con varias trazas)
```

`sh tests/recomp_game.sh rom nombre [cuadros] [semillas...]` hace los pasos 3, 4 y 5 de una vez, con los mismos argumentos que en GBRecomp, GENRecomp y NEORecomp (`cuadros`: los de cada `difftest`, 1500; `TRACE_FRAMES=N`: los de la traza guionada, 2400; `NOTRACE=1`: solo con el análisis estático). Corre `difftest` con varias semillas (`--seed`, por omisión 1 2 3), junta las instrucciones que cayeron al intérprete (`--uncovered`), recompila con esa traza y vuelve a probar. Es el mismo script en NESRecomp, GBRecomp, GENRecomp y NEORecomp (la consola sale del recompilador que hay en la carpeta, y `retrorecomp/tests/contract.py` comprueba que sigan idénticos): graba antes una traza guionada con `tests/inputs/ciclo_traza.txt` y corre cada `difftest` con `tests/inputs/ciclo_difftest.txt` más cada semilla; `TRACE_FRAMES=N` y `NOTRACE=1` valen en los cuatro, e `INPUT_TRACE=F` / `INPUT_DIFFTEST=F` cambian las entradas por una partida grabada.

**En el navegador** (WebAssembly): el mismo proyecto se compila con `make web`, con Emscripten si está instalado o
si no con Docker (la imagen `emscripten/emsdk` de versión fija que dice `platform/web.mk`):

```
make -C juegos/smb web
python3 -m http.server -d juegos/smb/web      # y abrir http://localhost:8000/smb.html
```

La página pide la ROM (no va en el build), comprueba el SHA1 como el ejecutable y la guarda en el navegador junto
con la partida, los estados y las preferencias (IndexedDB), así que la siguiente vez basta con «Jugar». La página
es la misma en los cuatro recompiladores (`retrorecomp/shared/web`); `make webtest` compila la de nestest.

**En Android** (APK): `make -C juegos/smb apk` deja `juegos/smb/apk/<nombre>-debug.apk`, que se instala con `adb
install -r`. Usa el SDK de Android si lo hay (`ANDROID_HOME` o `ANDROID_SDK_ROOT`, y un JDK 17 o más nuevo en
`JAVA_HOME`; el JBR de Android Studio sirve) y si no, Docker con la imagen de versiones fijas de
`platform/android/Dockerfile`, que se construye la primera vez (unos 3 GB). En el teléfono el juego arranca
horizontal y en pantalla completa, con un mando en pantalla (o un mando Bluetooth); Atrás o el botón de las tres
rayas abren el menú. La primera vez pide la ROM con el selector de archivos del teléfono y la guarda; las partidas y
las preferencias se guardan también al dejar la app en segundo plano. `make apk ROM=smb.nes` mete la ROM en la APK
(solo para uso personal: esa APK no se comparte) y `APK_RELEASE=1` firma una de release con tu keystore (ver
`platform/android/android.mk`). Es la misma plantilla en los cuatro recompiladores (`retrorecomp/shared/android`);
`make apktest` compila la de nestest.

**8. Entender qué encontró el recompilador**

```
./nesrecomp juego.nes --viz                          # mapa del PRG en la terminal, coloreado por origen
./nesrecomp juego.nes --why 00:8175                  # por qué esa instrucción (banco:dirección, o el offset hex en PRG) es código
./nesrecomp juego.nes --html visor.html              # lo mismo, interactivo: una página sola, sin servidor
./nesrecomp juego.nes --html visor.html --coverage smb.trace    # y en rojo lo que el juego ejecutó y no se encontró
```

`visor.html` muestra el mapa del PRG (una celda por 256 bytes, color por origen: flujo desde los vectores, traza, tabla de saltos,
especulativo), las instrucciones de cada celda y, al hacer clic en una, la cadena de saltos, tablas y bancos que llevó hasta
ella. `--coverage` también imprime cuántas de las instrucciones ejecutadas se descubrieron sin ayuda (en SMB: 100 %).

Más en [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).

## Estado

- CPU 6502 con opcodes no oficiales: pasa `nestest` y las 16 `instr_test-v5`.
- PPU 2C02 punto por punto: fondo, sprites, scroll, sprite 0 hit (pasa las 11 `sprite_hit_tests`).
- Mappers: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3 (IRQ por flanco de A12 con filtro), 7 AxROM, 11 Color Dreams,
  13 CPROM, 18 Jaleco SS88006, 19 Namco 163 (con su audio), 21, 22, 23 y 25 VRC2/VRC4, 24 y 26 VRC6 (con su audio), 85 VRC7 (con su audio FM), 75 VRC1, 95, 115, 184 Sunsoft 1, 210 Namco 175/340,
  34 BNROM/NINA-001, 66 GxROM, 71 Camerica, 79 NINA-03/06, 87, 140, 180, 206 (DxROM), 9 (MMC2), 10 (MMC4), 69 (FME-7,
  con el audio 5B), 119 (TQROM), 5 (MMC5, con sus pulsos, PCM y ExGrafix; sin pantalla dividida) y 118 (TxSROM, solo con `mappertest`). Probados con
  juegos reales: Punch-Out, Fire Emblem, Batman: Return of the Joker, Pin Bot, Castlevania III, Kirby, Battletoads,
  Dragon Warrior III (512 KB), Gauntlet, Micro Machines, Crystal Mines, SMB + Duck Hunt y Tiles of Fate.
- APU: 2 pulsos, triángulo, ruido, DMC, frame counter con IRQ, mezcla no lineal y filtros. Pasa las 8
  `apu_test`; la ventana saca audio por SDL (tecla M = silencio).
- Temporización por ciclo: cada acceso al bus (con lecturas y escrituras falsas) avanza 3 puntos de PPU y
  1 de APU. Pasa `nestest` (incluida la posición de la PPU), `cpu_timing_test`, `instr_timing`,
  `instr_misc`, `branch_timing_tests`, las 10 `ppu_vbl_nmi`, `cpu_interrupts_v2` 1, 2, 3 y 5 y las 11 pruebas viejas del
  frame counter del APU.
- RAM con batería: `juego.sav` en la carpeta de datos del juego (se guarda al salir y cada 5 s si cambia). Sin partida previa, la RAM se lee como `$FF` (memoria sin programar).
- **Interfaz** (SDL2, español e inglés): barra de menús y diálogos de Moskva 89 / GBRecomp; estados guardados (4 ranuras),
  pausa, avance rápido (Tab), 2 jugadores (teclado y mandos), **Four Score** (4 jugadores, con mandos), **Zapper** (ratón: apuntar y clic izquierdo; verificado con Duck Hunt: un programa que apunta a los patos los derriba y suma puntos), tamaños 1x-6x, pantalla completa, escala entera, aspecto 8:7,
  overscan, paletas (predeterminada, grises, saturada y `paleta.pal` propia), volumen y silenciar canales del APU,
  captura PNG, grabación de audio WAV, grabar/reproducir entradas (`--record`/`--play`), arrastrar una ROM a la ventana.
  Barra de menús oculta (Video → Ocultar la barra de menús, F9: aparece con el ratón arriba o con Alt/F10), y al
  arrancar `--fullscreen` (pantalla completa) y `--no-menu` (sin barra de menús; los atajos siguen).
  El juego recompilado pide la ROM una sola vez (valida el SHA1) y guarda una copia en su carpeta de datos.
- **Recompilador**: descubrimiento desde los vectores y una traza de ejecución, emisión de C por
  instrucción con los mismos accesos al bus que el intérprete, despacho por desplazamiento de PRG
  (sirve con cualquier banco) y `difftest`. Con una traza, Super Mario Bros., Donkey Kong, Arkanoid,
  Contra, Castlevania, Mega Man, Super Mario Bros. 3 y Zelda corren 1500 cuadros con 100 % de los ciclos
  en código recompilado (Zelda 99.7 %) e **idénticos al intérprete** (CPU, RAM, PPU, APU, mapper,
  imagen y audio). **Sin traza**, solo con el análisis estático (vectores, bancos, tablas de saltos), los mismos juegos corren con 97-100 % de
  los ciclos recompilados e idénticos al intérprete; el análisis encuentra el 95-100 % de lo que cada juego ejecuta
  (`tests/coverage.sh`). La traza queda como ayuda para lo que falte.

### Rendimiento

El proyecto que genera `nesrecomp` se compila con el mapper de la ROM fijo (`-DNES_MAPPER=<n>`, que sale de la cabecera): el
compilador elimina el código de los demás mappers y los `switch` del camino caliente. En Super Mario Bros. (NROM): `mapper.o` de
10,6 KB a 2,5 KB y ~20 % más rápido (39x frente a 32x tiempo real). El intérprete `nes` sigue decidiendo el mapper al cargar.

PPU y APU perezosos (solo avanzan cuando hace falta, resultado idéntico bit a bit; ver la arquitectura). Super Mario
Bros.: ~55x tiempo real recompilado y ~42x con el intérprete (antes 13x y 11x); Super Mario Bros. 3 (MMC3) ~25x con el
intérprete (antes 12x). Con el MMC3 la PPU solo va ciclo a ciclo a partir del punto 256 de cada línea, donde se
levanta su IRQ. `tests/golden.sh` vigila que ninguna optimización cambie
ni un byte de imagen o audio.

### Conocido

- AccuracyCoin (`testroms/accuracycoin`, marcador con `tests/accuracycoin.py`): 128 de 149 pruebas pasan (18 fallan; el detalle por grupos está en el issue #11). Faltan, sobre todo, el
  comportamiento por punto de la PPU (lecturas de `$2004`/`$2007` con el dibujo activo, registros de desplazamiento "viejos",
  activar o desactivar el dibujo en puntos concretos, la OAM2), y varios
  detalles del bus abierto de la PPU y del APU. `RMW2007`, `CPUInstr` y `Unimplemented` no llegan a ejecutarse.

- Las pruebas que arrastraba el proyecto (`mmc3_test_2` completo, con la revisión vieja del MMC3 en `NES_MMC3_ALT=1`, y
  `cpu_interrupts_v2` 1-5) ya pasan.
- El código que el descubrimiento no alcanza (y que no está en la traza), o que está en la RAM del cartucho, lo
  corre el intérprete. Es correcto, solo más lento. El de la RAM interna se aprende de la traza (líneas `ram`, ver
  `docs/ARQUITECTURA.md`): 3-D WorldRunner pasa del 62 % al 100 % de los ciclos en código recompilado. El descubrimiento especulativo agrega mucho código que nunca
  se ejecuta (SMB3: 69 mil instrucciones, ~13 s de compilación con `make -j8`).

### Compatibilidad

Probado con 732 ROMs de la colección No-Intro: 726 usan un mapper soportado y corren en el intérprete y las 66 que se recompilaron
como prueba (por mapper) no tuvieron diferencias. Detalle, lista por juego y cómo repetirlo en [docs/COMPATIBILIDAD.md](docs/COMPATIBILIDAD.md) y [docs/compat/index.html](docs/compat/index.html).

## Herramientas

| Comando | Qué hace |
|---|---|
| `nes juego.nes` | Ventana SDL2 con el intérprete. `NES_TRACE=archivo` guarda la traza mientras juegas. |
| `nesrecomp` | El recompilador. |
| `nestrace rom traza [--frames N] [--input archivo] [--seed S]` | Traza de ejecución sin ventana (también `cuadros [boton:desde-hasta ...]`). |
| `nesshot rom salida.png [--frames N] [--input archivo] [--expect esperado.png] [--hash] [...]` | Captura sin ventana; imprime el resultado de las ROMs de Blargg. Con `zapper:X,Y,desde-hasta` apunta y dispara el Zapper en esos cuadros. Como `gbtest` de GBRecomp: `--expect` pasa si la pantalla final es idéntica al PNG y `--hash` imprime un hash de todos los cuadros y de todo el audio. |
| `nesaudio rom salida.wav [--frames N] [--input archivo] [...]` | Audio a WAV, con el nivel por segundo. |
| `nesbench rom [--frames N] [--repeat N] [--input archivo]` | Velocidad sin ventana. |
| `nesrecomp juego.nes --why BB:DIRECCION` / `--viz` / `--html visor.html` / `--events archivo` / `--codemap archivo` | Explica por qué una instrucción es código (cadena de saltos, tablas y bancos), dibuja un mapa del PRG en la terminal coloreado por origen (con `--coverage traza`, en rojo lo que falta), vuelca cada instrucción con su origen o escribe qué bytes se tomaron como código (el formato de `difftest --executed`, para compararlos). |
| `nesramdump rom [--frames N] [--input archivo] [...]` | Vuelca la RAM tras correr la ROM (lo usa `tests/accuracycoin.py`; en GBRecomp, `gbramdump`). |
| `nestest`, `sramtest`, `statetest`, `mappertest`, `inputtest` | Pruebas. `make test` corre todas. |
| `difftest`, `bench` (en cada proyecto generado) | Comparan el código recompilado contra el intérprete y miden la velocidad. |
| `python3 tools/retrorecomp/retrorecomp_gui.py` | **GUI** local en el navegador (solo Python estándar), común con GBRecomp, GENRecomp y NEORecomp ([retrorecomp](https://github.com/Xalcker/retrorecomp), submódulo en `tools/retrorecomp`; `git submodule update --init`): mapa del PRG como `--viz` en colores con clic por celda y la cadena de «por qué», avance por ROM, trazas, entradas, recompilar, `difftest`, `bench`, `tests/recomp_game.sh`, sincronizar el repo y la salida en vivo. |
| `python3 tools/compat/{run,report}.py` | Prueba de compatibilidad con una colección de ROMs y su informe HTML, con el motor común de `tools/retrorecomp/compat.py` ([docs/COMPATIBILIDAD.md](docs/COMPATIBILIDAD.md)). |

**Opciones comunes.** Las herramientas sin ventana (`nestrace`, `nesshot`, `nesaudio`, `nesbench`, y `difftest` y
`bench` de cada proyecto) aceptan las mismas opciones que `gbtrace`, `gbshot`, `gbaudio`, `gbbench` y `difftest` de
GBRecomp: `--frames N`, `--input archivo` (el guion de `--record`: `cuadro BOTONES`, ver `src/platform/inputs.h`),
`--seed S` (suma un mando aleatorio reproducible: `src/platform/random_pad.h`, idéntico en GBRecomp, así que la misma
semilla da la misma secuencia de botones en las dos consolas) y, en `nesbench`/`bench`, `--repeat N` (se reporta la corrida más rápida).
Sigue valiendo la sintaxis de antes (el número de cuadros suelto y `boton:desde-hasta`):

```
./nestrace "roms/juego.nes" juego.trace --frames 3000 --input partida.txt
./nesshot  "roms/juego.nes" captura.png --frames 900 --input partida.txt
./nesbench "roms/juego.nes" --frames 3000 --repeat 3
```

`tests/recomp_game.sh rom nombre` recompila un juego con una traza (guionada y la que descubre `difftest --uncovered`) y lo compara contra el intérprete.
**Contrato común con GBRecomp, GENRecomp y NEORecomp.** `nestrace`, `nesshot`, `nesaudio`, `nesbench` y `difftest` tienen las mismas opciones que
las herramientas de [GBRecomp](https://github.com/Xalcker/GBRecomp), [GENRecomp](https://github.com/Xalcker/GENRecomp) y [NEORecomp](https://github.com/Xalcker/NEORecomp) (`--frames`, `--input`, `--seed`, `--repeat`), y la GUI común
([retrorecomp](https://github.com/Xalcker/retrorecomp)) se apoya en eso. `sh tests/contract.sh` (el mismo archivo en los
dos repos) comprueba que las herramientas compiladas las mencionan y, con las ROMs de prueba, que la misma entrada dé
la misma traza, captura y audio (`--seed`, `--input` con `--seed`, y `--input` igual a `boton:desde-hasta`); el CI la corre en cada PR (`.github/workflows/ci.yml`, como en los otros tres). Si cambias una opción aquí, cámbiala también en GBRecomp, GENRecomp, NEORecomp y en la tabla de retrorecomp.

`tests/golden.sh` compara el hash de todos los cuadros y del audio de cada juego de `roms/` y de las ROMs de prueba con una línea base (`tests/golden.txt`, por SHA1 de la ROM); `tests/coverage.sh` mide la cobertura del análisis estático.

## Estructura

| Ruta | Contenido |
|---|---|
| `src/runtime/` | La consola: `nes.c`, `cpu6502.c`, `cpu_ops.h`, `ppu.c`, `apu.c`, `mapper.c`, `recomp.h`; `sha1`, `console.h` (de GBRecomp). |
| `src/nesrecomp/` | El recompilador. |
| `src/platform/` | La interfaz SDL2 (`main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, `files`, `export`, `png`, `inputs.h`), `difftest.c` y `bench.c`. Se copian a cada proyecto generado. |
| `src/tools/` | Herramientas sin ventana y `embed`. |
| `tools/` | `retrorecomp/`: la GUI común con GBRecomp, GENRecomp y NEORecomp (submódulo). `compat/`: prueba de compatibilidad con una colección de ROMs (Python 3, con el motor común de retrorecomp). `oracles/`: emuladores de referencia que se descargan al usarlos (TriCNES y Mesen2). `truth/`: scripts de GBRecomp para comparar contra un desensamblado (por adaptar a 6502). |
| `docs/` | `ARQUITECTURA.md`, `COMPATIBILIDAD.md` y `compat/` (informe HTML, `results.json` y miniaturas). |
| `tests/` | `run_tests.sh` (`make test`), `recomp_game.sh`, `golden.sh`, `coverage.sh`, `contract.sh`, `accuracycoin.py`. |
| `testroms/` | ROMs de prueba: `nes-test-roms/` (nestest, Blargg, mmc3...; `make testroms`) y `accuracycoin/` (AccuracyCoin, MIT). No se versionan. |
| `roms/` | Tus juegos. No se versionan. |

## Pendiente

- Más mappers (105, 268, 446, mapper 15) y la pantalla dividida del MMC5.
  El resto está en los [issues](https://github.com/Xalcker/NESRecomp/issues).

## Créditos

- Interfaz basada en Moskva 89 y GBRecomp (MIT).
- Icono de la ventana: `FC.png` (control de NES) de la serie TriMui de
  [KyleBing/retro-game-console-icons](https://github.com/KyleBing/retro-game-console-icons) (`src/platform/icono.h`).
  Revisa la licencia de ese repositorio antes de distribuir el proyecto.
- ROMs de prueba de `testroms/`: [christopherpow/nes-test-roms](https://github.com/christopherpow/nes-test-roms)
  (nestest, Blargg y otros; no se versionan).

## Licencia

NESRecomp se distribuye con licencia [MIT](LICENSE), la misma que GBRecomp, GENRecomp, NEORecomp y retrorecomp. La licencia cubre
el código del recompilador y del runtime, no las ROMs ni el código que se genera a partir de ellas: ese
código es una traducción del programa del juego, así que sirve para jugar tu propia copia, pero no
distribuyas el proyecto ni el ejecutable de un juego comercial.
