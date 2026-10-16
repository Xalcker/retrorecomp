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

# SDL2 para el intérprete con ventana (nes). En Windows, enlazado de forma
# estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

RUNTIME := nes.c cpu6502.c ppu.c apu.c mapper.c sha1.c vrc7.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
# Lo del runtime que vive en shared/ (console.h)
SHARED_RT := console.h
rt = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_RT)),$(SHARED)/$(f),src/runtime/$(f)))
RT := $(BUILD)/libnesrt.a

PROGS := nestest nesshot nesaudio nestrace sramtest statetest mappertest inputtest nesramdump nesbench nes nesrecomp

.PHONY: all clean test dist testroms webtest apktest
all: $(PROGS:%=%$(EXE))

nestest$(EXE): src/tools/nestest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^

nesshot$(EXE): src/tools/nesshot.c $(SHARED)/png.c $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/nesshot.c $(SHARED)/png.c $(RT)

nesbench$(EXE): src/platform/bench.c $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/platform/bench.c $(RT) -lm

nestrace$(EXE): src/tools/nestrace.c $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/nestrace.c $(RT)

# Archivos que nesrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := nes.h nes.c cpu_ops.h cpu6502.c ppu.c apu.c mapper.c recomp.h console.h vrc7.c
EMBED_PLATFORM := main_sdl.c difftest.c bench.c cli.h ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h \
                  settings.c settings.h files.c files.h wav.h export.c export.h png.c png.h player_util.h player_common.h player_audio.h player_rom.h player_touch.h inputs.h input_host.h icono.h icono.rc icono.ico
# La APK de Android (make apk): shared/android va a platform/android del proyecto
EMBED_ANDROID  := android.mk Dockerfile settings.gradle build.gradle gradle.properties gradle-wrapper.properties app.gradle \
                  AndroidManifest.xml CMakeLists.txt RecompActivity.java android_jni.c
EMBED_SRC := $(call rt,$(EMBED_RUNTIME)) $(call plat,$(EMBED_PLATFORM)) $(SHARED)/random_pad.h $(SHARED)/trace_line.h $(SHARED)/ram_code.h $(SHARED)/sha1.c $(SHARED)/sha1.h \
             $(SHARED)/web/shell.html $(SHARED)/web/web.mk $(EMBED_ANDROID:%=$(SHARED)/android/%)
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=$(call rt,$(f))) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=$(call plat,$(f))) \
              $(foreach f,$(EMBED_ANDROID),platform/android/$(f)=$(SHARED)/android/$(f))

$(BUILD)/embed$(EXE): $(SHARED)/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS) runtime/sha1.c=$(SHARED)/sha1.c runtime/sha1.h=$(SHARED)/sha1.h platform/random_pad.h=$(SHARED)/random_pad.h runtime/trace_line.h=$(SHARED)/trace_line.h runtime/ram_code.h=$(SHARED)/ram_code.h \
	    platform/shell.html=$(SHARED)/web/shell.html platform/web.mk=$(SHARED)/web/web.mk

$(BUILD)/gen:
	mkdir -p $@

# El recompilador: un solo ejecutable, con el runtime adentro
nesrecomp$(EXE): src/nesrecomp/nesrecomp.c src/nesrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DNESRECOMP_VERSION='"$(VERSION)"' -Isrc/nesrecomp -Isrc/runtime -I$(SHARED) -o $@ \
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

nesramdump$(EXE): src/tools/nesramdump.c $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/nesramdump.c $(RT)

inputtest$(EXE): src/tools/inputtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^

mappertest$(EXE): src/tools/mappertest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^

statetest$(EXE): src/tools/statetest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^

sramtest$(EXE): src/tools/sramtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $^

nesaudio$(EXE): src/tools/nesaudio.c $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -o $@ src/tools/nesaudio.c $(RT) -lm

# (no se llama PLATFORM: ese nombre es el sistema del paquete de make dist, en el bloque común)
NES_SRC := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

nes$(EXE): $(call plat,$(NES_SRC)) $(wildcard src/platform/*.h) $(RT) $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(call plat,$(NES_SRC)) $(RT) $(WINRES) $(SDL_LIBS) -lm

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/nes.h $(SHARED)/trace_line.h $(SHARED)/ram_code.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

# Pruebas: necesitan las ROMs de prueba (make testroms)
test: all
	sh tests/run_tests.sh

# El juego recompilado en el navegador (retrorecomp#44): el proyecto de nestest con make web (Emscripten, o
# Docker con la imagen fija de platform/web.mk). El CI lo corre en Linux
webtest: nesrecomp$(EXE)
	rm -rf $(BUILD)/proy_web
	./nesrecomp$(EXE) testroms/nes-test-roms/other/nestest.nes -o $(BUILD)/proy_web --name prueba >/dev/null
	$(MAKE) -C $(BUILD)/proy_web web
	test -s $(BUILD)/proy_web/web/prueba.wasm && test -s $(BUILD)/proy_web/web/prueba.html && echo "ok    make web (nestest)"

# El juego recompilado en Android (retrorecomp#45): el proyecto de nestest con make apk (el SDK de Android, o Docker
# con la imagen de platform/android/Dockerfile). El CI lo corre en Linux, con el SDK que traen los runners
apktest: nesrecomp$(EXE)
	rm -rf $(BUILD)/proy_apk
	./nesrecomp$(EXE) testroms/nes-test-roms/other/nestest.nes -o $(BUILD)/proy_apk --name prueba >/dev/null
	$(MAKE) -C $(BUILD)/proy_apk apk $(if $(APK_ABIS),APK_ABIS="$(APK_ABIS)")
	unzip -l $(BUILD)/proy_apk/apk/prueba-debug.apk | grep -q 'lib/.*/libmain.so' && echo "ok    make apk (nestest)"

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
