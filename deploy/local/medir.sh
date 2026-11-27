#!/usr/bin/env bash
# Mide el throughput del servicio local con uno y con cuatro trabajadores sobre
# el lote de 64 imágenes de 1024x1024, REPETICIONES veces cada uno (3 por
# defecto), y deja la tabla en mediciones/servicio.csv.
#
#   bash deploy/local/medir.sh
#   HILOS=2 CONCURRENCIA=8 FILTROS=gris,gaussiano bash deploy/local/medir.sh
set -euo pipefail
cd "$(dirname "$0")"

repeticiones="${REPETICIONES:-3}"
docker compose down -v > /dev/null 2>&1 || true   # volumen limpio: sin filas viejas

for n in 1 4; do
    for r in $(seq "$repeticiones"); do
        echo "=== $n trabajador(es), corrida $r de $repeticiones ==="
        TRABAJADORES=$n docker compose up --build --scale trabajador="$n" \
            --abort-on-container-exit --exit-code-from despachador
        docker compose stop > /dev/null
    done
done

docker compose run --rm --no-deps --entrypoint cat despachador /datos/servicio.csv \
    > ../../mediciones/servicio.csv
docker compose down -v
echo "escrito mediciones/servicio.csv:"
cat ../../mediciones/servicio.csv
