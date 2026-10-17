# GBRecomp: runtime, intérprete generado y herramientas.
# Compila con gcc/clang C99 (Windows: MSYS2 UCRT64).

# --- Bloque común con NESRecomp (el mismo en los dos Makefiles) ---
CC     ?= cc
# MSYS2 sin "cc" (por ejemplo MINGW64): se usa gcc
ifeq ($(origin CC),default)
CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
# Sin contraer a*b+c en FMA (clang en arm64 lo hace por omisión): el mismo audio, bit a bit, en todas las
# plataformas (tests/golden.py). Con override, también si se da CFLAGS en la línea de make
override CFLAGS += -ffp-contract=off
BUILD  := build
# Lo que se comparte con los otros recompiladores (random_pad.h, trace_line.h...) vive en el submódulo
SHARED := ../shared
# Módulos de la ventana que viven en shared/ (el resto de src/platform es de esta consola)
SHARED_PLAT := ui_menu.c ui_menu.h ui_font.c ui_font.h png.c png.h files.h export.c export.h player_util.h player_common.h player_audio.h player_touch.h files.c wav.h inputs.h cli.h settings.c bench_comun.h difftest_comun.h
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
VERSION := $(patsubst gb-%,%,$(shell git describe --tags --match 'gb-v*' --always --dirty 2>/dev/null || echo dev))
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
# Lo del runtime que vive en shared/ (console.h)
SHARED_RT := console.h golden_hash.h metrica.h prof.h
rt = $(foreach f,$(1),$(if $(filter $(f),$(SHARED_RT)),$(SHARED)/$(f),src/runtime/$(f)))

.PHONY: all clean test dist testroms
all: $(BUILD)/libgbrt.a gbtest$(EXE) gbbench$(EXE) gbtrace$(EXE) gbshot$(EXE) gbrun$(EXE) gbaudio$(EXE) gbramdump$(EXE) gb$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE) statetest$(EXE) inputtest$(EXE) $(BUILD)/sgb_controles$(EXE)

# Verificador de la música exportada (reproduce el VGM y lo compara con el WAV)
vgmcheck$(EXE): src/tools/vgmcheck.c $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -o $@ src/tools/vgmcheck.c $(BUILD)/libgbrt.a -lm

# Las entradas grabadas y las opciones de las herramientas sin ventana: inputs.h y cli.h comunes (../shared),
# con lo propio del Game Boy en src/platform/input_host.h
INPUT_DEPS := $(SHARED)/inputs.h src/platform/input_host.h $(SHARED)/random_pad.h
CLI_DEPS := $(SHARED)/cli.h $(INPUT_DEPS) src/tools/romload.h

# Corredor de ROMs de prueba sin ventana
gbtest$(EXE): src/tools/gbtest.c src/runtime/gb_golden.h $(SHARED)/golden_hash.h $(SHARED)/png.c $(SHARED)/png.h $(INPUT_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbtest.c $(SHARED)/png.c $(BUILD)/libgbrt.a

# Velocidad del runtime sin ventana (tests/golden.py comprueba que no cambie nada)
# (src/platform/bench.c: el mismo archivo es el bench de los proyectos generados, con -DGB_RECOMP)
gbbench$(EXE): src/platform/bench.c $(SHARED)/bench_comun.h $(SHARED)/golden_hash.h $(INPUT_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/platform/bench.c $(BUILD)/libgbrt.a

# Herramientas sin ventana con las mismas opciones que las de las otras consolas (cli.h; más --model)
gbtrace$(EXE): src/tools/gbtrace.c $(CLI_DEPS) src/platform/ramtrace.h $(SHARED)/ram_code.h src/platform/romtrace.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbtrace.c $(BUILD)/libgbrt.a

# Estados guardados: guardar, cargar en otra máquina y comprobar que da exactamente lo mismo
statetest$(EXE): src/tools/statetest.c src/tools/romload.h src/runtime/gb_golden.h $(SHARED)/golden_hash.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -I$(SHARED) -Isrc/tools -o $@ src/tools/statetest.c $(BUILD)/libgbrt.a

# Entradas grabadas y opciones comunes de botones (inputs.h, cli.h)
inputtest$(EXE): src/tools/inputtest.c $(CLI_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/inputtest.c $(BUILD)/libgbrt.a

# RAM de trabajo tras correr la ROM (el equivalente de ramdump de NESRecomp)
gbramdump$(EXE): src/tools/gbramdump.c $(CLI_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbramdump.c $(BUILD)/libgbrt.a

gbshot$(EXE): src/tools/gbshot.c src/runtime/gb_golden.h $(SHARED)/golden_hash.h $(SHARED)/png.c $(SHARED)/png.h $(CLI_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbshot.c $(SHARED)/png.c $(BUILD)/libgbrt.a

gbrun$(EXE): src/tools/gbrun.c $(SHARED)/run_comun.h $(SHARED)/wav.h $(SHARED)/png.c $(SHARED)/png.h $(CLI_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbrun.c $(SHARED)/png.c $(BUILD)/libgbrt.a -lm

gbaudio$(EXE): src/tools/gbaudio.c $(CLI_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/gbaudio.c $(BUILD)/libgbrt.a -lm

# Dos Game Boy conectados por cable link, sin ventana
linktest$(EXE): src/tools/linktest.c $(SHARED)/png.c $(SHARED)/png.h $(INPUT_DEPS) $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -I$(SHARED) -o $@ src/tools/linktest.c $(SHARED)/png.c $(BUILD)/libgbrt.a

# El intérprete con ventana: gb juego.gb (como nes juego.nes en NESRecomp). Es la misma
# plataforma que la de los juegos generados, compilada con -DGB_INTERP. SDL2 como en
# NESRecomp: en Windows, enlazado de forma estática para que el .exe no pida SDL2.dll.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
ifeq ($(OS),Windows_NT)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif
GB_SRC := main_sdl.c files.c i18n.c settings.c ui_menu.c ui_font.c export.c gbexport.c png.c iconos.c
# Windows: el ícono del .exe (el del Game Boy)
ifeq ($(OS),Windows_NT)
WINRES := $(BUILD)/gb_icono.o
$(WINRES): src/platform/icono_gb.ico | $(BUILD)
	printf '1 ICON "icono_gb.ico"\n' > $(BUILD)/gb_icono.rc
	windres -I src/platform -i $(BUILD)/gb_icono.rc -o $@ -O coff
endif

gb$(EXE): $(call plat,$(GB_SRC)) $(wildcard src/platform/*.h) $(INPUT_DEPS) $(BUILD)/libgbrt.a $(WINRES)
	$(CC) $(CFLAGS) -I$(SHARED) $(SDL_CFLAGS) -DGB_INTERP -Isrc/runtime -Isrc/platform -o $@ \
		$(call plat,$(GB_SRC)) $(BUILD)/libgbrt.a $(WINRES) $(SDL_LIBS) -lm

# Archivos que gbrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := gb.h gb.c cpu_ops.h interp.c timer.c ppu.c apu.c mbc.c prof.h \
                  recomp.h console.h save.c sgb.c compat.c gb_golden.h golden_hash.h metrica.h
EMBED_PLATFORM := main_sdl.c difftest.c bench.c bench_comun.h difftest_comun.h inputs.h input_host.h cli.h ramtrace.h romtrace.h files.c files.h wav.h i18n.c i18n.h settings.c settings.h \
                  ui_menu.c ui_menu.h ui_font.c ui_font.h export.c export.h gbexport.c gbexport.h png.c png.h player_util.h player_common.h player_audio.h player_touch.h \
                  iconos.c iconos.h icono_gb.ico icono_gbc.ico icono_sgb.ico
EMBED_SRC := $(call rt,$(EMBED_RUNTIME)) $(call plat,$(EMBED_PLATFORM)) $(SHARED)/random_pad.h $(SHARED)/ram_code.h $(BUILD)/gen/interp_ops.inc $(SHARED)/sha1.c $(SHARED)/sha1.h
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=$(call rt,$(f))) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=$(call plat,$(f))) \
              platform/random_pad.h=$(SHARED)/random_pad.h platform/ram_code.h=$(SHARED)/ram_code.h \
              runtime/interp_ops.inc=$(BUILD)/gen/interp_ops.inc

$(BUILD)/embed$(EXE): $(SHARED)/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS) runtime/sha1.c=$(SHARED)/sha1.c runtime/sha1.h=$(SHARED)/sha1.h

# El recompilador: un solo ejecutable, con el runtime adentro
gbrecomp$(EXE): src/gbrecomp/gbrecomp.c src/gbrecomp/embedded.h src/gbrecomp/viz.c src/gbrecomp/viz.h \
                src/emit/sm83_emit.c src/emit/sm83_emit.h $(SHARED)/ram_code.h $(SHARED)/visor_html.h $(SHARED)/sha1.c $(BUILD)/gen/embedded.c
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
	python3 tests/run_tests.py

# ROMs de prueba libres en testroms/ (no se versionan): c-sp/game-boy-test-roms v7.0 (ver tests/testroms.py)
testroms:
	python3 tests/testroms.py

# Prueba de los controles 2-4 del Super Game Boy (la corre run_tests.py)
$(BUILD)/sgb_controles$(EXE): tests/sgb_controles.c $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -I$(SHARED) -o $@ tests/sgb_controles.c $(BUILD)/libgbrt.a

# (en Windows build/ ya existe por la carpeta temporal: build/gen va aparte)
$(BUILD) $(BUILD)/gen:
	mkdir -p $@

# El intérprete se genera con el mismo emisor que usa gbrecomp
$(BUILD)/gen_interp: src/tools/gen_interp.c src/emit/sm83_emit.c src/emit/sm83_emit.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/tools/gen_interp.c src/emit/sm83_emit.c

$(BUILD)/gen/interp_ops.inc: $(BUILD)/gen_interp | $(BUILD)/gen
	$(BUILD)/gen_interp $@

$(BUILD)/rt_interp.o: src/runtime/interp.c $(BUILD)/gen/interp_ops.inc src/runtime/cpu_ops.h src/runtime/gb.h $(SHARED)/metrica.h
	$(CC) $(CFLAGS) -I$(BUILD)/gen -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gb.h src/runtime/cpu_ops.h $(SHARED)/metrica.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/rt_sha1.o: $(SHARED)/sha1.c $(SHARED)/sha1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SHARED) -c -o $@ $<

$(BUILD)/libgbrt.a: $(RUNTIME_OBJ)
	ar rcs $@ $^

clean:
	rm -rf $(BUILD) dist statetest$(EXE) inputtest$(EXE) gbtest$(EXE) gbbench$(EXE) gbtrace$(EXE) gbshot$(EXE) gbrun$(EXE) gbaudio$(EXE) gbramdump$(EXE) gb$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE)
