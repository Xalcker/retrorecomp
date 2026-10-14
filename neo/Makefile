# NEORecomp: runtime, recompilador y herramientas.
# Compila con gcc/clang C99 (Windows: MSYS2 UCRT64).

# --- Bloque común con GBRecomp (el mismo en los dos Makefiles) ---
CC     ?= cc
# MSYS2 sin "cc" (por ejemplo MINGW64): se usa gcc
ifeq ($(origin CC),default)
CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD  := build
# Lo que se comparte con los otros recompiladores (random_pad.h,...) vive en el submódulo
SHARED := tools/retrorecomp/shared
# Módulos de la ventana que viven en shared/ (el resto de src/platform es de esta consola)
SHARED_PLAT := ui_menu.c ui_menu.h ui_font.c ui_font.h png.c png.h files.h
plat = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_PLAT)),$(SHARED)/$(f),src/platform/$(f)))
# Windows (MSYS2): los ejecutables llevan .exe. El make de MSYS2 no le pasa TMP
# a gcc, que entonces intenta escribir en C:\WINDOWS: carpeta temporal en build/.
ifeq ($(OS),Windows_NT)
EXE := .exe
ifneq ($(shell command -v cygpath 2>/dev/null),)
TMPDIR_WIN := $(shell mkdir -p $(BUILD)/tmp && cygpath -m $(CURDIR)/$(BUILD)/tmp)
export TMP  := $(TMPDIR_WIN)
export TEMP := $(TMPDIR_WIN)
endif
endif
# Versión: la etiqueta de git más cercana (o "dev" fuera de un repo)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
# Sistema para el nombre del paquete de make dist: windows, macos o linux
ifeq ($(OS),Windows_NT)
PLATFORM := windows
else ifeq ($(shell uname -s 2>/dev/null),Darwin)
PLATFORM := macos
else
PLATFORM := linux
endif
# --- Fin del bloque común ---

# Lo que hace `make` a secas (las reglas de abajo no siguen el orden en que se usan)
.DEFAULT_GOAL := all

# Crece con cada fase de docs/PLAN.md: cartucho, BIOS y zip (fase 0), 68000 y Z80 (fase 1), la
# consola, el bus, el LSPC sin imagen, el mapa del Z80 y los temporizadores del YM2610 (fase 2), el
# dibujo del LSPC (fase 3), el YM2610 y la mezcla (fase 4)
RUNTIME := cart.c bios.c zip.c neodb.c neo.c bus.c lspc.c lspc_render.c cpu68k.c z80.c z80bus.c ym2610.c audio.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libneort.a

# Partes de la plataforma que no dependen de SDL (las usarán también neoshot y neoaudio)
PLAT := png.c files.c export.c
PLAT_OBJ := $(PLAT:%.c=$(BUILD)/plat_%.o)

# La ventana: el intérprete con menús (neo)
NEO_SRC := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

neo$(EXE): $(call plat,$(NEO_SRC)) $(wildcard src/platform/*.h) $(RT) $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(call plat,$(NEO_SRC)) $(RT) $(WINRES) $(SDL_LIBS) -lm

PROGS := neorecomp neotrace neoshot neoaudio neobench neoramdump neorun neocard neo \
         carttest m68ktest z80test bustest lspctest audiotest statetest inputtest

# SDL2 para el intérprete con ventana (neo). En Windows, enlazado de forma
# estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

.PHONY: all clean test dist testroms neobios
all: $(PROGS:%=%$(EXE)) $(PLAT_OBJ) $(BUILD)/embed$(EXE)

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

bustest$(EXE): src/tools/bustest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

# Herramientas sin ventana con las opciones comunes (cli.h)
TOOL_DEPS := src/platform/cli.h src/platform/inputs.h $(SHARED)/random_pad.h src/tools/romload.h $(RT)
neotrace$(EXE): src/tools/neotrace.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/neotrace.c $(RT) -lm

neoshot$(EXE): src/tools/neoshot.c $(SHARED)/png.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/neoshot.c $(SHARED)/png.c $(RT) -lm

neoaudio$(EXE): src/tools/neoaudio.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/neoaudio.c $(RT) -lm

neobench$(EXE): src/platform/bench.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/platform/bench.c $(RT) -lm

neoramdump$(EXE): src/tools/neoramdump.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/neoramdump.c $(RT) -lm

statetest$(EXE): src/tools/statetest.c src/tools/romload.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/statetest.c $(RT) -lm

inputtest$(EXE): src/tools/inputtest.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/inputtest.c $(RT) -lm

audiotest$(EXE): src/tools/audiotest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

lspctest$(EXE): src/tools/lspctest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

neorun$(EXE): src/tools/neorun.c $(SHARED)/png.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ $^ -lm

neocard$(EXE): src/tools/neocard.c $(SHARED)/png.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ $^ -lm

# m68ktest y z80test ponen su propio bus (memoria plana): no usan el de la consola
m68ktest$(EXE): src/tools/m68ktest.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/m68ktest.c $(RT) -lm

z80test$(EXE): src/tools/z80test.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/z80test.c $(RT) -lm

# El juego de prueba del contrato (tests/testroms.sh lo escribe en testroms/neo/contrato.neo)
$(BUILD)/mkcontrato$(EXE): src/tools/mkcontrato.c $(RT) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/mkcontrato.c $(RT) -lm

# Oráculos: retro_run corre un núcleo libretro (Geolith, FBNeo) sin ventana; los núcleos los baja
# tools/oracles/oracle.py a build/oracles/ (no van en el repo)
ifeq ($(shell uname -s 2>/dev/null),Linux)
DL_LIBS := -ldl
endif
$(BUILD)/retro_run$(EXE): tools/oracles/retro_run.c tools/oracles/libretro_min.h $(SHARED)/png.c $(TOOL_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -Itools/oracles -o $@ tools/oracles/retro_run.c $(SHARED)/png.c $(RT) -lm $(DL_LIBS)

# La BIOS propia (fase 10): asm68k la ensambla (make neobios: build/neobios.bin, en el orden de los chips como
# neo-epo.bin; se usa con --bios o NEO_BIOS); dis68kbin prueba el ensamblador contra el desensamblador
neobios: $(BUILD)/neobios.bin
$(BUILD)/neobios.bin: tools/neobios/neobios.s tools/neobios/asm68k.py | $(BUILD)
	python3 tools/neobios/asm68k.py tools/neobios/neobios.s $@ --swap --list $(BUILD)/neobios.lst

$(BUILD)/dis68kbin$(EXE): tools/neobios/dis68kbin.c src/neorecomp/dis68k.c src/neorecomp/dis68k.h $(RT) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/neorecomp -Isrc/runtime -I$(SHARED) -o $@ tools/neobios/dis68kbin.c src/neorecomp/dis68k.c $(RT) -lm

# Archivos que neorecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := neo.h neodb.h recomp.h m68k.h m68k_ops.h z80.h console.h $(filter-out sha1.c,$(RUNTIME))
EMBED_PLATFORM := main_sdl.c ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h settings.c settings.h \
                  files.c files.h export.c export.h png.c png.h icono.h icono.rc icono.ico inputs.h cli.h \
                  difftest.c bench.c
EMBED_SRC := $(EMBED_RUNTIME:%=src/runtime/%) $(call plat,$(EMBED_PLATFORM)) $(SHARED)/random_pad.h $(SHARED)/trace_line.h $(SHARED)/sha1.c $(SHARED)/sha1.h
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=src/runtime/$(f)) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=$(call plat,$(f)))

$(BUILD)/embed$(EXE): src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS) runtime/sha1.c=$(SHARED)/sha1.c runtime/sha1.h=$(SHARED)/sha1.h platform/random_pad.h=$(SHARED)/random_pad.h runtime/trace_line.h=$(SHARED)/trace_line.h

$(BUILD)/gen:
	mkdir -p $@

# El recompilador: un solo ejecutable, con el runtime adentro
neorecomp$(EXE): src/neorecomp/neorecomp.c src/neorecomp/dis68k.c src/neorecomp/dis68k.h src/neorecomp/visor_html.h \
                 src/neorecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DNEORECOMP_VERSION='"$(VERSION)"' -Isrc/neorecomp -Isrc/runtime -I$(SHARED) -o $@ \
		src/neorecomp/neorecomp.c src/neorecomp/dis68k.c $(BUILD)/gen/embedded.c $(RT) -lm

# Paquete para distribuir: neorecomp, la documentación y la licencia
DIST := dist/neorecomp-$(VERSION)-$(PLATFORM)
dist: neorecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp neorecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/neo.h src/runtime/neodb.h src/runtime/m68k.h src/runtime/m68k_ops.h src/runtime/z80.h $(SHARED)/trace_line.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_%.o: src/platform/%.c $(SHARED)/png.h $(SHARED)/files.h src/platform/export.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_png.o: $(SHARED)/png.c $(SHARED)/png.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: all
	sh tests/run_tests.sh

# Pruebas libres en testroms/ (no se versionan), en versiones fijas: las SingleStepTests del 68000 y
# del Z80 (ver tests/testroms.sh)
testroms:
	sh tests/testroms.sh

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
