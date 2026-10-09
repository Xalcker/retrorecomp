# GBRecomp: runtime, intérprete generado y herramientas.
# Compila con gcc/clang C99 (Windows: MSYS2 UCRT64).

CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD  := build

# En Windows, el make de MSYS2 no le pasa TMP a gcc y éste intenta escribir
# en C:\WINDOWS. Usamos una carpeta temporal dentro de build/.
ifneq ($(shell command -v cygpath 2>/dev/null),)
TMPDIR_WIN := $(shell mkdir -p $(BUILD)/tmp && cygpath -m $(CURDIR)/$(BUILD)/tmp)
export TMP  := $(TMPDIR_WIN)
export TEMP := $(TMPDIR_WIN)
EXE := .exe
endif

RUNTIME := gb.c interp.c timer.c ppu.c apu.c mbc.c sha1.c save.c sgb.c compat.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)

.PHONY: all clean test dist
all: $(BUILD)/libgbrt.a gbtest$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE) $(BUILD)/sgb_controles$(EXE)

# Verificador de la música exportada (reproduce el VGM y lo compara con el WAV)
vgmcheck$(EXE): src/tools/vgmcheck.c $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ src/tools/vgmcheck.c $(BUILD)/libgbrt.a -lm

# Corredor de ROMs de prueba sin ventana
gbtest$(EXE): src/tools/gbtest.c src/platform/png.c src/platform/png.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ src/tools/gbtest.c src/platform/png.c $(BUILD)/libgbrt.a

# Dos Game Boy conectados por cable link, sin ventana
linktest$(EXE): src/tools/linktest.c src/platform/png.c src/platform/png.h src/platform/inputs.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ src/tools/linktest.c src/platform/png.c $(BUILD)/libgbrt.a

# Versión: la etiqueta de git más cercana (o "dev" fuera de un repo)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

# Archivos que gbrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := gb.h gb.c cpu_ops.h interp.c timer.c ppu.c apu.c mbc.c sha1.c sha1.h \
                  recomp.h console.h save.c sgb.c compat.c
EMBED_PLATFORM := main_sdl.c difftest.c inputs.h files.c files.h i18n.c i18n.h settings.c settings.h \
                  ui_menu.c ui_menu.h ui_font.c ui_font.h export.c export.h png.c png.h
EMBED_SRC := $(EMBED_RUNTIME:%=src/runtime/%) $(EMBED_PLATFORM:%=src/platform/%) $(BUILD)/gen/interp_ops.inc
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=src/runtime/$(f)) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=src/platform/$(f)) \
              runtime/interp_ops.inc=$(BUILD)/gen/interp_ops.inc

$(BUILD)/embed$(EXE): src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed$(EXE) $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed$(EXE) $@ $(EMBED_ARGS)

# El recompilador: un solo ejecutable, con el runtime adentro
gbrecomp$(EXE): src/gbrecomp/gbrecomp.c src/gbrecomp/embedded.h src/gbrecomp/viz.c src/gbrecomp/viz.h \
                src/emit/sm83_emit.c src/emit/sm83_emit.h src/runtime/sha1.c $(BUILD)/gen/embedded.c
	$(CC) $(CFLAGS) -DGBRECOMP_VERSION='"$(VERSION)"' -Isrc/gbrecomp -o $@ \
		src/gbrecomp/gbrecomp.c src/gbrecomp/viz.c src/emit/sm83_emit.c src/runtime/sha1.c $(BUILD)/gen/embedded.c

# Paquete para distribuir: gbrecomp y la documentación
DIST := dist/gbrecomp-$(VERSION)
dist: gbrecomp$(EXE)
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp gbrecomp$(EXE) README.md $(DIST)/
	cp docs/ARQUITECTURA.md $(DIST)/
	tar -czf $(DIST).tar.gz -C dist gbrecomp-$(VERSION)
	@echo "Listo: $(DIST).tar.gz"

test: gbtest$(EXE) $(BUILD)/sgb_controles$(EXE)
	sh tests/run_tests.sh

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

$(BUILD)/libgbrt.a: $(RUNTIME_OBJ)
	ar rcs $@ $^

clean:
	rm -rf $(BUILD) dist gbtest$(EXE) linktest$(EXE) gbrecomp$(EXE) vgmcheck$(EXE)
