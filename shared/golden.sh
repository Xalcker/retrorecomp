#!/bin/sh
# Línea base exacta (golden), común a los cinco sistemas: hash de todos los cuadros y de todo el audio de
# cada caso (una ROM corrida con entradas fijas). Una optimización no debe cambiar ni un bit.
#   sh tests/golden.sh                 compara con tests/golden.txt
#   sh tests/golden.sh --update        escribe en tests/golden.txt los casos que corrieron; los que no se
#                                      pudieron correr aquí (falta la ROM) se quedan como estaban
#   sh tests/golden.sh --recompilado   lo mismo con el código recompilado: recompila cada ROM (sin traza, en
#                                      build/golden_rec/SHA1), compila el bench del proyecto y compara su --hash
#                                      con la misma línea base (si hace lo mismo que el intérprete, da lo mismo)
#   sh tests/golden.sh --libres        solo los casos de testroms/ (las ROMs libres que baja make testroms: lo
#                                      que corre el CI completo)
#   sh tests/golden.sh ROM...          solo los casos de esas ROMs
#   GOLDEN_SHOT=ruta                   otro *shot
#   GOLDEN_JOBS=N                      casos a la vez (por omisión, uno por procesador)
#
# Los casos se nombran por el SHA1 de la ROM, con /VARIANTE si una ROM corre de más de una forma (otro
# modelo de consola, otra BIOS): no dependen del nombre del archivo. Cada línea de tests/golden.txt:
#   SHA1[/VARIANTE] hash imagen XXXXXXXXXXXXXXXX audio YYYYYYYYYYYYYYYY  # archivo
# La comparación salta los casos cuya ROM falta, cuenta aparte los nuevos (sin línea base: no cuentan
# como iguales) y falla si un caso difiere, si un caso no dio hash o si no se comparó ninguno. Los hashes
# no dependen de la plataforma (-ffp-contract=off en el Makefile).
#
# Vive en shared/; cada sistema lo carga desde su tests/golden.sh con `. ../shared/golden.sh` después
# de entrar a su raíz y de definir:
#   SIS            gb, nes, gen, neo o sms (para ${SIS}shot)
#   golden_casos   función que llama a `caso ROM [VARIANTE] [-- opciones del *shot...]` por cada caso;
#                  las opciones van después de GOLDEN_OPTS, así que pueden cambiar --frames
#   GOLDEN_OPTS    opciones de todos los casos (cuadros y entradas)
#   golden_sha1    (opcional) función ROM -> SHA1 que nombra el caso; por omisión, el del archivo
#   golden_skip    (opcional) función que imprime un motivo si el sistema no puede correr nada (y sale
#                  sin comparar, con 0)

exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
sha1_file() {   # macOS: shasum; Linux y MSYS2: sha1sum
    if command -v sha1sum >/dev/null 2>&1; then sha1sum | cut -d' ' -f1; else shasum | cut -d' ' -f1; fi
}
command -v golden_sha1 >/dev/null 2>&1 || golden_sha1() { sha1_file < "$1"; }

UPDATE=
REC=
LIBRES=
ONLY=
for a in "$@"; do
    case $a in
        --update) UPDATE=1 ;;
        --recompilado) REC=1 ;;
        --libres) LIBRES=1 ;;
        -*) echo "opción desconocida: $a" >&2; exit 2 ;;
        *) ONLY="$ONLY
$a" ;;
    esac
done

SHOT=${GOLDEN_SHOT:-$(exe "${SIS}shot")}
RECOMP=$(exe "${SIS}recomp")
[ -x "$SHOT" ] || { echo "falta $SHOT: corre make" >&2; exit 2; }
[ -z "$REC" ] || [ -x "$RECOMP" ] || { echo "falta $RECOMP: corre make" >&2; exit 2; }
[ -n "$REC" ] && [ -n "$UPDATE" ] && { echo "--update es con el intérprete: la línea base no sale del código recompilado" >&2; exit 2; }
EXE=
case $SHOT in *.exe) EXE=.exe ;; esac
if command -v golden_skip >/dev/null 2>&1; then
    why=$(golden_skip)
    [ -n "$why" ] && { echo "golden: nada que comparar ($why)"; exit 0; }
fi
JOBS=${GOLDEN_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
OUT=build/golden
rm -rf "$OUT"
mkdir -p "$OUT"

# Con --recompilado, el proyecto generado de la ROM (uno por SHA1, aunque la ROM tenga varias variantes). Sin
# traza: lo que el análisis estático no encuentra lo corre el intérprete, y el hash debe salir igual
proyecto() {
    dir=build/golden_rec/$1
    [ -x "$dir/bench$EXE" ] && return 0
    rm -rf "$dir"
    "$RECOMP" "$2" -o "$dir" >"$dir.log" 2>&1 && make -s -C "$dir" ${CC:+CC="$CC"} -j"$JOBS" "bench$EXE" >>"$dir.log" 2>&1
}

# Cada caso corre en segundo plano y deja su línea en build/golden/NNNN (en el orden de golden_casos)
n_case=0
running=0
caso() {
    rom=$1; shift
    var=
    if [ $# -gt 0 ] && [ "$1" != "--" ]; then var=$1; shift; fi
    [ "$1" = "--" ] && shift
    [ -f "$rom" ] || return 0
    case $rom in testroms/*) ;; *) [ -n "$LIBRES" ] && return 0 ;; esac
    if [ -n "$ONLY" ]; then
        printf '%s\n' "$ONLY" | grep -qxF "$rom" || return 0
    fi
    n_case=$((n_case + 1))
    f=$OUT/$(printf '%04d' $n_case)
    sha=$(golden_sha1 "$rom")
    [ -n "$sha" ] || sha=sin-sha1
    if [ -n "$REC" ]; then
        mkdir -p build/golden_rec
        proyecto "$sha" "$rom" || echo "  no se pudo recompilar o compilar $rom (build/golden_rec/$sha.log)" >&2
    fi
    (
        key=$sha${var:+/$var}
        if [ -n "$REC" ]; then
            h=$("build/golden_rec/$sha/bench$EXE" "$rom" $GOLDEN_OPTS "$@" --hash 2>/dev/null | grep '^hash imagen')
        else
            h=$("$SHOT" "$rom" /dev/null $GOLDEN_OPTS "$@" --hash 2>/dev/null | grep '^hash imagen')
        fi
        printf '%s %s  # %s\n' "$key" "${h:-hash error}" "$(basename "$rom")" > "$f"
    ) &
    running=$((running + 1))
    if [ $running -ge "$JOBS" ]; then wait; running=0; fi
}
golden_casos
wait
ls "$OUT" | sort | while read -r f; do cat "$OUT/$f"; done > "$OUT.txt"
ran=$(wc -l < "$OUT.txt" | tr -d ' ')
[ -f tests/golden.txt ] || : > tests/golden.txt
total=$(grep -c . tests/golden.txt | tr -d ' ')

if [ -n "$UPDATE" ]; then
    # Los casos que corrieron reemplazan su línea (o se agregan al final); los demás se quedan. Un caso que no dio
    # hash (una ROM que el emulador no carga) no entra
    awk 'NR == FNR { if ($0 ~ / hash error  #/) next; k = $1; nuevo[k] = $0; orden[++n] = k; next }
         { k = $1; if (k in nuevo) { print nuevo[k]; hecho[k] = 1 } else print }
         END { for (i = 1; i <= n; i++) if (!(orden[i] in hecho)) print nuevo[orden[i]] }' \
        "$OUT.txt" tests/golden.txt > "$OUT.nuevo"
    mv "$OUT.nuevo" tests/golden.txt
    echo "tests/golden.txt: $(grep -vc ' hash error ' "$OUT.txt") casos escritos ($(grep -c . tests/golden.txt | tr -d ' ') en total)"
    grep ' hash error ' "$OUT.txt" | sed 's/^/no corre: /'
    exit 0
fi

igual=0; nuevo=0; malo=0; nocorre=0
while read -r key rest; do
    now=${rest%%  #*}
    name=${rest#*  # }
    want=$(awk -v k="$key" '$1 == k { sub(/  #.*/, ""); sub(/^[^ ]+ /, ""); print; exit }' tests/golden.txt)
    if [ "$now" = "hash error" ] && [ -z "$want" ]; then
        echo "(no corre: $name ($key), sin línea base)"; nocorre=$((nocorre + 1))
    elif [ "$now" = "hash error" ]; then
        echo "ERROR: $name ($key): el caso no dio hash"; malo=$((malo + 1))
    elif [ -z "$want" ]; then
        echo "(nuevo: $key $now  # $name)"; nuevo=$((nuevo + 1))
    elif [ "$want" = "$now" ]; then
        igual=$((igual + 1))
    else
        echo "DIFERENTE: $name ($key)"; echo "  esperado: $want"; echo "  ahora:    $now"; malo=$((malo + 1))
    fi
done < "$OUT.txt"
sin=$(awk 'NR == FNR { corrio[$1] = 1; next } NF && !($1 in corrio) { n++ } END { print n + 0 }' "$OUT.txt" tests/golden.txt)
extra=
[ $nocorre -gt 0 ] && extra=", $nocorre que no corren"
resumen="$igual iguales, $malo diferentes, $nuevo nuevos$extra; $sin de los $total de tests/golden.txt sin correr"
QUE=golden${REC:+ (recompilado)}
if [ $malo -gt 0 ]; then echo "$QUE: DIFERENTE ($resumen)"; exit 1; fi
if [ $igual -eq 0 ]; then echo "$QUE: no se comparó nada ($resumen)"; exit 1; fi
echo "$QUE: igual ($resumen)"
