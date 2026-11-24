#!/usr/bin/env bash
# Prueba de integración: levanta dos trabajadores reales, reparte un lote y
# compara cada salida byte a byte con la que da el motor directamente.
# Hace falta haber corrido make en motor/.
set -uo pipefail

cd "$(dirname "$0")/.."
MOTOR="$PWD/motor/motor"
GENERAR="$PWD/motor/generar"
COMPARAR="$PWD/motor/comparar"
trabajo=$(mktemp -d)
pids=()
limpiar() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$trabajo"; }
trap limpiar EXIT
fallos=0

ok()   { echo "  ok    $1"; }
falla() { echo "  FALLA $1"; fallos=$((fallos + 1)); }

arrancar() {  # puerto
    PUERTO="$1" MOTOR="$MOTOR" python3 trabajador/trabajador.py > "$trabajo/t$1.log" 2>&1 &
    pids+=($!)
}
esperar() {  # puerto
    for _ in $(seq 50); do
        curl -s "http://127.0.0.1:$1/salud" > /dev/null && return 0
        sleep 0.1
    done
    return 1
}

mkdir "$trabajo/lote"
i=0
for tam in "512 512" "300 700" "1024 256" "640 480" "37 41" "800 800" "128 900" "512 512"; do
    i=$((i + 1))
    "$GENERAR" "$trabajo/lote/img$i.ppm" $tam > /dev/null
done

arrancar 18081
arrancar 18082
esperar 18081 && esperar 18082 || { echo "los trabajadores no arrancaron"; exit 1; }
A=http://127.0.0.1:18081,http://127.0.0.1:18082

echo "Lote de 8 imágenes con dos trabajadores:"
python3 despachador/despachador.py --trabajadores "$A" --filtros gris,gaussiano \
    --entrada "$trabajo/lote" --salida "$trabajo/sal" --informe "$trabajo/informe.csv" \
    > "$trabajo/despacho.txt" 2>&1
[ $? -eq 0 ] && ok "el despachador termina sin fallas" || { falla "el despachador falló"; cat "$trabajo/despacho.txt"; }
for i in 1 2 3 4 5 6 7 8; do
    "$MOTOR" gris,gaussiano "$trabajo/lote/img$i.ppm" "$trabajo/dir$i.pgm"
    "$COMPARAR" "$trabajo/sal/img$i.pgm" "$trabajo/dir$i.pgm" > /dev/null \
        && ok "img$i igual a la salida directa del motor" || falla "img$i difiere"
done
n=$(( $(wc -l < "$trabajo/informe.csv") - 1 ))
[ "$n" -eq 8 ] && ok "el informe tiene una fila por imagen" || falla "el informe tiene $n filas"

echo "Un trabajador que cae a mitad del lote:"
# Contesta /salud pero cierra la conexión al recibir una imagen.
python3 - 18083 > /dev/null 2>&1 <<'PY' &
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers(); self.wfile.write(b"ok")
    def do_POST(self):
        self.connection.close()
HTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
PY
pids+=($!)
esperar 18083
rm -rf "$trabajo/sal2"
python3 despachador/despachador.py --trabajadores "http://127.0.0.1:18083,http://127.0.0.1:18081" \
    --filtros gris --entrada "$trabajo/lote" --salida "$trabajo/sal2" > "$trabajo/caida.txt" 2>&1
[ $? -eq 0 ] && ok "las imágenes del que cayó las atiende el otro" || { falla "no se recuperó de la caída"; cat "$trabajo/caida.txt"; }
[ "$(ls "$trabajo/sal2" | wc -l)" -eq 8 ] && ok "salieron las 8 imágenes" || falla "faltan imágenes de salida"

echo "Trabajador que no responde desde el principio:"
python3 despachador/despachador.py --trabajadores "http://127.0.0.1:19999,http://127.0.0.1:18082" \
    --filtros gris --entrada "$trabajo/lote" --salida "$trabajo/sal3" > "$trabajo/muerto.txt" 2>&1
[ $? -eq 0 ] && grep -q "se omite" "$trabajo/muerto.txt" \
    && ok "se omite y el lote sale completo" || falla "no omitió al trabajador caído"

echo "Cadenas inválidas:"
python3 despachador/despachador.py --trabajadores "$A" --filtros gris,sobel \
    --entrada "$trabajo/lote" --salida "$trabajo/sal4" > "$trabajo/sobel.txt" 2>&1
[ $? -eq 1 ] && grep -q "sin implementar" "$trabajo/sobel.txt" \
    && ok "sobel (sin implementar) se reporta como falla, no se reintenta" || falla "sobel debería reportarse como falla"
python3 despachador/despachador.py --trabajadores "$A" --filtros gris,gris \
    --entrada "$trabajo/lote" --salida "$trabajo/sal5" > "$trabajo/gg.txt" 2>&1
[ $? -eq 1 ] && grep -q "tres canales" "$trabajo/gg.txt" \
    && ok "gris,gris se rechaza con el motivo del motor" || falla "gris,gris debería rechazarse"

echo "Trabajador:"
codigo=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:18081/procesar?filtros=gris;ls" --data-binary @"$trabajo/lote/img5.ppm")
[ "$codigo" = 400 ] && ok "filtros con caracteres raros dan 400" || falla "filtros raros dieron $codigo"
codigo=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:18081/procesar?filtros=nosexiste" --data-binary @"$trabajo/lote/img5.ppm")
[ "$codigo" = 400 ] && ok "filtro inexistente da 400" || falla "filtro inexistente dio $codigo"
codigo=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:18081/procesar?filtros=gris" --data-binary "esto no es una imagen")
[ "$codigo" = 400 ] && ok "un cuerpo que no es imagen da 400" || falla "cuerpo basura dio $codigo"

echo
[ "$fallos" -eq 0 ] && echo "todas las pruebas pasaron" || echo "$fallos prueba(s) fallaron"
exit "$fallos"
