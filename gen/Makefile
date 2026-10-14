# GENRecomp: runtime, recompilador y herramientas.
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
SHARED_PLAT := ui_menu.c ui_menu.h ui_font.c ui_font.h png.c png.h files.h export.c export.h player_util.h player_common.h
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

RUNTIME := cart.c gen.c bus.c io.c eeprom.c vdp.c vdp_render.c cpu68k.c z80.c z80bus.c psg.c ym2612.c audio.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libgenrt.a

# Partes de la plataforma que no dependen de SDL (las usan también genshot y genaudio)
PLAT := png.c files.c export.c
PLAT_OBJ := $(PLAT:%.c=$(BUILD)/plat_%.o)

# La ventana: el intérprete con menús (gen)
GEN_SRC := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

gen$(EXE): $(call plat,$(GEN_SRC)) $(wildcard src/platform/*.h) $(RT) $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(call plat,$(GEN_SRC)) $(RT) $(WINRES) $(SDL_LIBS) -lm

PROGS := genrecomp gentrace genshot genaudio genbench genramdump genrun gen \
         carttest m68ktest z80test bustest vdptest audiotest statetest inputtest

# SDL2 para el intérprete con ventana (gen). En Windows, enlazado de forma
# estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

.PHONY: all clean test dist testroms
all: $(PROGS:%=%$(EXE)) $(PLAT_OBJ)

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

bustest$(EXE): src/tools/bustest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

vdptest$(EXE): src/tools/vdptest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

audiotest$(EXE): src/tools/audiotest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

# Herramientas sin ventana con las opciones comunes (cli.h)
TOOL_DEPS := src/platform/cli.h src/platform/inputs.h $(SHARED)/random_pad.h src/tools/romload.h $(RT)
gentrace$(EXE): src/tools/gentrace.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/gentrace.c $(RT) -lm

genshot$(EXE): src/tools/genshot.c $(SHARED)/png.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/genshot.c $(SHARED)/png.c $(RT) -lm

genaudio$(EXE): src/tools/genaudio.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/genaudio.c $(RT) -lm

genbench$(EXE): src/platform/bench.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/platform/bench.c $(RT) -lm

genramdump$(EXE): src/tools/genramdump.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/genramdump.c $(RT) -lm

statetest$(EXE): src/tools/statetest.c src/tools/romload.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/statetest.c $(RT) -lm

inputtest$(EXE): src/tools/inputtest.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/inputtest.c $(RT) -lm

genrun$(EXE): src/tools/genrun.c $(SHARED)/png.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ $^ -lm

# m68ktest y z80test ponen su propio bus (memoria plana): no usan bus.c ni z80bus.c
m68ktest$(EXE): src/tools/m68ktest.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/m68ktest.c $(RT) -lm

z80test$(EXE): src/tools/z80test.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/z80test.c $(RT) -lm

# Archivos que genrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := gen.h cart.c gen.c bus.c io.c eeprom.c vdp.c vdp_render.c m68k.h m68k_ops.h cpu68k.c z80.h z80.c z80bus.c psg.c ym2612.c audio.c console.h recomp.h
EMBED_PLATFORM := main_sdl.c ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h settings.c settings.h \
                  files.c files.h export.c export.h png.c png.h player_util.h player_common.h icono.h icono.rc icono.ico inputs.h cli.h \
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
genrecomp$(EXE): src/genrecomp/genrecomp.c src/genrecomp/dis68k.c src/genrecomp/dis68k.h src/genrecomp/visor_html.h \
                 src/genrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DGENRECOMP_VERSION='"$(VERSION)"' -Isrc/genrecomp -Isrc/runtime -I$(SHARED) -o $@ \
		src/genrecomp/genrecomp.c src/genrecomp/dis68k.c $(BUILD)/gen/embedded.c $(RT) -lm

# Oráculos: retro_run corre un núcleo libretro (Genesis Plus GX, BlastEm) sin ventana; los núcleos los
# baja tools/oracles/oracle.py a build/oracles/ (no van en el repo)
ifeq ($(shell uname -s 2>/dev/null),Linux)
DL_LIBS := -ldl
endif
$(BUILD)/retro_run$(EXE): tools/oracles/retro_run.c tools/oracles/libretro_min.h $(SHARED)/png.c $(TOOL_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -Itools/oracles -o $@ tools/oracles/retro_run.c $(SHARED)/png.c $(RT) -lm $(DL_LIBS)

# Paquete para distribuir: genrecomp, la documentación y la licencia
DIST := dist/genrecomp-$(VERSION)-$(PLATFORM)
dist: genrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp genrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gen.h src/runtime/m68k.h src/runtime/m68k_ops.h src/runtime/z80.h $(SHARED)/trace_line.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_%.o: src/platform/%.c $(SHARED)/png.h $(SHARED)/files.h $(SHARED)/export.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_export.o: $(SHARED)/export.c $(SHARED)/export.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_png.o: $(SHARED)/png.c $(SHARED)/png.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: all
	sh tests/run_tests.sh

# Pruebas libres en testroms/ (no se versionan), en versiones fijas: las SingleStepTests del
# 68000 y las demos Overdrive de Titan (ver tests/testroms.sh)
testroms:
	sh tests/testroms.sh

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
