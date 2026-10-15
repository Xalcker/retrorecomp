# SMSRecomp: runtime, recompilador y herramientas.
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
SHARED_PLAT := ui_menu.c ui_menu.h ui_font.c ui_font.h png.c png.h files.h export.c export.h player_util.h player_common.h player_audio.h player_rom.h player_touch.h files.c wav.h inputs.h cli.h settings.c
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

# El cartucho, el Z80 común, el bus, la E/S y el VDP sin el dibujo (docs/PLAN.md: el dibujo, el PSG, la
# ventana y las herramientas llegan en las fases 3 y 4; el recompilador completo, en la 5)
RUNTIME := cart.c smsdb.c sms.c bus.c io.c vdp.c z80_host.c z80.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libsmsrt.a
# Lo del runtime que vive en shared/: el núcleo del Z80 (común con GENRecomp y NEORecomp; z80_ops.h es su
# semántica, la misma para el intérprete y el código generado) y console.h
SHARED_RT := z80.h z80_ops.h z80.c console.h
rt = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_RT)),$(SHARED)/$(f),src/runtime/$(f)))

PROGS := smsrecomp smsrun carttest bustest statetest z80test

.PHONY: all clean test dist testroms webtest apktest
all: $(PROGS:%=%$(EXE))

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

bustest$(EXE): src/tools/bustest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

statetest$(EXE): src/tools/statetest.c src/tools/romload.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/statetest.c $(RT) -lm

# Diagnóstico: corre una ROM sin ventana y resume lo que pasó (como genrun)
smsrun$(EXE): src/tools/smsrun.c src/tools/romload.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/smsrun.c $(RT) -lm

# z80test pone su propio bus (memoria plana): no usa el de la consola. Con --recomp ejecuta por el camino del
# código generado (z80_ops.h)
z80test$(EXE): $(SHARED)/z80test.c $(SHARED)/json.h $(SHARED)/z80_ops.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $(SHARED)/z80test.c $(RT) -lm

# El recompilador: por ahora solo lee el cartucho (--info); el runtime y la plataforma van adentro
# (embed) cuando genere proyectos, en la fase 5
smsrecomp$(EXE): src/smsrecomp/smsrecomp.c $(RT)
	$(CC) $(CFLAGS) -DSMSRECOMP_VERSION='"$(VERSION)"' -Isrc/runtime -I$(SHARED) -o $@ src/smsrecomp/smsrecomp.c $(RT) -lm

# Paquete para distribuir: smsrecomp, la documentación y la licencia
DIST := dist/smsrecomp-$(VERSION)-$(PLATFORM)
dist: smsrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp smsrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/sms.h src/runtime/z80_host.h $(SHARED)/z80.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -c -o $@ $<

# El núcleo del Z80 (shared/)
$(BUILD)/rt_z80.o: $(BUILD)/rt_%.o: $(SHARED)/%.c src/runtime/sms.h src/runtime/z80_host.h $(SHARED)/z80.h $(SHARED)/z80_ops.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: all
	sh tests/run_tests.sh

# El juego recompilado en el navegador y en Android (make web y make apk del proyecto generado): llegan en la
# fase 6, cuando smsrecomp genere proyectos. El CI los agrega entonces
webtest apktest:
	@echo "make $@: llega en la fase 6 (docs/PLAN.md)"

# Pruebas libres en testroms/ (no se versionan), en versiones fijas: las SingleStepTests del Z80, ZEXALL y
# homebrew con licencia MIT (ver tests/testroms.sh; con EXTRA=1, también las pruebas sin licencia)
testroms:
	sh tests/testroms.sh

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
