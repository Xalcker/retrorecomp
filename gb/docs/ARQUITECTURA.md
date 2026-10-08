# Arquitectura de GBRecomp

GBRecomp toma una ROM de Game Boy (DMG) y genera un proyecto C99 que se compila
en un ejecutable nativo. El ejecutable carga la ROM del usuario en tiempo de
ejecución (validando su SHA1): nunca se distribuyen datos de la ROM.

## Piezas

```
gbrecomp (herramienta)                proyecto generado
──────────────────────                ─────────────────
carga ROM → descubre código           gen/bankNN_*.c   código recompilado
 ├ recursivo desde $100, IRQ, RST     gen/dispatch.c   (banco, PC) → función
 ├ trazas de ejecución                runtime/         CPU, bus, MBC, PPU, APU,
 └ propagación de constantes                           timer, serial, joypad
emite C99 + Makefile                  platform/        SDL2
```

| Directorio | Contenido |
|---|---|
| `src/emit/` | Decodificador, desensamblador y **emisor** de C del SM83: la única fuente de verdad de la semántica y la temporización por M-ciclo. |
| `src/runtime/` | El hardware: `gb.c` (bus, sincronización, interrupciones, despachador, DMA, serial, joypad), `timer.c`, `ppu.c`, `apu.c`, `mbc.c`, `interp.c`. |
| `src/tools/` | `gen_interp` (genera el intérprete), `gbtest` (corre ROMs de prueba sin ventana), `png.c`. |
| `tests/` | `run_tests.sh`: Blargg, Mooneye y dmg-acid2. |
| `testroms/` | ROMs de prueba de código abierto (no se versionan). |

## Una sola fuente de verdad para el CPU

`sm83_emit.c` decide, para cada opcode, qué operaciones hace y en qué orden
ocurre cada M-ciclo (lectura del opcode, operandos, accesos a memoria y ciclos
internos). Las operaciones en sí están en `cpu_ops.h`.

- `gen_interp` usa el emisor en **modo intérprete** (operandos leídos en tiempo
  de ejecución, PC dinámico) y produce `build/gen/interp_ops.inc`.
- `gbrecomp` usará el emisor en **modo recompilado** (operandos y PC
  constantes).

Así el intérprete y el código recompilado no pueden discrepar en semántica ni
en temporización, y la prueba diferencial aísla errores del *recompilador*.

## Temporización

- `g->cyc` cuenta T-ciclos del CPU y siempre es múltiplo de 4.
- Cada acceso a memoria suma 4 ciclos y luego accede: el hardware ya avanzó
  hasta el final del M-ciclo del acceso (validado con `mem_timing` de Blargg).
- **Hardware perezoso:** timer, PPU, APU, serial y DMA solo avanzan en
  `gb_sync`, que se llama:
  - antes de cualquier acceso a I/O, VRAM, OAM, RAM de cartucho o durante DMA
    (la ruta lenta del bus);
  - cuando el CPU alcanza `g->next_event`, el primer ciclo en que el hardware
    podría cambiar IF por su cuenta (desbordamiento de TIMA, cambio de modo o
    línea de la PPU, fin de transferencia serial).
- Las lecturas de ROM y WRAM van por un mapa de páginas (`rpage`/`wpage`) sin
  sincronizar. Durante un DMA de OAM el mapa se vacía y todo pasa por la ruta
  lenta, que reproduce los choques de bus.

## Interrupciones y despachador

En cada límite de instrucción, si `g->cyc >= g->next_event`, el despachador
(`gb_run`) o el código recompilado (`gb_check`) sincroniza el hardware y
revisa IME/IE/IF. Las escrituras lentas, `EI` y `RETI` ponen `next_event = 0`
para forzar la revisión. El despacho de interrupción toma 5 M-ciclos y decide
el vector después de empujar el byte alto de PC (si esa escritura cambió IE,
salta a `$0000`).

`HALT` adelanta el reloj directo al siguiente evento. El bug de HALT (IME=0 con
interrupción pendiente) hace que la siguiente instrucción se ejecute en el
intérprete con el PC sin avanzar.

## Formato del código recompilado (fase 2)

Cada región descubierta se vuelve una función con un `switch` de entrada y una
etiqueta por instrucción, para poder entrar o salir en cualquier límite:

```c
/* Banco 0, región $0150-$0172 */
void f00_0150(GB *g)
{
    Cpu *c = &g->cpu;
    switch (c->pc) {
    case 0x0150: goto L_0150;
    case 0x0152: goto L_0152;
    default: c->pc = ...; return;   /* entrada desconocida: al despachador */
    }
L_0150: /* ldh a, [$FF44] */
    TICK(); TICK(); c->a = RD(0xFF00 | 0x44);
    if (UNLIKELY(g->cyc >= g->next_event)) { c->pc = 0x0152; if (gb_check(g)) return; }
L_0152: /* cp $90 */
    ...
}
```

- Saltos con destino constante dentro de la región → `goto`.
- `call`, `ret`, `jp hl`, `rst` y saltos fuera de la región → se asigna `c->pc`
  y se regresa al despachador. CALL y RET usan la **pila emulada**, así que los
  trucos de pila (`rst $28` con tablas, `push`+`ret`) funcionan.
- El despachador busca la función por (banco mapeado, PC). Si no hay, ejecuta
  una instrucción en el intérprete (código en RAM, código no descubierto).

## Estado de la validación (fase 1, solo intérprete)

`sh tests/run_tests.sh`: **102 de 111** pruebas que aplican a DMG.

- Blargg: `cpu_instrs` (las 11 y la combinada), `instr_timing`, `mem_timing`,
  `mem_timing-2` y `halt_bug` pasan.
- Mooneye: pasan todas las de temporización de instrucciones, interrupciones,
  HALT, timer (salvo `rapid_toggle`), DMA, arranque, serial y MBC1/2/5.
- dmg-acid2 pasa (pantalla idéntica).

Pendientes conocidos:

| Prueba | Causa | Fase |
|---|---|---|
| `ppu/intr_2_*`, `lcdon_*`, `stat_lyc_onoff` | Temporización fina de modos de la PPU (duración del modo 3, primera línea tras encender el LCD) | 5 |
| `timer/rapid_toggle` | Interrupción una vuelta tarde; caso límite de encender/apagar TAC | 5 |
| `mbc1/multicart_rom_8Mb` | MBC1 multicart (cableado distinto, se detecta por heurística) | 4 |
