# retrorecomp: plan para ordenar los cinco recompiladores

## Estado (16 de octubre de 2026)

| Fase | Estado | PR |
|---|---|---|
| F0. Decidir | Hecho: monorepo en `retrorecomp`, carpetas `gb/ nes/ gen/ neo/ sms/`, web y Android fuera (tag `web-android-final`), CI por niveles | — |
| F1. Monorepo | Hecho | #66 (merge commit) |
| F2. Medir bien | Hecho salvo F2.7 | #131–#139 |
| F2.7. Volver a correr compat en los cinco (por primera vez en SMS); después borrar `migrar()` y `neo/docs/compat-neobios` | **Pendiente** | — |
| F3.1. Registro de sistemas (`shared/sistemas.json`) | Hecho | #140 |
| F3.2. Nombres en `shared/` | Hecho | #144 |
| F3.3. Opciones (`--profile`, `--input-line`, `--screens`, `--shot`, `--model` en `cli.h`) | Hecho | #145 |
| F3.4. Llenar huecos por rol | Hecho salvo el Z80 de GEN y NEO en `--why`, `--events` y `--html` | (este PR) |
| F3.5. Plantillas comunes | **Pendiente** | — |
| F3.6. GB converge (`cli.h`, `inputs.h`, `ram_code.h`, visor común) | Hecho | #142 |
| F3.7. Línea `RESUMEN {json}` | Hecho | #141 |
| Scripts a Python, fase 1: los de `shared/` (`golden`, `estricto`, `sst`, `recomp_game`, `contrato`, `ui_golden`) y los `tests/golden.py` y `tests/estricto.py` de cada sistema | Hecho | #148 |
| Scripts a Python, fase 2: los `tests/run_tests.sh` de cada sistema pasan a `run_tests.py`, con el marco común `shared/pruebas.py` | Hecho | #149–#153 |
| Scripts a Python, fase 3: los scripts sueltos (`testroms.sh`, `batch_roms.sh`, `coverage.sh`...) | **Pendiente** | — |
| F4. Menos intérprete | **Pendiente** | — |
| F5. Juegos sin intérprete | **Pendiente** | — |

Desde F1, las rutas de este documento que hablan de los repos viejos (`GBRecomp`, `tools/retrorecomp`, el submódulo) describen
cómo era antes; el diagnóstico de la sección 1 es una foto del 15 de octubre y no se actualiza.

Se basa en el `main` de los seis repos el 15 de octubre de 2026, justo después de SMSRecomp#7. Las cifras salen de un mapeo de los seis repos hecho en copias; no se tocó ningún checkout ni nada en GitHub. Lo que no se pudo verificar está marcado **(sin verificar)**.

---

## 1. Diagnóstico

### 1.1 El submódulo es la fuente principal de fricción
- En 6 días, **99 fusiones movieron `tools/retrorecomp`**. El **84 %** traía código en el mismo PR, así que un bot de bumps no las habría resuelto.
- **26 cambios lógicos tocaron 2 o más repos**, con **4,4 PR de media** cada uno; retrorecomp#59 solo, 17. Entre el 10 y el 15 de octubre, el 46 % de los PR de los recompiladores movió el submódulo.
- **36 PR (38 %)** necesitaron un commit extra para volver a fijar el submódulo, porque el squash de retrorecomp cambia el SHA.
- **GB y NES siguen en c37892e**, 5 commits atrás de GEN, NEO y SMS (378062c). Es solo deriva: los dos compilan y pasan `make test` contra 378062c.
- **Dependabot (gitsubmodule) corrió 17 veces sin abrir un PR.**
- **La GUI cambia según el repo desde el que se lanza**: desde GB o NES no aparece SMSRecomp.
- **`tests/contract.py` no corre en ningún CI.** Sin repos imprime «190 revisados, 0 fallas» y sale con 0.
- **Muchos archivos deben ser idénticos byte a byte en 5 repos**: envoltorios, `report.py`, `ci.yml`, `dependabot.yml`. Corregir una coma en un comentario cuesta 5 PR.

### 1.2 El CI no es lento por las SingleStepTests
| Qué | Costo por corrida |
|---|---|
| Web + APK | 33–62 % del job de Linux en los cinco (≈2:20 cada repo) |
| ROM «mínima» de SMS (32 KB de ceros, que se recompilan como NOP) | 11,5 MB de C; web ≈3 min y APK ≈7 min. Con RETI en $38 y RETN en $66 bajaría a 127 KB. El job de Linux de SMS tarda 15–18 min |
| Proyecto generado de Overdrive (GEN) | 214 s Linux, 313 s Windows, 109 s macOS. Se duplicó desde GEN#49: el driver del Z80 suma 13 514 instrucciones y solo se ejecutan 2 946 |
| ZEXDOC + ZEXALL (SMS), en los 3 sistemas | 169 / 223 / 146 s |
| Caché de testroms (GEN, NEO, SMS) | 22–27 % de las corridas fallan y pierden ≈2:20. GEN y NEO llenan los 10 GB con 21 copias de ≈450 MB |
| SingleStepTests | 39 s GEN, 34 s NEO, 30 s SMS. Es poco, pero corren 3 veces el mismo Z80 y 2 veces el mismo 68000 |

El ruleset «Proteger main» exige los **tres** checks (`build (ubuntu|macos|windows-latest)`), aunque el comentario de `ci.yml` diga que solo se exige el de Ubuntu. Hubo 647 corridas de CI en 6 días, y se llegó al tope de 5 jobs de macOS a la vez, con colas de hasta 10 minutos.

### 1.3 Los golden no prueban lo que importa
- **Solo prueban el intérprete** (`*shot --hash`), nunca el código recompilado.
- **No corren en CI ni en `make test`**: dependen de ROMs comerciales en `roms/`.
- **Las diferencias de audio de GB vienen del punto flotante, no de un cambio de código.** Apple clang en arm64 fusiona multiplicación y suma (FMA, `-ffp-contract=on` por omisión), y la línea base de GB se hizo en Windows. Compilado con `-ffp-contract=off`, los 15 casos que pueden correr coinciden en imagen y audio. Esto está verificado.
- **En el Mac, GEN (26 casos), NEO (9), SMS (21) y NES (16 con línea base) dan iguales.** La nota de #64 dice que GEN no coincide en audio. La explicación probable es la misma al revés: esas líneas base se hicieron en el Mac con FMA y fallarían en Linux o Windows **(sin verificar)**.
- **Cada `golden.sh` tiene su propia semántica.** El de GB falla si falta una ROM, así que nunca pasa fuera de la máquina original. El de NES cuenta los casos «(nuevo)» como iguales. Los formatos difieren.

### 1.4 El intérprete hace dos trabajos distintos
- **(a) Cubrir código no descubierto.** El ciclo de dos rondas (`--uncovered` → `--trace`) ya lo lleva a casi 0: Shinobi GG 62 → 100 %, Battletoads 35 → 100 %, Street Fighter Alpha 87 → 100 %.
- **(b) Deberes que no son instrucciones del juego:**
  - la entrada a interrupciones y STOP en GEN, NEO y SMS;
  - lo que sigue a EI, el bug de HALT y el choque con la DMA en GB.

  Ninguna traza baja ese piso. Comix Zone se queda en 99,3 % con la traza vacía.
- **«% de los ciclos» significa cosas distintas en cada repo.** GB cuenta HALT, SMS lo excluye, y GEN y NEO cuentan STOP e IRQ como intérprete. Rayman marca 41,9 % de ciclos con 99,986 % de instrucciones nativas.
- **Los `results.json` de compat son anteriores** al código en RAM y al Z80 nativo. SMS no tiene compat.
- **No hay modo estricto ni puerta en CI.** Nada falla si un cambio manda más código al intérprete.
- **Descubrimiento estático sin traza:** NEO 99 %, GEN 97,5 %, NES 96,2 %, GB 63 % y SMS 46 %.

### 1.5 Herramientas: casi gemelas, con huecos
- **Los binarios ya siguen `<sys><rol>`** (gbtrace, nesshot, genbench…). Lo que falta es funcional:
  - GB no tiene statetest, inputtest, `*run`, coverage.sh, `--info`, ni `--hash`/`--expect` en gbshot (están en gbtest);
  - NES no tiene `*run` ni `--info`;
  - el Z80 de GEN y NEO no aparece en `--dis`, `--why`, `--events` ni `--html`;
  - GEN y NEO no corren las SST del Z80 por el camino recompilado.
- **El recompilador del 68000 está duplicado.** genrecomp.c y neorecomp.c tienen 66 funciones con el mismo nombre y 1079 líneas iguales, el 61 % de genrecomp.c. GEN se pierde las heurísticas de NEO (97,5 % contra 99 % estático).
- **`*trace`, `*shot`, `*audio` y `*ramdump` difieren solo en 6–30 líneas entre consolas.** difftest, bench y statetest también comparten el mismo esqueleto.
- **Opciones:**
  - hay dos en español (`--perfil`, `--entradas-241`);
  - una misma cosa tiene dos nombres (`--shots` en `*run`, `--screens` en difftest);
  - `--model` se implementa de tres maneras;
  - `smsbench --perfil` se acepta y no hace nada.
- **GB es el gemelo más alejado.** Usa sus propios `cli.h`, `inputs.h`, `ramtrace.h` y visor, y su difftest es el doble de largo.

### 1.6 GUI y contrato
- **Agregar un sistema obliga a editar unos 12 puntos** en `retrorecomp_gui.py` e `index.html`, más `contract.py`, `contract.sh`, `recomp_game.sh`, `compat_test.py` y el README.
- **La GUI lee la salida en español con unas 26 expresiones regulares.**
- **Errores ya reproducidos:**
  - la GUI rechaza las entradas de SMS con PAUSE;
  - si falta un sistema en `PRESETS`, `/api/state` falla y la página no carga;
  - «sincronizar» no actualiza el submódulo;
  - para Neo Geo el ciclo usa 1800 cuadros en la GUI y 3000 en `recomp_game.sh`.

### 1.7 Documentación
- **La causa de fondo es que los textos nombran a los gemelos por número** («los cuatro», «los tres»), así que cada sistema nuevo los deja viejos. Además describen fases hechas en futuro y repiten números de velocidad que no coinciden entre documentos.
- **README de retrorecomp:** unas 12 afirmaciones falsas, por ejemplo «repos privados» (los seis son públicos) o «contract.py comprueba las opciones», y un párrafo de 7420 caracteres.
- **Cada repo tiene entre 10 y 16 problemas por documento:**
  - PLAN.md con el estado de fases viejo;
  - COMPATIBILIDAD.md con datos anteriores a lo que más cambió;
  - el `ci.yml` de GB y NES habla de SingleStepTests que no tienen;
  - el README de NES cita `NES_TRACE`, que nadie lee.

---

## 2. Decisiones recomendadas

### 2.1 Un solo repo: el monorepo dentro de `Xalcker/retrorecomp` (opción D)

**Por qué:** casi todo el trabajo es transversal, y el submódulo convierte cada cambio en 2 a 6 PR en dos pasos. En un monorepo un cambio común es **un PR atómico**. Desaparecen:
- los 99 bumps y los commits para volver a fijar el submódulo;
- los pines desalineados entre GB/NES y el resto;
- los fallos por no haber hecho `git submodule update`;
- las GUIs viejas según el repo desde el que se lanzan;
- los archivos «idénticos por contrato», que pasan a ser un solo archivo.

Además, cada worktree de Claude tiene los cinco sistemas, y las sesiones dejan de editar los checkouts de los repos hermanos (ya hubo un choque registrado en GBRecomp).

**Lo que ya se probó:** el mapeo armó un monorepo de prueba con la historia reescrita a subcarpetas (392 commits, 7,9 MiB).
- Bastaron unas 40 rutas cambiadas.
- Los cinco compilan en 24 s y `contract.py` da 0 fallas.
- La GUI encuentra los cinco.
- El proyecto generado de SMS es **idéntico byte a byte** al del submódulo, porque `embed.c` mete copias de los archivos.

Así que nada cambia para el juego generado ni para los releases. **No se corrió `make test` en el monorepo (sin verificar).**

**Las otras opciones:**
- **B (paraguas con submódulos hacia abajo)** forma un ciclo de submódulos si `shared/` sigue en retrorecomp, y suma PR en vez de restarlos.
- **C1 (bot de bumps y CI cruzado)** solo cubre el 16 % de los bumps, que son puro puntero, y necesita un token con escritura en 5 repos.
- **C2 (subtree)** quita la fricción local, pero los PR por cambio común siguen siendo N.

**Por qué en `retrorecomp` y no en un repo nuevo:** conserva sus issues de plan (#1, #25, #45, #59, #64), sus PR y la GUI. Los cinco repos viejos se **archivan**, sin borrarlos, con un README que apunta al monorepo. Así siguen sirviendo sus 253 PR, los releases v0.1.0 y los commits que fijan esos releases.

**Estructura propuesta:**
```
retrorecomp/
  shared/          como hoy (núcleos 68000/Z80, ventana, utilidades)
  gb/ nes/ gen/ neo/ sms/
                   cada uno con su Makefile, src/, tests/, tools/, docs/
                   SHARED := ../shared
  gui/             retrorecomp_gui.py, index.html
  compat/          compat.py
  tests/           contrato y pruebas comunes (hoy tests/ de retrorecomp)
  docs/            documentación común
  .github/workflows/
```
- Los nombres cortos coinciden con los `sid` que ya usan el contrato y la GUI.
- La prueba usó `GBRecomp/`… y dejó retrorecomp como subcarpeta, que funciona sin tocar la GUI. Con `shared/` en la raíz hace falta cambiar una línea en `find_repo` y en `SHARED`.
- Es cuestión de gusto, pero conviene decidirlo antes de migrar.

**Riesgos y cómo cubrirlos:**

| Riesgo | Mitigación |
|---|---|
| Tags `v0.1.0` que chocan y `git describe` que toma el tag de otro sistema | Tags `gb-v*`, `nes-v*`…, con `git describe --match 'gb-v*'` en cada Makefile |
| Checks obligatorios con el mismo nombre en los cinco | Un solo job agregador `ci` como único check exigido |
| La caché de 10 GB pasa a ser compartida | Los ≈20 GB de hoy son copias por PR de las testroms; guardar solo desde main y un solo caché de vectores SST lo baja a ≈1–2 GB **(sin verificar)** |
| Los «(#N)» de los commits quedan ambiguos | `git filter-repo --message-callback` que reescriba `(#N)` a `(Xalcker/GBRecomp#N)` |
| Los commits viejos no compilan (gitlinks huérfanos) | Para bisecar historia vieja, usar los repos archivados |
| Un PR roto alcanza a los cinco | El CI por sistema afectado (2.3) lo detecta antes de fusionar |

### 2.2 Web y Android: fuera del proyecto, con una marca para volver
- **Quitarlos del CI** ahorra ≈2:20 por corrida en cada sistema y 9–11 min en SMS, y libera el fijado a ubuntu-24.04.
- **Quitar también el código** cuesta ≈1–2 días en el esquema actual (1 PR en retrorecomp más 5 PR). En el monorepo es **un solo PR**. Incluye:
  - `shared/web` y `shared/android`;
  - los objetivos `web`/`webtest`/`apk`/`apktest` y lo que se embebe de ellos;
  - los `#ifdef __EMSCRIPTEN__`/`__ANDROID__` (≈50–70 líneas por `main_sdl.c`);
  - «Crear APK» en la GUI y las secciones del README.
- **Antes de borrar, poner un tag** (por ejemplo `web-android-final`) para poder recuperarlos.
- **Conservar el mando táctil** (`player_touch.h`): según el mapeo, también sirve en pantallas táctiles de escritorio **(sin verificar en un escritorio táctil real)**.
- **Cerrar como «aparcado»** retrorecomp#45, GB#107, NES#76, GEN#36 y NEO#32, más lo pendiente de #44.
- **Aparcar sin borrar** sería más barato, pero el código se pudre sin que nadie lo note y sigue metiendo ruido en `main_sdl.c`, los Makefiles y el contrato. Recomiendo quitarlo.

### 2.3 CI por niveles
| Nivel | Cuándo | Qué corre |
|---|---|---|
| **Rápido** (obligatorio, check `ci`) | Cada PR | Solo los sistemas afectados: `<sys>/**` cambia ese sistema; `shared/**`, los sistemas que usan el archivo. Linux por sistema; **un** job de macOS y **uno** de Windows que compilan y prueban los afectados en serie. `run_tests.sh` con `PRUEBAS=rapidas`: sin Overdrive, sin ZEX, sin el ciclo completo de Dragon Blaster, sin SST salvo que cambie el hash del núcleo |
| **CPU** | Cuando cambian `shared/{z80*,m68k*,cpu68k*}` | SST del Z80 y del 68000 en los dos modos (intérprete y `--recomp`), ZEX y disz80. **Una sola vez**, no una por consola |
| **Completo** | Push a `main`, cada noche, `workflow_dispatch` o la etiqueta `ci-completo` | Todo en los tres sistemas operativos |

- **Cachés:**
  - testroms por sistema, guardadas solo desde main;
  - un caché propio para los vectores SST;
  - MSYS2 con `update: false` y sin el paquete `git`.
- **Concurrencia:** `cancel-in-progress` solo en PR.
- **Estimado:** el PR pasa a ≈2:15–3:00 (hoy GEN 7:48 y SMS 15–18 min), con 40–57 % menos runner-minutos.
- **Lo que se acepta a cambio:** lo pesado se descubre después del merge o en el nocturno (no hay merge queue en cuentas de usuario). Hace falta un aviso visible cuando falle el nocturno.

---

## 3. Plan por fases

El orden importa: **F1 primero**, porque cada paso posterior es transversal y hoy costaría 6 PR. **F2 antes de F3 y F4**, porque esos refactors necesitan una red «no cambia un bit» y una métrica.

### F0. Decidir (unos minutos)
Las cinco decisiones de la sección 5. No empezar trabajo transversal nuevo en los repos viejos mientras se migra.

### F1. Monorepo (1–2 sesiones)
1. Congelar los cinco repos: sin PR abiertos (hoy no hay ninguno).
2. Por repo, `git filter-repo --to-subdirectory-filter <sys>/` más el callback de `(#N)`. Unirlos en retrorecomp con `--allow-unrelated-histories` y mover lo de retrorecomp a `gui/`, `compat/` y `tests/`.
3. Corregir unas 40 rutas: `SHARED` en los Makefiles, los envoltorios, el `sys.path` de los `run.py` y `find_repo`. Quitar `.gitmodules` y gitlinks.
4. Los archivos «gemelos» pasan a ser uno solo: `contract.sh`, `recomp_game.sh`, `report.py`, los envoltorios de SST, `ci.yml`, `dependabot.yml` y `.editorconfig`. `contract.py` deja de comparar copias y se queda con lo que importa:
   - las mismas herramientas y opciones por rol;
   - el compat gemelo.

   Pasa a correr en CI y falla si encuentra menos sistemas que los pedidos.
5. Quitar web y Android (2.2), con el tag antes.
6. CI por niveles (2.3), un ruleset nuevo con el check `ci`, y tags `<sys>-v*`.
7. `make test` de los cinco en los tres sistemas operativos: es lo que el mapeo no alcanzó a correr.
8. **GitHub:**
   - cerrar las issues viejas (NEO#3 CI ya hecho, NEO#15, las de Android y otras);
   - crear etiquetas `sys:gb`… y transferir las abiertas con `gh issue transfer`;
   - archivar los cinco repos con un README de aviso.
9. **Local:** un solo checkout. Las carpetas viejas de `~/VSCode` se pueden borrar cuando quieras. La memoria de Claude de retrorecomp se conserva porque la ruta es la misma; las de GEN, NEO y SMS (7 archivos) se juntan ahí.
10. **Documentación base:**
    - un README raíz que diga qué es, qué instalar, cómo usar la GUI y una tabla de sistemas;
    - `docs/` común con la arquitectura de `shared/`, el contrato y el CI;
    - en cada sistema, README, ARQUITECTURA y COMPATIBILIDAD con los mismos encabezados;
    - PLAN.md solo con lo pendiente;
    - corregir de paso las afirmaciones viejas.

### F2. Medir bien: golden y métrica (≈2 sesiones)
1. **`-ffp-contract=off` en los cinco**, también en el Makefile del proyecto generado. Comprobar en macOS y Linux que coincidan, y regenerar todas las líneas base **una vez**, en un PR propio.
2. **Un solo `shared/golden.sh`:**
   - clave por SHA1 de la ROM;
   - las ROMs que faltan se saltan;
   - cuenta los casos nuevos aparte y falla si no comparó ninguno.

   GB lo adopta con `gbshot --hash`.
3. **Golden del código recompilado.** El binario del proyecto generado (difftest o bench) imprime el mismo `--hash`, y el golden compara también ese binario. Esta es la prueba central de «alejarse del intérprete»: el juego recompilado debe dar los mismos bits que la línea base.
4. **En el CI completo**, golden con las ROMs libres de cada sistema (demos, 240p, homebrew, contrato).
5. **Métrica única.** Una línea común en difftest y bench: `% de instrucciones nativas` como número principal, más ciclos por motivo (intérprete ROM/RAM, entrada a IRQ, espera HALT/STOP, otros). GB ya imprime casi todo. `compat.py` la lee.
6. **`--estricto`.** Cada despachador tiene una sola rama hacia el intérprete; ahí se anota el motivo (falta en ROM, falta en RAM, guarda, switch default, IRQ, STOP, tras EI…). Con `--estricto`, difftest falla en la primera entrada a una categoría de «falta código». Se suma al CI con las ROMs de prueba. Piloto: NES.
7. **Volver a correr compat** en los cinco, y por primera vez en SMS. Después, borrar `migrar()` y `docs/compat-neobios`.

### F3. Herramientas gemelas de verdad (3–4 sesiones; cada punto es independiente)
1. **Registro de sistemas** en un solo archivo: id, nombre, extensiones, botones, modelos, BIOS, cuadros por omisión y herramientas. Lo leen la GUI, el contrato, `recomp_game.sh`, `compat_test` y las tablas del README. Con eso un sexto sistema es una entrada más, y de paso se arreglan PAUSE y `PRESETS`.
2. **Nombres en `shared/`** (#64, corregido):

   | Hoy | Pasa a |
   |---|---|
   | `cpu68k.c` | `m68k.c` |
   | `dis68k.*` | `m68k_dis.*` |
   | `disz80.*` | `z80_dis.*` |
   | `sst68k.sh`, `sstz80.sh` | un solo `sst.sh <cpu>` |

   Los programas conservan el nombre de su binario (`m68ktest.c`, `z80test.c`). Hay que reservar `sm83_`/`m6502_`.
3. **Opciones** (con alias para los nombres viejos):

   | Hoy | Pasa a |
   |---|---|
   | `--perfil` | `--profile` |
   | `--entradas-241` | `--input-line 241` |
   | `--shots` | `--screens` |
   | `--dump` | `--shot` |

   `--model` va por un solo mecanismo en `cli.h`, y bench toma su nombre de `argv[0]`.
4. **Llenar huecos por rol:**
   - `gbshot --hash/--expect`, statetest e inputtest en GB;
   - `--info` en GB y NES;
   - `*run` en GB y NES, o declararlo ausente a propósito;
   - el Z80 de GEN y NEO en `--why`, `--events` y `--html` con `z80_dis`;
   - SST del Z80 con `--recomp` en GEN y NEO.

   El contrato deriva los nombres de la lista de roles.
5. **Plantillas comunes:** `trace`, `shot`, `audio` y `ramdump` como un solo archivo de `shared/` con macros de la consola, y lo mismo para el esqueleto de difftest y bench.
6. **GB converge:** `shared/cli.h`, `inputs.h` y `ram_code.h`, y el visor común.
7. **Salida legible por máquina** (`--json` o una línea `RESUMEN {json}`) en `*recomp --viz`, difftest, bench y `recomp_game.sh`. Así la GUI deja las 26 regex.

### F4. Menos intérprete (trabajo continuo; medir cada paso con F2)
1. **Deberes nativos:**
   - STOP salta al próximo evento y la entrada a IRQ se hace desde el bucle híbrido, en `shared/cpu68k.c`, para GEN y NEO;
   - INT/NMI en SMS;
   - lo que sigue a EI en GB.

   Es lo único que baja el piso que las trazas no tocan.
2. **`shared/m68k_recomp.h`** (#64, parte 2), en dos pasos: primero mover sin cambiar un bit (`--codemap`, coverage y golden iguales); después activar en GEN las heurísticas de NEO.
3. **Z80:** las heurísticas de SMS pasan a `z80_recomp.h`, más operandos variables en el código de RAM y semillas de traza en la M1 de NEO. Es lo que más falta en SMS, que tiene 46 % estático.
4. **Descubrimiento guiado por perfil** (retrorecomp#16), con piloto en SMS:
   - un almacén de trazas por SHA1 (`--traces-dir`) que generaliza la caché de BIOS de NEO;
   - conteos en `--uncovered`;
   - un sondeo dentro del recompilador.
5. **Huecos puntuales:**
   - NES#10 (una función por par desplazamiento/dirección);
   - espejos de RAM en GEN y NEO;
   - más de 4 versiones del driver del Z80;
   - GB#36, #14 y #32.

### F5. Juegos sin intérprete (meta)
`make SIN_INTERPRETE=1` en el proyecto generado: no enlaza el intérprete, y un fallo de búsqueda termina de forma controlada con PC, banco, estado y `--uncovered`.

Criterios por juego:
- 0 entradas con `--estricto` en las semillas de compat y en partidas grabadas;
- deberes nativos (F4.1);
- difftest y el golden del binario recompilado iguales.

Piloto: NES, porque sus interrupciones ya ocurren dentro del código recompilado y no tiene HALT. El intérprete se queda en el repo como oráculo: la máquina A de difftest, `*trace` y `--interp`.

---

## 4. Reglas para que no vuelva a enredarse
1. **Un cambio común es un PR**, y el CI prueba solo los sistemas que toca.
2. **No nombrar sistemas por número** en código, comentarios ni documentación: «todos» o la tabla del registro.
3. **Documentación en presente.** Lo pendiente vive en PLAN.md o en issues; las cifras viven solo en COMPATIBILIDAD, generada por compat.
4. **Una prueba de documentación en CI:**
   - cada `--opción` del README existe en el `--help` de su herramienta;
   - las rutas citadas existen;
   - no aparecen «los cuatro» ni sus variantes.
5. **«No cambia un bit» como contrato de los refactors:** golden (del intérprete y del recompilado), `--codemap` y coverage antes y después.
6. **Plataformas nuevas, solo con un costo de CI medido y aceptado de antemano.**

## 5. Decisiones que necesito de ti
1. **Estructura:** ¿monorepo en `retrorecomp` (D)? Es lo recomendado.
2. **Web y Android:** ¿quitarlos con un tag para volver (recomendado) o solo sacarlos del CI?
3. **Carpetas:** ¿`gb/ nes/ gen/ neo/ sms/` (recomendado) o `GBRecomp/`…?
4. **macOS y Windows:** ¿siguen obligatorios en cada PR, en versión rápida (recomendado porque atrapan fallas reales), o solo en el nocturno?
5. **Lo pesado** (Overdrive, ZEX, SST en los tres SO, golden): ¿aceptas que se descubra después del merge o en el nocturno?

## 6. Pendientes chicos que se pueden hacer en cualquier momento
- **GEN:** ¿hace falta compilar las 13 514 instrucciones del driver del Z80 de Overdrive en CI? Se ejecutan 2 946.
- **NEO:** 1800 cuadros (GUI y compat) contra 3000 (`recomp_game.sh`).
- **SMS:**
  - `smsrecomp` ignora `-o` en silencio si va junto a `--info`;
  - el proyecto generado no recuerda `--model`.
- **GB:** el `ci.yml` y los comentarios hablan de SingleStepTests y de `tests/testroms.sh`, que GB no tiene.
- **Merge commit en lugar de squash en retrorecomp:** deja de importar con el monorepo.
