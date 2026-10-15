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

RUNTIME := cart.c gen.c bus.c io.c eeprom.c vdp.c vdp_render.c cpu68k.c z80.c z80bus.c psg.c ym2612.c audio.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libgenrt.a
# Lo del runtime que vive en shared/: los núcleos del 68000 y del Z80 (comunes con NEORecomp) y console.h
SHARED_RT := m68k.h m68k_ops.h cpu68k.c z80.h z80_ops.h z80.c console.h audio.c audio.h psg.c psg.h prof.h
rt = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_RT)),$(SHARED)/$(f),src/runtime/$(f)))

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

.PHONY: all clean test dist testroms webtest apktest
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
TOOL_DEPS := $(SHARED)/cli.h $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h src/tools/romload.h $(RT)
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
m68ktest$(EXE): $(SHARED)/m68ktest.c $(SHARED)/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $(SHARED)/m68ktest.c $(RT) -lm

z80test$(EXE): $(SHARED)/z80test.c $(SHARED)/json.h $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ $(SHARED)/z80test.c $(RT) -lm

# Archivos que genrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := gen.h cart.c gen.c bus.c io.c eeprom.c vdp.c vdp_render.c m68k.h m68k_ops.h m68k_host.h z80_host.h cpu68k.c z80.h z80_ops.h z80.c z80bus.c psg.c psg.h psg_host.h ym2612.c audio.c audio.h audio_host.h console.h recomp.h prof.h z80_recomp_host.h
EMBED_PLATFORM := main_sdl.c ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h settings.c settings.h \
                  files.c files.h wav.h export.c export.h png.c png.h player_util.h player_common.h player_audio.h player_rom.h player_touch.h icono.h icono.rc icono.ico inputs.h input_host.h cli.h \
                  difftest.c bench.c
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
genrecomp$(EXE): src/genrecomp/genrecomp.c $(SHARED)/dis68k.c $(SHARED)/dis68k.h $(SHARED)/z80_recomp.h $(SHARED)/visor_html.h \
                 src/genrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DGENRECOMP_VERSION='"$(VERSION)"' -Isrc/genrecomp -Isrc/runtime -I$(SHARED) -o $@ \
		src/genrecomp/genrecomp.c $(SHARED)/dis68k.c $(BUILD)/gen/embedded.c $(RT) -lm

# Oráculos: retro_run corre un núcleo libretro (Genesis Plus GX, BlastEm) sin ventana; los núcleos los
# baja tools/oracles/oracle.py a build/oracles/ (no van en el repo)
ifeq ($(shell uname -s 2>/dev/null),Linux)
DL_LIBS := -ldl
endif
$(BUILD)/retro_run$(EXE): $(SHARED)/retro_run.c $(SHARED)/libretro_min.h tools/oracles/oracle_host.h $(SHARED)/wav.h $(SHARED)/png.c $(TOOL_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/platform -Itools/oracles -o $@ $(SHARED)/retro_run.c $(SHARED)/png.c $(RT) -lm $(DL_LIBS)

# Paquete para distribuir: genrecomp, la documentación y la licencia
DIST := dist/genrecomp-$(VERSION)-$(PLATFORM)
dist: genrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp genrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gen.h src/runtime/m68k_host.h src/runtime/z80_host.h $(SHARED)/m68k.h $(SHARED)/m68k_ops.h $(SHARED)/z80.h $(SHARED)/trace_line.h $(SHARED)/ram_code.h $(SHARED)/audio.h $(SHARED)/psg.h src/runtime/psg_host.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -c -o $@ $<

# Los núcleos del 68000 y del Z80 (shared/)
$(BUILD)/rt_cpu68k.o $(BUILD)/rt_z80.o $(BUILD)/rt_audio.o $(BUILD)/rt_psg.o: $(BUILD)/rt_%.o: $(SHARED)/%.c src/runtime/gen.h src/runtime/m68k_host.h src/runtime/z80_host.h $(SHARED)/m68k.h $(SHARED)/m68k_ops.h $(SHARED)/z80.h $(SHARED)/z80_ops.h $(SHARED)/trace_line.h $(SHARED)/ram_code.h src/runtime/audio_host.h $(SHARED)/audio.h src/runtime/psg_host.h $(SHARED)/psg.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_%.o: src/platform/%.c $(SHARED)/png.h $(SHARED)/files.h $(SHARED)/export.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_export.o: $(SHARED)/export.c $(SHARED)/export.h $(SHARED)/wav.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_files.o: $(SHARED)/files.c $(SHARED)/files.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/plat_png.o: $(SHARED)/png.c $(SHARED)/png.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: all
	sh tests/run_tests.sh

# webtest y apktest usan por omisión la ROM mínima de las pruebas (tests/minima.sh): comprueban que la web y la APK se
# arman (con todo el runtime, la plataforma y SDL2) sin compilar el código generado grande de overdrive.bin, que con el
# clang del NDK tarda ~5,5 min por ABI y con emcc ~45 s; ese código ya lo compila y lo compara make test con el cc
# nativo. Con código generado grande (p. ej. después de tocar las banderas de shared/android/CMakeLists.txt, como el
# -g0 que evita que clang se quede sin memoria): make apktest APKTEST_ROM=testroms/demos/overdrive.bin
MINIMA      := $(BUILD)/tests/minima.bin
WEBTEST_ROM ?= $(MINIMA)
APKTEST_ROM ?= $(MINIMA)
$(MINIMA): tests/minima.sh
	sh tests/minima.sh $@

# El juego recompilado en el navegador (retrorecomp#44): el proyecto de WEBTEST_ROM con make web (Emscripten, o
# Docker con la imagen fija de platform/web.mk). El CI lo corre en Linux
webtest: genrecomp$(EXE) $(WEBTEST_ROM)
	rm -rf $(BUILD)/proy_web
	./genrecomp$(EXE) $(WEBTEST_ROM) -o $(BUILD)/proy_web --name prueba >/dev/null
	$(MAKE) -C $(BUILD)/proy_web web
	test -s $(BUILD)/proy_web/web/prueba.wasm && test -s $(BUILD)/proy_web/web/prueba.html && echo "ok    make web ($(notdir $(WEBTEST_ROM)))"

# El juego recompilado en Android (retrorecomp#45): el proyecto de APKTEST_ROM con make apk (el SDK de Android, o
# Docker con la imagen de platform/android/Dockerfile). El CI lo corre en Linux, con el SDK que traen los runners
apktest: genrecomp$(EXE) $(APKTEST_ROM)
	rm -rf $(BUILD)/proy_apk
	./genrecomp$(EXE) $(APKTEST_ROM) -o $(BUILD)/proy_apk --name prueba >/dev/null
	$(MAKE) -C $(BUILD)/proy_apk apk $(if $(APK_ABIS),APK_ABIS="$(APK_ABIS)")
	unzip -l $(BUILD)/proy_apk/apk/prueba-debug.apk | grep -q 'lib/.*/libmain.so' && echo "ok    make apk ($(notdir $(APKTEST_ROM)))"

# Pruebas libres en testroms/ (no se versionan), en versiones fijas: las SingleStepTests del
# 68000 y las demos Overdrive de Titan (ver tests/testroms.sh)
testroms:
	sh tests/testroms.sh

clean:
	rm -rf $(BUILD) dist $(PROGS:%=%$(EXE))
