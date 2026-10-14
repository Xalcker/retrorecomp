# make web: el juego recompilado para el navegador (WebAssembly). Es el mismo en los cuatro recompiladores
# (retrorecomp/shared/web); lo incluye el Makefile del proyecto, que antes define:
#   WEB_NAME    el ejecutable (web/$(WEB_NAME).html, .js y .wasm)
#   WEB_TITLE   el nombre que muestra la página
#   WEB_PREF    la carpeta de datos que da SDL_GetPrefPath en el navegador: /libsdl/<Sistema>Recomp/<nombre>/
#   WEB_CACHE   la copia de la ROM que guarda el juego en esa carpeta (rom.nes, juego.neo...)
#   WEB_ACCEPT  las extensiones del selector de la página (.nes)
#   INC         los -I y -D del proyecto
#   WEB_EXTRA   (opcional) más opciones de emcc, p. ej. --embed-file para meter un archivo en el build
# Compila runtime/, gen/, platform/ (menos difftest y bench) y, si el proyecto la tiene, bios/ (NEORecomp).
# Con emcc (Emscripten) instalado se usa ese; si no, Docker con la imagen de abajo (versión fija, la misma
# del CI). Docker guarda los ports de Emscripten (SDL2) en el volumen retrorecomp-emcache.

ifeq ($(WEB_NAME),web)
$(error un juego llamado "web" choca con make web: recompílalo con otro --name)
endif

EMSDK_IMAGE := emscripten/emsdk:6.0.10
EMCC        ?= emcc
WEB_CFLAGS  := -std=c99 -O2 -Wall -Wno-unused-label
WEB_LDFLAGS := -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB -lidbfs.js \
               -sEXPORTED_RUNTIME_METHODS=callMain,FS,addRunDependency,removeRunDependency

WEB_PLAT := $(filter-out platform/difftest.c platform/bench.c,$(wildcard platform/*.c))
WEB_OBJ  := $(patsubst runtime/%.c,build/web/rt_%.o,$(wildcard runtime/*.c)) \
            $(patsubst gen/%.c,build/web/gen_%.o,$(wildcard gen/*.c)) \
            $(patsubst platform/%.c,build/web/pl_%.o,$(WEB_PLAT)) \
            $(patsubst bios/%.c,build/web/bios_%.o,$(wildcard bios/*.c))

.PHONY: web
ifneq ($(shell command -v $(EMCC) 2>/dev/null),)
web: web/$(WEB_NAME).html
	@echo "Listo: web/$(WEB_NAME).html (sírvelo con un servidor local, p. ej. python3 -m http.server -d web)"
else ifneq ($(shell command -v docker 2>/dev/null),)
# Sin emcc: el mismo make dentro de la imagen de Emscripten. En Linux los archivos salen del contenedor como
# root: se devuelven al usuario. En Windows (MSYS2) Docker quiere la ruta con letra de unidad.
WEB_HOST_DIR := $(if $(filter Windows_NT,$(OS)),$(shell cygpath -m "$(CURDIR)"),$(CURDIR))
web:
	docker run --rm -v "$(WEB_HOST_DIR)":/src -w /src -v retrorecomp-emcache:/emsdk/upstream/emscripten/cache \
	    $(EMSDK_IMAGE) sh -c 'make -j$$(nproc) web EMCC=emcc && chown -R $(shell id -u):$(shell id -g) web build/web'
else
web:
	@echo "make web necesita Emscripten (emcc) o Docker (con la imagen $(EMSDK_IMAGE))" >&2; exit 1
endif

build/web:
	mkdir -p build/web

build/web/rt_%.o: runtime/%.c $(wildcard runtime/*.h) | build/web
	$(EMCC) $(WEB_CFLAGS) $(INC) -c -o $@ $<

build/web/gen_%.o: gen/%.c $(wildcard runtime/*.h) $(wildcard gen/*.h) | build/web
	$(EMCC) $(WEB_CFLAGS) $(INC) -c -o $@ $<

build/web/bios_%.o: bios/%.c $(wildcard runtime/*.h) $(wildcard bios/*.h) | build/web
	$(EMCC) $(WEB_CFLAGS) $(INC) -c -o $@ $<

build/web/pl_%.o: platform/%.c $(wildcard platform/*.h) $(wildcard runtime/*.h) | build/web
	$(EMCC) $(WEB_CFLAGS) $(INC) -sUSE_SDL=2 -c -o $@ $<

build/web/shell.html: platform/shell.html Makefile | build/web
	sed -e 's|@TITLE@|$(WEB_TITLE)|g' -e 's|@PREF@|$(WEB_PREF)|g' -e 's|@CACHE@|$(WEB_CACHE)|g' \
	    -e 's|@ACCEPT@|$(WEB_ACCEPT)|g' $< > $@

web/$(WEB_NAME).html: $(WEB_OBJ) build/web/shell.html
	mkdir -p web
	$(EMCC) $(WEB_CFLAGS) -o $@ $(WEB_OBJ) $(WEB_LDFLAGS) $(WEB_EXTRA) --shell-file build/web/shell.html -lm
