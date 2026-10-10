# NESRecomp: runtime, intérprete 6502 y herramientas (en construcción).
CC     ?= cc
# En MSYS2 (Windows) no existe "cc": se usa gcc
ifeq ($(origin CC),default)
CC := $(if $(shell command -v cc 2>/dev/null),cc,gcc)
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD  := build
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
# Windows (MSYS2/MinGW): SDL2 enlazado de forma estática, para que el .exe no pida SDL2.dll
IS_WIN := $(or $(filter Windows_NT,$(OS)),$(findstring MINGW,$(shell uname -s 2>/dev/null)),$(findstring MSYS,$(shell uname -s 2>/dev/null)))
ifneq ($(IS_WIN),)
SDL_LIBS := -static $(shell pkg-config --libs --static sdl2 2>/dev/null) -lcomdlg32
else
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

RUNTIME := nes.c cpu6502.c ppu.c apu.c mapper.c sha1.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)
RT := $(BUILD)/libnesrt.a

.PHONY: all clean test
all: nestest nesshot nesaudio nestrace sramtest statetest mappertest inputtest ramdump nesbench nes nesrecomp

nestest: src/tools/nestest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

nesshot: src/tools/nesshot.c src/platform/png.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $^

nesbench: src/platform/bench.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^ -lm

nestrace: src/tools/nestrace.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

# Versión: la etiqueta de git más cercana (o "dev" fuera de un repo)
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

# Archivos que nesrecomp lleva adentro y escribe en cada proyecto generado
EMBED_RUNTIME  := nes.h nes.c cpu_ops.h cpu6502.c ppu.c apu.c mapper.c recomp.h sha1.c sha1.h console.h
EMBED_PLATFORM := main_sdl.c difftest.c bench.c ui_menu.c ui_menu.h ui_font.c ui_font.h i18n.c i18n.h \
                  settings.c settings.h files.c files.h export.c export.h png.c png.h inputs.h icono.h icono.rc icono.ico
EMBED_SRC := $(EMBED_RUNTIME:%=src/runtime/%) $(EMBED_PLATFORM:%=src/platform/%)
EMBED_ARGS := $(foreach f,$(EMBED_RUNTIME),runtime/$(f)=src/runtime/$(f)) \
              $(foreach f,$(EMBED_PLATFORM),platform/$(f)=src/platform/$(f))

$(BUILD)/embed: src/tools/embed.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/gen/embedded.c: $(BUILD)/embed $(EMBED_SRC) Makefile | $(BUILD)/gen
	$(BUILD)/embed $@ $(EMBED_ARGS)

$(BUILD)/gen:
	mkdir -p $@

# El recompilador: un solo ejecutable, con el runtime adentro
nesrecomp: src/nesrecomp/nesrecomp.c src/nesrecomp/embedded.h $(BUILD)/gen/embedded.c $(RT)
	$(CC) $(CFLAGS) -DNESRECOMP_VERSION='"$(VERSION)"' -Isrc/nesrecomp -Isrc/runtime -o $@ \
		src/nesrecomp/nesrecomp.c $(BUILD)/gen/embedded.c $(RT)

ramdump: src/tools/ramdump.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

inputtest: src/tools/inputtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

mappertest: src/tools/mappertest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

statetest: src/tools/statetest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

sramtest: src/tools/sramtest.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^

nesaudio: src/tools/nesaudio.c $(RT)
	$(CC) $(CFLAGS) -Isrc/runtime -o $@ $^ -lm

PLATFORM := main_sdl.c ui_menu.c ui_font.c i18n.c settings.c files.c export.c png.c
# Windows: el icono del .exe sale de un recurso (windres)
ifneq ($(IS_WIN),)
WINRES := $(BUILD)/icono_res.o
$(WINRES): src/platform/icono.rc src/platform/icono.ico | $(BUILD)
	windres -I src/platform -i $< -o $@ -O coff
endif

nes: $(PLATFORM:%=src/platform/%) $(wildcard src/platform/*.h) $(RT) $(WINRES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc/runtime -Isrc/platform -o $@ $(PLATFORM:%=src/platform/%) $(RT) $(WINRES) $(SDL_LIBS) -lm

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/nes.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RT): $(RUNTIME_OBJ)
	ar rcs $@ $^

$(BUILD):
	mkdir -p $@

test: nestest
	sh tests/run_tests.sh

clean:
	rm -rf $(BUILD) dist nestest nesshot nesaudio nestrace sramtest statetest mappertest nesbench nes nesrecomp
