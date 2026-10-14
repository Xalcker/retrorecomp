# make apk: el juego recompilado como APK de Android. Es el mismo en los cuatro recompiladores
# (retrorecomp/shared/android); lo incluye el Makefile del proyecto, que antes define:
#   APK_NAME    el juego: apk/$(APK_NAME)-debug.apk (la de release, apk/$(APK_NAME).apk)
#   APK_TITLE   el nombre que muestra el teléfono
#   APK_SYSTEM  el recompilador en minúsculas (nesrecomp...): el paquete es io.github.xalcker.<sistema>.<juego>
#   APK_CACHE   la copia de la ROM que guarda el juego en su carpeta de datos (rom.nes, juego.neo...)
#   APK_ICON    el ícono, un arreglo de C con un PNG o los pixeles RGBA de un cuadrado (archivo o archivo:arreglo)
#   INC         los -I y -D del proyecto
#   APK_DATA    (opcional) archivos que van en la APK y que el juego encuentra en su carpeta de datos la primera
#               vez, como origen=nombre (bios/neobios.bin=bios.bin)
# Opciones de make apk:
#   ROM=archivo  mete la ROM en la APK, para no tener que elegirla en el teléfono. Solo para uso personal: esa APK
#                lleva la ROM y no se comparte
#   APK_ABIS     las arquitecturas (arm64-v8a x86_64: teléfonos y el emulador)
#   APK_RELEASE=1  la APK de release, firmada con tu keystore: APK_KEYSTORE (ruta), APK_KEYSTORE_PASS,
#                APK_KEY_ALIAS y APK_KEY_PASS en el entorno. La keystore nunca va al repositorio; con Docker, la ruta
#                es relativa a la carpeta del proyecto (el contenedor solo ve esa carpeta)
# Compila aquí si hay SDK de Android (ANDROID_HOME o ANDROID_SDK_ROOT) y un JDK 17 o más nuevo (JAVA_HOME o java);
# Gradle baja lo que falte del SDK y el NDK si las licencias están aceptadas (ANDROID_NDK_HOME elige otro NDK).
# Si no hay SDK, usa Docker con la imagen de platform/android/Dockerfile (versiones fijas), que construye la primera
# vez (unos 3 GB). SDL2 sale de su código fuente, que se baja una vez a APK_CACHE_DIR. En Docker, esa caché, la de
# Gradle y la clave de depuración quedan en el volumen retrorecomp-android; como dos Gradle en contenedores distintos no
# pueden compartir esa caché, un make apk espera a que termine el otro (el contenedor se llama retrorecomp-android).
# La APK de depuración va firmada con la clave de depuración de cada máquina: la de otra máquina no se instala encima
# de la anterior (hay que desinstalarla, y con ella se van las partidas). Con APK_RELEASE y tu keystore no pasa.

ifeq ($(APK_NAME),apk)
$(error un juego llamado "apk" choca con make apk: recompílalo con otro --name)
endif

APK_SDL_VER   := 2.32.10
APK_SDL_SHA   := 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
APK_IMAGE     := retrorecomp-android:35-r27d-1
APK_ABIS      ?= arm64-v8a x86_64
APK_CACHE_DIR ?= $(HOME)/.cache/retrorecomp
APK_SDK       := $(firstword $(ANDROID_HOME) $(ANDROID_SDK_ROOT))
APK_DIR       := build/android
APK_SDL       := $(APK_CACHE_DIR)/SDL2-$(APK_SDL_VER)
APK_KIND      := $(if $(APK_RELEASE),release,debug)
APK_OUT       := apk/$(APK_NAME)$(if $(APK_RELEASE),,-debug).apk
# Gradle y CMake quieren rutas de Windows (C:/...) en MSYS2. En Windows, AGP rechaza una carpeta con acentos si no se
# le dice que la acepte (overridePathCheck)
apk_comma := ,
apk_empty :=
apk_space := $(apk_empty) $(apk_empty)
apk_path = $(if $(filter Windows_NT,$(OS)),$(shell cygpath -m "$(1)"),$(1))
APK_SHA256 := $(if $(shell command -v sha256sum 2>/dev/null),sha256sum,shasum -a 256)
# El java con el que se comprueba la versión (en MSYS2, JAVA_HOME viene como C:\...)
APK_JAVA := $(if $(JAVA_HOME),$(if $(filter Windows_NT,$(OS)),$(shell cygpath -u "$(JAVA_HOME)"),$(JAVA_HOME))/bin/java,java)

.PHONY: apk
ifneq ($(APK_SDK),)
apk: $(APK_SDL)/CMakeLists.txt
	@v=$$("$(APK_JAVA)" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -n 1); \
	if [ "$${v:-0}" -lt 17 ]; then \
	    echo "make apk necesita un JDK 17 o más nuevo: define JAVA_HOME (por ejemplo, el JBR de Android Studio)" >&2; exit 1; fi
	@if [ -n "$(APK_RELEASE)" ] && [ -z "$$APK_KEYSTORE" ]; then \
	    echo "APK_RELEASE=1 necesita tu keystore: APK_KEYSTORE, APK_KEYSTORE_PASS, APK_KEY_ALIAS y APK_KEY_PASS" >&2; exit 1; fi
	rm -rf $(APK_DIR)/assets
	mkdir -p $(APK_DIR)/app $(APK_DIR)/gradle/wrapper $(APK_DIR)/assets/datos apk
	cp platform/android/settings.gradle platform/android/build.gradle platform/android/gradle.properties $(APK_DIR)/
	cp platform/android/app.gradle $(APK_DIR)/app/build.gradle
	cp platform/android/gradle-wrapper.properties $(APK_SDL)/android-project/gradle/wrapper/gradle-wrapper.jar $(APK_DIR)/gradle/wrapper/
	cp $(APK_SDL)/android-project/gradlew $(APK_DIR)/
	$(foreach d,$(APK_DATA),cp "$(firstword $(subst =, ,$(d)))" "$(APK_DIR)/assets/datos/$(lastword $(subst =, ,$(d)))" &&) true
	$(if $(ROM),cp "$(ROM)" "$(APK_DIR)/assets/datos/$(APK_CACHE)")
	cd $(APK_DIR) && bash gradlew --console=plain assemble$(if $(APK_RELEASE),Release,Debug) \
	    -PrecompDir="$(call apk_path,$(CURDIR))" -PsdlDir="$(call apk_path,$(abspath $(APK_SDL)))" \
	    -PappName='$(subst ','\'',$(APK_NAME))' -PappTitle='$(subst ','\'',$(APK_TITLE))' -PappSystem=$(APK_SYSTEM) \
	    -Picon='$(APK_ICON)' -Pinc='$(INC)' -Pabis=$(subst $(apk_space),$(apk_comma),$(strip $(APK_ABIS))) \
	    $(if $(filter Windows_NT,$(OS)),-Pandroid.overridePathCheck=true)
	cp $(APK_DIR)/app/build/outputs/apk/$(APK_KIND)/app-$(APK_KIND).apk $(APK_OUT)
	@echo "Listo: $(APK_OUT) (instálala con adb install -r $(APK_OUT))"

# El código fuente de SDL2, una sola vez (comprueba la suma)
$(APK_SDL)/CMakeLists.txt:
	mkdir -p "$(APK_CACHE_DIR)"
	curl -fL -o "$(APK_SDL).tar.gz.part" \
	    https://github.com/libsdl-org/SDL/releases/download/release-$(APK_SDL_VER)/SDL2-$(APK_SDL_VER).tar.gz
	echo "$(APK_SDL_SHA)  $(APK_SDL).tar.gz.part" | $(APK_SHA256) -c -
	tar xzf "$(APK_SDL).tar.gz.part" -C "$(APK_CACHE_DIR)" && rm "$(APK_SDL).tar.gz.part"
	touch $@
else ifneq ($(shell command -v docker 2>/dev/null),)
# Sin SDK: el mismo make dentro de la imagen. La ROM de ROM= se copia antes al proyecto, que es lo único que ve el
# contenedor. En Linux los archivos salen del contenedor como root: se devuelven al usuario. En Windows (MSYS2)
# Docker quiere la ruta con letra de unidad.
APK_HOST_DIR := $(if $(filter Windows_NT,$(OS)),$(shell cygpath -m "$(CURDIR)"),$(CURDIR))
apk:
	docker image inspect $(APK_IMAGE) > /dev/null 2>&1 || docker build --platform linux/amd64 -t $(APK_IMAGE) platform/android
	mkdir -p build
	rm -f build/apk-rom
	$(if $(ROM),cp "$(ROM)" build/apk-rom)
	@while docker container inspect retrorecomp-android > /dev/null 2>&1; do \
	    echo "esperando a que termine otro make apk (el contenedor retrorecomp-android)"; sleep 10; done
	docker run --rm --name retrorecomp-android --platform linux/amd64 -v "$(APK_HOST_DIR)":/src -w /src -v retrorecomp-android:/cache \
	    -e APK_CACHE_DIR=/cache -e GRADLE_USER_HOME=/cache/gradle -e ANDROID_USER_HOME=/cache/android $(if $(APK_RELEASE),-e APK_KEYSTORE -e APK_KEYSTORE_PASS -e APK_KEY_ALIAS -e APK_KEY_PASS) \
	    $(APK_IMAGE) sh -c 'make apk APK_ABIS="$(APK_ABIS)" $(if $(ROM),ROM=build/apk-rom) $(if $(APK_RELEASE),APK_RELEASE=1); r=$$?; \
	        chown -R $(shell id -u):$(shell id -g) build $$([ -d apk ] && echo apk); exit $$r'
else
apk:
	@echo "make apk necesita el SDK de Android (ANDROID_HOME y un JDK 17) o Docker (con la imagen de platform/android/Dockerfile)" >&2; exit 1
endif
