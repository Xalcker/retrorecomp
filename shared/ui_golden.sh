#!/bin/sh
# Capturas de la interfaz contra una referencia: dibuja cada menú y cada diálogo con --ui-shot (sin ventana,
# ajustes de fábrica y en español: ver shot_mode en main_sdl.c) y compara el cksum de cada PNG con el de un
# archivo de referencia. Es la red de seguridad para tocar main_sdl.c: cualquier cambio visible sale aquí.
#   sh ui_golden.sh EXE ROM REFERENCIA "MENÚS" "ACCIONES"          compara
#   UI_GOLDEN_UPDATE=1 sh ui_golden.sh ...                          reescribe la referencia (revisa las capturas antes)
# MENÚS y ACCIONES son listas de números (--ui-menu N, --ui-action N). Las capturas quedan en build/shots/.
exe=$1; rom=$2; ref=$3; menus=$4; acciones=$5
mkdir -p build/shots
nuevo=$(mktemp 2>/dev/null || echo build/shots/ref_nuevo.txt)
: > "$nuevo"
fallas=0
captura() {    # clave opción número
    out=build/shots/ui_$1.png
    rm -f "$out"
    "$exe" "$rom" --ui-shot "$out" --ui-frames 5 "$2" "$3" >/dev/null 2>&1
    if [ ! -s "$out" ]; then echo "FALLA ui $1: no dibuja"; fallas=$((fallas + 1)); return; fi
    suma=$(cksum < "$out" | cut -d' ' -f1-2)
    echo "$1 $suma" >> "$nuevo"
    if [ -f "$ref" ] && [ -z "$UI_GOLDEN_UPDATE" ]; then
        # Una línea "clave@Sistema" (uname -s: Darwin, Linux, MINGW...) pesa más que la general: macOS dibuja
        # distinto los diálogos de texto largo
        esperada=$(grep "^$1@$(uname -s) " "$ref" | cut -d' ' -f2-3)
        [ -n "$esperada" ] || esperada=$(grep "^$1 " "$ref" | cut -d' ' -f2-3)
        if [ -z "$esperada" ]; then echo "FALLA ui $1: sin referencia en $ref"; fallas=$((fallas + 1))
        elif [ "$esperada" != "$suma" ]; then echo "FALLA ui $1: la captura cambió (esperada $esperada, salió $suma; build/shots/ui_$1.png)"; fallas=$((fallas + 1)); fi
    fi
}
for m in $menus; do captura "menu$m" --ui-menu "$m"; done
for a in $acciones; do captura "accion$a" --ui-action "$a"; done
if [ -n "$UI_GOLDEN_UPDATE" ]; then
    cp "$nuevo" "$ref"; echo "referencia escrita en $ref"
elif [ ! -f "$ref" ]; then
    echo "FALLA ui: no existe $ref (UI_GOLDEN_UPDATE=1 sh $0 ... la crea)"; fallas=$((fallas + 1))
fi
[ "$nuevo" = build/shots/ref_nuevo.txt ] || rm -f "$nuevo"
[ $fallas -eq 0 ]
