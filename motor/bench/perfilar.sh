#!/usr/bin/env bash
# Perfilado con UNA herramienta: perf stat (ciclos, instrucciones y fallos de
# caché), antes y después de la optimización de localidad del gaussiano. En
# Windows se corre dentro de WSL (sudo apt install linux-tools-generic).
#
#   cd motor && make && ./generar /tmp/medicion.ppm 4096 4096
#   bash bench/perfilar.sh /tmp/medicion.ppm [hilos]
#
# Deja la salida completa en ../mediciones/perfilado.txt: es la evidencia que
# se cita en docs/02-motor.md.
set -euo pipefail
imagen="${1:?uso: bash bench/perfilar.sh <imagen.ppm> [hilos]}"
hilos="${2:-4}"
salida="../mediciones/perfilado.txt"
: > "$salida"
for filtro in gaussiano gaussiano_bandas; do
    echo "=== $filtro, $hilos hilos, 3 repeticiones, gris antes del cronómetro ===" | tee -a "$salida"
    OMP_NUM_THREADS="$hilos" perf stat -e cycles,instructions,cache-references,cache-misses \
        ./arnes "$filtro" "$imagen" 3 --gris 2>&1 >/dev/null | tee -a "$salida"
done
echo "escrito $salida"
