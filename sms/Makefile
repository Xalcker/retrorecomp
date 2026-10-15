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

# El cartucho, el Z80 y el PSG comunes, el bus, la E/S, el VDP con su dibujo y la mezcla del sonido (docs/PLAN.md:
# las herramientas sin ventana llegan en la fase 4; el recompilador completo, en la 5)
RUNTIME := cart.c smsdb.c sms.c bus.c io.c vdp.c vdp_render.c z80_host.c z80.c psg.c audio.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libsmsrt.a
# Lo del runtime que vive en shared/: el núcleo del Z80 (común con GENRecomp y NEORecomp; z80_ops.h es su
# semántica, la misma para el intérprete y el código generado) y console.h
SHARED_RT := z80.h z80_ops.h z80.c psg.c psg.h audio.c audio.h console.h
rt = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_RT)),$(SHARED)/$(f),src/runtime/$(f)))

# La ventana: el intérprete con menús (sms)
SMS_SRC := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

sms$(EXE): $(call plat,$(SMS_SRC)) $(wildcard src/platform/*.h) $(SHARED)/player_common.h $(SHARED)/player_rom.h $(SHARED)/player_touch.h $(SHARED)/inputs.h $(RT) $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(call plat,$(SMS_SRC)) $(RT) $(WINRES) $(SDL_LIBS) -lm

PROGS := smsrecomp smstrace smsshot smsaudio smsbench smsramdump smsrun sms \
         carttest bustest vdptest audiotest statetest inputtest z80test

# SDL2 para el intérprete con ventana (sms). En Windows, enlazado de forma
# estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

.PHONY: all clean test dist testroms webtest apktest
all: $(PROGS:%=%$(EXE))

carttest$(EXE): src/tools/carttest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

bustest$(EXE): src/tools/bustest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

vdptest$(EXE): src/tools/vdptest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

audiotest$(EXE): src/tools/audiotest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^ -lm

# Herramientas sin ventana con las opciones comunes (cli.h)
TOOL_DEPS := $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h src/tools/romload.h $(RT)
smstrace$(EXE): src/tools/smstrace.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/smstrace.c $(RT) -lm

smsshot$(EXE): src/tools/smsshot.c $(SHARED)/png.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/smsshot.c $(SHARED)/png.c $(RT) -lm

smsaudio$(EXE): src/tools/smsaudio.c $(SHARED)/wav.h $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/smsaudio.c $(RT) -lm

smsbench$(EXE): src/platform/bench.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/platform/bench.c $(RT) -lm

smsramdump$(EXE): src/tools/smsramdump.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/smsramdump.c $(RT) -lm

inputtest$(EXE): src/tools/inputtest.c $(TOOL_DEPS)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/inputtest.c $(RT) -lm

# El juego de prueba del contrato (tests/testroms.sh lo escribe en testroms/sms/contrato.sms) y la ROM mínima
$(BUILD)/mkcontrato$(EXE): src/tools/mkcontrato.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -o $@ src/tools/mkcontrato.c

statetest$(EXE): src/tools/statetest.c src/tools/romload.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/statetest.c $(RT) -lm

# Diagnóstico: corre una ROM sin ventana y resume lo que pasó (como genrun)
smsrun$(EXE): src/tools/smsrun.c src/tools/romload.h $(SHARED)/png.c $(SHARED)/wav.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/smsrun.c $(SHARED)/png.c $(RT) -lm

# z80test pone su propio bus (memoria plana): no usa el de la consola. Con --recomp ejecuta por el camino del
# código generado (z80_ops.h)
z80test$(EXE): $(SHARED)/z80test.c $(SHARED)/json.h $(SHARED)/z80_ops.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $(SHARED)/z80test.c $(RT) -lm

# Archivos que smsrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := sms.h cart.c smsdb.c sms.c bus.c io.c vdp.c vdp_render.c z80_host.c z80_host.h z80_recomp_host.h recomp.h \
                  psg_host.h audio_host.h z80.h z80_ops.h z80.c psg.c psg.h audio.c audio.h console.h
EMBED_PLATFORM := main_sdl.c ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h settings.c settings.h \
                  files.c files.h wav.h export.c export.h png.c png.h player_util.h player_common.h player_audio.h player_rom.h \
                  player_touch.h icono.h icono.rc icono.ico inputs.h input_host.h cli.h difftest.c bench.c
EMBED_SRC := $(call rt,$(EMBED_RUNTIME)) $(call plat,$(EMBED_PLATFORM)) $(SHARED)/random_pad.h $(SHARED)/trace_line.h \
             $(SHARED)/ram_code.h $(SHARED)/sha1.c $(SHARED)/sha1.h
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=$(call rt,$(f))) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=$(call plat,$(f)))

$(BUILD)/embed$(EXE): $(SHARED)/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/sms/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/sms
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS) runtime/sha1.c=$(SHARED)/sha1.c runtime/sha1.h=$(SHARED)/sha1.h \
	    runtime/trace_line.h=$(SHARED)/trace_line.h runtime/ram_code.h=$(SHARED)/ram_code.h platform/random_pad.h=$(SHARED)/random_pad.h

$(BUILD)/sms:
	mkdir -p $@

# El recompilador: un solo ejecutable, con el runtime y la plataforma adentro (embed); el Z80 se compila con el
# recompilador común (z80_recomp.h) y se desensambla con disz80
smsrecomp$(EXE): src/smsrecomp/smsrecomp.c src/smsrecomp/embedded.h $(SHARED)/z80_recomp.h $(SHARED)/disz80.c $(SHARED)/disz80.h \
                 $(SHARED)/visor_html.h $(SHARED)/ram_code.h $(BUILD)/sms/embedded.c $(RT)
	$(CC) $(CFLAGS) -DSMSRECOMP_VERSION='"$(VERSION)"' -Isrc/smsrecomp -Isrc/runtime -I$(SHARED) -o $@ \
		src/smsrecomp/smsrecomp.c $(SHARED)/disz80.c $(BUILD)/sms/embedded.c $(RT) -lm

# Oráculos: retro_run corre un núcleo libretro (Genesis Plus GX, Gearsystem) sin ventana; los núcleos los baja
# tools/oracles/oracle.py a build/oracles/ (no van en el repo)
ifeq ($(shell uname -s 2>/dev/null),Linux)
DL_LIBS := -ldl
endif
$(BUILD)/retro_run$(EXE): $(SHARED)/retro_run.c $(SHARED)/libretro_min.h tools/oracles/oracle_host.h $(SHARED)/wav.h $(SHARED)/png.c $(TOOL_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -Itools/oracles -o $@ $(SHARED)/retro_run.c $(SHARED)/png.c $(RT) -lm $(DL_LIBS)

# Paquete para distribuir: smsrecomp, la documentación y la licencia
DIST := dist/smsrecomp-$(VERSION)-$(PLATFORM)
dist: smsrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp smsrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

RT_DEPS := src/runtime/sms.h src/runtime/z80_host.h src/runtime/psg_host.h src/runtime/audio_host.h $(SHARED)/z80.h $(SHARED)/psg.h $(SHARED)/audio.h $(SHARED)/trace_line.h $(SHARED)/ram_code.h $(SHARED)/z80_ops.h
$(BUILD)/rt_%.o: src/runtime/%.c $(RT_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -c -o $@ $<

# El Z80, el PSG y la mezcla (shared/)
$(BUILD)/rt_z80.o $(BUILD)/rt_psg.o $(BUILD)/rt_audio.o: $(BUILD)/rt_%.o: $(SHARED)/%.c $(RT_DEPS) $(SHARED)/z80_ops.h | $(BUILD)
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
