#!/usr/bin/env python3
"""Trabajador: expone el motor de filtros por HTTP.

    GET  /healthz                    -> 200 si el proceso está vivo (liveness)
    GET  /readyz                     -> 200 solo si el binario del motor está
                                        presente y responde (readiness)
    POST /procesar?filtros=a,b,c     -> cuerpo: imagen PPM/PGM en crudo
                                        respuesta: la imagen resultante, con
                                        X-Tiempo-Ms (lo que tardó el motor) y
                                        X-Hilos (hilos con que corrió)

Códigos de error: 400 (filtro desconocido, cadena o imagen inválida), 411/413 (cuerpo ausente o
demasiado grande), 501 (filtro sin implementar), 504 (el motor se pasó del
tiempo), 500 (cualquier otra falla del motor).

Variables de entorno: PUERTO (8000), MOTOR, OMP_NUM_THREADS (hilos del motor;
HILOS lo sobrescribe), TIEMPO_MAX (segundos), MAX_BYTES, MAX_CONCURRENTES.
"""
import json
import os
import re
import socket
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

AQUI = os.path.dirname(os.path.abspath(__file__))
MOTOR = os.environ.get("MOTOR", os.path.join(AQUI, "..", "motor", "motor"))
PUERTO = int(os.environ.get("PUERTO", "8000"))
# Hilos con que corre el motor: HILOS, si no OMP_NUM_THREADS, si no los núcleos.
HILOS = os.environ.get("HILOS") or os.environ.get("OMP_NUM_THREADS") or str(os.cpu_count() or 1)
TIEMPO_MAX = float(os.environ.get("TIEMPO_MAX", "120"))
MAX_BYTES = int(os.environ.get("MAX_BYTES", str(512 * 1024 * 1024)))

# El motor ya usa todos los núcleos con OpenMP: dos imágenes a la vez solo
# se estorban. Las demás peticiones esperan su turno.
turno = threading.BoundedSemaphore(int(os.environ.get("MAX_CONCURRENTES", "1")))

FILTROS_OK = re.compile(r"^[a-z_]+(,[a-z_]+)*$")


def motor_listo():
    """El binario existe, es ejecutable y responde: sin argumentos imprime su
    uso y sale con código 2, sin leer ninguna imagen."""
    if not (os.path.isfile(MOTOR) and os.access(MOTOR, os.X_OK)):
        return False
    try:
        r = subprocess.run([MOTOR], capture_output=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 2 and b"uso:" in r.stderr


class Manejador(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def responder(self, codigo, cuerpo=b"", tipo="text/plain; charset=utf-8", extra=None):
        if isinstance(cuerpo, str):
            cuerpo = cuerpo.encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(cuerpo)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(cuerpo)

    def do_GET(self):
        ruta = urlparse(self.path).path
        if ruta == "/healthz":
            self.responder(200, json.dumps({"estado": "vivo"}), "application/json")
        elif ruta == "/readyz":
            if motor_listo():
                self.responder(200, json.dumps({"estado": "listo"}), "application/json")
            else:
                self.responder(503, json.dumps({"estado": "motor no disponible"}), "application/json")
        else:
            self.responder(404, "no existe\n")

    def do_POST(self):
        url = urlparse(self.path)
        if url.path != "/procesar":
            self.responder(404, "no existe\n")
            return
        filtros = parse_qs(url.query).get("filtros", [""])[0]
        largo = self.headers.get("Content-Length")
        # Se lee el cuerpo siempre, incluso si se va a rechazar, para no
        # dejar bytes sin consumir en la conexión.
        if largo is None:
            self.close_connection = True
            self.responder(411, "falta Content-Length\n")
            return
        try:
            largo = int(largo)
        except ValueError:
            largo = -1
        if largo < 0:
            self.close_connection = True
            self.responder(400, "Content-Length inválido\n")
            return
        if largo > MAX_BYTES:
            self.close_connection = True
            self.responder(413, "imagen demasiado grande\n")
            return
        cuerpo = self.rfile.read(largo)
        if not FILTROS_OK.match(filtros):
            self.responder(400, "filtros inválidos: use ?filtros=a,b,c\n")
            return

        with turno, tempfile.TemporaryDirectory() as tmp:
            entrada = os.path.join(tmp, "entrada")
            salida = os.path.join(tmp, "salida")
            with open(entrada, "wb") as f:
                f.write(cuerpo)
            entorno = dict(os.environ)
            entorno["OMP_NUM_THREADS"] = HILOS
            inicio = time.time()
            try:
                r = subprocess.run([MOTOR, filtros, entrada, salida], capture_output=True,
                                   timeout=TIEMPO_MAX, env=entorno)
            except subprocess.TimeoutExpired:
                self.responder(504, "el motor tardó demasiado\n")
                return
            ms = (time.time() - inicio) * 1000.0
            error = r.stderr.decode("utf-8", "replace").strip()
            if r.returncode == 0:
                with open(salida, "rb") as f:
                    imagen = f.read()
                self.responder(200, imagen, "application/octet-stream",
                               {"X-Tiempo-Ms": "%.1f" % ms, "X-Hilos": HILOS,
                                "X-Trabajador": socket.gethostname()})
            elif "sin implementar" in error:
                self.responder(501, error + "\n")
            elif r.returncode in (1, 2):
                self.responder(400, error + "\n")
            else:
                self.responder(500, "el motor terminó con código %d: %s\n" % (r.returncode, error))

    def log_message(self, formato, *args):
        print("%s %s" % (self.address_string(), formato % args), flush=True)


if __name__ == "__main__":
    if not os.path.exists(MOTOR):
        raise SystemExit("no existe el motor en %s (¿corrió make?)" % MOTOR)
    servidor = ThreadingHTTPServer(("0.0.0.0", PUERTO), Manejador)
    print("trabajador en el puerto %d, motor %s" % (PUERTO, MOTOR), flush=True)
    servidor.serve_forever()
