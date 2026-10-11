# Plan de GENRecomp

Recompilador estático de Sega Genesis con la misma estructura que
[NESRecomp](https://github.com/Xalcker/NESrecomp) y GBRecomp: runtime con intérprete de respaldo,
proyecto C99 generado sin datos de la ROM (valida su SHA1), `difftest` contra el intérprete y el
mismo contrato de línea de comandos.

## Qué cambia respecto a NES

| | NES | Genesis | Consecuencia |
|---|---|---|---|
| CPU principal | 6502, 1-3 bytes | 68000, 2-10 bytes, alineado a palabra, big-endian | Decodificador más grande; muchos opcodes ilegales ayudan a separar código de datos. |
| Bancos | Mappers por todos lados | ROM lineal hasta 4 MB; solo el mapper de SSF2 y algunos sin licencia | Identidad por desplazamiento casi trivial. |
| Vectores | NMI/RESET/IRQ | 64 en `$000000` | Muchas semillas para el descubrimiento. |
| Segundo CPU | — | Z80 de sonido, código en su RAM (lo sube el 68k) | El Z80 va siempre interpretado. |
| Video | PPU | VDP: 2 planos, ventana, sprites, scroll por línea/celda, 3 DMA, HINT/VINT, H32/H40, sombra/resalte, entrelazado | La pieza más grande del runtime. |
| Audio | APU | YM2612 (FM + DAC) + SN76489 | YM2612 lo más difícil; cuidar la licencia (Nuked-OPN2 es LGPL; MAME/GPGX no son compatibles con MIT). |

## Fases

- [x] **0. Esqueleto.** Repo, bloque común del Makefile, CI, licencia MIT, `contract.sh` (rama `gen`),
  submódulo `retrorecomp`, partes de la plataforma sin SDL (`png`, `files`, `export`, `random_pad.h`),
  `embed`, `sha1`. Cartucho: `.bin`/`.md`/`.gen`/`.smd` (normalizado; el SHA1 es de la imagen
  normalizada), cabecera, suma, SRAM declarada y vectores. `genrecomp --info` y `carttest`.
- [x] **1. CPU 68000.** `m68k_ops.h` como única fuente de verdad (intérprete y código generado),
  `cpu68k.c` (decodificador con validación de modos, excepciones, error de dirección, interrupciones
  por nivel, traza). Ciclos por acceso al bus más los internos. Las 1 000 058 SingleStepTests pasan por
  el intérprete y por el camino del código generado (`m68ktest`, `--recomp`; en `make test`). Pendiente
  para la fase 3: el orden exacto de los accesos dentro de una instrucción (78 % con `--bus`).
  Sin probar todavía con hardware: interrupciones, traza y reset (llegan con el bus y el VDP).
- [x] **2. Bus y cartucho.** Mapa de memoria (`bus.c`), RAM, E/S y mandos de 3 y 6 botones (`io.c`),
  pedido y reset del bus del Z80, SRAM de la cabecera (y guardarla), mapper de SSF2, TMSS (apagado
  por omisión: sin "SEGA" en `$A14000`, tocar el VDP cuelga el 68000). Más el esqueleto del VDP (registros, memorias sin DMA, estado, líneas,
  VINT/HINT) para correr juegos reales: Sonic 1 llega a jugar el nivel con el intérprete y Super
  Street Fighter II corre con su mapper. EEPROM serie I2C con base de datos por juego (por ahora NBA
  Jam, comprobado: guarda y acepta su partida). `bustest` y `genrun` (con `--watch` y `--sram`).
  Pendiente: más juegos con EEPROM, mappers sin licencia y esperas del bus del Z80 (el refresco de la
  DRAM llegó en la fase 9).
- [x] **3. VDP.** Puertos, los tres DMA (el del 68000 lo detiene lo que dura), estado, contador HV,
  dibujo por línea (planos con todos los scrolls, ventana, sprites con sus límites, máscara y colisión,
  prioridades, sombra y resalte), perezoso. Ventana `gen` (menús, controles de 6 botones, aspecto 4:3,
  capturas; icono `MD.png` de KyleBing, CC BY 4.0). `vdptest`, `genrun --shot/--shots`. Sonic 1, SSF2 y
  NBA Jam se ven bien. Pendiente: orden exacto de los accesos del 68000, entrelazado doble (la FIFO
  llegó en la fase 9),
  posición exacta de HINT/VINT/HBLANK en la línea.
- [ ] **PAL**: 312 líneas y 50 Hz. Lo piden los juegos solo europeos (Street Fighter II' de Europa) y
  las demos Overdrive.
- [x] **4. Z80 y audio.** Z80 completo (las 1 604 000 SingleStepTests del Z80), su mapa y su ritmo
  detrás del 68000, INT en el VBLANK; PSG; YM2612 propio (fase, envolvente, 8 algoritmos, LFO,
  temporizadores, DAC); mezcla a 44 100 Hz con pasa-altos; sonido en `gen` (menú Audio). `audiotest`,
  `genrun --wav`. MK1 y Mickey Mania pasan su espera del Z80; Sonic 1 dice "SEGA". EEPROM de Wily
  Wars. SSG-EG y la espera del Z80 al usar el bus del 68000. Pendiente: CSM, retardos entre
  operadores, distorsión de la DAC, lo que pierde el 68000 cuando el Z80 usa su bus, y que el YM2612
  sea más rápido (fase 8: hoy ~15 veces el tiempo real).
- [x] **5. Herramientas sin ventana.** Hecho: `gentrace`, `genshot` (`--expect`, `--hash`),
  `genaudio`, `genbench`, `ramdump`, `statetest`, `inputtest`; `cli.h` e `inputs.h` con 12 botones (el
  mismo formato de texto; `--seed` traduce el mando aleatorio común); `contract.sh` completo con
  Overdrive; estados guardados (F5/F8, 4 ranuras), `--record`/`--play` y `--trace` en `gen`. Lo que
  decía el plan: `gentrace`, `genshot` (`--expect`, `--hash`), `genaudio`,
  `genbench`, `ramdump`, `statetest`, `sramtest`, `inputtest`; `cli.h` e `inputs.h` con 8 botones
  (A B C X Y Z START MODE) sin romper el formato común. `contract.sh` con sus comprobaciones.
- [x] **6. Recompilador v1.** `genrecomp juego.bin -o carpeta [--name] [--trace] [--coverage]`:
  descubrimiento desde los vectores siguiendo `BRA/BSR/Bcc/DBcc` y `JMP/JSR` constantes, más las
  trazas; funciones por tramos de ROM dentro de un banco de 512 KB; emisión por instrucción con los
  `k_*` de `m68k_ops.h` (`AFTER`, `LEAVE`, `MAPCHK` con SSF2 o SRAM encima de la ROM); despacho por
  desplazamiento de ROM (`recomp_lookup`, `gen_code_offset`); el código en RAM y las interrupciones
  los atiende el intérprete (`m68k_run_hybrid`). Proyecto generado sin datos de la ROM (SHA1), con la
  ventana, `difftest` (68000, RAM, VDP, Z80, YM2612, PSG, imagen y audio, `--uncovered`, `--bench`,
  `--screens`, `--executed`) y `bench`. Los 23 juegos de `roms/` dan 1800 cuadros iguales al
  intérprete (con traza de otras entradas) y Sonic 1 corre el 100 % de los ciclos en código
  recompilado. Pendiente: encontrar sin traza lo que se alcanza por saltos indirectos (fase 7) y la
  velocidad (fase 8: Sonic 1, 14.9x contra 11.7x del intérprete).
- [x] **7. Heurísticas de cobertura.** Tablas de desplazamientos (`move.w tbl(pc,d0.w)` + `jmp
  tbl(pc,d1.w)`), de instrucciones (`bra.w`/`jmp`), de punteros de 32 bits y bases constantes;
  especulativas: punteros inmediatos (`move.l/pea/lea rutina`), tablas sueltas y lo que sigue a un
  final de rutina; plausibilidad (opcodes válidos, direcciones razonables, final de rutina).
  Desensamblador (`dis68k.c`); `--coverage`, `--why`, `--viz`, `--html`, `--events`, `--codemap`,
  `--dis`, `--no-tables`, `--no-spec`; `tests/coverage.sh`. Sin traza, 93-100 % de lo que ejecutan
  los 23 juegos de `roms/` (Sonic 1: de 25 a 100 %). Pendiente: compilar menos código de más (Sonic &
  Knuckles descubre 130 000 instrucciones y tarda ~1 minuto con 8 núcleos).
- [x] **8. Rendimiento.** `tests/golden.sh` (hash de cuadros y audio por ROM: nada cambia un bit).
  Perfilado: el 68000 recompilado ya era una parte chica; el tiempo estaba en el dibujo del VDP, el
  Z80 y el YM2612. VDP de a un mosaico y mezcla sin ramas, lecturas de ROM de 16 bits, canales del
  YM2612 callados y frecuencias guardadas, HALT del Z80 de una vez, `gen_after_step` y `m68k_idle` en
  línea. Sonic 1: intérprete de 11.6x a 20.3x, recompilado de 14.9x a 28.2x; los demás juegos medidos,
  28-31x. No se hizo (no pesa): vida de los flags del CCR, encadenar funciones sin el despachador,
  `-DGEN_MAPPER`, saltar los bucles de espera del VBLANK.
- [x] **9. Compatibilidad y documentación.** Oráculos: Genesis Plus GX y BlastEm como núcleos libretro
  (`tools/oracles/`: `retro_run`, frontend sin ventana con las entradas de las herramientas; `oracle.py`
  los baja y compara RAM e imagen). Con ellos se encontró que el 68000 corría algo rápido y se
  agregaron la FIFO del VDP y el refresco de la DRAM (más parecido con los dos). `tools/compat/` (etapa 1
  con el intérprete, etapa 2 recompilando sin traza, oráculos) y `docs/COMPATIBILIDAD.md` con
  `docs/compat/index.html`. Pendiente: una colección grande (No-Intro) cuando la haya; PAL.

## Comparación con SonicTheHedgehogRecomp (mstan/segagenesisrecomp)

Mismo objetivo, decisiones casi opuestas en lo central: ellos recompilan un juego a la vez guiados
por su desensamblado (`game.toml`, `s1disasm`), mapean `jsr`/`rts` a llamadas de C (con parches para
`addq.l #4,sp` y similares), corren el código en un *fiber* que cede en puntos de sondeo con ciclos de
una tabla medida, y tratan lo que no encuentran como un fallo grave. Nosotros: cualquier ROM sin
configuración, pila emulada, ciclos por acceso al bus e interrupciones tras cada instrucción, y el
intérprete por instrucción cubre lo que falte. Coincidimos en ymfm para el YM2612 y en la
cosimulación contra el intérprete propio. Su licencia es PolyForm Noncommercial: no se copia código.
Ideas que tomamos: detectar los bucles de espera del VBlank (fase 8) y usar Sonic 1 como primer juego
comercial de referencia.

## Decisiones

1. Precisión: accesos al bus del 68000 contados (como en NES) con el VDP perezoso; no todo ciclo a ciclo.
2. El Z80 siempre interpretado (recompilar drivers conocidos como SMPS o GEMS, quizá después).
3. Fuera de alcance al principio: Sega CD, 32X, modo SMS, Pico. PAL después de NTSC.
4. Orden: CPU → bus → VDP → herramientas → recompilador; el audio en paralelo (`difftest` sirve sin YM2612 al principio).
