# retrorecomp

Una sola interfaz local para los recompiladores estáticos de Nintendo de esta serie:
[NESRecomp](https://github.com/Xalcker/NESRecomp) (NES) y [GBRecomp](https://github.com/Xalcker/GBRecomp)
(Game Boy, Game Boy Color y Super Game Boy). Detecta el sistema de cada ROM por su cabecera y llama a las
herramientas del recompilador que corresponda; cada uno sigue siendo su propio repositorio.

```
python3 retrorecomp_gui.py            # abre el navegador; solo Python 3, sin dependencias
```

Busca los repositorios en `--nes DIR` / `--gb DIR`, en `NESRECOMP_DIR` / `GBRECOMP_DIR`, en la carpeta que
contiene a este (si va como submódulo de uno de ellos) o en las hermanas `NESRecomp` y `GBRecomp` (si es submódulo de uno, el otro se busca junto a ese).
Como submódulo: `git submodule update --init` y `python3 tools/retrorecomp/retrorecomp_gui.py`. Las ROMs salen
de la carpeta `roms/` de cada uno y de las que se pasen con `--roms DIR`. Compila las herramientas desde la
propia interfaz («Compilar herramientas»), o con `make` en cada repositorio.

## Qué hace

- **Avance por ROM:** análisis estático, cobertura, trazas, proyecto generado, compilado, `difftest` y velocidad.
- **Mapa del descubrimiento** (como `--viz`): una fila por banco de 16 KB, celdas de 256 bytes coloreadas por cómo se
  halló el código; en rojo lo que el juego ejecutó y no se halló. Clic en una celda lista sus instrucciones y clic en
  una instrucción muestra la cadena de «por qué» (`--why`).
- **Trazas:** grabar sin ventana (`nestrace` / `gbtrace`) con un guion de entradas o una semilla, jugar grabando, con
  `difftest --uncovered`, unir y borrar; las marcadas se suman al análisis. Captura PNG y audio WAV.
- **Entradas:** editor de guiones `--input` / `--play` / `--record` (`cuadro BOTONES`), con plantillas y las del
  repositorio (`tests/inputs`).
- **Recompilar y probar:** recompilar y compilar el proyecto generado, `difftest`, `bench` del intérprete y del
  recompilado, y el ciclo completo (`tests/recomp_game.sh`).
- **Repos:** sincronizar (`git fetch` y fast-forward) y compilar los dos, y la salida de cada trabajo en vivo.

Escucha solo en `127.0.0.1` con un token en la URL, valida que las ROMs estén en las carpetas configuradas y arma
cada comando como una lista de argumentos (sin shell).

## El contrato

La interfaz no conoce el interior de los recompiladores, solo esta línea de comandos común:

| Herramienta (NES / GB) | Uso |
|---|---|
| `nestrace` / `gbtrace` | `rom traza [--frames N] [--input archivo] [--seed S]` |
| `nesshot` / `gbshot` | `rom salida.png [--frames N] [--input archivo] [--seed S]` |
| `nesaudio` / `gbaudio` | `rom salida.wav [--frames N] [--input archivo] [--seed S]` |
| `nesbench` / `gbbench` | `rom [--frames N] [--repeat N] [--input archivo]` |
| `difftest` (cada proyecto) | `rom [--frames N] [--input archivo] [--seed S] [--uncovered archivo]` |
| `nesrecomp` / `gbrecomp` | `rom -o carpeta --name nombre [--trace archivo]...`, más `--viz`, `--events`, `--why`, `--coverage` |

Cada recompilador trae `sh tests/contract.sh`, la misma comprobación sobre sus propias herramientas, y su CI la corre
en cada PR. (Lo hacen por separado porque, con repos privados, un CI no puede leer el otro repositorio sin un
token.) Cuando la tabla de arriba cambie, actualiza las listas de `tests/contract.sh` en los dos y la de este repo.

Los dos se compilan igual: mismos paquetes (MSYS2 UCRT64 en Windows), el mismo bloque común al principio del
`Makefile` y los mismos objetivos, `make`, `make testroms` (ROMs de prueba libres), `make test` y `make dist`. El
`.github/workflows/ci.yml` también es el mismo en los dos y corre esos pasos en Linux, macOS y Windows. Lo que cambia
entre consolas queda dentro de esos objetivos.

`python3 tests/contract.py` comprueba (sin ROMs) que las herramientas compiladas de los dos mencionan esas opciones, mirando los dos repositorios a la vez.
Si cambias una opción en un recompilador, cámbiala también en el otro y aquí.

## Diferencias que la interfaz esconde

| | NES | Game Boy |
|---|---|---|
| Filas del mapa | 16 KB de PRG (`--viz --color`) | un banco (`--events`) |
| Dirección de «por qué» | desplazamiento en PRG | `BB:AAAA` |
| Proyecto generado | `build/out/NOMBRE` | `out/NOMBRE` |
| Jugar y grabar traza | `nes` con `NES_TRACE` | el recompilado con `--uncovered` |
| Visor | HTML (`--html`) | `tools/visor.html` con los eventos |
| Extras | segunda columna (jugador 2), Zapper en `nesshot` | `--model dmg/cgb/sgb` |

## Licencia

retrorecomp se distribuye con licencia [MIT](LICENSE), la misma que NESRecomp y GBRecomp.
