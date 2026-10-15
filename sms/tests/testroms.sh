#!/bin/sh
# make testroms: baja las pruebas libres a testroms/ (no se versionan), cada una en una versión fija.
#   - testroms/z80: SingleStepTests del Z80 (github.com/SingleStepTests/z80, MIT; el mismo commit que GENRecomp
#     y NEORecomp; se guardan comprimidas, z80test las lee con gunzip -c)
#   - testroms/zexall: ZEXALL y ZEXDOC para el SMS (github.com/maxim-zhao/zexall-sms, GPL-2.0, la v0.21 con
#     su SHA-256): el ejercitador de instrucciones del Z80 de Frank Cringle; escribe el resultado en la SRAM y
#     en la consola de depuración SDSC
#   - testroms/homebrew: juegos con licencia MIT (sus releases de GitHub, con el SHA-256 de cada zip):
#     Dragon Blaster 0.11, Food Fighter 0.4 y River Strike 0.10 de haroldo-ok (SMS, 32 KB) y Waternet 1.0 de
#     joyrider3774 (SMS y Game Gear, 64 KB)
#   - con EXTRA=1 (opcional, no en el CI): pruebas de hardware sin licencia publicada, que se bajan para usarlas
#     en tu máquina y nunca se redistribuyen: el VDPTEST de FluBBa, las pruebas de Charles MacDonald (contador V,
#     scroll, paleta de 64 colores), PauseTest y SMSCycleCounter de FluBBa, el Sound Test, las dos pruebas del
#     Game Gear de SMS Power! y el SMS Test Suite de sverx. Van en testroms/extra
cd "$(dirname "$0")/.." || exit 2
mkdir -p testroms build

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# SKIP_SST=1 (el CI de macOS y Windows): sin las SingleStepTests, que son lo que más pesa; make test las omite
# si no están. En Linux y en tu máquina se bajan completas.
if [ -n "$SKIP_SST" ]; then echo "SKIP_SST: sin las SingleStepTests del Z80"
elif [ -f "testroms/z80/v1/00.json.gz" ]; then echo "testroms/z80 ya tiene las SingleStepTests del Z80"
else
    rm -rf build/z80tests && mkdir -p build/z80tests testroms/z80/v1 && \
    curl -fsSL -o build/z80.tar.gz https://codeload.github.com/SingleStepTests/z80/tar.gz/ebe1875d48f374bcfd4b505d8eb8ee751568b5f7 && \
    tar -xzf build/z80.tar.gz -C build/z80tests --strip-components=1 && rm -f build/z80.tar.gz && \
    for f in build/z80tests/v1/*.json; do gzip -c "$f" > "testroms/z80/v1/$(basename "$f").gz" || exit 1; done && \
    cp build/z80tests/LICENSE testroms/z80/ && rm -rf build/z80tests && \
    echo "SingleStepTests del Z80 en testroms/z80" || exit 1
fi

# zip URL sha256-del-zip carpeta miembro=destino ...: baja el zip una vez, comprueba su SHA-256 y saca cada
# miembro a testroms/carpeta/destino
zip_get() {
    url=$1; sum=$2; dir=testroms/$3; shift 3
    falta=
    for m in "$@"; do [ -f "$dir/${m#*=}" ] || falta=1; done
    if [ -z "$falta" ]; then echo "$dir: ya están $(for m in "$@"; do printf '%s ' "${m#*=}"; done)"; return 0; fi
    mkdir -p "$dir"
    z=build/$(basename "$url")
    curl -fsSL -o "$z" "$url" || { echo "no se pudo bajar $url"; return 1; }
    if [ "$(sha256 "$z")" != "$sum" ]; then echo "$url: el SHA-256 no coincide"; rm -f "$z"; return 1; fi
    for m in "$@"; do
        unzip -p "$z" "${m%%=*}" > "$dir/${m#*=}" && [ -s "$dir/${m#*=}" ] || { echo "$z: falta ${m%%=*}"; rm -f "$dir/${m#*=}"; return 1; }
    done
    rm -f "$z"
    echo "$dir: $(for m in "$@"; do printf '%s ' "${m#*=}"; done)"
}

GH=https://github.com
zip_get $GH/maxim-zhao/zexall-sms/releases/download/v0.21/ZEXALL-SMS-0.21.zip \
    704e39402516fba832b923395c7579b617896e1f00d2f036696342ab4b5fde7c zexall \
    zexall.sms=zexall.sms zexdoc.sms=zexdoc.sms LICENSE=LICENSE || exit 1
zip_get $GH/haroldo-ok/dragon-blaster/releases/download/v0.11/dragon_blaster-0.11.sms.zip \
    729becdc8e5244902589b46acb9364ccf05633adb04b1b82484a29f85ce77fdc homebrew dragon_blaster.sms=dragon_blaster.sms || exit 1
zip_get $GH/haroldo-ok/food-fighter/releases/download/v0.4/food_fighter-0.4.sms.zip \
    e7756fc79f2c8ea84b782a50defd0ed4f51024509ee9a6c3672e76df8ed8f7b8 homebrew food_fighter-0.4.sms=food_fighter.sms || exit 1
zip_get $GH/haroldo-ok/river-strike/releases/download/v0.10/river_strike.0.10.sms.zip \
    a9bab03f19daa589801405b9a09c9d2b112d7c8f820d5fa3f06f3f195bc56c33 homebrew river_strike.0.10.sms=river_strike.sms || exit 1
zip_get $GH/joyrider3774/waternet/releases/download/v1.0/Waternet.Sega.Master.System.zip \
    b0abd1c7ca992709c4a3fcb4fb3e8a0ffaf42aa2fe216784d00d683347ba2581 homebrew waternet.sms=waternet.sms || exit 1
zip_get $GH/joyrider3774/waternet/releases/download/v1.0/Waternet.Sega.Game.Gear.zip \
    16a06f51168717c252b08687abe6dd5980a4090d5cbe228f5e01d4ed28579fee homebrew waternet.gg=waternet.gg || exit 1

# Opcionales, sin licencia publicada: solo con EXTRA=1
if [ -z "$EXTRA" ]; then
    echo "(opcional: EXTRA=1 make testroms baja también las pruebas de hardware sin licencia a testroms/extra)"
    exit 0
fi
SP=https://www.smspower.org/uploads/Homebrew
zip_get $SP/SMSVDPTest-SMS-1.31.zip 6b8f113e54c084d542ae2f87951451724b4dfa85eae95f3b9a5121e6b1bd1179 extra \
    VDPTEST.sms=vdptest.sms || exit 1
zip_get $SP/VCounterTest-SMS-1.00.zip c7015c35e3cee79f9cf718da3f0d2a9388a7761bcdd03ccdded954e48501877f extra \
    TEST.SMS=vcounter.sms || exit 1
zip_get $SP/ScrollTestProgram-SMS-1.00.zip 3f4ceb9b178bc92745a2924380d15503f8a472b3c1eadf9afc5a1ac6bab647ea extra \
    TEST.SMS=scroll.sms || exit 1
zip_get $SP/64ColorPaletteTestProgram-SMS-1.00.zip c5f8001f337439aa7503523cea4144d55387151ee18126ba24d141729b77615d extra \
    TEST.SMS=paleta64.sms || exit 1
zip_get $SP/PauseTest-SMS-1.00.zip 364ec7f12792cf80a971e11675c995d97031f25a8bc973a14bc2b292dada4551 extra \
    output.sms=pausa.sms || exit 1
zip_get $SP/SMSCycleCounter-SMS-1.0.zip a32bdd7ae257a37ad6d87439c4defcb3aa100116f0bc491cbd47b1dbf685efac extra \
    output.sms=ciclos.sms || exit 1
zip_get $SP/SMSSoundTest-SMS-1.2.zip a7cfdf04119789fd9c74310c1eb9814c2cc97f6b71a43cf73cdd6d735a3cb40d extra \
    sms-sound-test.sms=sonido.sms || exit 1
zip_get $SP/GameGearHighResolutionGraphicsDemo-GG-1.00.zip 635be3150632ef375171217e99751e03be689d3b32d3e97273e130e206cbd364 extra \
    "Game Gear High Resolution Graphics Demo.gg=hires.gg" || exit 1
zip_get $SP/FadeTest-GG-1.00.zip 95213acd89bb34c69a98ea5facda4397c428e008b21661baaad0d516f7a81524 extra \
    "Fade Test.gg=fundido.gg" || exit 1
if [ -f testroms/extra/smstestsuite.sms ]; then echo "testroms/extra/smstestsuite.sms ya está"
else
    curl -fsSL -o build/smstestsuite.sms $GH/sverx/SMSTestSuite/releases/download/v0.38/SMSTestSuite.sms && \
    [ "$(sha256 build/smstestsuite.sms)" = edbe62071e94cbd10faa51f9035159f36b1b2fae8e3e4bec2a0f20848d877ebb ] && \
    mv build/smstestsuite.sms testroms/extra/ && echo "testroms/extra: smstestsuite.sms" || { echo "SMS Test Suite: no se pudo bajar o el SHA-256 no coincide"; exit 1; }
fi
