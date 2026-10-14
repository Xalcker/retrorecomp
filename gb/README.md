Hay que clonar con el submódulo de retrorecomp, que trae el código que comparten los cuatro recompiladores (`random_pad.h`, `trace_line.h`): `git clone --recursive`, o `git submodule update --init` en un clon ya hecho. El ZIP de GitHub no lo incluye.

# GBRecomp

Recompilador estático de juegos de Game Boy. Toma una ROM (`.gb` o `.gbc`) sin
desensamblado previo y genera un proyecto en C99 portable que se compila en
un ejecutable nativo para Windows, Linux o macOS. El ejecutable se ve, suena y
se juega igual que el original.

```
gbrecomp juego.gb -o juego/
cd juego && make
./juego
```

- **Híbrido:** el código que se descubre se traduce a C. Lo que no se pudo
  descubrir (saltos indirectos raros, código en RAM, código automodificable)
  lo ejecuta un intérprete SM83 exacto al ciclo, y el control regresa al
  código recompilado en cuanto se puede. La rutina de DMA de OAM que los
  juegos copian a HRAM sí se compila, y el código que copian a WRAM o HRAM
  se aprende de la traza de `difftest` (ver `docs/ARQUITECTURA.md`).
- **Exacto al ciclo:** el código recompilado cuenta los M-ciclos de cada
  acceso al bus. Timer, PPU, APU, serial y DMA avanzan igual que en el
  hardware, así que los efectos a mitad de línea funcionan.
- **Sin datos de la ROM:** el proyecto generado no contiene la ROM. El
  ejecutable la pide una vez, valida su SHA1 y guarda una copia en la carpeta
  de datos del juego.
- **Verificable:** cada proyecto trae `difftest`, que corre el código
  recompilado y el intérprete lado a lado y reporta la primera diferencia
  (registros, RAM, VRAM, OAM, I/O).

## Modelos

| Cabecera del juego | Corre como |
|---|---|
| Admite color (`$143` bit 7) | Game Boy Color, en modo CGB |
| Admite Super Game Boy | Super Game Boy: borde y colores de la SNES (256×224) |
| Ninguno de los dos | Game Boy Color en modo de compatibilidad, coloreado con la paleta que le asigna la boot ROM según el título |

Es lo que hace el ejecutable del juego. Las herramientas sin ventana
(`gbtest`, `gbbench`, `difftest`, `bench`) usan el automático del runtime, que a un
juego sin color ni SGB lo corre como Game Boy (DMG).

`--model dmg|cgb|sgb` fuerza otro modelo. En el modo de compatibilidad,
`--palette` elige una de las 12 combinaciones de botones del GBC (`up`,
`up+a`, `left+b`, …).

## Compilar GBRecomp

Se necesita un compilador C99, `make`, `pkg-config` y SDL2 (para los juegos generados); `git` y `unzip` se usan
para la versión y para `make testroms`. Son los mismos paquetes que en NESRecomp, GENRecomp y NEORecomp.

| Sistema | Paquetes |
|---|---|
| Windows | [MSYS2](https://www.msys2.org/) UCRT64: `pacman -S --needed make git unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2` |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential pkg-config libsdl2-dev curl unzip` |
| macOS | `brew install sdl2 pkg-config` |

```
make            # gbrecomp, gb (intérprete con ventana), las herramientas sin ventana y el runtime
make testroms   # una sola vez: baja las ROMs de prueba libres (c-sp/game-boy-test-roms v7.0) a testroms/
make test       # opcional: Blargg, Mooneye, SameSuite, acid2, recompilado contra intérprete...
make dist       # dist/gbrecomp-<versión>-<sistema>.tar.gz (sistema: linux, macos o windows)
```

Son los mismos objetivos que en NESRecomp, GENRecomp y NEORecomp, y el CI (`.github/workflows/ci.yml`, el mismo flujo en los cuatro) corre
exactamente eso en Linux, macOS y Windows.

**Probar una ROM con el intérprete, sin recompilar** (como `nes juego.nes` en NESRecomp):

```
./gb roms/juego.gb            # la misma ventana que un juego recompilado, todo con el intérprete
./gb roms/juego.gb --uncovered juego.trace    # y al salir, lo que ejecutó, como traza para gbrecomp
```

Acepta cualquier ROM (no valida el SHA1 ni guarda copia). La partida, los estados y lo exportado
van a la carpeta de datos `interprete/`, con el nombre del archivo de la ROM.

**Versiones.** La versión es la etiqueta de git más cercana (`git describe`: `v0.1.0`, o `v0.1.0-3-gabc1234` con
commits después de ella). Al subir una etiqueta `vX.Y.Z` de `main` (`git tag v0.2.0 && git push origin v0.2.0`), el CI
publica un release en GitHub con los paquetes de Linux, macOS y Windows. Igual que en los otros recompiladores.

`gbrecomp` es un solo ejecutable que lleva el runtime adentro, así que se
puede copiar a cualquier carpeta. En Windows, el juego generado queda
enlazado de forma estática y no necesita DLLs.

## Recompilación paso a paso

Un ejemplo completo con un juego cuya primera pasada se queda corta, Street
Fighter Alpha (`roms/sfa2.gbc`). Los números son los de ese juego; con otros
cambian, pero los pasos son los mismos. Las ROMs van en `roms/`, que no se
sube al repositorio.

**1. Primera pasada, mirando qué se descubre**

```
make
./gbrecomp roms/sfa2.gbc -o build/out/sfa2 --name sfa2 --viz --events build/sfa2_eventos.txt
```

`--viz` anima el descubrimiento en la terminal y `--events` guarda cada paso
para verlo después en el navegador. Al final, gbrecomp resume lo que
encontró:

```
código: 6248 instrucciones (10519 bytes, 1.00% de la ROM) en 51 funciones
```

Ese porcentaje es de la ROM entera, que también trae gráficos, música y
niveles: no dice qué tan bien va a correr. Para eso está el paso 2.

**2. Compilar y medir cuánto se ejecuta recompilado**

```
make -C build/out/sfa2
build/out/sfa2/difftest roms/sfa2.gbc --frames 6000 --seed 1
```

`difftest` juega solo, con entradas al azar, y compara el código recompilado
contra el intérprete cuadro por cuadro. El resumen es el mismo que en NESRecomp,
GENRecomp y NEORecomp (con diferencias, en cambio, imprime líneas `DIFERENCIA ...`
y sale con 1):

```
difftest: 6000 cuadros iguales (20993550 llamadas, 713352 a código recompilado, 76.0% de los ciclos)
Instrucciones: 61658128 recompiladas, 20280198 en el intérprete (75.249% de las instrucciones)
Instrucciones de ROM interpretadas: 10299
```

`build/out/sfa2/bench roms/sfa2.gbc` mide la velocidad del código recompilado sin
ventana, con las mismas opciones que `gbbench` (el intérprete).

**En el navegador** (WebAssembly): `make -C build/out/sfa2 web` compila el mismo proyecto con Emscripten si
está instalado, o si no con Docker (la imagen `emscripten/emsdk` de versión fija que dice `platform/web.mk`), y
`python3 -m http.server -d build/out/sfa2/web` lo sirve (abrir `http://localhost:8000/sfa2.html`). La página pide
la ROM (no va en el build), comprueba el SHA1 y la guarda en el navegador con la partida, los estados y las
preferencias (IndexedDB). Es la misma página en los cuatro recompiladores (`retrorecomp/shared/web`); `make
webtest` compila la de `cpu_instrs`.

**En Android** (APK): `make -C build/out/sfa2 apk` deja `build/out/sfa2/apk/<nombre>-debug.apk`, que se instala con
`adb install -r`. Usa el SDK de Android si lo hay (`ANDROID_HOME` o `ANDROID_SDK_ROOT`, y un JDK 17 o más nuevo en
`JAVA_HOME`; el JBR de Android Studio sirve) y si no, Docker con la imagen de versiones fijas de
`platform/android/Dockerfile`, que se construye la primera vez (unos 3 GB). En el teléfono el juego arranca
horizontal y en pantalla completa, con un mando en pantalla (o un mando Bluetooth); Atrás o el botón de las tres
rayas abren el menú. La primera vez pide la ROM con el selector de archivos del teléfono y la guarda; las partidas y
las preferencias se guardan también al dejar la app en segundo plano. `make apk ROM=sfa2.gb` mete la ROM en la APK
(solo para uso personal: esa APK no se comparte) y `APK_RELEASE=1` firma una de release con tu keystore (ver
`platform/android/android.mk`). Es la misma plantilla en los cuatro recompiladores (`retrorecomp/shared/android`);
`make apktest` compila la de `cpu_instrs`.

Si el porcentaje de los ciclos pasa de ~98 % y no hay diferencias, ya está:
salta al paso 6. Si queda bajo (aquí, 76 %), es que hay
código al que el análisis estático no llega (saltos por punteros, tablas que
no reconoce...). Lo que falta lo ejecuta el intérprete, así que el juego
funciona igual, solo más lento. La traza lo resuelve.

**3. Ciclo con trazas**

```
sh tests/recomp_game.sh roms/sfa2.gbc sfa2
```

Graba una traza guionada, corre `difftest` con 3 semillas de 6000 cuadros, junta
en `build/sfa2.trace` las direcciones que tuvo que ejecutar el intérprete, vuelve
a recompilar con esa traza (`--trace`) y prueba otra vez (poco más de un minuto):

```
sfa2: primera ronda
código en WRAM/HRAM aprendido de la traza: 6 instrucciones (0 con operandos variables) en 1 bloques
código: 31756 instrucciones (54256 bytes, 5.17% de la ROM) en 113 funciones
destinos de punteros a código guardados en RAM: 5
  semilla 1    99.6% de los ciclos Instrucciones de ROM interpretadas: 1363
  semilla 2    99.9% de los ciclos Instrucciones de ROM interpretadas: 1287
  semilla 3    99.9% de los ciclos Instrucciones de ROM interpretadas: 482
sfa2: con la traza (   20302 direcciones)
código en WRAM/HRAM aprendido de la traza: 6 instrucciones (0 con operandos variables) en 1 bloques
código: 37393 instrucciones (64859 bytes, 6.19% de la ROM) en 129 funciones
destinos de punteros a código guardados en RAM: 5
  semilla 1    100.0% de los ciclos Instrucciones de ROM interpretadas: 1
  semilla 2    100.0% de los ciclos
  semilla 3    100.0% de los ciclos
Sin diferencias.
```

La traza se acumula: si corres el script otra vez, la primera ronda ya parte
de ella.

El proyecto queda en `build/out/sfa2`, ya compilado con la traza. Las entradas al
azar no llegan a todo el juego: para lo que falte, juega con `--uncovered`,
que agrega a un archivo lo que ejecutó el intérprete, y recompila con esa
traza:

```
build/out/sfa2/sfa2 roms/sfa2.gbc --uncovered build/sfa2.trace
./gbrecomp roms/sfa2.gbc -o build/out/sfa2 --name sfa2 --trace build/sfa2.trace && make -C build/out/sfa2
```

**4. Ver qué faltaba y por qué**

```
./gbrecomp roms/sfa2.gbc -o build/out/sfa2-viz --viz --coverage build/sfa2.trace
./gbrecomp roms/sfa2.gbc -o build/out/sfa2-viz --viz --trace build/sfa2.trace \
    --coverage build/sfa2.trace --events build/sfa2_traza.txt
./gbrecomp roms/sfa2.gbc --why 00:0008
./gbrecomp roms/sfa2.gbc --trace build/sfa2.trace --why 00:0008
```

- El primero marca en rojo (`×`) lo que el juego ejecutó y el análisis no
  descubrió.
- El segundo muestra cómo la traza lo va cubriendo (al final, 0 sin
  descubrir).
- `--why` explica por qué una dirección es código: sin la traza dice "no se
  descubrió"; con ella, la cadena de saltos hasta un punto de entrada.

Las zonas rojas grandes y los `!` (saltos sin resolver) son donde una nueva
heurística de descubrimiento rendiría más (ver `docs/ARQUITECTURA.md`).

**5. Verlo en el navegador**

Abre `tools/visor.html` (doble clic; no necesita servidor) y arrastra a la
ventana:
- `build/sfa2_traza.txt` (o `build/sfa2_eventos.txt`, la primera pasada),
- `roms/sfa2.gbc`, para el desensamblado,
- `build/sfa2.trace`, como cobertura.

Con la línea de tiempo se avanza, retrocede o reproduce el descubrimiento;
la fase "traza" muestra el rojo apagándose. Al hacer clic en una celda se ven
sus instrucciones, y al hacer clic en una dirección, la cadena de `--why`.

**6. Jugar**

```
build/out/sfa2/sfa2 roms/sfa2.gbc
```

La primera vez guarda una copia de la ROM en su carpeta de datos; después
basta con `build/out/sfa2/sfa2`. El menú Ayuda > Datos del juego dice qué porcentaje
de lo ejecutado va recompilado.

Con un juego que el análisis estático cubre bien, como Tetris (98.9 %
recompilado sin traza), bastan los pasos 1, 2 y 6.

## Uso

```
gbrecomp juego.gb -o salida/ [--name nombre] [--trace archivo]... [--viz] [--events archivo]
         [--coverage archivo] [--why BB:AAAA]
```

`--trace` se puede repetir: se leen todas las trazas (la guionada de `gbtrace` y la de `difftest`,
por ejemplo), igual que en nesrecomp. `gbrecomp --help` lista todas las opciones, también
`--codemap`, `--dis BB:AAAA:N` y `--verbose`.

El descubrimiento estático encuentra casi todo el código. Para lo que quede
(saltos por tablas que no se pudieron resolver), se hace un ciclo con trazas:

1. Juega (o corre `difftest` con entradas aleatorias) con `--uncovered
   faltantes.txt`. Quedan anotadas las direcciones que tuvo que ejecutar el
   intérprete.
2. `gbrecomp juego.gb -o salida/ --trace faltantes.txt` las agrega como
   puntos de entrada.

Si el juego ejecuta código desde WRAM o HRAM (manejadores de interrupción, trampolines de cambio de
banco, rutinas copiadas de la ROM), ese código no está en la ROM donde corre. `--uncovered` (en
`difftest` y en el ejecutable del juego, `gb` incluido) también anota sus bytes, en líneas
`ram DIRECCIÓN BYTES` que cualquier lector de trazas anterior ignora. `gbrecomp --trace` las compila con
sus direcciones y una guarda que comprueba que la RAM siga teniendo esos bytes; si el juego los cambia,
el código cae al intérprete (ver «Código que el juego copia a WRAM o HRAM» en `docs/ARQUITECTURA.md`,
que también explica por qué y cómo llevarlo a otra consola). `gbrecomp` dice cuántas instrucciones de
RAM aprendió y `difftest`, cuántas siguen en el intérprete.

`tests/recomp_game.sh ROM NOMBRE` hace ese ciclo completo con `difftest`. Es el mismo script en NESRecomp, GBRecomp, GENRecomp y NEORecomp (la consola sale del recompilador que hay en la carpeta, y `retrorecomp/tests/contract.py` comprueba que sigan idénticos): graba antes una traza guionada con `tests/inputs/ciclo_traza.txt` y corre cada `difftest` con `tests/inputs/ciclo_difftest.txt` más cada semilla; `TRACE_FRAMES=N` y `NOTRACE=1` valen en los cuatro, e `INPUT_TRACE=F` / `INPUT_DIFFTEST=F` cambian las entradas por una partida grabada. Game Boy corre 6000 cuadros por difftest si no se dice otra cosa.

**Contrato común con NESRecomp, GENRecomp y NEORecomp.** Las herramientas sin ventana (`gbtrace`, `gbshot`, `gbaudio`, `gbbench`,
`difftest`) tienen las mismas opciones que las de [NESRecomp](https://github.com/Xalcker/NESRecomp)
(`--frames`, `--input`, `--seed`, `--repeat`), y la GUI común ([retrorecomp](https://github.com/Xalcker/retrorecomp))
se apoya en eso. `sh tests/contract.sh` (el mismo archivo en los dos repos) comprueba que las
herramientas compiladas las mencionan y, con las ROMs de prueba, que la misma entrada dé la misma traza,
captura y audio (`--seed`, y `--input` con `--seed`); el CI lo corre en cada PR. Si cambias una opción aquí, cámbiala también en NESRecomp, GENRecomp, NEORecomp y en la tabla de retrorecomp.

Para ver qué encontró el descubrimiento y de dónde salió cada parte:

```
gbrecomp juego.gb -o salida/ --viz                  # mapa animado de la ROM
gbrecomp juego.gb -o salida/ --viz-time 0           # solo el mapa final
gbrecomp juego.gb -o salida/ --events eventos.txt   # cada paso, en texto
gbrecomp juego.gb --html visor.html                 # el visor del navegador, con todo adentro
gbrecomp juego.gb --codemap codigo.txt              # qué bytes son código (C), como difftest --executed
```

`--viz` dibuja cada banco como una cuadrícula que se va encendiendo a medida
que aparece código. El color indica de dónde vino ese código: verde para el
arranque, cian para las interrupciones, amarillo para las tablas de punteros y
magenta para la traza. `!` marca los saltos que no se pudieron resolver, que
son los candidatos a necesitar una traza. `--events` anota cada recorrido con
la instrucción que lo agregó (`recorrido 01:5C45 arranque desde 00:0101`), así
que se puede seguir hacia atrás la cadena que llevó a cualquier dirección.

Para depurar el ciclo con trazas:

```
gbrecomp juego.gb -o salida/ --viz --coverage faltantes.txt
gbrecomp juego.gb --why 01:6500
```

`--coverage` toma un archivo de `--uncovered` (lo que el juego ejecutó sin que
se hubiera descubierto), cuenta cuántas de esas direcciones siguen sin
descubrirse y, con `--viz`, las marca con `×` sobre fondo rojo hasta que algún
recorrido llega a ellas. Con `--trace` y `--coverage` sobre el mismo archivo se
ve cómo la traza las va cubriendo. `--why` explica por qué una dirección se tomó
como código: la cadena de saltos y tablas desde un punto de entrada.

```
¿Por qué 00:03A0 es código?
  00:03A0  ldh a, [$FF81]         en el recorrido 00:03A0
  00:03A0  <- entrada 00:0365 de la tabla 00:02FB (en línea)
  00:02FA  rst $28                en el recorrido 00:02F8
  00:02F8  <- salto desde 00:02C7
  ...
  00:0100  <- punto de entrada (arranque)
```

Sirve para revisar código descubierto de más, por ejemplo una tabla de
punteros que se leyó más allá de su final.

`tools/visor.html` es lo mismo en el navegador: se abre directo (sin
servidor) y se le arrastra el registro de `--events`, y si se quiere la ROM y
una lista de cobertura. Muestra el mapa de la ROM con una línea de tiempo para
avanzar, retroceder o reproducir el descubrimiento; al hacer clic en una celda
desensambla sus instrucciones y explica, como `--why`, por qué cada una es
código. Los archivos se leen en la computadora; no se mandan a ningún lado.

`gbrecomp juego.gb --html visor.html` escribe ese mismo visor con los eventos
adentro (y la cobertura, con `--coverage`): una página sola que abre ya cargada,
igual que `nesrecomp --html`. La ROM, si se quiere desensamblar, se arrastra a la
página; no va adentro. Sin `-o`, `--viz`, `--events`, `--html`, `--codemap` y
`--coverage` no generan el proyecto. `--viz --color` colorea el mapa aunque la
salida no sea una terminal (la GUI lo usa), sin animación.

**Herramientas sin ventana** (las mismas opciones que `nestrace`, `nesshot` y `nesaudio` de NESRecomp:
`--frames N`, `--input archivo`, `--seed S`, `--model dmg|cgb|sgb`):

```
gbtrace rom.gb traza.txt --frames 3000 --input tests/inputs/partida1.txt   # direcciones de ROM ejecutadas (BB:AAAA) y código de WRAM/HRAM (ram ...)
gbshot  rom.gb captura.png --frames 900 --input tests/inputs/partida1.txt
gbaudio rom.gb audio.wav   --frames 600 --seed 3                            # WAV estéreo y nivel por segundo
gbramdump rom.gb          --frames 600 --input partida.txt                 # WRAM y HRAM (como ramdump de NES)
```

`gbtrace` corre el intérprete, así que no necesita el proyecto compilado: la traza se pasa a
`gbrecomp --trace` o a `--coverage` igual que la de `--uncovered`. Sin `--input` ni `--seed` no se
pulsa ningún botón; `--seed S` suma al guion un mando al azar reproducible
(`random_pad.h`, en `tools/retrorecomp/shared`), el mismo en todas las herramientas, en `difftest` y en
NESRecomp: con la misma semilla, la misma secuencia de botones en las dos consolas.

**GUI local.** `python3 tools/retrorecomp/retrorecomp_gui.py` abre en el navegador (solo Python
estándar; escucha en 127.0.0.1 con un token) una interfaz sobre todo lo anterior, común con
NESRecomp, GENRecomp y NEORecomp ([retrorecomp](https://github.com/Xalcker/retrorecomp), submódulo en `tools/retrorecomp`:
`git submodule update --init`): el mapa de cada banco como `--viz` con clic por celda y la cadena de
`--why`, el avance por ROM (análisis, cobertura, proyecto, `difftest`, velocidad), trazas con
`gbtrace`, `difftest --uncovered` o jugando, guiones de entradas (`--input`, `--play`, `--record`),
`gbshot`/`gbaudio`, recompilar, `difftest`, `gbbench`, `tests/recomp_game.sh`, sincronizar el repo y la
salida de cada trabajo en vivo.

El ejecutable generado tiene una barra de menús (la de Moskva 89, dibujada
por el propio juego para verse igual en todos los sistemas; Alt o F10):

| Menú | Opciones |
|---|---|
| **Juego** | Reiniciar (`Ctrl+R`), Pausa (`P`), Guardar y cargar estado (`F5` / `F8`, cuatro ranuras), consola (Game Boy, Game Boy Color o Super Game Boy, según lo que admita el juego), cable link (2 jugadores), Salir |
| **Video** | Borde del SGB, tamaño ×1 a ×6, pantalla completa (`F11`) |
| **Paleta** | En DMG: verde, Pocket o grises. En compatibilidad GBC: la del juego o las 12 combinaciones de botones (`F2` las rota) |
| **Audio** | Sonido (`Ctrl+M`), volumen y, con el cable link, de qué jugador viene el sonido |
| **Exportar** | Grabar música en VGM y WAV (`Ctrl+G`), juntar gráficos (`Ctrl+T`), captura de pantalla (`F12`), abrir la carpeta |
| **Idioma** | Español o English |
| **Ayuda** | Controles (`F1`), datos del juego (consola, % recompilado, carpeta de datos), acerca de |

Las preferencias se guardan en `settings.ini`, en la carpeta de datos del
juego, junto con la partida y los estados.

**Multijugador local:**
- **Cable link** (menú Juego, o `--link`): un segundo Game Boy con la misma
  ROM, conectado por cable, a la derecha de la ventana. Lo maneja el jugador
  2, con su propia partida (`<juego>_jugador2.sav`). Las etiquetas J1 y J2
  aparecen al conectar, en pausa o con un menú abierto. En el menú Audio se
  elige si se oye al jugador 1, al 2, a los dos mezclados, o cada uno en un
  oído (audífonos: el jugador 1 a la izquierda y el 2 a la derecha, en
  mono). Los estados guardados (F5 / F8) guardan los dos Game Boy juntos; al
  cargar uno así sin el cable, el cable se conecta solo. La paleta es la
  misma para los dos.
- **Super Game Boy:** los juegos de varios jugadores (MLT_REQ) leen los
  controles 2 a 4.
- **Controles:** el jugador 1 usa las flechas, X, Z, Enter y Shift derecho, y
  el primer control que se conecte; el jugador 2 usa WASD, G (A), F (B), E
  (Start) y Q (Select), y el segundo control; los jugadores 3 y 4, el tercer y
  cuarto control.

**Exportar** deja los archivos en `exportado/`, dentro de esa misma carpeta:
- **Música:** el VGM guarda cada escritura del juego al chip de sonido con su
  tiempo exacto, así que se oye igual y sin pérdida en foobar2000 (con
  foo_input_vgm) o vgmplay. El WAV guarda el audio ya mezclado. `vgmcheck
  música.vgm música.wav` reproduce el VGM en el APU del runtime y lo compara
  con el WAV.
- **Gráficos:** cada mosaico distinto que pasa por VRAM, en hojas PNG de 4
  tonos de gris, editables. Los colores dependen de la paleta con que el
  juego dibuja cada mosaico; para verlos están las capturas.

Solo se exporta lo que el juego llega a cargar mientras juegas.

Controles: flechas, Z = B, X = A, Enter = Start, Retroceso o Shift derecho =
Select, o un control (A y B en la posición de Nintendo). Tab (mantenido): avance rápido, sin audio.

Video → Ocultar la barra de menús (F9) quita la barra de la ventana; aparece con el ratón arriba o con Alt/F10,
como en pantalla completa.

Opciones de línea de comandos: `--scale N`, `--fullscreen` (arranca en pantalla completa), `--no-menu` (sin
barra de menús; los atajos siguen), `--model`, `--palette`,
`--record archivo` / `--play archivo` (entradas grabadas, reproducibles en
`difftest --input`), `--link` (cable link), `--uncovered archivo` (lo que
ejecutó el intérprete, ROM y código de RAM, para `gbrecomp --trace`), `--interp` (todo con el
intérprete), `--force` (acepta una ROM con otro SHA1; solo con el intérprete)
y `--forget-rom`. Para pruebas sin ventana: `--ui-shot archivo.png` con
`--ui-frames N`, `--ui-menu N`, `--ui-action N` y `--ui-export`.

## Estado

**Pruebas de hardware** (`make test`, con las ROMs de prueba de `make testroms`):
143 de 143.
- Blargg `cpu_instrs`, `instr_timing`, `mem_timing` y `halt_bug`.
- Todas las de aceptación de Mooneye para DMG, más las de MBC1/2/5, las de
  arranque de GBC y SGB, y `dmg-acid2` y `cgb-acid2` (pantalla idéntica).
- `mbc3-tester` y `rtc3test`.
- SameSuite: las de DMA del GBC y las de multijugador del SGB.
- `sgb_controles`: lee los controles 2 a 4 del SGB como lo haría un juego.
- Proyecto generado de `cpu_instrs`: se compila y `difftest` lo compara contra el intérprete.
- Proyectos generados de `firstwhite` y `strikethrough`, que copian la rutina de DMA de OAM a HRAM: se
  compilan, esa rutina no pasa por el intérprete y `difftest` no encuentra diferencias.
- Proyecto generado de `ei_di`, una ROM mínima que repite `ei` / `di`: el código recompilado no atiende
  una interrupción antes de la instrucción que sigue a `ei`, y `difftest` no encuentra diferencias.
- Proyectos generados de una ROM mínima que copia una rutina a WRAM (con un operando que ella misma
  cambia) y de `interrupt_time`, de Blargg, que ejecuta unas 600 instrucciones distintas desde WRAM: la
  primera ronda las interpreta, con la traza que deja se compilan, y la segunda no interpreta ninguna
  y `difftest` no encuentra diferencias.
- El ejecutable (`gb`) con `--uncovered` y `gbtrace` anotan el mismo código de RAM que `difftest` en la
  ROM mínima.
- `gbrecomp` con dos `--trace` (una de código de RAM y otra de ROM): se leen las dos.
- `gbrecomp --html`, `--viz --color` y `--codemap`, `gbramdump` y la ventana de `gb` (cada menú, con `--ui-shot`).

Además, Mealybug Tearoom pasa 18 de 24 (`tests/run_mealybug.sh`) y Blargg
`dmg_sound` 9 de 12.

**Línea base exacta** (`sh tests/golden.sh`): 24 casos (11 juegos de
`roms/` con entradas fijas, más acid2, Mealybug, HDMA y `cgb_sound`) con el
hash de todos los cuadros y de todo el audio (`gbtest --hash`). Una
optimización del runtime no debe cambiar ni un byte: `difftest` no lo
detectaría, porque el código recompilado y el intérprete usan la misma PPU y
el mismo APU. Tarda unos 30 s. Tras un cambio que sí debe cambiar la salida, se
revisa a ojo y se regenera con `sh tests/golden.sh --update`. La velocidad se
mide con `gbbench rom.gb [--frames N] [--repeat N] [--input archivo]`
(intérprete sin ventana, con audio; reporta la más rápida de N corridas), y la
del código recompilado con el `bench` del proyecto generado, que es el mismo
programa (`src/platform/bench.c`) con las mismas opciones.

**Velocidad** (`gbbench`, intérprete con audio, sin ventana): Tetris ~48x,
Super Mario Land ~55x, Pokémon Rojo ~43x, Tetris DX ~44x, Super Mario Bros.
Deluxe ~50x y Harry Potter ~31x el tiempo real. El hardware (PPU ~69 %, APU
~17 %) se lleva casi todo el tiempo y el CPU ~8 %, así que el código
recompilado corre a una velocidad parecida a la del intérprete. El perfil y
cada optimización (todas sin cambiar ni un byte de imagen o audio) están en
`docs/ARQUITECTURA.md`.

**Juegos probados**, sin diferencias en `difftest`, con el porcentaje de
instrucciones que corrió como código recompilado:

| Juego | Modelo | Recompilado |
|---|---|---|
| Tetris | DMG / compatibilidad GBC | 98.9 % |
| Super Mario Land | DMG / compatibilidad GBC | 93.9 % |
| Pokémon Rojo | SGB (borde y colores) | 97.5 % |
| Wario Land II | SGB | 94.7 % |
| Pokémon Oro, Plata y Amarillo | GBC | sin diferencias |

Además, `tests/batch_roms.sh` recompila una lista de juegos y corre `difftest`
60 s con entradas aleatorias en cada uno. Con 33 juegos sin diferencias:
- **Efectos de pantalla:** Link's Awakening, Super Mario Land 2 y Prehistorik
  Man.
- **Doble velocidad del GBC:** Pokémon Cristal, Zelda Oracle of Ages, Shantae,
  Alone in the Dark (portada con imagen de muchos colores) y Metal Gear Solid.
- **Muestra al azar:** 25 juegos de GB y GBC.

Ninguno tuvo diferencias. La cobertura sin traza varía entre 1 % y 99 % según
qué tan indirecto es el código del juego (ver `docs/ARQUITECTURA.md`). Con el
ciclo de trazas, los juegos revisados quedaron entre 97 % y 99.9 %.

**Lista de compatibilidad** ([`docs/compat/index.html`](docs/compat/index.html)):
las 948 ROMs de las colecciones No-Intro de Game Boy y Game Boy Color, cada
una con su captura, estado, MBC, consola y, para una selección por MBC y
consola, el resultado del ciclo completo del recompilador y su velocidad.
934 corren en el intérprete. De las 14 que no, casi todas son de GBC con MBC5
y se quedan en blanco o ejecutan un opcode ilegal; una usa HuC3, que no está
soportado (issue #55). Las 31 que pasan por el recompilador quedan sin
diferencias.
Se genera con dos scripts de Python 3 sin dependencias, desde la raíz del
repo y con todo compilado con `make`. El motor es el mismo de NESRecomp,
GENRecomp y NEORecomp, `tools/retrorecomp/compat.py`; `run.py` solo dice lo propio del
Game Boy (cabecera, MBC soportados, cupo por MBC):

```
python3 tools/compat/run.py "DIR/Nintendo - Game Boy" "DIR/Nintendo - Game Boy Color"
python3 tools/compat/report.py     # escribe docs/compat/index.html
```

- Etapa 1: corre cada ROM en el intérprete (`gbshot`) con entradas
  guionadas y toma capturas en los cuadros 450, 900 y 1500; marca las que no
  cargan, detienen el CPU (opcode ilegal) o quedan en blanco. Etapa 2: para
  las ROMs elegidas por MBC (variando consola, tamaños y batería), `gbrecomp`,
  compilar, `difftest` con dos semillas y una segunda ronda con lo que cayó al
  intérprete (`--uncovered` → `--trace`), más `difftest --bench`.
- Opciones: `--solo etapa1|etapa2`, `--etapa2 N` (cuántas ROMs pasan a la
  etapa 2, por turnos entre MBC), `--lista ARCHIVO`, `--juego TEXTO` (solo
  esos juegos, y todos pasan a la etapa 2), `--categoria MBC5,MBC3`,
  `--hilos N`, `--salida DIR` y `--rehacer`.
- Los resultados quedan en `docs/compat/results.json` (se guardan después de
  cada ROM: si la corrida se corta, la misma orden sigue donde iba) y las
  capturas en `docs/compat/thumbs/`; las ROMs extraídas y los proyectos, en
  `build/compat/` (los proyectos sin diferencias se borran).

La integración continua compila, corre la batería y recompila una ROM de
prueba en Linux, macOS y Windows. En Linux y Windows además se jugaron los
proyectos generados; en macOS todavía no.

## Límites

- **Descubrimiento:** recompilar estáticamente el 100 % de una ROM arbitraria
  es indecidible. Lo que no se descubre siempre corre bien, en el intérprete;
  solo es más lento.
- **No emulados:** el sonido y los programas de la SNES del Super Game Boy,
  ni su borde de fábrica (está en la ROM de la SNES). Tampoco las rarezas del
  canal de onda del DMG.
- **PPU:** seis casos de Mealybug con cambios de LCDC o de la ventana a mitad
  de línea.
- **Game Boy Color:** faltan algunos detalles finos de la HDMA. La doble
  velocidad se verificó con Shantae, Warlocked y Street Fighter Alpha.
- La lista de pendientes está en los *issues* del repositorio.

## Aspectos legales

GBRecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GENRecomp, NEORecomp y
retrorecomp. La licencia cubre el código del recompilador y del runtime, no
las ROMs ni el código que se genera a partir de ellas.

GBRecomp no incluye ROMs ni datos de ninguna. El código que genera es una
traducción del programa de la ROM, o sea una obra derivada del juego: sirve
para jugar tu propia copia, pero no distribuyas el proyecto ni el ejecutable
de un juego comercial.

Las paletas del modo de compatibilidad del GBC (`src/runtime/compat.c`)
vienen de la boot ROM libre de [SameBoy](https://github.com/LIJI32/SameBoy),
con licencia MIT; el aviso está en ese archivo.

Los íconos de los juegos recompilados (ventana y `.exe` de Windows) son de
[Retro game console icons](https://github.com/KyleBing/retro-game-console-icons),
de KyleBing, con licencia
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/): recortados,
centrados y escalados por `tools/iconos.py`. Los originales y el detalle de
los cambios están en [`assets/iconos/`](assets/iconos/LEEME.md). Los proyectos
generados llevan el crédito en su `LEEME.txt` y en Ayuda > Acerca de.

## Documentación

[`docs/ARQUITECTURA.md`](docs/ARQUITECTURA.md) explica el diseño: emisor
único del SM83, temporización, despachador, heurísticas de descubrimiento,
bancos, PPU, Game Boy Color, Super Game Boy y el estado de la validación.
