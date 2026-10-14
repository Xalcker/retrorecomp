#!/bin/sh
# Capturas de la interfaz contra una referencia: dibuja cada menú y cada diálogo con --ui-shot (sin ventana,
# ajustes de fábrica y en español: ver shot_mode en main_sdl.c) y compara el cksum de cada PNG con el de un
# archivo de referencia. Es la red de seguridad para tocar main_sdl.c: cualquier cambio visible sale aquí.
#   sh ui_golden.sh EXE ROM REFERENCIA "MENÚS" "ACCIONES" ["EXTRAS"]   compara
#   UI_GOLDEN_UPDATE=1 sh ui_golden.sh ...                              reescribe la referencia (revisa las capturas antes)
# MENÚS y ACCIONES son listas de números (--ui-menu N, --ui-action N). EXTRAS, capturas con otras opciones:
# cada una es una lista de opciones separadas por comas (--no-menu,--ui-menu,1), y su clave, "con" seguido
# de esa lista. Las capturas quedan en build/shots/.
exe=$1; rom=$2; ref=$3; menus=$4; acciones=$5; extras=$6
mkdir -p build/shots
nuevo=$(mktemp 2>/dev/null || echo build/shots/ref_nuevo.txt)
: > "$nuevo"
fallas=0
captura() {    # clave opciones...
    clave=$1; shift
    out=build/shots/ui_$clave.png
    rm -f "$out"
    "$exe" "$rom" --ui-shot "$out" --ui-frames 5 "$@" >/dev/null 2>&1
    if [ ! -s "$out" ]; then echo "FALLA ui $clave: no dibuja"; fallas=$((fallas + 1)); return; fi
    suma=$(cksum < "$out" | cut -d' ' -f1-2)
    echo "$clave $suma" >> "$nuevo"
    if [ -f "$ref" ] && [ -z "$UI_GOLDEN_UPDATE" ]; then
        # Una línea "clave@Sistema" (uname -s: Darwin, Linux, MINGW...) pesa más que la general, por si un
        # sistema dibuja distinto. Hoy ninguno de los cuatro la necesita: lo que GBRecomp sacaba distinto en
        # macOS era la captura (la ventana oculta, no una superficie), y los diálogos que muestran rutas del
        # sistema (Este juego) no se prueban
        esperada=$(grep "^$clave@$(uname -s) " "$ref" | cut -d' ' -f2-3)
        [ -n "$esperada" ] || esperada=$(grep "^$clave " "$ref" | cut -d' ' -f2-3)
        if [ -z "$esperada" ]; then echo "FALLA ui $clave: sin referencia en $ref"; fallas=$((fallas + 1))
        elif [ "$esperada" != "$suma" ]; then echo "FALLA ui $clave: la captura cambió (esperada $esperada, salió $suma; build/shots/ui_$clave.png)"; fallas=$((fallas + 1)); fi
    fi
}
for m in $menus; do captura "menu$m" --ui-menu "$m"; done
for a in $acciones; do captura "accion$a" --ui-action "$a"; done
for x in $extras; do
    captura "con$x" $(echo "$x" | tr ',' ' ')
done
if [ -n "$UI_GOLDEN_UPDATE" ]; then
    cp "$nuevo" "$ref"; echo "referencia escrita en $ref"
elif [ ! -f "$ref" ]; then
    echo "FALLA ui: no existe $ref (UI_GOLDEN_UPDATE=1 sh $0 ... la crea)"; fallas=$((fallas + 1))
fi
if [ $fallas -ne 0 ]; then
    echo "valores de esta corrida, por si es la referencia de este sistema:"
    while read -r clave a b; do echo "$clave@$(uname -s) $a $b"; done < "$nuevo"
fi
[ "$nuevo" = build/shots/ref_nuevo.txt ] || rm -f "$nuevo"
[ $fallas -eq 0 ]
