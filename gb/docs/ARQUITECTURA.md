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
| `src/gbrecomp/` | El recompilador: descubrimiento, partición en funciones, emisión del proyecto. |
| `src/platform/` | Se copia a cada proyecto generado: `main_sdl.c` (ventana, controles, audio) y `difftest.c` (prueba diferencial). |
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

## Código recompilado (fase 2)

### Descubrimiento y funciones

`gbrecomp` decodifica recursivamente desde `$0100` y los vectores de
interrupción, siguiendo saltos, llamadas y `rst` con destino constante (las
llamadas se asumen con regreso). Con `--trace` agrega como puntos de entrada las
direcciones de una traza de ejecución (formato `BB:AAAA`); así se resuelven los
saltos indirectos (`jp hl`, tablas de `rst $28`).

Las instrucciones de cada banco se agrupan en funciones por tramos contiguos de
direcciones: se corta cuando hay más de 32 bytes sin código o al llegar a 1,500
instrucciones. Cada instrucción pertenece a una sola función. Si dos
decodificaciones se traslapan (saltos a mitad de instrucción), ambas existen,
cada una con su etiqueta.

### Formato

```c
/* Banco 00, $14E0-$1530 (25 instrucciones) */
void f00_14E0(GB *g)
{
    Cpu *c = &g->cpu;
    switch (c->pc) {                 /* se puede entrar en cualquier instrucción */
    case 0x14E0: goto L_14E0;
    ...
    default: gb_interp_step(g); return;
    }
L_14F3: /* cp $1D */
    CHECK(0x14F3);                   /* ¿evento de hardware? → al despachador */
    {
        TICK();                      /* lectura del opcode */
        TICK();                      /* lectura del operando */
        op_cp(c, 0x1D);
    }
L_14F5: /* jr nz, $1500 */
    ...
}
```

- `CHECK` al inicio de cada instrucción revisa `g->cyc >= g->next_event`. Si
  hay que atender una interrupción o terminar la corrida, regresa con
  `c->pc` apuntando a esa instrucción, todavía sin ejecutar.
- Un salto con destino constante a una instrucción de la misma función se
  vuelve `goto`. Cualquier otro (`call`, `ret`, `jp hl`, `rst`, fuera de la
  función) asigna `c->pc` y regresa al despachador. CALL y RET usan la **pila
  emulada**, así que los trucos de pila funcionan.
- En funciones de bancos ≥ 1, después de cada instrucción que puede escribir en
  `$0000-$7FFF` se comprueba que el banco siga siendo el mismo. Si el código
  cambió su propio banco, regresa al despachador.
- `gen/dispatch.c` tiene tablas banco → página de 256 bytes → función. El
  código del banco 0 solo se usa si está mapeado en `$0000` y el de los demás
  bancos solo en `$4000`. Cualquier otra combinación va al intérprete.

### Proyecto generado

```
gbrecomp juego.gb -o salida/ [--trace traza.txt] [--name nombre]
cd salida && make
./nombre rom.gb          # el juego (SDL2); valida el SHA1 de la ROM
./difftest rom.gb        # prueba diferencial
```

`salida/` trae `gen/` (código generado), `runtime/` (copia del runtime),
`platform/` (SDL2 y difftest) y un `Makefile`. No contiene datos de la ROM.

### Prueba diferencial

`difftest` corre la misma ROM con código recompilado y con el intérprete, con
las mismas entradas pseudoaleatorias (`--seed`), y compara el estado completo al
final de cada cuadro: registros, ciclos, WRAM, HRAM, VRAM, OAM, I/O, timer,
PPU, banco y RAM del cartucho. Si hay diferencia, repite el cuadro instrucción
por instrucción y reporta la primera, con banco, dirección, opcode y el campo
que difiere:

```
Primera diferencia en el cuadro 401, instrucción 2943 del cuadro,
  tras ejecutar 00:14F3 (opcode $FE, recompilada)
  A: recompilado=FF intérprete=1C
```

Otras opciones: `--uncovered archivo` escribe las direcciones de ROM que ejecutó
el intérprete (para `gbrecomp --trace`), `--screens carpeta --every N` guarda
capturas y `--bench` mide la velocidad de cada máquina.

### Resultados con Tetris (JUE) (V1.1)

| | Solo estático | Con trazas de 7 sesiones |
|---|---|---|
| Instrucciones recompiladas | 2,189 | 4,321 |
| Ejecutado en código recompilado | 96.6 % | 98.9 % |
| Direcciones de ROM interpretadas | 1,239 | 0 a 99 según la semilla |

- **Sin diferencias** en más de 160,000 cuadros (13 sesiones de 3,000 a 20,000
  cuadros con semillas distintas). Las capturas confirman partidas reales.
- El 1.1 % que queda en el intérprete es la rutina de DMA que Tetris copia a
  HRAM: el código en RAM siempre va al intérprete.
- Se comprobó el localizador inyectando un error a mano (`cp` → `sub`):
  reportó la instrucción y el registro exactos.
- Velocidad (sin audio): recompilado 47x tiempo real, intérprete 44x. Casi
  todo el tiempo se va en el hardware, que se simula punto por punto. Optimizar
  `gb_sync` es lo que más rendiría.

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
