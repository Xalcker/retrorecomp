# NESRecomp: runtime, intérprete 6502 y herramientas.
# Compila con gcc/clang C99 (Windows: MSYS2 UCRT64).

# --- Bloque común con GBRecomp (el mismo en los dos Makefiles) ---
CC     ?= cc
# MSYS2 sin "cc" (por ejemplo MINGW64): se usa gcc
ifeq ($(origin CC),default)
CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD  := build
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

# SDL2 para el intérprete con ventana (nes). En Windows, enlazado de forma
# estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

RUNTIME := nes.c cpu6502.c ppu.c apu.c mapper.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libnesrt.a

PROGS := nestest nesshot nesaudio nestrace sramtest statetest mappertest inputtest ramdump nesbench nes nesrecomp

.PHONY: all clean test dist testroms
all: $(PROGS:%=%$(EXE))

nestest$(EXE): src/tools/nestest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

nesshot$(EXE): src/tools/nesshot.c src/platform/png.c src/platform/cli.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -o $@ src/tools/nesshot.c src/platform/png.c $(RT)

nesbench$(EXE): src/platform/bench.c src/platform/cli.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -o $@ src/platform/bench.c $(RT) -lm

nestrace$(EXE): src/tools/nestrace.c src/platform/cli.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -o $@ src/tools/nestrace.c $(RT)

# Archivos que nesrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := nes.h nes.c cpu_ops.h cpu6502.c ppu.c apu.c mapper.c recomp.h sha1.c sha1.h console.h
EMBED_PLATFORM := main_sdl.c difftest.c bench.c cli.h ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h \
                  settings.c settings.h files.c files.h export.c export.h png.c png.h inputs.h icono.h icono.rc icono.ico
EMBED_SRC := $(EMBED_RUNTIME:%=src/runtime/%) $(EMBED_PLATFORM:%=src/platform/%)
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=src/runtime/$(f)) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=src/platform/$(f))

$(BUILD)/embed$(EXE): src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS)

$(BUILD)/gen:
	mkdir -p $@

# El recompilador: un solo ejecutable, con el runtime adentro
nesrecomp$(EXE): src/nesrecomp/nesrecomp.c src/nesrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DNESRECOMP_VERSION='"$(VERSION)"' -Isrc/nesrecomp -Isrc/runtime -o $@ \
		src/nesrecomp/nesrecomp.c $(BUILD)/gen/embedded.c $(RT)

# Paquete para distribuir: nesrecomp, la documentación y la licencia
DIST := dist/nesrecomp-$(VERSION)-$(PLATFORM)
dist: nesrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp nesrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

ramdump$(EXE): src/tools/ramdump.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

inputtest$(EXE): src/tools/inputtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

mappertest$(EXE): src/tools/mappertest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

statetest$(EXE): src/tools/statetest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

sramtest$(EXE): src/tools/sramtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

nesaudio$(EXE): src/tools/nesaudio.c src/platform/cli.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -o $@ src/tools/nesaudio.c $(RT) -lm

PLATFORM := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

nes$(EXE): $(PLATFORM:%=src/platform/%) $(wildcard src/platform/*.h) $(RT) $(WINRES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(PLATFORM:%=src/platform/%) $(RT) $(WINRES) $(SDL_LIBS) -lm

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/nes.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

# Pruebas: necesitan las ROMs de prueba (make testroms)
test: all
	sh tests/run_tests.sh

# ROMs de prueba libres en testroms/ (no se versionan): christopherpow/nes-test-roms
# en un commit fijo (el repositorio no tiene releases)
TESTROMS_URL := https://github.com/christopherpow/nes-test-roms/archive/95d8f621ae55cee0d09b91519a8989ae0e64753b.tar.gz
testroms:
	@if [ -d testroms/nes-test-roms/other ]; then echo "testroms/ ya tiene las ROMs de prueba"; else \
		mkdir -p testroms/nes-test-roms $(BUILD) && curl -fsSL -o $(BUILD)/testroms.tar.gz $(TESTROMS_URL) && \
		tar -xzf $(BUILD)/testroms.tar.gz -C testroms/nes-test-roms --strip-components=1 && rm -f $(BUILD)/testroms.tar.gz && \
		echo "ROMs de prueba en testroms/"; fi

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
