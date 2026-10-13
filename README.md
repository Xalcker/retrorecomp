# retrorecomp

Una sola interfaz local para los recompiladores estáticos de esta serie:
[NESRecomp](https://github.com/Xalcker/NESRecomp) (NES), [GBRecomp](https://github.com/Xalcker/GBRecomp)
(Game Boy, Game Boy Color y Super Game Boy) y [GENRecomp](https://github.com/Xalcker/GENRecomp) (Sega Genesis).
Detecta el sistema de cada ROM por su cabecera y llama a las herramientas del recompilador que corresponda;
cada uno sigue siendo su propio repositorio.

```
python3 retrorecomp_gui.py            # abre el navegador; solo Python 3, sin dependencias
```

Busca los repositorios en `--nes DIR` / `--gb DIR` / `--gen DIR`, en `NESRECOMP_DIR` / `GBRECOMP_DIR` / `GENRECOMP_DIR`,
en la carpeta que contiene a este (si va como submódulo de uno de ellos) o en las hermanas `NESRecomp`, `GBRecomp` y
`GENRecomp` (si es submódulo de uno, los otros se buscan junto a ese). Los que no encuentre no aparecen.
Como submódulo: `git submodule update --init` y `python3 tools/retrorecomp/retrorecomp_gui.py`. Las ROMs salen
de la carpeta `roms/` de cada uno y de las que se pasen con `--roms DIR`. Compila las herramientas desde la
propia interfaz («Compilar herramientas»), o con `make` en cada repositorio.

En Windows, `make` y `sh` (compilar, pruebas, ciclo completo, compilar el proyecto generado) corren dentro de MSYS2
UCRT64, como se compila a mano: la GUI los lanza con su `bash` (`MSYSTEM=UCRT64`) y pasa las rutas como `C:/...`.
Busca MSYS2 en `--msys DIR`, en `MSYS2_ROOT` o en `C:msys64`, y al arrancar dice cuál usa. Los ejecutables de los
recompiladores y `git` se lanzan directo.

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

| Herramienta (NES / GB / Genesis) | Uso |
|---|---|
| `nestrace` / `gbtrace` / `gentrace` | `rom traza [--frames N] [--input archivo] [--seed S]` |
| `nesshot` / `gbshot` / `genshot` | `rom salida.png [--frames N] [--input archivo] [--seed S]`; si el CPU se detiene (opcode KIL / ilegal, doble falta) escribe `CPU detenido en el cuadro N (motivo)` |
| `nesaudio` / `gbaudio` / `genaudio` | `rom salida.wav [--frames N] [--input archivo] [--seed S]` |
| `nesbench` / `gbbench` / `genbench` | `rom [--frames N] [--repeat N] [--input archivo] [--seed S]` (sin entradas: Start y luego la derecha) |
| `nesramdump` / `gbramdump` / `genramdump` | `rom [--frames N] [--input archivo] [--seed S]` (RAM de trabajo a la salida estándar) |
| `difftest` (cada proyecto) | `rom [--frames N] [--input archivo] [--seed S] [--uncovered archivo] [--screens carpeta [--every N]] [--bench] [--executed archivo]` (sin `--input` ni `--seed` no pulsa nada; el nombre es el mismo en los tres porque va dentro del proyecto generado, que ya es de una consola) |
| `nesrecomp` / `gbrecomp` / `genrecomp` | `rom -o carpeta --name nombre [--trace archivo]...`, más `--viz` (`--color`), `--events`, `--why`, `--coverage`, `--html archivo`, `--codemap archivo` (sin `-o`, sin proyecto) |

Cada recompilador trae `sh tests/contract.sh`, la misma comprobación sobre sus propias herramientas, y su CI la corre
en cada PR. (Lo hacen por separado porque, con repos privados, un CI no puede leer el otro repositorio sin un
token.) Cuando la tabla de arriba cambie, actualiza `tests/contract.sh` (el mismo archivo en los tres) y `tests/contract.py` de este repo.

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

Los resultados (`docs/compat/results.json` y las miniaturas) se guardan después de cada ROM; la misma orden sigue
donde iba y salta las ROMs que ya tienen resultado del mismo commit del recompilador (`--rehacer` las repite).

## Diferencias que la interfaz esconde

| | NES | Game Boy | Genesis |
|---|---|---|---|
| Filas del mapa | 16 KB de PRG (`--viz --color`) | un banco (`--events`) | 16 KB de ROM (`--viz --color`) |
| Banco de la traza (`BB:AAAA`) | 8 KB del PRG | 16 KB (el banco del MBC) | 512 KB de la ROM (las ventanas de SSF2) |
| Jugar y grabar traza | `nes` (intérprete) con `NES_TRACE` | `gb` (intérprete) o el recompilado, con `--uncovered` | `gen` (intérprete) con `--trace` |
| Visor | `--html`: página sola con el mapa del PRG | `--html`: `tools/visor.html` con los eventos adentro | `--html`: página sola con el mapa de la ROM |
| Extras | segunda columna (jugador 2), Zapper en `nesshot` | `--model dmg/cgb/sgb` | segunda columna (jugador 2), botones C X Y Z MODE (sin SELECT) |

## Licencia

retrorecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp, GBRecomp y GENRecomp.
