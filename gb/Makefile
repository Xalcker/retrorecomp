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

RUNTIME := gb.c interp.c timer.c ppu.c apu.c mbc.c
RUNTIME_OBJ := $(RUNTIME:%.c=$(BUILD)/rt_%.o)

.PHONY: all clean test
all: $(BUILD)/libgbrt.a gbtest$(EXE)

# Corredor de ROMs de prueba sin ventana
gbtest$(EXE): src/tools/gbtest.c src/tools/png.c src/tools/png.h $(BUILD)/libgbrt.a
	$(CC) $(CFLAGS) -o $@ src/tools/gbtest.c src/tools/png.c $(BUILD)/libgbrt.a

test: gbtest$(EXE)
	sh tests/run_tests.sh

$(BUILD):
	mkdir -p $(BUILD)/gen

# El intérprete se genera con el mismo emisor que usa gbrecomp
$(BUILD)/gen_interp: src/tools/gen_interp.c src/emit/sm83_emit.c src/emit/sm83_emit.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/tools/gen_interp.c src/emit/sm83_emit.c

$(BUILD)/gen/interp_ops.inc: $(BUILD)/gen_interp | $(BUILD)
	$(BUILD)/gen_interp $@

$(BUILD)/rt_interp.o: src/runtime/interp.c $(BUILD)/gen/interp_ops.inc src/runtime/cpu_ops.h src/runtime/gb.h
	$(CC) $(CFLAGS) -I$(BUILD)/gen -c -o $@ $<

$(BUILD)/rt_%.o: src/runtime/%.c src/runtime/gb.h src/runtime/cpu_ops.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/libgbrt.a: $(RUNTIME_OBJ)
	ar rcs $@ $^

clean:
	rm -rf $(BUILD)
