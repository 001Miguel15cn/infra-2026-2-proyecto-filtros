#!/usr/bin/env bash
# Prueba de integración: levanta un trabajador real, comprueba su contrato
# HTTP, reparte un lote con el despachador y compara cada salida byte a byte
# con la del motor corrido directamente. Después prueba los reintentos contra
# un servicio que falla a propósito.
# Hace falta haber corrido make en motor/. Solo usa la biblioteca estándar.
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

ok()    { echo "  ok    $1"; }
falla() { echo "  FALLA $1"; fallos=$((fallos + 1)); }

esperar() {  # puerto
    for _ in $(seq 50); do
        curl -s "http://127.0.0.1:$1/healthz" > /dev/null && return 0
        sleep 0.1
    done
    return 1
}

PUERTO=18000
OMP_NUM_THREADS=2 PUERTO=$PUERTO MOTOR="$MOTOR" python3 trabajador/trabajador.py > "$trabajo/t.log" 2>&1 &
pids+=($!)
esperar $PUERTO || { echo "el trabajador no arrancó"; cat "$trabajo/t.log"; exit 1; }
URL=http://127.0.0.1:$PUERTO

echo "Contrato HTTP del trabajador:"
[ "$(curl -s -o /dev/null -w '%{http_code}' $URL/healthz)" = 200 ] && ok "GET /healthz responde 200" || falla "/healthz"
[ "$(curl -s -o /dev/null -w '%{http_code}' $URL/readyz)" = 200 ] && ok "GET /readyz responde 200 con el motor presente" || falla "/readyz"
"$GENERAR" "$trabajo/uno.ppm" 200 150 > /dev/null
curl -s -D "$trabajo/cab.txt" -o "$trabajo/resp.pgm" -X POST -H 'Content-Type: application/octet-stream' \
    --data-binary "@$trabajo/uno.ppm" "$URL/procesar?filtros=gris,gaussiano"
grep -qi '^HTTP/1.1 200' "$trabajo/cab.txt" && ok "POST /procesar responde 200" || falla "POST /procesar no dio 200"
grep -qi '^X-Tiempo-Ms: [0-9.]*' "$trabajo/cab.txt" && ok "trae la cabecera X-Tiempo-Ms" || falla "falta X-Tiempo-Ms"
grep -qi '^X-Hilos: 2' "$trabajo/cab.txt" && ok "trae X-Hilos con el valor de OMP_NUM_THREADS" || falla "falta X-Hilos o no vale 2"
"$MOTOR" gris,gaussiano "$trabajo/uno.ppm" "$trabajo/directo.pgm"
"$COMPARAR" "$trabajo/resp.pgm" "$trabajo/directo.pgm" > /dev/null \
    && ok "la imagen devuelta es idéntica a la del motor" || falla "la imagen difiere de la del motor"
codigo=$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary "@$trabajo/uno.ppm" "$URL/procesar?filtros=gris,nosexiste")
[ "$codigo" = 400 ] && ok "un filtro desconocido se rechaza con 400" || falla "filtro desconocido dio $codigo, se esperaba 400"
codigo=$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary "@$trabajo/uno.ppm" "$URL/procesar?filtros=gris;ls")
[ "$codigo" = 400 ] && ok "una cadena con caracteres raros se rechaza con 400" || falla "cadena rara dio $codigo"
codigo=$(MOTOR=/bin/true python3 -c "import sys; sys.path.insert(0, 'trabajador'); import trabajador; print(trabajador.motor_listo())")
[ "$codigo" = False ] && ok "readiness es falso si el motor no responde como el motor" || falla "motor_listo aceptó un binario cualquiera"

echo "Lote de 8 imágenes, concurrencia 4:"
mkdir -p "$trabajo/datos/entrada"
i=0
for tam in "512 512" "300 700" "1024 256" "640 480" "37 41" "800 800" "128 900" "512 512"; do
    i=$((i + 1))
    "$GENERAR" "$trabajo/datos/entrada/img$i.ppm" $tam > /dev/null
done
URL_TRABAJADOR=$URL CONCURRENCIA=4 FILTROS=gris,gaussiano DATOS="$trabajo/datos" GENERAR=0 \
    ENTORNO=prueba TRABAJADORES_REPORTE=1 python3 despachador/despachador.py > "$trabajo/despacho.txt" 2>&1
[ $? -eq 0 ] && ok "el despachador termina sin fallas" || { falla "el despachador falló"; cat "$trabajo/despacho.txt"; }
for i in 1 2 3 4 5 6 7 8; do
    "$MOTOR" gris,gaussiano "$trabajo/datos/entrada/img$i.ppm" "$trabajo/dir$i.pgm"
    "$COMPARAR" "$trabajo/datos/salida/img$i.pgm" "$trabajo/dir$i.pgm" > /dev/null \
        && ok "img$i igual a la salida directa del motor" || falla "img$i difiere"
done
grep -q '^entorno,trabajadores,concurrencia,imagenes,segundos$' "$trabajo/datos/servicio.csv" \
    && ok "el reporte tiene el encabezado pedido" || falla "encabezado del reporte"
grep -q '^prueba,1,4,8,' "$trabajo/datos/servicio.csv" \
    && ok "el reporte tiene la fila de la corrida" || falla "fila del reporte"

echo "Lote generado por el despachador:"
URL_TRABAJADOR=$URL CONCURRENCIA=2 FILTROS=gris DATOS="$trabajo/gen" GENERAR=3 TAM=64x48 \
    python3 despachador/despachador.py > "$trabajo/gen.txt" 2>&1
n=$(ls "$trabajo/gen/salida" 2>/dev/null | wc -l)
[ "$n" -eq 3 ] && ok "con entrada/ vacía genera el lote y lo procesa" || { falla "se esperaban 3 salidas y hay $n"; cat "$trabajo/gen.txt"; }

echo "Servicio que falla a propósito:"
# Contesta /readyz, pero las dos primeras peticiones de /procesar dan 503 y a
# partir de ahí contesta bien. Otro puerto responde 400 siempre.
cat > "$trabajo/falso.py" <<'PY'
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
modo, puerto = sys.argv[1], int(sys.argv[2])
cuenta = [0]
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def responder(self, c, cuerpo=b"x"):
        self.send_response(c); self.send_header("Content-Length", str(len(cuerpo))); self.end_headers(); self.wfile.write(cuerpo)
    def do_GET(self): self.responder(200)
    def do_POST(self):
        self.rfile.read(int(self.headers["Content-Length"]))
        cuenta[0] += 1
        if modo == "400": return self.responder(400, b"mala")
        if cuenta[0] <= 2: return self.responder(503, b"ocupado")
        self.responder(200, b"P5\n1 1\n255\n\x07")
    def log_message(self, *a): pass
ThreadingHTTPServer(("127.0.0.1", puerto), H).serve_forever()
PY
python3 "$trabajo/falso.py" 503 18001 & pids+=($!)
python3 "$trabajo/falso.py" 400 18002 & pids+=($!)
sleep 0.5
mkdir -p "$trabajo/f1/entrada" "$trabajo/f2/entrada"
cp "$trabajo/uno.ppm" "$trabajo/f1/entrada/a.ppm"; cp "$trabajo/uno.ppm" "$trabajo/f2/entrada/a.ppm"
URL_TRABAJADOR=http://127.0.0.1:18001 DATOS="$trabajo/f1" GENERAR=0 REINTENTOS=3 \
    python3 despachador/despachador.py > "$trabajo/f1.txt" 2>&1
[ $? -eq 0 ] && [ -f "$trabajo/f1/salida/a.pgm" ] \
    && ok "un 503 se reintenta y la imagen sale" || { falla "no se recuperó de los 503"; cat "$trabajo/f1.txt"; }
URL_TRABAJADOR=http://127.0.0.1:18002 DATOS="$trabajo/f2" GENERAR=0 REINTENTOS=3 \
    python3 despachador/despachador.py > "$trabajo/f2.txt" 2>&1
codigo=$?
[ "$codigo" -eq 1 ] && ok "un 400 no se reintenta y el código de salida es 1" || falla "400: código $codigo"
URL_TRABAJADOR=http://127.0.0.1:1 DATOS="$trabajo/f2" GENERAR=0 ESPERA_LISTOS=1 \
    python3 despachador/despachador.py > /dev/null 2>&1
[ $? -eq 2 ] && ok "si el servicio nunca está listo termina con 2" || falla "servicio ausente no dio 2"

echo
if [ "$fallos" -eq 0 ]; then echo "todas las pruebas pasaron"; else echo "$fallos prueba(s) fallaron"; fi
exit "$fallos"
