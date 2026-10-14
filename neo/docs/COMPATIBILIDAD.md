# Compatibilidad

Registro de pruebas de NEORecomp con los 8 juegos de `roms/` (Neo Geo AES, con la BIOS de exportación
`neo-epo.bin`). El resultado completo, con una miniatura de cada juego, está en
[`compat/index.html`](compat/index.html) (datos crudos en [`compat/results.json`](compat/results.json)). Los
juegos y la BIOS no se versionan.

## Resumen (12 de octubre de 2026)

| | |
|---|---|
| Juegos probados | 8: Metal Slug, Metal Slug 2, Samurai Shodown II, Neo Turf Masters, KOF '97 (MVS), KOF '98 (AES, de un zip merged), The Last Blade y Shock Troopers (solo MVS); de 2 a 5 MB de programa, 5 con bancos de P2 |
| Corren en el intérprete (etapa 1) | 8 de 8 |
| Recompilados sin traza y comparados contra el intérprete (etapa 2) | **8 de 8 sin diferencias** (dos semillas de 1800 cuadros, y una segunda ronda con lo que cayó al intérprete) |
| Ciclos en código recompilado (peor semilla) | 99,1 % a 100 %; 100 % en 5. KOF '98 se quedaba en 99,1 % por un bucle de espera que corre desde la RAM; ahora ese código se aprende de la traza (ver «Código que el juego copia a RAM» en `ARQUITECTURA.md`) y el ciclo de `tests/recomp_game.sh` (3000 cuadros, semilla 1) llega al 100 %, sin diferencias en los 8 juegos |
| Velocidad recompilada (`difftest --bench`, 1800 cuadros) | 42× (Neo Turf Masters) a 78× (KOF '97) el tiempo real, 58× de media; el intérprete, de 27× a 35× |
| Compilar el proyecto generado (`make -j8`) | 31 s (Shock Troopers) a 122 s (KOF '98) |
| Parecido con Geolith (cuadros 1200 y 2400) | RAM del 68000: 99,6 % a 100 % (95,6 % en The Last Blade en el 2400); imagen: 99 % o más en 11 de los 16 cuadros (idéntica en 7) |
| Parecido con FBNeo | RAM: 78 % a 95 % (FBNeo corre a 59,18 cuadros por segundo, el reloj del MVS, y se va corriendo); imagen: 89 % a 100 % |

De las imágenes que no llegan al 99 %, dos difieren poco (Metal Slug 2 y Shock Troopers en el cuadro 2400,
98,6-98,8 %: no se miraron) y tres mucho: Samurai Shodown II (94,7 % y 78,4 %) y The Last Blade (16,6 %). En esos
dos, NEORecomp y FBNeo coinciden entre ellos (100 % y 89,5 % en el cuadro 2400) y Geolith no: en The Last Blade
eligió otro escenario y otro rival (el azar del juego se separó; su RAM coincide un 95,6 %), y en Samurai
Shodown II muestra otro fondo.

## Cómo se probó

`tools/compat/` (Python 3 sin dependencias, desde la raíz del repo, con `make` hecho y la BIOS en `roms/aes.zip`):

```
python3 tools/compat/run.py [carpeta] [--oraculos] [--etapa2 N] [--hilos N] [--solo etapa1|etapa2]
python3 tools/compat/report.py                                   # docs/compat/index.html
```

El motor es el común de los cuatro recompiladores (`tools/retrorecomp/compat.py`); `tools/compat/run.py` pone lo
propio del Neo Geo: el juego es el zip de MAME entero (o un `.neo`), los zips de las BIOS (`aes.zip`, `neogeo.zip`)
se saltan, los datos salen de `neorecomp --info`, los cifrados o protegidos quedan «sin soporte» y, como la BIOS
del AES tarda unos 20 s en dejar el juego en su demo, las capturas van a los cuadros 1200, 2400 y 3600 y los
guiones pulsan Start y A desde el cuadro 1200.

- **Etapa 1:** el juego en el intérprete (`neoshot`) con entradas guionadas; la miniatura es la captura más
  colorida.
- **Etapa 2:** `neorecomp` sin traza, compilar el proyecto, `difftest` con las semillas 1 y 2 (68000, ciclos,
  RAM, VRAM, LSPC, paleta, banco de P2, tarjeta, Z80 y su RAM, YM2610, mezcla y bus abierto tras cada función;
  imagen y audio al final de cada cuadro) y una segunda ronda recompilando con lo que `difftest --uncovered` vio
  caer al intérprete (KOF '97 y KOF '98 no la necesitaron). Al final, de un juego a la vez, la velocidad.
- **Oráculos** (`--oraculos`): la RAM del 68000 y la imagen en los cuadros 1200 y 2400 contra Geolith y FBNeo
  (`tools/oracles/oracle.py`, en modo AES); la imagen se compara en el área común (Geolith da 320 × 240 con los
  bordes, FBNeo 304 × 224).

## Con la BIOS propia

Los mismos 8 juegos con la BIOS propia (`make neobios`, fase 10) en lugar de `neo-epo.bin`
(`NEO_BIOS=build/neobios.bin python3 tools/compat/run.py --salida build/compat-neobios`; el informe en
[`compat-neobios/index.html`](compat-neobios/index.html)): **8 de 8 corren y salen recompilados sin
diferencias**, con el 99,2-100 % de los ciclos en código recompilado y de 49× a 102× el tiempo real (más rápido
que con la original: su VBLANK y su `SYSTEM_IO` hacen menos). Todavía no tiene la animación de la pantalla de
SNK.

**La tarjeta de memoria**, juego por juego, con una tarjeta recién formateada (`neorun --cardlog`, 4000
cuadros): los 7 que la usan hacen las mismas llamadas y reciben las mismas respuestas con las dos BIOS, y la
tarjeta queda igual. Metal Slug, Samurai Shodown II, Neo Turf Masters, KOF '97 y KOF '98 cargan su partida al
empezar (no está: `$82`); Metal Slug 2 y The Last Blade buscan; Shock Troopers no la usa. Ninguno guardó en las
corridas guionadas, así que guardar se probó por la interfaz (`tests/neobios/`) y no desde un juego.

## Lo que no se probó todavía

- Los juegos cifrados (NEO-CMC, SMA, PCM2, PVC: casi todos desde 1999) y los protegidos: se detectan y quedan sin
  soporte (el `kof98` de MVS se rechaza y se usa el `kof98h` del mismo zip merged).
- MVS, PAL, el CD.
- Una colección grande: estos 8 son los que había en `roms/`.
