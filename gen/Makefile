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

RUNTIME := cart.c gen.c bus.c io.c vdp.c cpu68k.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libgenrt.a

# Partes de la plataforma que no dependen de SDL: se compilan ya para que no se queden atrás
# (las usarán genshot, genaudio y la ventana)
PLAT := png.c files.c export.c
PLAT_OBJ := $(PLAT:%.c=$(BUILD)/plat_%.o)

PROGS := genrecomp carttest m68ktest bustest genrun

.PHONY: all clean test dist testroms
all: $(PROGS:%=%$(EXE)) $(PLAT_OBJ)

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

bustest$(EXE): src/tools/bustest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

genrun$(EXE): src/tools/genrun.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

# m68ktest pone su propio bus (memoria plana): no usa bus.c
m68ktest$(EXE): src/tools/m68ktest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

# Archivos que genrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := gen.h cart.c gen.c bus.c io.c vdp.c m68k.h m68k_ops.h cpu68k.c sha1.c sha1.h console.h
EMBED_PLATFORM := random_pad.h png.c png.h files.c files.h export.c export.h
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
genrecomp$(EXE): src/genrecomp/genrecomp.c src/genrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DGENRECOMP_VERSION='"$(VERSION)"' -Isrc/genrecomp -Isrc/runtime -o $@ \
		src/genrecomp/genrecomp.c $(BUILD)/gen/embedded.c $(RT)

# Paquete para distribuir: genrecomp, la documentación y la licencia
DIST := dist/genrecomp-$(VERSION)-$(PLATFORM)
dist: genrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp genrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gen.h src/runtime/m68k.h src/runtime/m68k_ops.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/plat_%.o: src/platform/%.c $(wildcard src/platform/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: all
	sh tests/run_tests.sh

# Pruebas libres en testroms/ (no se versionan). Por ahora las SingleStepTests del 68000
# (~200 MB comprimidas; se quedan comprimidas y m68ktest las lee con gunzip -c) en un commit fijo.
SST68K_URL := https://codeload.github.com/SingleStepTests/680x0/tar.gz/e0d5ece9670205cc84a0101081837deb446f86a3
testroms:
	@if [ -f testroms/680x0/68000/v1/NOP.json.gz ]; then echo "testroms/680x0 ya tiene las SingleStepTests del 68000"; else \
		mkdir -p testroms/680x0 $(BUILD) && curl -fsSL -o $(BUILD)/680x0.tar.gz $(SST68K_URL) && \
		tar -xzf $(BUILD)/680x0.tar.gz -C testroms/680x0 --strip-components=1 && rm -f $(BUILD)/680x0.tar.gz && \
		echo "SingleStepTests del 68000 en testroms/680x0"; fi

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
