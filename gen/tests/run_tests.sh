#!/bin/sh
# Batería de make test. Crece con cada fase (docs/PLAN.md); por ahora, el cartucho.
cd "$(dirname "$0")/.." || exit 2
exe() { if [ -x "./$1.exe" ]; then echo "./$1.exe"; else echo "./$1"; fi; }
fallas=0
corre() {   # descripción comando...
    d=$1; shift
    if "$@" >/dev/null 2>&1; then echo "ok    $d"; else echo "FALLA $d"; fallas=$((fallas + 1)); fi
}

corre "carttest (formatos .bin/.smd, cabecera, suma, SRAM, vectores)" "$(exe carttest)"

# genrecomp: --info con una ROM que no es de Mega Drive debe fallar sin caerse, y sin ROM, dar el uso
D=build/tests; mkdir -p "$D"
printf 'no es una rom' > "$D/chica.bin"
if "$(exe genrecomp)" "$D/chica.bin" >/dev/null 2>&1; then echo "FALLA genrecomp acepta un archivo de 13 bytes"; fallas=$((fallas + 1))
else echo "ok    genrecomp rechaza un archivo que no es ROM"; fi
if "$(exe genrecomp)" 2>&1 | grep -q "uso:"; then echo "ok    genrecomp sin argumentos da el uso"
else echo "FALLA genrecomp sin argumentos"; fallas=$((fallas + 1)); fi

# Las ROMs de roms/ (tuyas, no se versionan): genrecomp --info no debe fallar con ninguna
for r in roms/*.bin roms/*.md roms/*.gen roms/*.smd; do
    [ -f "$r" ] || continue
    corre "genrecomp --info $(basename "$r")" "$(exe genrecomp)" "$r" --info
done

[ $fallas -eq 0 ] && echo "Todas las pruebas pasaron." || echo "$fallas pruebas fallaron."
[ $fallas -eq 0 ]
