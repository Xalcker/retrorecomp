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
| `src/runtime/` | La consola: `gen.h` (estado y interfaz), `cart.c` (formatos, cabecera, vectores); `sha1`, `console.h`. |
| `src/genrecomp/` | El recompilador. Lleva adentro el runtime y la plataforma (`embed`), así que es un solo ejecutable. |
| `src/platform/` | Por ahora las partes sin SDL (`png`, `files`, `export`, `random_pad.h`). Se copian a cada proyecto generado. |
| `src/tools/` | `carttest`, `embed`. |
| `tests/` | `run_tests.sh` (`make test`), `contract.sh` (contrato común). |
| `tools/` | `retrorecomp/` (la GUI común, submódulo). |

## Una sola fuente de verdad para el CPU

Fase 1: `m68k_ops.h` tendrá la semántica de cada operación y de cada modo de direccionamiento; el
intérprete la usa con valores de tiempo de ejecución y el código generado con constantes.

## Temporización

Fase 1-3: ciclos del 68000 por acceso al bus (4 relojes por acceso) más los internos; VDP y audio
perezosos.

## Interrupciones y despachador

Fase 6.

## Recompilador

```
genrecomp juego.bin [--info]
```

### Identidad de las instrucciones

Una instrucción se identificará por su desplazamiento en la ROM normalizada. Sin mapper coincide
con su dirección (`$000000-$3FFFFF`); con el de SSF2, la dirección se traduce con los 8 bancos de 512 KB.

### Descubrimiento

Fase 6. Las semillas son los 64 vectores de `$000000` (`cart_vector`), que `genrecomp --info` ya
muestra.

## Bancos, mappers y guardado

### Cartucho

`gen_load` acepta `.bin`, `.md` y `.gen` (la imagen tal como la ve el 68000) y `.smd` (512 bytes de
cabecera del copiador y cada bloque de 16 KB con los bytes impares primero y los pares después). Un
`.smd` se reconoce por la marca `$AA $BB` de su cabecera o porque tras deshacer el entrelazado
aparece `SEGA` en `$100`; también se acepta un `.bin` con cabecera de copiador. Todo se normaliza a
la imagen del 68000 (tamaño par), y el **SHA1 se calcula sobre la imagen normalizada**: la misma ROM
en `.bin` o en `.smd` da el mismo SHA1.

`cart_parse_header` lee la cabecera de `$100-$1FF` (sistema, nombres, serie, suma, periféricos,
rangos, región) y la RAM del cartucho declarada con `RA` en `$1B0`. `cart_checksum` suma las
palabras de `$200` al final, como la rutina de comprobación de los juegos.

## Video y audio

Fases 3 y 4.

## Variantes de la consola y accesorios

Solo NTSC al principio; mandos de 3 y 6 botones en la fase 2.

## Interfaz del ejecutable

Fase 3.

## Empaquetado y pruebas

- **`make dist`:** deja `dist/genrecomp-<versión>-<sistema>.tar.gz` con `genrecomp`, el README, la
  licencia y este documento. La versión sale de `git describe`.
- **Integración continua** (`.github/workflows/ci.yml`, el mismo flujo que en NESRecomp y GBRecomp),
  en Linux, macOS y Windows: `make`, `make testroms`, `sh tests/contract.sh`, `make test` y `make dist`.
- **`make test`:** `carttest` (la misma ROM sintética como `.bin`, `.smd` con y sin marca y `.bin`
  con cabecera de copiador debe dar la misma imagen, SHA1 y cabecera) y `genrecomp --info` con cada
  ROM de `roms/`.
