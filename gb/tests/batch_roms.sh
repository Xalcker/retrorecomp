#!/bin/sh
# Prueba en lote: recompila varios juegos, compila cada proyecto y corre la
# prueba diferencial con entradas aleatorias, guardando capturas.
#
# Uso: sh tests/batch_roms.sh LISTA CARPETA [CUADROS] [SEMILLA]
#   LISTA: un archivo con una ruta por línea (.gb, .gbc o .zip con la ROM).
#   CARPETA: donde quedan las ROMs extraídas, los proyectos, las capturas y
#   resultados.txt (una línea por juego, separada por tabuladores).
#
# Resultado por juego: OK (sin diferencias), DIFERENCIA (difftest encontró
# una), FALLA_GEN (gbrecomp falló), FALLA_COMPILA (el proyecto no compiló),
# FALLA_CORRE (difftest terminó mal sin reportar diferencia).

cd "$(dirname "$0")/.." || exit 2
LIST=$1
DIR=$2
FRAMES=${3:-3600}
SEED=${4:-1}
[ -f "$LIST" ] && [ -n "$DIR" ] || { echo "uso: sh tests/batch_roms.sh LISTA CARPETA [CUADROS] [SEMILLA]" >&2; exit 2; }
EXE=
[ -x ./gbrecomp.exe ] && EXE=.exe
mkdir -p "$DIR/roms" "$DIR/proyectos" "$DIR/capturas"
RES="$DIR/resultados.txt"
printf 'juego\tresultado\trecompilado\tsegundos\tdetalle\n' > "$RES"

while IFS= read -r src; do
    [ -n "$src" ] || continue
    name=$(basename "$src")
    name=${name%.*}
    slug=$(printf '%s' "$name" | tr -c 'A-Za-z0-9' '_' | cut -c1-40)
    start=$(date +%s)

    # ROM: tal cual o extraída del zip
    case "$src" in
        *.zip)
            mkdir -p "$DIR/roms/$slug"
            unzip -o -q "$src" -d "$DIR/roms/$slug" || { printf '%s\tFALLA_ZIP\t\t\t\n' "$name" >> "$RES"; continue; }
            found=$(find "$DIR/roms/$slug" -type f \( -iname '*.gb' -o -iname '*.gbc' \) | head -1)
            rom=
            if [ -f "$found" ]; then
                # Nombre sin comas ni apóstrofos: MSYS2 no convierte bien esas
                # rutas al pasarlas a un ejecutable de Windows
                rom="$DIR/roms/$slug/$slug.${found##*.}"
                [ "$found" = "$rom" ] || mv -f "$found" "$rom"
            fi
            ;;
        *) rom=$src ;;
    esac
    [ -f "$rom" ] || { printf '%s\tFALLA_ZIP\t\t\t\n' "$name" >> "$RES"; continue; }

    proj="$DIR/proyectos/$slug"
    printf '%-60s ' "$name"
    if ! ./gbrecomp$EXE "$rom" -o "$proj" --name "$slug" > "$DIR/proyectos/$slug.gen.log" 2>&1; then
        echo FALLA_GEN
        printf '%s\tFALLA_GEN\t\t%s\t%s\n' "$name" $(( $(date +%s) - start )) \
            "$(tail -1 "$DIR/proyectos/$slug.gen.log")" >> "$RES"
        continue
    fi
    if ! make -s -C "$proj" -j8 difftest$EXE > "$DIR/proyectos/$slug.make.log" 2>&1; then
        echo FALLA_COMPILA
        printf '%s\tFALLA_COMPILA\t\t%s\t%s\n' "$name" $(( $(date +%s) - start )) \
            "$(grep -m1 error "$DIR/proyectos/$slug.make.log")" >> "$RES"
        continue
    fi
    mkdir -p "$DIR/capturas/$slug"
    "$proj/difftest$EXE" "$rom" --frames "$FRAMES" --seed "$SEED" --every 1200 \
        --screens "$DIR/capturas/$slug" > "$DIR/proyectos/$slug.diff.log" 2>&1
    rc=$?
    pct=$(grep -oE '[0-9.]+% de los ciclos' "$DIR/proyectos/$slug.diff.log" | cut -d' ' -f1)
    secs=$(( $(date +%s) - start ))
    if [ $rc -eq 0 ] && grep -q '^difftest: .* cuadros iguales' "$DIR/proyectos/$slug.diff.log"; then
        r=OK; detail=""
    elif grep -qi 'diferencia' "$DIR/proyectos/$slug.diff.log"; then
        r=DIFERENCIA; detail=$(grep -i -m1 'diferencia' "$DIR/proyectos/$slug.diff.log")
    else
        r=FALLA_CORRE; detail="salida $rc: $(tail -1 "$DIR/proyectos/$slug.diff.log")"
    fi
    echo "$r $pct"
    printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$r" "$pct" "$secs" "$detail" >> "$RES"
    # Los proyectos pesan: se conserva solo el registro (salvo que fallen)
    [ "$r" = OK ] && rm -rf "$proj"
done < "$LIST"

echo
echo "Resultados en $RES"
awk -F'\t' 'NR > 1 { n[$2]++ } END { for (k in n) printf "  %s: %d\n", k, n[k] }' "$RES"
