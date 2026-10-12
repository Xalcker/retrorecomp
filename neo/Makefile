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

# Crece con cada fase de docs/PLAN.md: cartucho, BIOS y zip (fase 0), 68000 y Z80 (fase 1)
RUNTIME := cart.c bios.c zip.c neodb.c cpu68k.c z80.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libneort.a

# Partes de la plataforma que no dependen de SDL (las usarán también neoshot y neoaudio)
PLAT := png.c files.c export.c
PLAT_OBJ := $(PLAT:%.c=$(BUILD)/plat_%.o)

PROGS := neorecomp carttest m68ktest z80test

.PHONY: all clean test dist testroms
all: $(PROGS:%=%$(EXE)) $(PLAT_OBJ) $(BUILD)/embed$(EXE)

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^ -lm

# m68ktest y z80test ponen su propio bus (memoria plana): no usan el de la consola
m68ktest$(EXE): src/tools/m68ktest.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ src/tools/m68ktest.c $(RT) -lm

z80test$(EXE): src/tools/z80test.c src/tools/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ src/tools/z80test.c $(RT) -lm

# embed mete el runtime y la plataforma dentro de neorecomp (lo usará desde la fase 6)
$(BUILD)/embed$(EXE): src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

# El recompilador: un solo ejecutable, con el runtime adentro
neorecomp$(EXE): src/neorecomp/neorecomp.c $(RT)
	$(CC) $(CFLAGS) -DNEORECOMP_VERSION='"$(VERSION)"' -Isrc/neorecomp -Isrc/runtime -o $@ \
		src/neorecomp/neorecomp.c $(RT) -lm

# Paquete para distribuir: neorecomp, la documentación y la licencia
DIST := dist/neorecomp-$(VERSION)-$(PLATFORM)
dist: neorecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp neorecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/neo.h src/runtime/neodb.h src/runtime/m68k.h src/runtime/m68k_ops.h src/runtime/z80.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/plat_%.o: src/platform/%.c src/platform/png.h src/platform/files.h src/platform/export.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

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
