# Plan de GENRecomp

Recompilador estático de Mega Drive / Genesis con la misma estructura que
[NESRecomp](https://github.com/Xalcker/NESrecomp) y GBRecomp: runtime con intérprete de respaldo,
proyecto C99 generado sin datos de la ROM (valida su SHA1), `difftest` contra el intérprete y el
mismo contrato de línea de comandos.

## Qué cambia respecto a NES

| | NES | Mega Drive | Consecuencia |
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
- [ ] **2. Bus y cartucho.** Mapa de memoria, RAM, I/O (`$A10000`), arbitraje del Z80 (BUSREQ/RESET),
  SRAM/EEPROM con batería, mapper de SSF2, TMSS opcional, mandos de 3 y 6 botones.
- [ ] **3. VDP.** Puerto de control, VRAM/CRAM/VSRAM, FIFO, DMA (68k→VRAM con pausa del 68k, relleno,
  copia), dibujo por línea, HV counter, HINT/VINT. Perezoso como `ppu_sync`. Ventana `gen` (llegan
  `main_sdl.c`, `ui_menu`, `ui_font`, `i18n`, `settings`, icono propio).
- [ ] **4. Z80 y audio.** Z80 interpretado (zexdoc/zexall, SingleStepTests z80), SN76489, YM2612,
  mezcla y remuestreo a 44 100 Hz.
- [ ] **5. Herramientas sin ventana.** `gentrace`, `genshot` (`--expect`, `--hash`), `genaudio`,
  `genbench`, `ramdump`, `statetest`, `sramtest`, `inputtest`; `cli.h` e `inputs.h` con 8 botones
  (A B C X Y Z START MODE) sin romper el formato común. `contract.sh` con sus comprobaciones.
- [ ] **6. Recompilador v1.** Descubrimiento desde los vectores siguiendo `bra/bsr/Bcc/DBcc/jmp/jsr`
  constantes; emisión por instrucción (`FRAME`, `ICHK`, `LEAVE`, `MAPCHK` solo con SSF2); despacho por
  desplazamiento de ROM; el código en RAM lo corre el intérprete; `difftest` (68k, RAM, VDP, Z80,
  YM, PSG, imagen y audio).
- [ ] **7. Heurísticas de cobertura.** Tablas `move.w tbl(pc,d0.w)` + `jmp tbl(pc,d1.w)`, tablas de
  `bra.w`, tablas de punteros de 32 bits, trampolines `jmp` en RAM; plausibilidad por opcodes
  ilegales; `--trace`, `--coverage`, `--why`, `--viz`, `--html`, `--codemap`, `coverage.sh`.
- [ ] **8. Rendimiento.** Vida de los flags del CCR, `-DGEN_MAPPER`, VDP y audio perezosos, `golden.sh`.
- [ ] **9. Compatibilidad y documentación.** `tools/compat` con el set No-Intro, `COMPATIBILIDAD.md`,
  oráculos (BlastEm y/o Genesis Plus GX) en `tools/oracles/`.

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
