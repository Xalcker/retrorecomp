# retrorecomp

Una sola interfaz local para los recompiladores estáticos de esta serie:
[NESRecomp](https://github.com/Xalcker/NESRecomp) (NES), [GBRecomp](https://github.com/Xalcker/GBRecomp)
(Game Boy, Game Boy Color y Super Game Boy), [GENRecomp](https://github.com/Xalcker/GENRecomp) (Sega Genesis) y
[NEORecomp](https://github.com/Xalcker/NEORecomp) (Neo Geo AES).
Detecta el sistema de cada ROM por su cabecera y llama a las herramientas del recompilador que corresponda;
cada uno sigue siendo su propio repositorio.

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

Busca los repositorios en `--nes DIR` / `--gb DIR` / `--gen DIR` / `--neo DIR`, en `NESRECOMP_DIR` / `GBRECOMP_DIR` /
`GENRECOMP_DIR` / `NEORECOMP_DIR`, en la carpeta que contiene a este (si va como submódulo de uno de ellos) o en las
hermanas `NESRecomp`, `GBRecomp`, `GENRecomp` y `NEORecomp` (si es submódulo de uno, los otros se buscan junto a ese). Los que no encuentre no aparecen.
Como submódulo: `git submodule update --init` y `python3 tools/retrorecomp/retrorecomp_gui.py`. Las ROMs salen
de la carpeta `roms/` de cada uno y de las que se pasen con `--roms DIR`. Compila las herramientas desde la
propia interfaz («Compilar herramientas»), o con `make` en cada repositorio.

En Windows, `make` y `sh` (compilar, pruebas, ciclo completo, compilar el proyecto generado) corren dentro de MSYS2
UCRT64, como se compila a mano: la GUI los lanza con su `bash` (`MSYSTEM=UCRT64`) y pasa las rutas como `C:/...`.
Busca MSYS2 en `--msys DIR`, en `MSYS2_ROOT` o en `C:msys64`, y al arrancar dice cuál usa. Los ejecutables de los
recompiladores y `git` se lanzan directo.

## Modos

El interruptor de la barra superior elige entre dos modos (se recuerda en el navegador):

- **Fácil** (por omisión): lista de ROMs y un solo botón, «Jugar». Si falta algo, primero lo prepara solo
  (compila las herramientas, recompila la ROM y compila el proyecto, con las trazas que ya existan) y luego abre
  la versión recompilada nativa. «Mostrar detalles» muestra el registro del trabajo.
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
- **Trazas:** grabar sin ventana (`nestrace` / `gbtrace` / `gentrace`) con un guion de entradas o una semilla, jugar grabando, con
  `difftest --uncovered`, unir y borrar; las marcadas se suman al análisis. Captura PNG y audio WAV.
- **Entradas:** editor de guiones `--input` / `--play` / `--record` (`cuadro BOTONES`), con plantillas y las del
  repositorio (`tests/inputs`).
- **Recompilar y probar:** recompilar y compilar el proyecto generado, `difftest`, `bench` del intérprete y del
  recompilado, y el ciclo completo (`tests/recomp_game.sh`).
- **Repos:** sincronizar (`git fetch` y fast-forward) y compilar cada uno, y la salida de cada trabajo en vivo.

Escucha solo en `127.0.0.1` con un token en la URL, valida que las ROMs estén en las carpetas configuradas y arma
cada comando como una lista de argumentos (sin shell).

## El contrato

La interfaz no conoce el interior de los recompiladores, solo esta línea de comandos común:

| Herramienta (NES / GB / Genesis / Neo Geo) | Uso |
|---|---|
| `nestrace` / `gbtrace` / `gentrace` / `neotrace` | `rom traza [--frames N] [--input archivo] [--seed S]` |
| `nesshot` / `gbshot` / `genshot` / `neoshot` | `rom salida.png [--frames N] [--input archivo] [--seed S]`; si el CPU se detiene (opcode KIL / ilegal, doble falta) escribe `CPU detenido en el cuadro N (motivo)` |
| `nesaudio` / `gbaudio` / `genaudio` / `neoaudio` | `rom salida.wav [--frames N] [--input archivo] [--seed S]` |
| `nesbench` / `gbbench` / `genbench` / `neobench` | `rom [--frames N] [--repeat N] [--input archivo] [--seed S]` (sin entradas: Start y luego la derecha) |
| `nesramdump` / `gbramdump` / `genramdump` / `neoramdump` | `rom [--frames N] [--input archivo] [--seed S]` (RAM de trabajo a la salida estándar) |
| `difftest` (cada proyecto) | `rom [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--screens carpeta [--every N]] [--bench] [--executed archivo]` (sin `--input` ni `--seed` no pulsa nada; el nombre es el mismo en todos porque va dentro del proyecto generado, que ya es de una consola) |
| `nesrecomp` / `gbrecomp` / `genrecomp` / `neorecomp` | `rom -o carpeta --name nombre [--trace archivo]...`, más `--viz` (`--color`), `--events`, `--why`, `--coverage`, `--html archivo`, `--codemap archivo` (sin `-o`, sin proyecto) |

Cada recompilador trae `sh tests/contract.sh`, la misma comprobación sobre sus propias herramientas, y su CI la corre
en cada PR. (Lo hacen por separado porque, con repos privados, un CI no puede leer el otro repositorio sin un
token.) Cuando la tabla de arriba cambie, actualiza `shared/contract.sh` y `tests/contract.py` de este repo.

**Código compartido (`shared/`).** Lo que antes se copiaba idéntico en los cuatro recompiladores vive aquí y cada uno lo toma por el submódulo `tools/retrorecomp`: `random_pad.h` (el mando aleatorio de `--seed`), `trace_line.h` (el formato `BANCO:DIRECCION`), `sha1.c` y `sha1.h`, la interfaz de la ventana (`ui_menu`, `ui_font`), `png` y `files.h`, `contract.sh` y `recomp_game.sh`. Los repos compilan con `-Itools/retrorecomp/shared` y embeben esos headers en los proyectos que generan; `tests/contract.sh` y `tests/recomp_game.sh` son envoltorios idénticos que entran a la raíz del repo y llaman a `shared/`. Por eso compilar un recompilador exige el submódulo (`git clone --recursive`, o `git submodule update --init`); el ZIP de GitHub no lo trae. `tests/contract.py` comprueba que los envoltorios sean idénticos y que nadie conserve una copia vieja de los headers.

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

`python3 tests/contract.py` comprueba (sin ROMs) que las herramientas compiladas de los tres mencionan esas opciones y que el código compartido (`SHARED`) sea idéntico,
mirando todos los repositorios que encuentre. Si cambias una opción en un recompilador, cámbiala también en los otros y aquí.

## Prueba de compatibilidad

`compat.py` es el motor común de la prueba de compatibilidad de los tres (solo la biblioteca estándar). Cada
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

## Diferencias que la interfaz esconde

| | NES | Game Boy | Genesis | Neo Geo |
|---|---|---|---|---|
| El juego | `.nes` | `.gb`, `.gbc`, `.sgb` | `.md`, `.gen`, `.bin`, `.smd` | zip de MAME (entero: muchos chips) o `.neo`; necesita la BIOS del AES |
| Filas del mapa | 16 KB de PRG (`--viz --color`) | un banco (`--events`) | 16 KB de ROM (`--viz --color`) | 64 celdas del programa P (`--viz --color`): de 256 bytes hasta 1 MB, más grandes después |
| Banco de la traza (`BB:AAAA`) | 8 KB del PRG | 16 KB (el banco del MBC) | 512 KB de la ROM (las ventanas de SSF2) | 1 MB de P (P1 y cada banco de P2); `FF` la BIOS |
| Jugar y grabar traza | `nes` (intérprete) con `NES_TRACE` | `gb` (intérprete) o el recompilado, con `--uncovered` | `gen` (intérprete) con `--trace` | `neo` (intérprete) con `--trace` |
| Visor | `--html`: página sola con el mapa del PRG | `--html`: `tools/visor.html` con los eventos adentro | `--html`: página sola con el mapa de la ROM | `--html`: página sola con el mapa de P |
| Extras | segunda columna (jugador 2), Zapper en `nesshot` | `--model dmg/cgb/sgb` | segunda columna (jugador 2), botones C X Y Z MODE (sin SELECT) | segunda columna (jugador 2), botones C y D; la BIOS tarda unos 20 s en dejar el juego en su demo |

## Licencia

retrorecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp, GENRecomp y NEORecomp.
