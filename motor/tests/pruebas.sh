#!/usr/bin/env bash
# Pruebas de corrección del motor. Cada filtro se compara contra la salida
# que el repositorio trae calculada, y después la versión paralela contra la
# secuencial sobre una imagen grande, donde el reparto entre hilos sí tiene
# efecto.

#archivo /tests/pruebas.sh
set -uo pipefail

cd "$(dirname "$0")/.."
fallos=0
trabajo=$(mktemp -d)
trap 'rm -rf "$trabajo"' EXIT

revisar() {
    local nombre="$1"
    shift
    if "$@" > "$trabajo/salida.txt" 2>&1; then
        echo "  ok    $nombre"
    else
        echo "  FALLA $nombre"
        sed 's/^/        /' "$trabajo/salida.txt"
        fallos=$((fallos + 1))
    fi
}

echo "Salida esperada, calculada aparte:"
./motor gris tests/datos/entrada.ppm "$trabajo/gris.pgm"
revisar "gris sobre la imagen de prueba" ./comparar "$trabajo/gris.pgm" tests/datos/gris.pgm
./motor caja tests/datos/entrada.ppm "$trabajo/caja.ppm"
revisar "caja sobre la imagen de prueba" ./comparar "$trabajo/caja.ppm" tests/datos/caja.ppm

./motor gaussiano tests/datos/entrada.ppm "$trabajo/gauss.ppm"
revisar "gaussiano sobre la imagen de prueba" ./comparar "$trabajo/gauss.ppm" tests/datos/gaussiano.ppm
./motor gaussiano tests/datos/gris.pgm "$trabajo/gauss1.pgm"
revisar "gaussiano sobre un canal" ./comparar "$trabajo/gauss1.pgm" tests/datos/gaussiano_gris.pgm
# 37x41 no es múltiplo de la banda: la última tiene 9 filas.
for filtro in gaussiano gaussiano_bandas; do
    ./motor "$filtro" tests/datos/medio.ppm "$trabajo/medio.ppm"
    revisar "$filtro sobre 37x41, contra la implementación aparte" \
        ./comparar "$trabajo/medio.ppm" tests/datos/gaussiano_medio.ppm
done

echo "Lectura y escritura:"
./motor gris tests/datos/gris.pgm "$trabajo/vuelta.pgm" 2> "$trabajo/err.txt"
if grep -q "tres canales" "$trabajo/err.txt"; then
    echo "  ok    gris rechaza una imagen de un canal"
else
    echo "  FALLA gris debería rechazar una imagen de un canal"
    fallos=$((fallos + 1))
fi
./motor caja tests/datos/gris.pgm "$trabajo/caja1.pgm"
revisar "caja sobre un canal escribe P5" bash -c \
    "head -c 2 '$trabajo/caja1.pgm' | grep -q P5"

echo "Paralelo contra secuencial, imagen de 512x512:"
./generar "$trabajo/grande.ppm" 512 512 > /dev/null
for filtro in gris caja gaussiano gaussiano_bandas; do
    ./motor "$filtro" "$trabajo/grande.ppm" "$trabajo/$filtro-par" 
    ./motor "$filtro" "$trabajo/grande.ppm" "$trabajo/$filtro-sec" --secuencial
    revisar "$filtro idéntico con uno y con varios hilos" \
        ./comparar "$trabajo/$filtro-par" "$trabajo/$filtro-sec"
done

echo "Tamaños pequeños y bordes, gaussiano:"
# Imágenes más angostas o más bajas que el núcleo: todo es borde.
for tam in "1 1" "2 3" "5 1" "1 7" "16 16" "17 33"; do
    ./generar "$trabajo/t.ppm" $tam > /dev/null
    ./motor gaussiano "$trabajo/t.ppm" "$trabajo/t-sec" --secuencial
    ./motor gaussiano "$trabajo/t.ppm" "$trabajo/t-par"
    ./motor gaussiano_bandas "$trabajo/t.ppm" "$trabajo/t-ban"
    revisar "gaussiano ${tam/ /x}: dos pasadas y bandas iguales a la secuencial" bash -c \
        "./comparar '$trabajo/t-sec' '$trabajo/t-par' && ./comparar '$trabajo/t-sec' '$trabajo/t-ban'"
done

echo "Sobel:"
# Sobel pide un canal, así que se le da la salida de gris. Las salidas
# esperadas de tests/datos/ las calculó tests/esperado_sobel.py, aparte.
./motor sobel tests/datos/gris.pgm "$trabajo/sobel_gris.pgm"
revisar "sobel sobre la imagen de prueba de 4x3" \
    ./comparar "$trabajo/sobel_gris.pgm" tests/datos/sobel_gris.pgm
./motor gris tests/datos/medio.ppm "$trabajo/medio_gris.pgm"
./motor sobel "$trabajo/medio_gris.pgm" "$trabajo/sobel_medio.pgm"
revisar "sobel sobre 37x41, contra la implementación aparte" \
    ./comparar "$trabajo/sobel_medio.pgm" tests/datos/sobel_medio.pgm

# Una imagen sin cambios no tiene bordes: la salida es cero en todos los píxeles.
printf 'P5\n5 4\n255\n' > "$trabajo/constante.pgm"
head -c 20 /dev/zero | tr '\0' '\310' >> "$trabajo/constante.pgm"
printf 'P5\n5 4\n255\n' > "$trabajo/ceros.pgm"
head -c 20 /dev/zero >> "$trabajo/ceros.pgm"
./motor sobel "$trabajo/constante.pgm" "$trabajo/sobel_constante.pgm"
revisar "sobel de una imagen constante da cero en todos los píxeles" \
    ./comparar "$trabajo/sobel_constante.pgm" "$trabajo/ceros.pgm"

echo "Sobel, paralelo contra secuencial, imagen de 512x512:"
./generar "$trabajo/s_grande.ppm" 512 512 > /dev/null
./motor gris "$trabajo/s_grande.ppm" "$trabajo/s_grande.pgm"
./motor sobel "$trabajo/s_grande.pgm" "$trabajo/sobel-sec" --secuencial
./motor gris,gaussiano,sobel "$trabajo/s_grande.ppm" "$trabajo/cadena-sec" --secuencial
for hilos in 1 8; do
    OMP_NUM_THREADS=$hilos ./motor sobel "$trabajo/s_grande.pgm" "$trabajo/sobel-par$hilos"
    revisar "sobel idéntico a la secuencial con $hilos hilos" \
        ./comparar "$trabajo/sobel-par$hilos" "$trabajo/sobel-sec"
    OMP_NUM_THREADS=$hilos ./motor gris,gaussiano,sobel "$trabajo/s_grande.ppm" "$trabajo/cadena-par$hilos"
    revisar "gris,gaussiano,sobel idéntica a la secuencial con $hilos hilos" \
        ./comparar "$trabajo/cadena-par$hilos" "$trabajo/cadena-sec"
done

echo "Tamaños pequeños y bordes, sobel:"
# Imágenes más angostas o más bajas que la máscara: todo es borde.
for tam in "1 1" "2 3" "5 1" "1 7" "16 16" "17 33"; do
    ./generar "$trabajo/t.ppm" $tam > /dev/null
    ./motor gris "$trabajo/t.ppm" "$trabajo/t.pgm"
    ./motor sobel "$trabajo/t.pgm" "$trabajo/ts-sec" --secuencial
    OMP_NUM_THREADS=8 ./motor sobel "$trabajo/t.pgm" "$trabajo/ts-par"
    revisar "sobel ${tam/ /x}: paralelo igual a la secuencial" \
        ./comparar "$trabajo/ts-sec" "$trabajo/ts-par"
done

echo "Filtro desconocido:"
# El motor sale con código 1 al rechazar la cadena, así que su salida se
# recoge antes de buscar en ella: con pipefail, la tubería heredaría ese
# código y la prueba fallaría por el motivo contrario al que busca.
queja=$(./motor nosexiste tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
if printf '%s' "$queja" | grep -q "desconocido"; then
    echo "  ok    la cadena con un filtro inexistente se rechaza"
else
    echo "  FALLA una cadena con un filtro inexistente debería rechazarse"
    fallos=$((fallos + 1))
fi

echo "Cadenas que no se pueden aplicar:"
for caso in "sobel:un canal" "gris,gris:tres canales" "otsu:un canal"; do
    cadena="${caso%%:*}"
    espera="${caso##*:}"
    queja=$(./motor "$cadena" tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
    if printf '%s' "$queja" | grep -q "$espera"; then
        echo "  ok    $cadena se rechaza antes de aplicar nada"
    else
        echo "  FALLA $cadena debería rechazarse: $queja"
        fallos=$((fallos + 1))
    fi
done

echo
if [ "$fallos" -eq 0 ]; then
    echo "todas las pruebas pasaron"
else
    echo "$fallos prueba(s) fallaron"
fi
exit "$fallos"
