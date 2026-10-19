# Trazas: el almacén por SHA1 y las trazas compartidas

Una traza dice qué instrucciones de la ROM ejecutó un juego: una línea `BANCO:DIRECCION` por instrucción
(`shared/trace_line.h`) y, para el código que corre desde la RAM, líneas `ram DIRECCION B0 B1 ...`
(`shared/ram_code.h`). Los recompiladores las usan como puntos de entrada (`--trace`) para encontrar el código que el
análisis estático no ve, como los saltos por tablas o los bancos que se eligen con un número calculado. Una traza no lleva
nada de la ROM, solo desplazamientos y direcciones.

## El almacén por SHA1

Es una carpeta con las trazas de cada juego, identificado por el SHA1 de su ROM:

```
DIR/<sistema>/<SHA1>/*.trace      (sistema: nes, gb, gen, neo o sms, como en shared/sistemas.json)
```

- **Leer.** `*recomp --traces-dir DIR`, o la variable `RETROCOMP_TRAZAS=DIR`, suma todas las trazas de ese juego como
  si fueran `--trace`. Se pueden dar varias carpetas separadas como en `PATH` (`;` en Windows, `:` en los demás); se
  leen todas y lo que se escribe va a la primera.
- **Guardar.** `python3 ../shared/trazas.py agregar ROM TRAZA...` (desde la raíz del sistema) junta trazas en
  `acumulada.trace`, sin repetir. `recomp_game.py` lo hace solo si el ciclo termina sin diferencias, y
  `compat.py --almacen DIR` también, ROM por ROM.
- **Sondear.** `python3 ../shared/trazas.py sondear ROM` corre el juego en el intérprete (`*trace`) con las entradas
  de `tests/inputs/ciclo_traza.txt` y deja `sondeo.trace`. En SMS, `smsrecomp --probe N` hace lo mismo sin salir del
  recompilador.
- **El hueco de `--estricto`.** `difftest --estricto --uncovered F` se detiene en la primera instrucción de la ROM que
  no está recompilada y deja en `F` lo que el intérprete ejecutó hasta ahí, esa instrucción incluida. Esa traza va al
  almacén con `trazas.py agregar`.
- **La GUI** usa `RETROCOMP_TRAZAS` o `build/gui/almacen`, más `../recomp-trazas` si existe. Al jugar el recompilado
  con «grabar traza», graba con `--uncovered` lo que tuvo que pasarle al intérprete, y «Preparar para compartir» junta
  las trazas marcadas en una aportación (`build/gui/compartir/`, con su `.zip`). No sube nada.

## Compartir trazas (Xalcker/retrorecomp#16)

Las trazas de varias personas se suman: cada una que juega un juego encuentra código que las otras no tocaron. La
propuesta es un repositorio de trazas compartidas con la misma estructura que el almacén, de modo que un clon sirve
directamente como una carpeta más de `RETROCOMP_TRAZAS`:

```
recomp-trazas/<sistema>/<SHA1>/AAAA-MM-DD-<alias o huella>.trace
recomp-trazas/<sistema>/<SHA1>/AAAA-MM-DD-<alias o huella>.txt     (opcional: las entradas grabadas con --record)
```

**Aportar.** Desde la raíz del sistema:

```
python3 ../shared/trazas.py compartir --dir ../../recomp-trazas [--alias nombre] [--entradas partida.txt] ROM TRAZA...
```

Junta las trazas, les quita los conteos y las repetidas, y escribe un archivo nuevo por aportación. Sin `--alias` el
archivo es anónimo: su nombre lleva una huella del contenido en lugar de quién lo hizo. Las entradas grabadas sirven para
volver a generar la traza si cambia el runtime, y como prueba de regresión con `difftest --input`.

**Validar.** `python3 ../shared/trazas.py validar CARPETA` comprueba lo que comprobaría el CI del repositorio:

- cada carpeta es `<sistema>/<SHA1>/`, con un sistema del registro y el SHA1 en minúsculas;
- solo hay archivos `.trace` y `.txt` (nada de ROMs) y ninguno pasa de 8 MB;
- cada línea de un `.trace` es una línea de traza.

Un CI mínimo para ese repositorio, con GitHub Actions:

```yaml
on: [push, pull_request]
jobs:
  validar:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/checkout@v4
        with: { repository: Xalcker/retrorecomp, path: .retrorecomp }
      - run: python3 .retrorecomp/shared/trazas.py validar .
```

### Lo que falta decidir

El código sirve para cualquiera de estas opciones; falta elegir:

- **Dónde viven:** un repositorio aparte (`recomp-trazas`, el recomendado en el issue) o una carpeta en este.
- **Público o privado:** las trazas no tienen nada de la ROM, pero dicen qué juegos se probaron.
- **Alias o anónimas:** `compartir` hace las dos cosas (`--alias`, o la huella si no se da).
