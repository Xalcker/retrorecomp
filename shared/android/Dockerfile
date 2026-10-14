# La imagen de make apk cuando no hay SDK de Android en la máquina: JDK 17, SDK, NDK y CMake en versiones fijas
# (retrorecomp/shared/android). android.mk la construye la primera vez con la etiqueta APK_IMAGE; si cambias algo
# aquí, cambia también esa etiqueta. Es linux/amd64 porque el NDK de Linux solo existe para x86_64 (en un Mac con
# Apple Silicon, Docker la corre con Rosetta).
FROM --platform=linux/amd64 eclipse-temurin:17.0.20_8-jdk-jammy

RUN apt-get update && apt-get install -y --no-install-recommends make unzip curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV ANDROID_HOME=/opt/android-sdk
RUN curl -fsSL -o /tmp/tools.zip https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip \
    && echo "5fdcc763663eefb86a5b8879697aa6088b041e70  /tmp/tools.zip" | sha1sum -c - \
    && mkdir -p $ANDROID_HOME/cmdline-tools \
    && unzip -q /tmp/tools.zip -d $ANDROID_HOME/cmdline-tools \
    && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest \
    && rm /tmp/tools.zip
# sdkmanager comprueba la suma de cada paquete. Las cmdline-tools son las 19.0 y no las últimas: las más nuevas
# escriben el SDK en un formato que AGP 8 no entiende (avisa en cada compilación). AGP pide también platform-tools
RUN yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses > /dev/null \
    && $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --install \
       "platforms;android-35" "build-tools;35.0.0" "ndk;27.3.13750724" "cmake;3.22.1" "platform-tools" > /dev/null
# El Makefile del proyecto le pregunta a pkg-config por el SDL2 del escritorio, que la APK no usa: sin avisos
RUN printf '#!/bin/sh\nexit 0\n' > /usr/local/bin/pkg-config && chmod +x /usr/local/bin/pkg-config
