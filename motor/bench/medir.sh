#!/usr/bin/env bash
# Corre el arnés para 1, 2, 4 y 8 hilos sobre la imagen de medición y deja
# la tabla en mediciones/motor.csv.
#
#   bash bench/medir.sh imagen.ppm gris gaussiano gaussiano_bandas gaussiano_directo
#   bash bench/medir.sh imagen.ppm gris,gaussiano,sobel,otsu      # la cadena completa
#
# - Los filtros que reciben un solo canal (gaussiano*, sobel, otsu) se miden
#   sobre la imagen ya en grises, que es lo que reciben dentro de la cadena. La
#   conversión a grises se hace antes de arrancar el cronómetro (--gris).
# - gaussiano_directo es solo secuencial: se mide con 1 hilo, para compararlo
#   con la separable con un solo hilo antes de hablar de núcleos.
# - Una cadena (con comas) se mide completa, tal cual la aplica el servicio.
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "uso: bash bench/medir.sh <imagen.ppm> <filtro|cadena> [filtro|cadena...]" >&2
    exit 2
fi

imagen="$1"
shift
salida="../mediciones/motor.csv"
repeticiones=5

echo "filtro,hilos,repeticion,milisegundos" > "$salida"
for filtro in "$@"; do
    extra=()
    case "$filtro" in
        gaussiano*|sobel|otsu) extra=(--gris) ;;
    esac
    hilos_lista="1 2 4 8"
    [ "$filtro" = "gaussiano_directo" ] && hilos_lista="1"
    for hilos in $hilos_lista; do
        OMP_NUM_THREADS="$hilos" ./arnes "$filtro" "$imagen" "$repeticiones" "${extra[@]}" >> "$salida"
    done
done

echo "escrito $salida con $(( $(wc -l < "$salida") - 1 )) filas"
