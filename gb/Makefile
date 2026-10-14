# GBRecomp: runtime, intérprete generado y herramientas.
# Compila con gcc/clang C99 (Windows: MSYS2 UCRT64).

# --- Bloque común con NESRecomp (el mismo en los dos Makefiles) ---
CC     ?= cc
# MSYS2 sin "cc" (por ejemplo MINGW64): se usa gcc
ifeq ($(origin CC),default)
CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD  := build
# Lo que se comparte con los otros recompiladores (random_pad.h, trace_line.h...) vive en el submódulo
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

RUNTIME := gb.c interp.c timer.c ppu.c apu.c mbc.c sha1.c save.c sgb.c compat.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)

.PHONY: all clean test dist testroms
all: $(BUILD)/libgbrt.a gbtest$(EXE) gbbench$(EXE) gbtrace$(EXE) gbshot$(EXE) gbaudio$(EXE) gbramdump$(EXE) gb$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE) $(BUILD)/sgb_controles$(EXE)

# Verificador de la música exportada (reproduce el VGM y lo compara con el WAV)
vgmcheck$(EXE): src/tools/vgmcheck.c $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/vgmcheck.c $(BUILD)/libgbrt.a -lm

# Corredor de ROMs de prueba sin ventana
gbtest$(EXE): src/tools/gbtest.c $(SHARED)/png.c $(SHARED)/png.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbtest.c $(SHARED)/png.c $(BUILD)/libgbrt.a

# Velocidad del runtime sin ventana (tests/golden.sh comprueba que no cambie nada)
gbbench$(EXE): src/tools/gbbench.c src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbbench.c $(BUILD)/libgbrt.a

# Herramientas sin ventana con las mismas opciones que las de NESRecomp (--frames, --input, --seed, --model)
gbtrace$(EXE): src/tools/gbtrace.c src/tools/cli.h $(SHARED)/random_pad.h src/platform/inputs.h src/platform/ramtrace.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbtrace.c $(BUILD)/libgbrt.a

# RAM de trabajo tras correr la ROM (el equivalente de ramdump de NESRecomp)
gbramdump$(EXE): src/tools/gbramdump.c src/tools/cli.h $(SHARED)/random_pad.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbramdump.c $(BUILD)/libgbrt.a

gbshot$(EXE): src/tools/gbshot.c src/tools/cli.h $(SHARED)/random_pad.h $(SHARED)/png.c $(SHARED)/png.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbshot.c $(SHARED)/png.c $(BUILD)/libgbrt.a

gbaudio$(EXE): src/tools/gbaudio.c src/tools/cli.h $(SHARED)/random_pad.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/gbaudio.c $(BUILD)/libgbrt.a -lm

# Dos Game Boy conectados por cable link, sin ventana
linktest$(EXE): src/tools/linktest.c $(SHARED)/png.c $(SHARED)/png.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/linktest.c $(SHARED)/png.c $(BUILD)/libgbrt.a

# El intérprete con ventana: gb juego.gb (como nes juego.nes en NESRecomp). Es la misma
# plataforma que la de los juegos generados, compilada con -DGB_INTERP. SDL2 como en
# NESRecomp: en Windows, enlazado de forma estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif
GB_SRC := main_sdl.c files.c i18n.c settings.c ui_menu.c ui_font.c export.c png.c iconos.c
# Windows: el ícono del .exe (el del Game Boy)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/gb_icono.o
$(WINRES): src/platform/icono_gb.ico | $(BUILD)
	printf '1 ICON "icono_gb.ico"\n' > $(BUILD)/gb_icono.rc
	windres -I src/platform -i $(BUILD)/gb_icono.rc -o $@ -O coff
endif

gb$(EXE): $(call plat,$(GB_SRC)) $(wildcard src/platform/*.h) $(BUILD)/libgbrt.a $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -DGB_INTERP -Isrc/runtime -Isrc/platform -o $@ \
		$(call plat,$(GB_SRC)) $(BUILD)/libgbrt.a $(WINRES) $(SDL_LIBS) -lm

# Archivos que gbrecomp lleva adentro y escribe en cada proyecto generado
# (tools/visor.html no va al proyecto: es para gbrecomp --html)
EMBED_RUNTIME  := gb.h gb.c cpu_ops.h interp.c timer.c ppu.c apu.c mbc.c \
                  recomp.h console.h save.c sgb.c compat.c
EMBED_PLATFORM := main_sdl.c difftest.c inputs.h ramtrace.h files.c files.h i18n.c i18n.h settings.c settings.h \
                  ui_menu.c ui_menu.h ui_font.c ui_font.h export.c export.h png.c png.h \
                  iconos.c iconos.h icono_gb.ico icono_gbc.ico icono_sgb.ico
EMBED_SRC := $(EMBED_RUNTIME:%=src/runtime/%) $(call plat,$(EMBED_PLATFORM)) $(SHARED)/random_pad.h $(BUILD)/gen/interp_ops.inc tools/visor.html $(SHARED)/sha1.c $(SHARED)/sha1.h
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=src/runtime/$(f)) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=$(call plat,$(f))) \
              platform/random_pad.h=$(SHARED)/random_pad.h \
              runtime/interp_ops.inc=$(BUILD)/gen/interp_ops.inc \
              tools/visor.html=tools/visor.html

$(BUILD)/embed$(EXE): src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS) runtime/sha1.c=$(SHARED)/sha1.c runtime/sha1.h=$(SHARED)/sha1.h

# El recompilador: un solo ejecutable, con el runtime adentro
gbrecomp$(EXE): src/gbrecomp/gbrecomp.c src/gbrecomp/embedded.h src/gbrecomp/viz.c src/gbrecomp/viz.h \
                src/emit/sm83_emit.c src/emit/sm83_emit.h $(SHARED)/sha1.c $(BUILD)/gen/embedded.c
	$(CC) $(CFLAGS) -DGBRECOMP_VERSION='"$(VERSION)"' -Isrc/gbrecomp -I$(SHARED) -o $@ \
		src/gbrecomp/gbrecomp.c src/gbrecomp/viz.c src/emit/sm83_emit.c $(SHARED)/sha1.c $(BUILD)/gen/embedded.c

# Paquete para distribuir: gbrecomp, la documentación y la licencia
DIST := dist/gbrecomp-$(VERSION)-$(PLATFORM)
dist: gbrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp gbrecomp$(EXE) README.md LICENSE $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist $(notdir $(DIST))
	@echo "Listo: $(DIST).tar.gz"

# Pruebas: necesitan las ROMs de prueba (make testroms)
test: all
	sh tests/run_tests.sh

# ROMs de prueba libres en testroms/ (no se versionan): c-sp/game-boy-test-roms v7.0
TESTROMS_URL := https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip
testroms:
	@if [ -d testroms/blargg ]; then echo "testroms/ ya tiene las ROMs de prueba"; else \
		mkdir -p testroms $(BUILD) && curl -fsSL -o $(BUILD)/testroms.zip $(TESTROMS_URL) && \
		unzip -q -o $(BUILD)/testroms.zip -d testroms && rm -f $(BUILD)/testroms.zip && \
		echo "ROMs de prueba en testroms/"; fi

# Prueba de los controles 2-4 del Super Game Boy (la corre run_tests.sh)
$(BUILD)/sgb_controles$(EXE): tests/sgb_controles.c $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -o $@ tests/sgb_controles.c $(BUILD)/libgbrt.a

# (en Windows build/ ya existe por la carpeta temporal: build/gen va aparte)
$(BUILD) $(BUILD)/gen:
	mkdir -p $@

# El intérprete se genera con el mismo emisor que usa gbrecomp
$(BUILD)/gen_interp: src/tools/gen_interp.c src/emit/sm83_emit.c src/emit/sm83_emit.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/tools/gen_interp.c src/emit/sm83_emit.c

$(BUILD)/gen/interp_ops.inc: $(BUILD)/gen_interp | $(BUILD)/gen
	$(BUILD)/gen_interp $@

$(BUILD)/rt_interp.o: src/runtime/interp.c $(BUILD)/gen/interp_ops.inc src/runtime/cpu_ops.h src/runtime/gb.h
	$(CC) $(CFLAGS) -I$(BUILD)/gen -c -o $@ $<

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gb.h src/runtime/cpu_ops.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/libgbrt.a: $(RUNTIME_OBJ)
	ar rcs $@ $^

clean:
	rm -rf $(BUILD) dist gbtest$(EXE) gbbench$(EXE) gbtrace$(EXE) gbshot$(EXE) gbaudio$(EXE) gbramdump$(EXE) gb$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE)
